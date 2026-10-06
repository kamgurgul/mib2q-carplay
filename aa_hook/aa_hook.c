/*
 * libaa_cluster_hook.so — Android Auto cluster display for Audi MHI2Q, LD_PRELOADed into gal.
 *
 * Adapted from wasimlhr/mib2q-android-auto-cluster (android_auto/hook: probe.c with the
 * build-time patches of build_in_container.sh applied, versionfix.c, unexpected.c), whose
 * protocol injection derives from OneB1t/chopinwong01 mhi2-android-auto-video-vc
 * video_sink_hook.c. All GPL-3.0.
 *
 * What it does, inside the stock 2016 libautoreceiver (no stock file is replaced):
 *   - registers a SECOND video sink (H.264, 1920x1080, margins 480x540 -> a centred
 *     1440x540 viewport) and an input endpoint for display 1, so the phone opens a cluster
 *     display beside the centre screen. The sink uses a private vtable: setup, codec, data,
 *     start/stop and focus callbacks are ours; nothing reaches the stock renderer;
 *   - acknowledges every cluster frame and hands it to the loopback tee (aa_tee.c,
 *     127.0.0.1:19821), which altscreen_render presents in window 99;
 *   - requests Android Auto protocol 4.3 (the phone gates resizable cluster layouts on it)
 *     and rewrites the phone's newer answer to 1.7 before the 2016 receiver reads it;
 *   - suppresses the receiver's "unexpected message" reply to newer stock-service messages,
 *     which made the phone reset the link;
 *   - translates the new navigation messages to the legacy pair (aa_navxlate.c) and writes
 *     the current step's lanes for Java;
 *   - sends the cockpit view's safe area at connect and on every view change (aa_uiconfig.c).
 *
 * ABI safety: the hook relies on object layouts inside libautoreceiver. It runs only when the
 * receiver matches a known profile (below), checked by symbol offsets at load time. Any other
 * receiver keeps stock behaviour and logs a fingerprint line from which a profile can be made
 * (docs/android-auto/firmware-porting.md). The marker aa_cluster.abi_trial forces a trial on an
 * unknown receiver; gal may then crash and Android Auto restarts, so use it only to test.
 * Without this hook Android Auto still drives the cluster arrow, HUD and lanes from Java.
 *
 * Runs only when gal is started by aa_startup.sh (AA_CLUSTER_HOOK=1). Log: /tmp/aa_cluster_hook.log.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "aa_hook.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define EXPORT __attribute__((visibility("default")))

typedef int (*register_fn)(void *, void *);
typedef void (*config_fn)(void *, int, int, int, int, int, int, int);
typedef void (*discovery_fn)(void *, void *);
typedef void (*sendconfig_fn)(void *, int);
typedef void (*focus_fn)(void *, int, int);
typedef int (*ack_fn)(void *, int, unsigned);
typedef void (*queue_fn)(void *, unsigned char, void *, unsigned);
typedef void (*route_fn)(void *, unsigned char, const void *);
typedef int (*open_fn)(void *, unsigned char, const void *);
typedef int (*version_resp_fn)(void *, void *, unsigned);
typedef void (*unexpected_fn)(void *, unsigned char);

/* ---- receiver profiles -------------------------------------------------------------- */

/* Offsets of the interposed/called libautoreceiver symbols from the library base
 * (GalReceiver::registerService minus .reg), and two absolute gal addresses that pin the
 * gal build. A profile is only added after its object layouts were verified. */
struct aa_profile {
    const char *name;
    uint32_t reg, sink_vt, base_vt, config, send_config, set_focus, ack;
    uint32_t gal_controller, gal_vsink_vt;
};

static const struct aa_profile profiles[] = {
    /* MHI2Q_US_AUG22_P3639 MU0918: validated on the car by the fork. */
    { "MU0918", 0xe4928, 0x102b90, 0x102af0, 0xfafc4, 0xec748, 0xfa978, 0xec294,
      0x2544a0, 0x24dfb8 },
    /* MHI2Q_ER_AUG22_P5152: libautoreceiver.so byte-identical to MU0918 (sha256 6ef4abea...),
     * so every object layout above applies; only gal is a different build (sha256 a659a955...,
     * read from the unit, carplay_logs/aa). */
    { "P5152", 0xe4928, 0x102b90, 0x102af0, 0xfafc4, 0xec748, 0xfa978, 0xec294,
      0x258580, 0x251b28 },
};

/* ---- state -------------------------------------------------------------------------- */

static register_fn real_register;
static config_fn real_config;
static discovery_fn real_discovery, real_input_discovery;
static sendconfig_fn send_config;
static focus_fn set_focus;
static ack_fn ack_frames;
static queue_fn queue_plain, queue_encrypted;
static route_fn real_route;
static open_fn real_open;
static void (*real_version)(void *);
static version_resp_fn real_version_response;
static unexpected_fn real_unexpected;
static void *primary, *secondary, *pending, *sink_vtable, *input_endpoint, *base_vtable;
static uint32_t private_vtable[18];   /* 8-byte header, sixteen method pointers */
static uint32_t input_vtable[10];
static unsigned video_service, input_service, nav_service = 256;
static unsigned input_channel = 256;
static int enabled, registered, attempted, captured_session;
static unsigned long frame_count, frame_bytes;
static unsigned long channel_messages[256];
static const char *profile_name = "none";

static FILE *logfile;
static size_t logbytes;
static pthread_mutex_t loglock = PTHREAD_MUTEX_INITIALIZER;

static uint32_t u32(const void *p, unsigned off) { uint32_t v; memcpy(&v, (const char *)p + off, 4); return v; }
static void put32(void *p, unsigned off, uint32_t v) { memcpy((char *)p + off, &v, 4); }
static unsigned u8(const void *p, unsigned off) { return ((const unsigned char *)p)[off]; }

void aa_log(const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    struct timespec t;
    int n, k;
    clock_gettime(CLOCK_MONOTONIC, &t);
    n = snprintf(line, sizeof(line), "t=%ld.%03ld ", (long)t.tv_sec, t.tv_nsec / 1000000);
    va_start(ap, fmt); vsnprintf(line + n, sizeof(line) - (size_t)n - 2, fmt, ap); va_end(ap);
    k = (int)strlen(line); line[k++] = '\n';
    pthread_mutex_lock(&loglock);
    if (logfile && logbytes + (size_t)k <= 1024u * 1024u) {
        fwrite(line, 1, (size_t)k, logfile); fflush(logfile); logbytes += (size_t)k;
    }
    pthread_mutex_unlock(&loglock);
}

int aa_marker(const char *path) { return access(path, F_OK) == 0; }

static void *sym(const char *s) { return dlsym(RTLD_NEXT, s); }

/* Only append to a fresh protobuf-lite SSO unknown-field string. */
static int unknown(void *object, const unsigned char *data, unsigned n)
{
    if (!object || n > 15 || u32(object, 0x1c) != 15 || u32(object, 0x18) != 0) return 0;
    memcpy((char *)object + 8, data, n); ((char *)object)[8 + n] = 0;
    put32(object, 0x18, n);
    return 1;
}

static void *last_service(void *response)
{
    unsigned n = u32(response, 0x2c);
    void *array = (void *)(uintptr_t)u32(response, 0x28);
    if (!n || n > 128 || !array) return NULL;
    return (void *)(uintptr_t)u32(array, 4 * (n - 1));
}

static const unsigned char *payload(const void *shared, unsigned skip, unsigned *size)
{
    const void *io;
    uint32_t base, off, end;
    *size = 0;
    if (!shared) return NULL;
    io = (void *)(uintptr_t)u32(shared, 4);
    if (!io) return NULL;
    base = u32(io, 0); off = u32(io, 8); end = u32(io, 12);
    if (!base || end < off || skip > end - off || end - off - skip > 4u * 1024u * 1024u ||
        (uint64_t)base + end > UINT32_MAX) return NULL;
    *size = end - off - skip;
    return (const unsigned char *)(uintptr_t)(base + off + skip);
}

/* ---- the cluster video sink (private vtable) ---------------------------------------- */

EXPORT void _ZN9VideoSink16addDiscoveryInfoEP24ServiceDiscoveryResponse(void *, void *);

static int secondary_setup(void *sink, int type)
{
    aa_log("cluster.setup service=%u channel=%u codec=%d", video_service, u8(sink, 5), type);
    if (type != 3) return -8;           /* stock accepts H.264 (codec type 3) only */
    send_config(sink, 2);               /* the verified stock handleSetup status value */
    set_focus(sink, 1, 1);
    return 0;
}

static int secondary_configuration(void *sink, int config)
{
    /* +0x18 is the session set by MediaSinkBase::handleStart, not the config index. */
    aa_log("cluster.configuration index=%d session=%u", config, u32(sink, 0x18));
    return config == 0 ? 0 : -8;
}

static void secondary_start(void *sink, int session)
{
    if (captured_session) aa_tee_reset("next_playback");
    captured_session = 1;
    frame_count = frame_bytes = 0;
    aa_log("cluster.start session=%d service=%u channel=%u", session, video_service, u8(sink, 5));
}

static void secondary_stop(void *sink, int session)
{
    (void)sink;
    aa_log("cluster.stop session=%d frames=%lu bytes=%lu", session, frame_count, frame_bytes);
    aa_tee_reset("playback_stop");
}

static void secondary_codec(void *sink, void *data, unsigned size)
{
    (void)sink;
    aa_log("cluster.codec_config bytes=%u", size);
    aa_tee_codec(data, size);
}

static void secondary_data(void *sink, uint64_t timestamp, const void *shared, unsigned skip)
{
    unsigned size;
    const unsigned char *p = payload(shared, skip, &size);
    int rc;
    aa_relayout_tick(sink);
    ++frame_count;
    if (p && size) { frame_bytes += size; aa_tee_frame(p, size); }
    rc = ack_frames(sink, (int)u32(sink, 0x18), 1);
    if (frame_count <= 3 || frame_count % 900 == 0)
        aa_log("cluster.frame count=%lu bytes=%u total=%lu pts=%llu nal_mask=0x%x ack_rc=%d",
               frame_count, size, frame_bytes, (unsigned long long)timestamp,
               p ? aa_nal_types(p, size) : 0, rc);
}

static int secondary_focus(void *sink, const void *request)
{
    int mode = request ? (int)u32(request, 0x2c) : 0;
    aa_log("cluster.focus_request mode=%d", mode);
    if (mode != 1 && mode != 2) return 0;
    set_focus(sink, mode, 0);
    return 1;
}

static int secondary_closed(void *sink, unsigned char channel)
{
    int (*f)(void *, unsigned char) = sym("_ZN20ProtocolEndpointBase15onChannelClosedEh");
    secondary_stop(sink, (int)u32(sink, 0x18));
    return f ? f(sink, channel) : 0;
}

static void secondary_gone(void)
{
    aa_tee_reset("sink_destructor");
    registered = 0; secondary = NULL; input_channel = 256;
}

static void secondary_destroy(void *sink)
{
    void (*f)(void *) = sym("_ZN9VideoSinkD1Ev");
    secondary_gone();
    if (f) f(sink);
}

static void secondary_delete(void *sink)
{
    void (*f)(void *) = sym("_ZN9VideoSinkD0Ev");
    secondary_gone();
    if (f) f(sink);
}

static void input_discovery(void *endpoint, void *response)
{
    unsigned char input[14] = { 0x0a, 0x0c, 0x08, 0, 0x22, 0x08, 0x0a, 0x04, 0x17, 0x80, 0x80, 0x04, 0x28, 1 };
    (void)endpoint;
    input[3] = (unsigned char)input_service;
    aa_log("discovery.input service=%u display_id=1 metadata_ok=%d", input_service,
           unknown(response, input, sizeof(input)));
}

static int input_route(void *endpoint, unsigned char channel, unsigned short id, const void *shared)
{
    (void)shared;
    if (id == 0x8002) {                 /* KeyBindingRequest -> success */
        unsigned char reply[4] = { 0x80, 3, 8, 0 };
        queue_encrypted((void *)(uintptr_t)u32(endpoint, 8), channel, reply, sizeof(reply));
        aa_log("input.binding channel=%u response=ok", channel);
        return 0;
    }
    return -4;
}

/* Over-allocated: real_config/registerService touch only the first 0x50 / 0x14 bytes of
 * the validated layout; the slack keeps a slightly larger layout from writing past us. */
#define SECONDARY_ALLOC 0x100
#define INPUT_ALLOC     0x40

static void build_sink(void *receiver, void *main_sink)
{
    unsigned id;
    void *router = (void *)(uintptr_t)u32(main_sink, 8);
    if (attempted || !enabled) return;
    attempted = 1;
    if (router != receiver || u32(main_sink, 0) != (uint32_t)(uintptr_t)sink_vtable + 8) {
        aa_log("cluster.refused reason=primary_vtable_or_router"); return;
    }
    /* Two free one-byte-varint service IDs. */
    for (id = 64; id < 126; id += 2)
        if (!u32(router, (id + 64) * 4) && !u32(router, (id + 65) * 4)) break;
    if (id >= 126) { aa_log("cluster.refused reason=no_service_pair"); return; }
    secondary = calloc(1, SECONDARY_ALLOC);
    input_endpoint = calloc(1, INPUT_ALLOC);
    if (!secondary || !input_endpoint) {
        free(secondary); free(input_endpoint); secondary = input_endpoint = NULL; return;
    }
    video_service = id; input_service = id + 1;
    memcpy(private_vtable, sink_vtable, sizeof(private_vtable));
#define SLOT(n, fn) private_vtable[2 + (n)] = (uint32_t)(uintptr_t)(fn)
    SLOT(0, secondary_destroy); SLOT(1, secondary_delete); SLOT(2, secondary_closed);
    SLOT(7, _ZN9VideoSink16addDiscoveryInfoEP24ServiceDiscoveryResponse);
    SLOT(8, secondary_setup); SLOT(9, secondary_codec); SLOT(10, secondary_data);
    SLOT(11, secondary_start); SLOT(12, secondary_stop); SLOT(13, secondary_configuration);
    SLOT(14, secondary_focus);
#undef SLOT
    put32(secondary, 0, (uint32_t)(uintptr_t)(private_vtable + 2));
    put32(secondary, 8, (uint32_t)(uintptr_t)router);
    ((unsigned char *)secondary)[12] = (unsigned char)id;
    put32(secondary, 0x14, 3); put32(secondary, 0x18, UINT32_MAX);
    put32(secondary, 0x1c, 8); ((unsigned char *)secondary)[0x30] = 1;
    put32(secondary, 0x4c, 700);
    memcpy(input_vtable, base_vtable, sizeof(input_vtable));
    input_vtable[2 + 5] = (uint32_t)(uintptr_t)input_route;
    input_vtable[2 + 7] = (uint32_t)(uintptr_t)input_discovery;
    put32(input_endpoint, 0, (uint32_t)(uintptr_t)(input_vtable + 2));
    put32(input_endpoint, 8, (uint32_t)(uintptr_t)router);
    ((unsigned char *)input_endpoint)[12] = (unsigned char)input_service;
    real_config(secondary, AA_CODEC_RES_1080P, 30, AA_MARGIN_W, AA_MARGIN_H, aa_dpi(), 3, 10000);
    if (u32(secondary, 0x44) - u32(secondary, 0x40) != 8) {
        aa_log("cluster.refused reason=configuration_vector"); return;
    }
    aa_uiconfig_sink(secondary);
    registered = real_register(receiver, secondary) != 0;
    if (registered) registered = real_register(receiver, input_endpoint) != 0;
    aa_log("cluster.register ok=%d video_service=%u input_service=%u codec=%dx%d@30 viewport=%dx%d",
           registered, video_service, input_service, AA_STREAM_W, AA_STREAM_H, AA_VIEW_W, AA_VIEW_H);
}

/* gal's navigation-status endpoint: its channel is where 0x8006/0x8007 are translated. */
static void learn_endpoint(void *e)
{
    Dl_info info;
    uint32_t vt = u32(e, 0);
    if (!vt || !dladdr((void *)(uintptr_t)(vt - 8), &info) || !info.dli_sname) return;
    if (strstr(info.dli_sname, "NavigationStatus")) {
        nav_service = u8(e, 12);
        aa_log("endpoint.navigation service=%u class=%s", nav_service, info.dli_sname);
    }
}

/* ---- interposers -------------------------------------------------------------------- */

EXPORT int _ZN11GalReceiver15registerServiceEP20ProtocolEndpointBase(void *r, void *e)
{
    int rc = real_register ? real_register(r, e) : 0;
    if (enabled && rc && e && e != secondary && e != input_endpoint) learn_endpoint(e);
    if (enabled && rc && e && e == pending) { primary = e; build_sink(r, e); }
    return rc;
}

EXPORT void _ZN9VideoSink25addSupportedConfigurationEiiiiiii(void *s, int a, int b, int c, int d, int e, int f, int g)
{
    if (enabled && s != secondary) pending = s;
    if (real_config) real_config(s, a, b, c, d, e, f, g);
}

EXPORT void _ZN9VideoSink16addDiscoveryInfoEP24ServiceDiscoveryResponse(void *sink, void *response)
{
    void *service, *media;
    unsigned char meta[4] = { 0x30, 0, 0x38, 0 };   /* display type / display id */
    if (real_discovery) real_discovery(sink, response);
    if (!registered || !response || (sink != primary && sink != secondary)) return;
    service = last_service(response);
    if (!service || u32(service, 0x58) != u8(sink, 12)) { aa_log("discovery.error service_mismatch"); return; }
    media = (void *)(uintptr_t)u32(service, 0x2c);
    if (sink == secondary) meta[1] = meta[3] = 1;
    aa_log("discovery.video service=%u cluster=%d metadata_ok=%d", u8(sink, 12), sink == secondary,
           unknown(media, meta, 4));
    if (sink == secondary) aa_uiconfig_media(media);
}

EXPORT void _ZN11InputSource16addDiscoveryInfoEP24ServiceDiscoveryResponse(void *s, void *r)
{
    void *entry;
    const unsigned char tag[2] = { 0x28, 0 };
    if (real_input_discovery) real_input_discovery(s, r);
    if (!registered || !r) return;
    entry = last_service(r);
    if (entry && (u32(entry, 0x20) & 8))
        aa_log("discovery.main_input display_id=0 metadata_ok=%d",
               unknown((void *)(uintptr_t)u32(entry, 0x30), tag, 2));
}

/* Request protocol 4.3: Android Auto gates resizable cluster layouts (0x8009) on the
 * requested version; it answers 6.1, which handleVersionResponse below turns into 1.7. */
EXPORT void _ZN10Controller18sendVersionRequestEv(void *s)
{
    unsigned char msg[6] = { 0, 1, 0, 4, 0, 3 };   /* VERSION_REQUEST, major 4, minor 3 */
    if (!enabled || !registered) { if (real_version) real_version(s); return; }
    if (s && u8(s, 4)) {
        queue_plain((void *)(uintptr_t)u32(s, 8), (unsigned char)u8(s, 5), msg, sizeof(msg));
        aa_log("version.sent major=4 minor=3");
    }
}

/* Response layout (Controller::handleVersionResponse disassembly): u16 BE major, u16 BE
 * minor, u16 BE status. The 2016 receiver understands up to 1.7. */
EXPORT int _ZN10Controller21handleVersionResponseEPvj(void *self, void *data, unsigned len)
{
    unsigned char *p = (unsigned char *)data;
    if (!real_version_response)
        real_version_response = (version_resp_fn)sym("_ZN10Controller21handleVersionResponseEPvj");
    if (enabled && registered && p && len >= 4) {
        unsigned major = (unsigned)p[0] << 8 | p[1], minor = (unsigned)p[2] << 8 | p[3];
        if (major > 1u) {
            aa_log("version.response major=%u minor=%u -> told gal 1.7", major, minor);
            p[0] = 0; p[1] = 1; p[2] = 0; p[3] = 7;
        } else {
            aa_log("version.response major=%u minor=%u", major, minor);
        }
    }
    return real_version_response ? real_version_response(self, data, len) : 0;
}

/* With the newer protocol the phone sends a stock service messages this receiver does not
 * handle; its "unexpected message" reply made the phone reset the link (fork runs 3-4). An
 * unsupported message left unanswered is ordinary for the phone, so the reply is dropped. */
EXPORT void _ZN13MessageRouter21sendUnexpectedMessageEh(void *router, unsigned char channel)
{
    static unsigned count;
    if (!real_unexpected)
        real_unexpected = (unexpected_fn)sym("_ZN13MessageRouter21sendUnexpectedMessageEh");
    if (enabled && registered) {
        if (count++ < 64) aa_log("unexpected.suppressed channel=%u n=%u", channel, count);
        return;
    }
    if (real_unexpected) real_unexpected(router, channel);
}

EXPORT int _ZN13MessageRouter20handleChannelOpenReqEhRK18ChannelOpenRequest(void *r, unsigned char ch, const void *req)
{
    unsigned service = req ? u8(req, 0x2c) : 255;
    int rc = real_open ? real_open(r, ch, req) : -4;
    if (!enabled) return rc;
    if (registered && service == input_service && rc == 0) input_channel = ch;
    if (service == nav_service && rc == 0) aa_nav_set_channel(ch);
    aa_log("channel.open role=%s service=%u channel=%u result=%d",
           registered && service == video_service ? "cluster_video" :
           registered && service == input_service ? "cluster_input" :
           service == nav_service ? "navigation" : "stock", service, ch, rc);
    return rc;
}

EXPORT void _ZN13MessageRouter12routeMessageEhRK10shared_ptrI8IoBufferE(void *r, unsigned char ch, const void *shared)
{
    if (enabled && registered) {
        if (ch == input_channel || (secondary && ch == u8(secondary, 5))) {
            unsigned n = 0;
            const unsigned char *p = payload(shared, 0, &n);
            if (channel_messages[ch]++ < 24 && p && n >= 2)
                aa_log("channel.message channel=%u id=0x%02x%02x bytes=%u", ch, p[0], p[1], n);
        }
        aa_nav_translate(ch, shared);
        if (secondary) aa_relayout_tick(secondary);   /* view changes on a still map */
    }
    if (real_route) real_route(r, ch, shared);
}

/* ---- load-time gate ----------------------------------------------------------------- */

static const struct aa_profile *match_profile(void)
{
    unsigned i;
    uintptr_t gal_ctl = (uintptr_t)dlsym(RTLD_DEFAULT, "_ZN3gal14CGALController10s_instanceE");
    uintptr_t gal_vsink = (uintptr_t)dlsym(RTLD_DEFAULT, "_ZTVN3gal14CVideoSinkImplE");
    for (i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
        const struct aa_profile *p = &profiles[i];
        uintptr_t base = (uintptr_t)real_register - p->reg;
        if (gal_ctl == p->gal_controller && gal_vsink == p->gal_vsink_vt &&
            (uintptr_t)sink_vtable == base + p->sink_vt && (uintptr_t)base_vtable == base + p->base_vt &&
            (uintptr_t)real_config == base + p->config && (uintptr_t)send_config == base + p->send_config &&
            (uintptr_t)set_focus == base + p->set_focus && (uintptr_t)ack_frames == base + p->ack)
            return p;
    }
    return NULL;
}

/* One line with everything a new profile needs (offsets from the receiver's load base). */
static void log_fingerprint(void)
{
    Dl_info info;
    uintptr_t b = 0;
    if (dladdr((void *)real_register, &info) && info.dli_fbase) b = (uintptr_t)info.dli_fbase;
    aa_log("abi.fingerprint receiver=%s base=0x%lx reg=0x%lx sink_vt=0x%lx base_vt=0x%lx config=0x%lx "
           "send_config=0x%lx set_focus=0x%lx ack=0x%lx gal_controller=0x%lx gal_vsink_vt=0x%lx",
           b && info.dli_fname ? info.dli_fname : "?", (unsigned long)b,
           (unsigned long)((uintptr_t)real_register - b), (unsigned long)((uintptr_t)sink_vtable - b),
           (unsigned long)((uintptr_t)base_vtable - b), (unsigned long)((uintptr_t)real_config - b),
           (unsigned long)((uintptr_t)send_config - b), (unsigned long)((uintptr_t)set_focus - b),
           (unsigned long)((uintptr_t)ack_frames - b),
           (unsigned long)(uintptr_t)dlsym(RTLD_DEFAULT, "_ZN3gal14CGALController10s_instanceE"),
           (unsigned long)(uintptr_t)dlsym(RTLD_DEFAULT, "_ZTVN3gal14CVideoSinkImplE"));
}

__attribute__((constructor)) static void aa_hook_init(void)
{
    const struct aa_profile *p;
    const char *opt = getenv("AA_CLUSTER_HOOK");
    const char *port = getenv("AA_CLUSTER_TEE_PORT");
    int resolved;
    /* Always resolve: every interposer forwards to these, enabled or not. */
#define RES(var, name) var = (void *)sym(name)
    RES(real_register, "_ZN11GalReceiver15registerServiceEP20ProtocolEndpointBase");
    RES(real_config, "_ZN9VideoSink25addSupportedConfigurationEiiiiiii");
    RES(real_discovery, "_ZN9VideoSink16addDiscoveryInfoEP24ServiceDiscoveryResponse");
    RES(real_input_discovery, "_ZN11InputSource16addDiscoveryInfoEP24ServiceDiscoveryResponse");
    RES(send_config, "_ZN13MediaSinkBase10sendConfigEi");
    RES(set_focus, "_ZN9VideoSink13setVideoFocusEib");
    RES(ack_frames, "_ZN13MediaSinkBase9ackFramesEij");
    RES(real_open, "_ZN13MessageRouter20handleChannelOpenReqEhRK18ChannelOpenRequest");
    RES(real_route, "_ZN13MessageRouter12routeMessageEhRK10shared_ptrI8IoBufferE");
    RES(queue_plain, "_ZN13MessageRouter24queueOutgoingUnencryptedEhPvj");
    RES(queue_encrypted, "_ZN13MessageRouter13queueOutgoingEhPvj");
    RES(real_version, "_ZN10Controller18sendVersionRequestEv");
    RES(real_version_response, "_ZN10Controller21handleVersionResponseEPvj");
    RES(real_unexpected, "_ZN13MessageRouter21sendUnexpectedMessageEh");
    RES(sink_vtable, "_ZTV9VideoSink");
    RES(base_vtable, "_ZTV20ProtocolEndpointBase");
#undef RES
    if (!opt || strcmp(opt, "1")) return;          /* only the gal that aa_startup.sh launched */
    logfile = fopen(AA_LOG_PATH, "a");
    resolved = real_register && real_config && real_discovery && real_input_discovery && send_config &&
        set_focus && ack_frames && real_open && real_route && queue_plain && queue_encrypted &&
        real_version && real_version_response && real_unexpected && sink_vtable && base_vtable;
    if (!resolved) {
        aa_log("init disabled reason=symbols_missing (not the expected Android Auto receiver)");
        return;
    }
    log_fingerprint();
    p = match_profile();
    if (p) {
        profile_name = p->name;
    } else if (aa_marker(AA_MARKER_ABI_TRIAL)) {
        profile_name = "trial";
        aa_log("abi.TRIAL unknown receiver, forced by %s: gal may crash", AA_MARKER_ABI_TRIAL);
    } else {
        aa_log("init disabled reason=unknown_receiver (stock Android Auto; see the fingerprint line)");
        return;
    }
    if (aa_marker(AA_MARKER_OFF)) {
        aa_log("init disabled reason=%s", AA_MARKER_OFF);
        return;
    }
    if (aa_tee_start(port && *port ? atoi(port) : AA_TEE_PORT_DEFAULT) != 0) {
        aa_log("init disabled reason=tee_listen");
        return;
    }
    enabled = 1;
    aa_log("init enabled profile=%s", profile_name);
}
