/*
 * cluster_dual.c - HW301 dedicated Virtual Cockpit (CarPlay stream 111).
 *
 * One wrapper-rename shim installed as libCoreUtils.so (stock renamed to
 * libCoreUtilsReal.so). CPAAProxyEx NEEDs libCoreUtils.so before libAirPlay.so,
 * so this object precedes libAirPlay in musl's lookup order and can interpose
 * libAirPlay's PLT exports as well as CPAAProxyEx's CoreUtils imports;
 * dlsym(RTLD_NEXT) reaches the real libAirPlay and the real libCoreUtils.
 *
 *   iPhone ==Wi-Fi AirPlay==> HW301 (receiver: libAirPlay) ==USB CarPlay==> MHI2Q
 *     110 -> stock path ------------------------------------------------> 110 (stock)
 *     111 -> [this shim] listen, decrypt --> re-encrypt, connect -------> 111 (altscreen hook :7100)
 *
 * Phone side (dongle = AirPlay receiver):
 *   - AirPlayCopyServerInfo: advertise a 1440x540 type-111 cluster display.
 *   - AirPlayReceiverSessionSetup: stock skips type 111 (switch type-100 0..10);
 *     after stock succeeds, open our own listener and append dataPort+streamID
 *     to the stock response (as libaltscreen111_gen2.c does on the head unit).
 *   - key: the wireless screen is ChaCha20-Poly1305, not the AES master.
 *     STEP_DECRYPT derives the 32-byte stream key with PairingSessionDeriveKey
 *     (session+0x3ec, salt "DataStream-Salt"<cid>) and opens each VIDEO_FRAME
 *     with libCoreUtils chacha20_poly1305_*. Layout is pinned in
 *     FINDINGS.md ("Phone side: ChaCha20-Poly1305").
 *
 * Head-unit side (dongle = CarPlay client; NO CPAAProxyEx patch):
 *   - CFArrayEnsureCreatedAndAppend: clone the SetupScreen stream 110 dict as
 *     type 111 (old mirror experiment). Stock CPAAProxyEx then skips 111, so the
 *     single head-unit socket slot is never clobbered. The clone keeps 110's
 *     streamConnectionID, so the head unit derives the same key for 111 as 110.
 *   - that key is captured where CPAAProxyEx derives it: AES_CTR_Init called
 *     from SetupProxyScreenSecurityInfo.
 *   - the head-unit 111 port defaults to 7100, or /mnt/UDISK/cluster_hu.port.
 *     That number must match the head unit's /mnt/app/cluster_hu.port. The
 *     hook binds only that port for the dongle; it does not try another.
 *     The head-unit address is the peer of the
 *     dongle's stream-110 data socket (peer port 6030).
 *
 * Relay (step 3, and only once STEP_DECRYPT has produced plaintext): phone 111
 * VIDEO_FRAME bodies are ChaCha-opened, then sealed the way other dongles seal
 * a screen toward the head unit. This firmware logs "use aes ctr security", so
 * the seal is AES-CTR of the whole AVCC access unit (SendProxyScreenFrame's
 * AES_CTR_Update of the sample; that function's 4-byte rewrite only replaces
 * an Annex-B start code, and this buffer is already AVCC). No Poly1305 tag.
 * VIDEO_CONFIG stays plaintext. One head-unit connection is kept across phone
 * reconnects. The captured key is stream 110's: the direct path does not
 * inject a second screen, and the head-unit hook derives 111 from that same
 * connection id.
 *
 * Step is chosen at BUILD time (no dongle shell needed): -DCLUSTER_STEP=N
 *   1  advertise the cluster display to the iPhone + log SETUP/keys only
 *   2  + open the 111 listener, append its dataPort, receive and decrypt, count
 *   3  + inject 111 to the head unit and relay (full dual screen)  [default]
 * Each step is its own flashable image; validate with getlogs -> dmesg.log.
 *
 * Optional runtime overrides (only if you DO get a dongle shell; files on
 * /mnt/UDISK, which survives flashing - not needed for the normal flow):
 *   cluster_dual.off       everything stock (pure pass-through)
 *   cluster_dual.noinject  force off: no 111 in the SETUP to the head unit
 *   cluster_dual.norelay   force off: receive+decrypt but never touch the HU
 *   (viewAreas are always advertised now - required by iOS, see v1142 result)
 *   cluster_dual.plainhu   send plain (unencrypted) frames to the head unit
 *   cluster_dump.on        dump decrypted 111 AUs to /mnt/UDISK/c111.h264 (~2MB)
 * Port override: cluster_hu.port
 * To disable without a shell, flash stock fw/hw301_v129.tar.
 *
 * Logs -> /dev/kmsg (getlogs.py pulls dmesg.log; app.log loses late appends).
 *
 * Build (produces an image, does not flash): work/tools/build_dual.sh release
 * (or optA for the same image with bring-up diagnostics).
 */
#ifndef CLUSTER_STEP
#define CLUSTER_STEP 3
#endif
#define STEP_RECEIVE (CLUSTER_STEP >= 2)   /* listen, append dataPort, count frames */
#define STEP_RELAY   (CLUSTER_STEP >= 3)   /* inject 111 to the HU and forward */

/* Phase A (step 2a) is transport only: frames are counted, not decrypted.
 * Phase B (step 2b, v1148) sets STEP_DECRYPT=1 and opens VIDEO_FRAME with the
 * stock ChaCha20-Poly1305 primitives. CLUSTER_KEY_INPUT=1 swaps to the Input
 * info string when a car trip shows nal_ok stuck at 0. */
#ifndef STEP_DECRYPT
#define STEP_DECRYPT 0
#endif
#ifndef CLUSTER_KEY_INPUT
#define CLUSTER_KEY_INPUT 0
#endif

/* Diagnostics used while bringing the feature up: stock /info dump, the stock
 * 110 response-entry dump, per-SETUP/TEARDOWN stream-type lines and the
 * c111.h264 AU dump. 1 for test builds (unchanged), 0 for the release build
 * (build_dual.sh release). Status, error and 10 s frame-rate lines stay. */
#ifndef CLUSTER_DIAG
#define CLUSTER_DIAG 1
#endif

/* Phase C direct path (HU_DIRECT): do NOT inject 111 into the dongle->head-unit
 * SetupScreen (that makes stock CPAAProxyEx a 2-screen bridge and crashes the
 * session, v1150). The relay connects to the head unit :7100 itself and seals
 * each AU with the AES-CTR key captured from SetupProxyScreenSecurityInfo —
 * the same key other dongles install for the screen, and the same key the
 * head-unit hook derives for stream 110's connection id. cluster_dual.plainhu
 * is the only plaintext escape. See FINDINGS.md ("Head-unit side"). */
#ifndef HU_DIRECT
#define HU_DIRECT 0
#endif

/* Which /info edits to apply (step 1 found iOS ends the session right after our
 * modified /info; this isolates the cause). bit0 = merge enabledFeatures,
 * bit1 = append the cluster display. 0 = stock /info, logged only. */
#ifndef CLUSTER_ADVERT
#define CLUSTER_ADVERT 3
#endif
#define ADVERT_FEATURES (CLUSTER_ADVERT & 1)
#define ADVERT_DISPLAY  (CLUSTER_ADVERT & 2)
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define UTF8 0x08000100
#define TYPE_MAIN 110
#define TYPE_CLUSTER 111
/* Advertised cluster geometry. Overridable at build time per cluster variant
 * (e.g. the larger MIBSI geometry): build_dual.sh passes -DCLUSTER_W=... etc.
 * Defaults are the B9 Virtual Cockpit 1440x540, 290x108 mm. */
#ifndef CLUSTER_W
#define CLUSTER_W 1440
#endif
#ifndef CLUSTER_H
#define CLUSTER_H 540
#endif
#ifndef CLUSTER_FPS
#define CLUSTER_FPS 30
#endif
#ifndef CLUSTER_WMM
#define CLUSTER_WMM 290
#endif
#ifndef CLUSTER_HMM
#define CLUSTER_HMM 108
#endif
#define CLUSTER_UUID "b7e6c5a0-2222-4000-8000-000000000002"
/* The INITIAL URL decides what the phone renders into the cluster video. The
 * bare URL gives a plain map with no maneuver/ETA (the arrow was missing). The
 * head-unit hook uses showETA + maneuverLayout for a real iPhone, which makes
 * the phone draw the turn card and ETA into the cluster itself. Match that.
 * Override with -DCLUSTER_URL_INITIAL=... (e.g. the RGI-on bare /map when the
 * head unit draws its own maneuver). */
#ifndef CLUSTER_URL_INITIAL
#define CLUSTER_URL_INITIAL "maps:/car/instrumentcluster?showETA=yes&maneuverLayout=rightAligned&showSpeedLimit=no"
#endif
#define URL_CLUSTER "maps:/car/instrumentcluster"
#define URL_MAP "maps:/car/instrumentcluster/map"
#define URL_CARD "maps:/car/instrumentcluster/instructioncard"

/* SafeArea per view, matching the head-unit hook's calibrated A5 values
 * (ALT111_DEFAULT_SAFE_* wide, _SMALL_SAFE_* classic). Height is 270, not the
 * canvas 540: the dials cover the rest. Overridable per cockpit at build time. */
#ifndef CLUSTER_SAFE0_X
#define CLUSTER_SAFE0_X 360
#endif
#ifndef CLUSTER_SAFE0_Y
#define CLUSTER_SAFE0_Y 87
#endif
#ifndef CLUSTER_SAFE0_W
#define CLUSTER_SAFE0_W 720
#endif
#ifndef CLUSTER_SAFE0_H
#define CLUSTER_SAFE0_H 270
#endif
#ifndef CLUSTER_SAFE1_X
#define CLUSTER_SAFE1_X 520
#endif
#ifndef CLUSTER_SAFE1_Y
#define CLUSTER_SAFE1_Y 87
#endif
#ifndef CLUSTER_SAFE1_W
#define CLUSTER_SAFE1_W 400
#endif
#ifndef CLUSTER_SAFE1_H
#define CLUSTER_SAFE1_H 270
#endif

#define OFF_FILE "/mnt/UDISK/cluster_dual.off"
#define NOINJECT_FILE "/mnt/UDISK/cluster_dual.noinject"
#define NORELAY_FILE "/mnt/UDISK/cluster_dual.norelay"
#define PLAINHU_FILE "/mnt/UDISK/cluster_dual.plainhu"
#define DUMP_FILE "/mnt/UDISK/cluster_dump.on"
#define DUMP_PATH "/mnt/UDISK/c111.h264"
/* Same number as the head unit's /mnt/app/cluster_hu.port. Default 7100. */
#define HU_PORT_FILE "/mnt/UDISK/cluster_hu.port"

#define PHONE_PORT 7100          /* preferred phone-facing listener port */
#define HU_PORT_DEFAULT 7100     /* head-unit altscreen hook default (MHI2Q) */
#define HU_MAIN_PEER_PORT 6030   /* dongle's stream-110 data socket peer port */
#define HU_CONTROL_PEER_PORT 5000 /* dongle's AirPlay control socket peer port */
#define HU_CONNECT_MS 1000       /* runs on the phone read path: keep it short */
#define HU_RETRY_S 5             /* pre-connect retry interval */
#define HU_MAX_TRIES 12          /* per head-unit key generation (~1 min) */
#define HU_WRITE_MS 60           /* max per-frame wait for the head-unit socket; else drop */
#define VIEW_COUNT 2             /* advertised ViewAreas (wide, classic) */
#define BC_MAGIC "VIEW"          /* head-unit -> dongle back-channel: "VIEW" + u32 LE index */
#define SPSSI_SIZE 0x2b8         /* SetupProxyScreenSecurityInfo span, v129 */

#define HDR_SIZE 128
#define OP_VIDEO_FRAME 0
#define OP_VIDEO_CONFIG 1
#define MAX_BODY (8u * 1024u * 1024u)
#define DESYNC_LIMIT 5           /* consecutive 111 verify fails -> drop the stream, let iOS re-SETUP */
#define DUMP_LIMIT (2u * 1024u * 1024u)

typedef void *CFRef;
typedef int OSStatus;

typedef struct {
    uint32_t bodySize;
    uint8_t opcode;
    uint8_t smallParam[3];
    uint8_t params[15 * 8];
} ScreenHeader;

typedef union { uint64_t align; unsigned char bytes[512]; } AesCtx;

_Static_assert(sizeof(ScreenHeader) == HDR_SIZE, "screen header must be 128 bytes");

/* ---- real functions (resolved lazily) ---------------------------------- */
static void (*r_ensure)(void **, const void *);
static int64_t (*r_get_i64)(CFRef, CFRef, OSStatus *);
static OSStatus (*r_ctr_init)(void *, const uint8_t *, const uint8_t *);
static OSStatus (*r_ctr_update)(void *, const void *, size_t, void *);
static void (*r_ctr_final)(void *);
static CFRef (*r_info)(void *, void *, void *, OSStatus *);
static OSStatus (*r_setup)(void *, CFRef, CFRef *);
static OSStatus (*r_set_security)(void *, const uint8_t *, const uint8_t *);
static void (*r_teardown)(void *, CFRef, OSStatus, void *);
static OSStatus (*r_send_cmd)(void *, CFRef, void *, void *);
static void (*r_derive)(const void *, size_t, uint64_t, uint8_t *, uint8_t *);

#if STEP_DECRYPT
/* ChaCha state in libCoreUtils v129 writes through offset 0xe8 (233 bytes). */
typedef union { uint64_t align; unsigned char b[256]; } ChaChaCtx;
static OSStatus (*r_pair_derive)(void *, const void *, size_t, const void *, size_t, size_t, uint8_t *);
static void (*r_cc_init)(void *, const uint8_t *, const uint8_t *);
static void (*r_cc_aad)(void *, const void *, size_t);
static size_t (*r_cc_dec)(void *, const void *, size_t, void *);
static size_t (*r_cc_verify)(void *, void *, const uint8_t *, OSStatus *);
#endif

static void (*cf_release)(CFRef);
static CFRef (*cf_retain)(CFRef);
static CFRef (*cf_string)(CFRef, const char *, uint32_t);
static unsigned long (*cf_get_type)(CFRef);
static unsigned long (*cf_dict_type)(void);
static unsigned long (*cf_array_type)(void);
static unsigned long (*cf_string_type)(void);
static CFRef (*cf_dict_get)(CFRef, CFRef);
static void (*cf_dict_set)(CFRef, CFRef, CFRef);
static void (*cf_dict_remove)(CFRef, CFRef);
static void (*cf_dict_set_i64)(CFRef, CFRef, int64_t);
static CFRef (*cf_dict_copy)(CFRef, long, CFRef);
static CFRef (*cf_dict_new)(CFRef, long, const void *, const void *);
static CFRef (*cf_array_new)(CFRef, long, const void *);
static CFRef (*cf_array_copy)(CFRef, long, CFRef);
static void (*cf_array_append)(CFRef, const void *);
static CFRef (*cf_array_get)(CFRef, long);
static long (*cf_array_count)(CFRef);
static const char *(*cf_cstr_ptr)(CFRef, uint32_t);
static int (*cf_cstr_get)(CFRef, char *, long, uint32_t);
static const void *array_cb, *dict_key_cb, *dict_val_cb, *bool_false;

/* introspection (optional; resolved best-effort, not required by the hooks) */
static void (*cf_dict_apply)(CFRef, void (*)(CFRef, CFRef, void *), void *);
static CFRef (*cf_num_create)(CFRef, int, const void *);   /* CFNumberCreate, for adjacentViewAreas */
static int64_t (*cf_get_i64)(CFRef);
static unsigned long (*cf_number_type)(void);
static unsigned long (*cf_boolean_type)(void);
static unsigned long (*cf_data_type)(void);

static CFRef k_type, k_conn, k_streams, k_displays, k_features;

/* address range of CPAAProxyEx's SetupProxyScreenSecurityInfo, for caller test */
static uintptr_t g_spssi_lo, g_spssi_hi;
static void *g_cpaa_base;        /* dli_fbase of a CPAAProxyEx symbol */

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static int g_ok;
static __thread int t_busy;

/* ---- shared session state ---------------------------------------------- */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t g_master[16];
static int g_master_valid;
static uint8_t g_hu_key[16], g_hu_iv[16];   /* head-unit 111 key (== stream 110) */
static int g_hu_key_valid;
/* Two independent generations. A phone-side 111 SETUP or teardown retires the
 * receiver thread (g_rx_gen). A head-unit screen re-setup only re-keys the
 * relay (g_hu_gen). They must not share a counter: a HU re-setup ~190s into a
 * session would otherwise kill the live phone 111 stream. */
static unsigned g_rx_gen;
static unsigned g_hu_gen;

/* ---- logging ----------------------------------------------------------- */
static void dlog(const char *fmt, ...) {
    char b[256], line[288];
    va_list ap;
    int n;
    int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (fd < 0) return;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    n = snprintf(line, sizeof line, "cluster_dual: %s\n", b);
    if (n > 0) { ssize_t w = write(fd, line, (size_t)(n < (int)sizeof line ? n : (int)sizeof line - 1)); (void)w; }
    close(fd);
}

static int flag(const char *path) { return access(path, F_OK) == 0; }
static int disabled(void) {
    const char *e = getenv("CLUSTER_DUAL");
    if (e && e[0] == '0') return 1;
    return flag(OFF_FILE);
}

/* The dongle's musl predates the time64 ABI: no stock binary imports any
 * *time64 symbol. The toolchain's newer musl headers rename dlsym() and time()
 * to __dlsym_time64 / __time64 once <bits/alltypes.h> has been included (here
 * via <arpa/inet.h> before <dlfcn.h>). musl refuses to load a library with an
 * unresolved symbol, so CPAAProxyEx would exit at startup: no hostapd (AP),
 * no USB gadget, no Wi-Fi flashing. Bind the original names explicitly;
 * build.sh also rejects any shim that imports a *time64 symbol. */
void *dlsym_plain(void *__restrict, const char *__restrict) __asm__("dlsym");
long time_plain(long *) __asm__("time");

static void *next_sym(const char *name) {
    void *p = dlsym_plain(RTLD_NEXT, name);
    return p ? p : dlsym_plain(RTLD_DEFAULT, name);
}

#if STEP_DECRYPT
/* Optional symbols: a missing one disables decrypt and leaves the shim up.
 * next_sym only, so check_imports.sh does not grow a hard NEEDED entry. */
static int chacha_resolve(void) {
    if (!r_pair_derive) *(void **)&r_pair_derive = next_sym("PairingSessionDeriveKey");
    if (!r_cc_init) *(void **)&r_cc_init = next_sym("chacha20_poly1305_init_64x64");
    if (!r_cc_aad) *(void **)&r_cc_aad = next_sym("chacha20_poly1305_add_aad");
    if (!r_cc_dec) *(void **)&r_cc_dec = next_sym("chacha20_poly1305_decrypt");
    if (!r_cc_verify) *(void **)&r_cc_verify = next_sym("chacha20_poly1305_verify");
    if (r_pair_derive && r_cc_init && r_cc_aad && r_cc_dec && r_cc_verify) return 1;
    dlog("chacha missing derive=%d init=%d aad=%d dec=%d verify=%d",
         r_pair_derive != 0, r_cc_init != 0, r_cc_aad != 0, r_cc_dec != 0, r_cc_verify != 0);
    return 0;
}
#endif

static CFRef s_cf(const char *text) { return cf_string ? cf_string(0, text, UTF8) : 0; }

/* ---- bind ------------------------------------------------------------- */
static void bind_all(void) {
    Dl_info di;
    void *spssi;
#define NEED(dst, name) do { *(void **)&(dst) = next_sym(name); if (!(dst)) { dlog("missing %s", name); return; } } while (0)
    NEED(r_ensure, "CFArrayEnsureCreatedAndAppend");
    NEED(r_get_i64, "CFDictionaryGetInt64");
    NEED(r_ctr_init, "AES_CTR_Init");
    NEED(r_ctr_update, "AES_CTR_Update");
    NEED(r_ctr_final, "AES_CTR_Final");
    NEED(r_info, "AirPlayCopyServerInfo");
    NEED(r_setup, "AirPlayReceiverSessionSetup");
    NEED(r_set_security, "AirPlayReceiverSessionSetSecurityInfo");
    NEED(r_teardown, "AirPlayReceiverSessionTearDown");
    NEED(r_send_cmd, "AirPlayReceiverSessionSendCommand");
    NEED(r_derive, "AirPlay_DeriveAESKeySHA512ForScreen");
    NEED(cf_release, "CFRelease");
    NEED(cf_retain, "CFRetain");
    NEED(cf_string, "CFStringCreateWithCString");
    NEED(cf_get_type, "CFGetTypeID");
    NEED(cf_dict_type, "CFDictionaryGetTypeID");
    NEED(cf_array_type, "CFArrayGetTypeID");
    NEED(cf_string_type, "CFStringGetTypeID");
    NEED(cf_dict_get, "CFDictionaryGetValue");
    NEED(cf_dict_set, "CFDictionarySetValue");
    NEED(cf_dict_remove, "CFDictionaryRemoveValue");
    NEED(cf_dict_set_i64, "CFDictionarySetInt64");
    NEED(cf_dict_copy, "CFDictionaryCreateMutableCopy");
    NEED(cf_dict_new, "CFDictionaryCreateMutable");
    NEED(cf_array_new, "CFArrayCreateMutable");
    NEED(cf_array_copy, "CFArrayCreateMutableCopy");
    NEED(cf_array_append, "CFArrayAppendValue");
    NEED(cf_array_get, "CFArrayGetValueAtIndex");
    NEED(cf_array_count, "CFArrayGetCount");
    NEED(array_cb, "kCFLArrayCallBacksCFLTypes");
    NEED(dict_key_cb, "kCFLDictionaryKeyCallBacksCFLTypes");
    NEED(dict_val_cb, "kCFLDictionaryValueCallBacksCFLTypes");
    NEED(bool_false, "kCFLBooleanFalse");
    /* kCFLBooleanFalse is a 4-byte pointer VARIABLE (libCoreUtils .data, size 4),
     * not the boolean object: dlsym gives the variable's address. The callback
     * tables above are real structs (20/24 bytes), so they are used by address.
     * v1143 put the variable's address into viewAreas -> malformed /info. */
    bool_false = *(const void *const *)bool_false;
    if (!bool_false) { dlog("kCFLBooleanFalse is NULL"); return; }
#undef NEED
    *(void **)&cf_cstr_ptr = next_sym("CFStringGetCStringPtr");
    *(void **)&cf_cstr_get = next_sym("CFStringGetCString");
    *(void **)&cf_dict_apply = next_sym("CFDictionaryApplyFunction");
    *(void **)&cf_num_create = next_sym("CFNumberCreate");
    *(void **)&cf_get_i64 = next_sym("CFGetInt64");
    *(void **)&cf_number_type = next_sym("CFNumberGetTypeID");
    *(void **)&cf_boolean_type = next_sym("CFBooleanGetTypeID");
    *(void **)&cf_data_type = next_sym("CFDataGetTypeID");

    /* SetupProxyScreenSecurityInfo is a CPAAProxyEx (executable) export */
    spssi = dlsym_plain(RTLD_DEFAULT, "SetupProxyScreenSecurityInfo");
    if (spssi && dladdr(spssi, &di)) {
        g_spssi_lo = (uintptr_t)spssi;
        g_spssi_hi = (uintptr_t)spssi + SPSSI_SIZE;
        g_cpaa_base = di.dli_fbase;
    }

#if STEP_DECRYPT
    chacha_resolve();   /* best-effort; decrypt bails later if any pointer is NULL */
#endif
    k_type = s_cf("type");
    k_conn = s_cf("streamConnectionID");
    k_streams = s_cf("streams");
    k_displays = s_cf("displays");
    k_features = s_cf("enabledFeatures");
    g_ok = k_type && k_conn && k_streams && k_displays && k_features;
    if (!g_ok) dlog("CF key alloc failed");
    /* step-1 readiness signal for step 3: both must be non-zero, otherwise the
     * head-unit key capture and the 111 injection are silently inactive */
    dlog("bind ok=%d step=%d spssi=%p cpaa_base=%p", g_ok, CLUSTER_STEP,
         (void *)g_spssi_lo, g_cpaa_base);
}

static int ready(void) {
    pthread_once(&g_once, bind_all);
    return g_ok;
}

/* ---- CF helpers -------------------------------------------------------- */
static int is_type(CFRef v, unsigned long (*type_fn)(void)) {
    return v && type_fn && cf_get_type(v) == type_fn();
}

static int dict_type(CFRef d) {
    OSStatus err = 0;
    int64_t t;
    if (!is_type(d, cf_dict_type)) return -1;
    t = r_get_i64(d, k_type, &err);
    return err ? -1 : (int)t;
}

static uint64_t dict_cid(CFRef d) {
    OSStatus err = 0;
    int64_t v;
    if (!is_type(d, cf_dict_type)) return 0;
    v = r_get_i64(d, k_conn, &err);
    return err ? 0 : (uint64_t)v;
}

static void set_i64(CFRef d, const char *key, int64_t v) {
    CFRef k = s_cf(key);
    if (k) { cf_dict_set_i64(d, k, v); cf_release(k); }
}
static void set_str(CFRef d, const char *key, const char *v) {
    CFRef k = s_cf(key), s = s_cf(v);
    if (k && s) cf_dict_set(d, k, s);
    if (s) cf_release(s);
    if (k) cf_release(k);
}
static void set_value(CFRef d, const char *key, CFRef v) {
    CFRef k = s_cf(key);
    if (k) { cf_dict_set(d, k, v); cf_release(k); }
}
static void remove_key(CFRef d, const char *key) {
    CFRef k = s_cf(key);
    if (k) { cf_dict_remove(d, k); cf_release(k); }
}
static int ascii_eq(CFRef v, const char *want) {
    char buf[64];
    const char *p = 0;
    if (!is_type(v, cf_string_type)) return 0;
    if (cf_cstr_ptr) p = cf_cstr_ptr(v, UTF8);
    if (!p && cf_cstr_get && cf_cstr_get(v, buf, (long)sizeof buf, UTF8)) p = buf;
    return p && strcmp(p, want) == 0;
}
static CFRef streams_of(CFRef d) {
    CFRef s;
    if (!is_type(d, cf_dict_type)) return 0;
    s = cf_dict_get(d, k_streams);
    return is_type(s, cf_array_type) ? s : 0;
}
static CFRef find_stream(CFRef dict, int type) {
    CFRef s = streams_of(dict);
    long i, n;
    if (!s) return 0;
    n = cf_array_count(s);
    for (i = 0; i < n; i++) {
        CFRef sd = cf_array_get(s, i);
        if (dict_type(sd) == type) return sd;
    }
    return 0;
}
static void log_stream_types(const char *tag, CFRef dict) {
    CFRef s = streams_of(dict);
    char b[96];
    long i, n;
    int used;
    if (!s) { dlog("%s streams=<none>", tag); return; }
    n = cf_array_count(s);
    used = snprintf(b, sizeof b, "%s types=", tag);
    for (i = 0; i < n && i < 8 && used > 0 && used < (int)sizeof b - 8; i++)
        used += snprintf(b + used, sizeof b - (size_t)used, "%s%d", i ? "," : "", dict_type(cf_array_get(s, i)));
    dlog("%s", b);
}

/* ---- caller identity --------------------------------------------------- */
static int caller_is_cpaa(void *ra) {
    Dl_info di;
    if (!g_cpaa_base) return 0;
    return dladdr(ra, &di) && di.dli_fbase == g_cpaa_base;
}

/* ============ PHONE SIDE: /info advertisement ============ */
static void merge_features(CFRef info) {
    CFRef existing = cf_dict_get(info, k_features), copy, alt = 0, va = 0;
    long i, n = 0;
    /* viewAreas always: every head-unit session where iOS accepted the cluster
     * advertised it (root feature + per-display viewAreas); v1142 without it
     * was torn down by the phone ~100 ms after /info. */
    int have_alt = 0, have_va = 0, want_va = 1;
    if (is_type(existing, cf_array_type)) {
        copy = cf_array_copy(0, 0, existing);
        if (copy) n = cf_array_count(copy);
    } else {
        copy = cf_array_new(0, 0, array_cb);
    }
    if (!copy) return;
    for (i = 0; i < n; i++) {
        CFRef it = cf_array_get(copy, i);
        if (ascii_eq(it, "altScreen")) have_alt = 1;
        else if (ascii_eq(it, "viewAreas")) have_va = 1;
    }
    if (!have_alt && (alt = s_cf("altScreen"))) cf_array_append(copy, alt);
    if (want_va && !have_va && (va = s_cf("viewAreas"))) cf_array_append(copy, va);
    cf_dict_set(info, k_features, copy);
    if (va) cf_release(va);
    if (alt) cf_release(alt);
    cf_release(copy);
}

/* One view area: full 1440x540 canvas with a safe area, policy booleans false.
 * Identical to the head-unit hook's add_reference_viewarea (carplay_logs/021). */
static void add_view(CFRef areas, int sx, int sy, int sw, int sh) {
    CFRef view = cf_dict_new(0, 0, dict_key_cb, dict_val_cb);
    CFRef safe = cf_dict_new(0, 0, dict_key_cb, dict_val_cb);
    if (view && safe) {
        set_i64(view, "widthPixels", CLUSTER_W);
        set_i64(view, "heightPixels", CLUSTER_H);
        set_i64(view, "originXPixels", 0);
        set_i64(view, "originYPixels", 0);
        set_i64(safe, "widthPixels", sw);
        set_i64(safe, "heightPixels", sh);
        set_i64(safe, "originXPixels", sx);
        set_i64(safe, "originYPixels", sy);
        set_value(view, "drawUIOutsideSafeArea", (CFRef)bool_false);
        set_value(view, "viewAreaTransitionControl", (CFRef)bool_false);
        set_value(view, "safeArea", safe);
        cf_array_append(areas, view);
    }
    if (safe) cf_release(safe);
    if (view) cf_release(view);
}

static void add_views(CFRef alt) {
    CFRef areas = cf_array_new(0, 0, array_cb);
    if (!areas) return;
    /* SafeArea = where the phone keeps the puck/turn card so the dials do not
     * cover them. These must match the head-unit hook's calibrated values for
     * this cockpit (ALT111_DEFAULT_SAFE_* / _SMALL_SAFE_*); the dongle advertised
     * height 297 vs the real 270, so content landed too low/wide (car photo). */
    add_view(areas, CLUSTER_SAFE0_X, CLUSTER_SAFE0_Y, CLUSTER_SAFE0_W, CLUSTER_SAFE0_H); /* [0] wide */
    add_view(areas, CLUSTER_SAFE1_X, CLUSTER_SAFE1_Y, CLUSTER_SAFE1_W, CLUSTER_SAFE1_H); /* [1] classic */
    set_value(alt, "viewAreas", areas);
    set_i64(alt, "initialViewArea", 0);
    cf_release(areas);
}

static void add_urls(CFRef alt) {
    CFRef urls = cf_array_new(0, 0, array_cb);
    CFRef a = s_cf(URL_CLUSTER), b = s_cf(URL_MAP), c = s_cf(URL_CARD);
    if (urls && a && b && c) {
        cf_array_append(urls, a);
        cf_array_append(urls, b);
        cf_array_append(urls, c);
        set_value(alt, "altScreenSuggestUIURLs", urls);
    }
    if (c) cf_release(c);
    if (b) cf_release(b);
    if (a) cf_release(a);
    if (urls) cf_release(urls);
}

static int already_cluster(CFRef displays) {
    long i, n = cf_array_count(displays);
    for (i = 0; i < n; i++) {
        CFRef d = cf_array_get(displays, i);
        if (dict_type(d) == TYPE_CLUSTER) return 1;
    }
    return 0;
}

/* ---- stock /info introspection (diagnostic; see STEP 1 RESULT) ---------- */
static void describe(CFRef v, char *out, size_t cap) {
    unsigned long tid;
    if (!v) { snprintf(out, cap, "<null>"); return; }
    tid = cf_get_type ? cf_get_type(v) : 0;
    if (cf_string_type && tid == cf_string_type()) {
        /* Copy into our own buffer. CFStringGetCStringPtr returns an internal
         * (sometimes shared/stale) pointer, which garbled this dump before. */
        char b[96];
        b[0] = 0;
        if (!(cf_cstr_get && cf_cstr_get(v, b, (long)sizeof b, UTF8)) && cf_cstr_ptr) {
            const char *p = cf_cstr_ptr(v, UTF8);
            if (p) { strncpy(b, p, sizeof b - 1); b[sizeof b - 1] = 0; }
        }
        snprintf(out, cap, "\"%s\"", b);
    } else if (cf_number_type && tid == cf_number_type()) {
        snprintf(out, cap, "%lld", (long long)(cf_get_i64 ? cf_get_i64(v) : 0));
    } else if (cf_boolean_type && tid == cf_boolean_type()) {
        snprintf(out, cap, "%s", (v == bool_false) ? "false" : "true");
    } else if (cf_array_type && tid == cf_array_type()) {
        snprintf(out, cap, "[array n=%ld]", cf_array_count(v));
    } else if (cf_dict_type && tid == cf_dict_type()) {
        snprintf(out, cap, "{dict}");
    } else if (cf_data_type && tid == cf_data_type()) {
        snprintf(out, cap, "<data>");
    } else {
        snprintf(out, cap, "<tid=%lu>", tid);   /* unknown type -> show its id */
    }
}

static void log_kv(CFRef key, CFRef val, void *ctx) {
    char k[64], v[128];
    describe(key, k, sizeof k);
    describe(val, v, sizeof v);
    dlog("  %s %s = %s", (const char *)ctx, k, v);
}

static void log_info_dict(CFRef base) {
    CFRef displays;
    long i, n;
    if (!cf_dict_apply || !is_type(base, cf_dict_type)) {
        dlog("stock /info: introspection unavailable");
        return;
    }
    dlog("stock /info root:");
    cf_dict_apply(base, log_kv, (void *)"root");
    displays = cf_dict_get(base, k_displays);
    if (!is_type(displays, cf_array_type)) return;
    n = cf_array_count(displays);
    for (i = 0; i < n && i < 4; i++) {
        CFRef d = cf_array_get(displays, i);
        char tag[16];
        snprintf(tag, sizeof tag, "disp[%ld]", i);
        if (is_type(d, cf_dict_type)) cf_dict_apply(d, log_kv, tag);
    }
}

/* Return a new info dict, or 0 to keep base. Caller releases base on success. */
static CFRef augment_info(CFRef base) {
    CFRef info, old, displays, stock, alt;
    if (!ADVERT_FEATURES && !ADVERT_DISPLAY) return 0;   /* stock /info (log only) */
    if (!is_type(base, cf_dict_type)) return 0;
    old = cf_dict_get(base, k_displays);
    if (!is_type(old, cf_array_type) || cf_array_count(old) < 1) return 0;
    if (already_cluster(old)) return 0;
    info = cf_dict_copy(0, 0, base);
    if (!info) return 0;
    if (ADVERT_FEATURES) merge_features(info);
    if (!ADVERT_DISPLAY) {
        dlog("/info enabledFeatures merged, display not added (ADVERT=%d)", CLUSTER_ADVERT);
        return info;
    }
    displays = cf_array_copy(0, 0, old);
    stock = cf_array_get(old, 0);
    alt = is_type(stock, cf_dict_type) ? cf_dict_copy(0, 0, stock) : 0;
    if (!displays || !alt) {
        if (displays) cf_release(displays);
        if (alt) cf_release(alt);
        cf_release(info);
        return 0;
    }
    remove_key(alt, "primaryInputDevice");
    remove_key(alt, "edid");
    remove_key(alt, "platformLayer");
    remove_key(alt, "avcc");
    remove_key(alt, "xOffset");
    remove_key(alt, "yOffset");
    remove_key(alt, "windowWidth");
    remove_key(alt, "windowHeight");
    set_i64(alt, "type", TYPE_CLUSTER);
    set_i64(alt, "maxFPS", CLUSTER_FPS);
    set_i64(alt, "features", 0);
    set_i64(alt, "widthPixels", CLUSTER_W);
    set_i64(alt, "heightPixels", CLUSTER_H);
    set_i64(alt, "widthPhysical", CLUSTER_WMM);
    set_i64(alt, "heightPhysical", CLUSTER_HMM);
    set_str(alt, "uuid", CLUSTER_UUID);
    /* no "displayUUID": the stock display and the head-unit hook's both use only "uuid" */
    set_str(alt, "initialURL", CLUSTER_URL_INITIAL);
    add_urls(alt);
    add_views(alt);
    cf_array_append(displays, alt);
    cf_dict_set(info, k_displays, displays);
    dlog("/info cluster display added displays=%ld", cf_array_count(displays));
    cf_release(alt);
    cf_release(displays);
    return info;
}

CFRef AirPlayCopyServerInfo(void *session, void *properties, void *mac, OSStatus *err) {
    CFRef base, edited;
    if (!ready()) return r_info ? r_info(session, properties, mac, err) : 0;
    if (t_busy) return r_info(session, properties, mac, err);
    t_busy = 1;
    base = r_info(session, properties, mac, err);
    t_busy = 0;
    if (!base || disabled()) return base;
    {
        static int dumped;
        if (CLUSTER_DIAG && dumped < 2) { dumped++; log_info_dict(base); }
    }
    edited = augment_info(base);
    if (!edited) return base;
    cf_release(base);
    return edited;
}

/* ============ KEY CAPTURE ============ */
/* Phone master key: AirPlayReceiverSessionSetSecurityInfo(session, key16, iv16). */
OSStatus AirPlayReceiverSessionSetSecurityInfo(void *session, const uint8_t *key, const uint8_t *iv) {
    OSStatus e;
    if (!ready()) return r_set_security ? r_set_security(session, key, iv) : -1;
    e = r_set_security(session, key, iv);
    if (e == 0 && key && !disabled()) {
        pthread_mutex_lock(&g_lock);
        memcpy(g_master, key, 16);
        g_master_valid = 1;
        pthread_mutex_unlock(&g_lock);
        dlog("phone master key captured");
    }
    return e;
}

/* Which cipher does stock pick for the phone's 110 screen? (log-only; answers
 * U2 in step 1). A wireless session with pair-verify can use ChaCha for the
 * screen even when an AES master was set, and then the step-2 AES derivation
 * for 111 would be wrong. Signatures from libAirPlay v129:
 *   Screen_SetSecurityInfo(screen, key[16], iv[16])        0x1b3a0 -> AES_CTR_Init
 *   Screen_SetChaChaSecurityInfo(screen, key, keyLen==32)  0x1b318 */
static OSStatus (*r_scr_aes)(void *, const uint8_t *, const uint8_t *);
static OSStatus (*r_scr_chacha)(void *, const uint8_t *, size_t);

OSStatus AirPlayReceiverSessionScreen_SetSecurityInfo(void *screen, const uint8_t *key, const uint8_t *iv) {
    if (!r_scr_aes) *(void **)&r_scr_aes = next_sym("AirPlayReceiverSessionScreen_SetSecurityInfo");
    if (!r_scr_aes) return -1;
    if (!disabled()) dlog("phone screen security: AES-CTR screen=%p", screen);
    return r_scr_aes(screen, key, iv);
}

OSStatus AirPlayReceiverSessionScreen_SetChaChaSecurityInfo(void *screen, const uint8_t *key, size_t len) {
    if (!r_scr_chacha) *(void **)&r_scr_chacha = next_sym("AirPlayReceiverSessionScreen_SetChaChaSecurityInfo");
    if (!r_scr_chacha) return -1;
    if (!disabled()) dlog("phone screen security: ChaCha20-Poly1305 keylen=%u screen=%p (step 2 AES path will not decrypt 111)",
                          (unsigned)len, screen);
    return r_scr_chacha(screen, key, len);
}

/* Head-unit 111 key: AES_CTR_Init called from SetupProxyScreenSecurityInfo.
 * The clone keeps 110's streamConnectionID, so 110's key == 111's key. */
OSStatus AES_CTR_Init(void *ctx, const uint8_t *key, const uint8_t *iv) {
    OSStatus e;
    unsigned gen;
    void *ra = __builtin_return_address(0);
    if (!ready()) return r_ctr_init ? r_ctr_init(ctx, key, iv) : -1;
    e = r_ctr_init(ctx, key, iv);
    if (e == 0 && key && iv && !disabled() && g_spssi_lo &&
        (uintptr_t)ra >= g_spssi_lo && (uintptr_t)ra < g_spssi_hi) {
        pthread_mutex_lock(&g_lock);
        memcpy(g_hu_key, key, 16);
        memcpy(g_hu_iv, iv, 16);
        g_hu_key_valid = 1;
        gen = ++g_hu_gen;        /* new head-unit SETUP -> relay reconnects + re-keys */
        pthread_mutex_unlock(&g_lock);
        dlog("head-unit screen key captured hu_gen=%u", gen);
    }
    return e;
}

/* ============ HEAD-UNIT SIDE: inject 111 into the SetupScreen streams ============ */
void CFArrayEnsureCreatedAndAppend(void **ioArray, const void *value) {
    void *ra = __builtin_return_address(0);
    CFRef clone;
    if (!r_ensure) { r_ensure = (void (*)(void **, const void *))next_sym("CFArrayEnsureCreatedAndAppend"); }
    r_ensure(ioArray, value);                 /* normal append (main stream) */
    if (!STEP_RELAY || HU_DIRECT) return;      /* 111->HU injection is step 3; HU_DIRECT relays directly, no inject */
    if (!ready() || disabled() || flag(NOINJECT_FILE)) return;
    if (t_busy || !ioArray || !*ioArray || !value) return;
    if (!caller_is_cpaa(ra)) return;
    if (!is_type((CFRef)value, cf_dict_type)) return;
    if (!cf_dict_get((CFRef)value, k_conn)) return;        /* must be a stream dict */
    {   /* clone the main stream only. The old mirror shim matched on
         * streamConnectionID alone, so also accept a dict whose "type" is not
         * set yet at append time; reject any other explicit type. */
        int t = dict_type((CFRef)value);
        if (t != TYPE_MAIN && t != -1) return;
    }
    t_busy = 1;
    clone = cf_dict_copy(0, 0, (CFRef)value);
    if (clone) {
        set_i64(clone, "type", TYPE_CLUSTER);              /* keep streamConnectionID */
        r_ensure(ioArray, clone);
        cf_release(clone);
        dlog("injected head-unit cluster stream type=111");
    }
    t_busy = 0;
}

/* ---- socket helpers ---------------------------------------------------- */
static int read_full(int fd, void *buf, size_t len, unsigned gen) {
    uint8_t *p = buf;
    size_t off = 0;
    while (off < len) {
        struct pollfd pf = { fd, POLLIN, 0 };
        ssize_t r;
        int pr = poll(&pf, 1, 1000);
        if (pr < 0) { if (errno == EINTR) continue; return -1; }
        if (pr == 0) {
            int stale;
            pthread_mutex_lock(&g_lock);
            stale = (gen != g_rx_gen);
            pthread_mutex_unlock(&g_lock);
            if (stale) return -1;
            continue;
        }
        r = read(fd, p + off, len - off);
        if (r <= 0) { if (r < 0 && errno == EINTR) continue; return r == 0 ? 0 : -1; }
        off += (size_t)r;
    }
    return 1;
}

static int send_full(int fd, const void *buf, size_t len) {
    const uint8_t *p = buf;
    size_t off = 0;
    while (off < len) {
        ssize_t w;
        struct pollfd pf = { fd, POLLOUT, 0 };
        int pr = poll(&pf, 1, 2000);
        if (pr <= 0) return -1;
        w = send(fd, p + off, len - off, MSG_NOSIGNAL);
        if (w <= 0) { if (w < 0 && errno == EINTR) continue; return -1; }
        off += (size_t)w;
    }
    return 0;
}

static int hu_port(void) {
    char b[16];
    int fd = open(HU_PORT_FILE, O_RDONLY | O_CLOEXEC), p = 0;
    if (fd >= 0) {
        ssize_t n = read(fd, b, sizeof b - 1);
        close(fd);
        if (n > 0) { b[n] = 0; p = atoi(b); }
    }
    return (p > 0 && p < 65536) ? p : HU_PORT_DEFAULT;
}

/* Find the head-unit address: the peer of the dongle's stream-110 data socket
 * (HU port 6030), else the peer of its AirPlay control socket (HU port 5000,
 * "Host:fe80::…%usb0:5000" in app.log). Both are outbound IPv6 link-local. */
static int hu_addr(struct sockaddr_in6 *out) {
    static const int ports[] = { HU_MAIN_PEER_PORT, HU_CONTROL_PEER_PORT };
    unsigned i;
    int fd;
    for (i = 0; i < sizeof ports / sizeof ports[0]; i++) {
        for (fd = 3; fd < 1024; fd++) {
            struct sockaddr_in6 sa;
            socklen_t sl = sizeof sa;
            if (getpeername(fd, (struct sockaddr *)&sa, &sl) != 0) continue;
            if (sa.sin6_family != AF_INET6) continue;
            if (ntohs(sa.sin6_port) != ports[i]) continue;
            *out = sa;                  /* keeps the %usb0 scope id */
            return 1;
        }
    }
    return 0;
}

static int hu_connect(void) {
    struct sockaddr_in6 sa;
    int fd, one = 1;
    long fl;
    struct pollfd pf;
    if (!hu_addr(&sa)) { dlog("head-unit addr not found (no peer :6030/:5000)"); return -1; }
    sa.sin6_port = htons((uint16_t)hu_port());
    fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return -1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0 && errno != EINPROGRESS) {
        close(fd); return -1;
    }
    pf.fd = fd; pf.events = POLLOUT; pf.revents = 0;
    if (poll(&pf, 1, HU_CONNECT_MS) <= 0) { close(fd); dlog("head-unit connect timeout"); return -1; }
    {
        int err = 0; socklen_t el = sizeof err;
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
        if (err) { close(fd); dlog("head-unit connect err=%d", err); return -1; }
    }
    fcntl(fd, F_SETFL, fl);
    {
        char ip[64] = "?";
        inet_ntop(AF_INET6, &sa.sin6_addr, ip, sizeof ip);
        dlog("head-unit 111 connected %s%%%u:%u", ip, sa.sin6_scope_id, ntohs(sa.sin6_port));
    }
    return fd;
}

/* SendCommand dispatches onto the session queue and the stock callers always
 * pass a completion; give it a no-op one rather than NULL. */
static void kf_done(OSStatus e, CFRef resp, void *ctx) {
    (void)resp; (void)ctx;
    if (e) dlog("forceKeyFrame completion rc=%d", e);
}

static void ask_keyframe(void *session) {
    CFRef req, params;
    if (!session || !r_send_cmd) return;
    req = cf_dict_new(0, 0, dict_key_cb, dict_val_cb);
    params = cf_dict_new(0, 0, dict_key_cb, dict_val_cb);
    if (req && params) {
        OSStatus e;
        set_str(req, "type", "forceKeyFrame");
        set_str(params, "uuid", CLUSTER_UUID);
        set_value(req, "params", params);
        e = r_send_cmd(session, req, (void *)kf_done, 0);
        dlog("forceKeyFrame requested rc=%d", e);
    }
    if (params) cf_release(params);
    if (req) cf_release(req);
}

/* Forward a cockpit layout change to the phone: updateViewArea{viewAreaIndex}.
 * Same send path as ask_keyframe. adjacentViewAreas (the other advertised
 * indices, as CFNumbers) is required or iOS acks but keeps the old layout. */
__attribute__((unused)) static void send_view(void *session, int view) {
    CFRef req, params, adj = 0;
    if (!session || !r_send_cmd || view < 0 || view >= VIEW_COUNT) return;
    req = cf_dict_new(0, 0, dict_key_cb, dict_val_cb);
    params = cf_dict_new(0, 0, dict_key_cb, dict_val_cb);
    if (req && params) {
        OSStatus e;
        int v;
        set_str(req, "type", "updateViewArea");
        set_str(params, "uuid", CLUSTER_UUID);
        set_i64(params, "viewAreaIndex", view);
        set_i64(params, "animationDurationMillis", 0);
        if (cf_num_create && (adj = cf_array_new(0, 0, array_cb))) {
            for (v = 0; v < VIEW_COUNT; v++) {
                int64_t idx = v;
                CFRef num;
                if (v == view) continue;
                num = cf_num_create(0, 4 /* kCFNumberSInt64Type */, &idx);
                if (num) { cf_array_append(adj, num); cf_release(num); }
            }
            set_value(params, "adjacentViewAreas", adj);
        }
        set_value(req, "params", params);
        e = r_send_cmd(session, req, (void *)kf_done, 0);
        dlog("updateViewArea -> phone index=%d rc=%d", view, e);
    }
    if (adj) cf_release(adj);
    if (params) cf_release(params);
    if (req) cf_release(req);
}

/* ---- H.264 sanity checks (the step-2 decrypt test; no dongle shell needed) --
 * A decrypted AVCC access unit tiles exactly into length-prefixed NALs; a wrong
 * key fails this on the first frame. nal_len comes from the avcC (byte 4 & 3)+1. */
static int au_ok(const uint8_t *p, uint32_t n, int nal_len, int *has_idr) {
    uint32_t off = 0;
    int nals = 0;
    *has_idr = 0;
    if (nal_len < 1 || nal_len > 4) nal_len = 4;
    while (off + (uint32_t)nal_len <= n) {
        uint32_t len = 0;
        int k;
        for (k = 0; k < nal_len; k++) len = (len << 8) | p[off + (uint32_t)k];
        off += (uint32_t)nal_len;
        if (len == 0 || len > n - off) return 0;
        if (p[off] & 0x80) return 0;                 /* forbidden_zero_bit must be 0 */
        if ((p[off] & 0x1f) == 5) *has_idr = 1;      /* IDR slice anywhere in the AU */
        off += len;
        nals++;
    }
    return off == n && nals > 0;
}

/* minimal MSB-first bit reader over an RBSP */
struct br { const uint8_t *p; uint32_t n, pos, bad; };
static unsigned br_bit(struct br *b) {
    unsigned v;
    if (b->pos >= b->n * 8) { b->bad = 1; return 0; }
    v = (b->p[b->pos >> 3] >> (7 - (b->pos & 7))) & 1u;
    b->pos++;
    return v;
}
static unsigned br_bits(struct br *b, int k) {
    unsigned v = 0;
    while (k-- > 0) v = (v << 1) | br_bit(b);
    return v;
}
static unsigned br_ue(struct br *b) {
    int z = 0;
    while (!b->bad && br_bit(b) == 0) if (++z > 31) { b->bad = 1; return 0; }
    return ((1u << z) - 1u) + br_bits(b, z);
}

/* NAL-length field width + coded WxH from the first SPS in an avcC record.
 * Returns the nal_len (1..4); sets w/h to 0 if the SPS is not parsed. */
static int avcc_parse(const uint8_t *c, uint32_t n, int *w, int *h) {
    uint8_t rb[192];
    uint32_t sps_len, i, rn = 0;
    unsigned profile, cw, ch, fmo = 1, cl = 0, cr = 0, ct = 0, cb = 0, poc;
    int nal_len = 4;
    struct br b;
    *w = *h = 0;
    if (n >= 5 && c[0] == 1) nal_len = (c[4] & 3) + 1;
    if (n < 9 || c[0] != 1 || (c[5] & 0x1f) < 1) return nal_len;
    sps_len = ((uint32_t)c[6] << 8) | c[7];
    if (sps_len < 4 || 8 + sps_len > n) return nal_len;
    for (i = 1; i < sps_len && rn < sizeof rb; i++) {       /* strip header + emulation */
        if (i >= 3 && c[8 + i] == 3 && c[8 + i - 1] == 0 && c[8 + i - 2] == 0) continue;
        rb[rn++] = c[8 + i];
    }
    b.p = rb; b.n = rn; b.pos = 0; b.bad = 0;
    profile = br_bits(&b, 8);
    br_bits(&b, 16);                                        /* constraints + level */
    br_ue(&b);                                              /* sps id */
    if (profile == 100 || profile == 110 || profile == 122 || profile == 244 ||
        profile == 44 || profile == 83 || profile == 86 || profile == 118 || profile == 128) {
        if (br_ue(&b) == 3) br_bit(&b);                     /* chroma_format_idc */
        br_ue(&b); br_ue(&b); br_bit(&b);                   /* bit depths, qpprime */
        if (br_bit(&b)) return nal_len;                     /* scaling lists: give up */
    }
    br_ue(&b);                                              /* log2_max_frame_num */
    poc = br_ue(&b);
    if (poc == 0) { br_ue(&b); }
    else if (poc == 1) {
        unsigned nref, j;
        br_bit(&b); br_ue(&b); br_ue(&b); nref = br_ue(&b);
        if (nref > 128) return nal_len;
        for (j = 0; j < nref; j++) br_ue(&b);
    }
    br_ue(&b); br_bit(&b);                                  /* max_num_ref_frames, gaps */
    cw = br_ue(&b); ch = br_ue(&b);
    if (br_bit(&b)) fmo = 1; else { fmo = 0; br_bit(&b); }  /* frame_mbs_only, field flag */
    br_bit(&b);                                             /* direct_8x8_inference */
    if (br_bit(&b)) { cl = br_ue(&b); cr = br_ue(&b); ct = br_ue(&b); cb = br_ue(&b); }
    if (b.bad) return nal_len;
    *w = (int)((cw + 1) * 16 - 2 * (cl + cr));
    *h = (int)((2 - fmo) * (ch + 1) * 16 - 2 * (2 - fmo) * (ct + cb));
    if (*w <= 0 || *h <= 0) { *w = *h = 0; }
    return nal_len;
}

/* ============ PHONE 111 RECEIVER + RELAY ============ */
#if STEP_DECRYPT
/* session+0x3ec is the PairingSession (FUN_0x12dcc, ldr [r0,#0x3ec]).
 * Salt and info match that function: Output info is 32 bytes, Input is 31.
 * The key is the phone's TX key, which is what the dongle decrypts with. */
static int chacha_derive(void *session, uint64_t cid, uint8_t key[32]) {
    void *pair;
    char salt[48];
    const char *info = CLUSTER_KEY_INPUT
        ? "DataStream-Input-Encryption-Key"
        : "DataStream-Output-Encryption-Key";
    int n;
    OSStatus e;
    if (!session || !chacha_resolve()) return 0;
    pair = *(void **)((char *)session + 0x3ec);
    if (!pair) { dlog("pairing session at +0x3ec is NULL"); return 0; }
    n = snprintf(salt, sizeof salt, "DataStream-Salt%llu", (unsigned long long)cid);
    if (n <= 0 || (size_t)n >= sizeof salt) return 0;
    e = r_pair_derive(pair, salt, (size_t)n, info, strlen(info), 32, key);
    if (e != 0) {
        memset(key, 0, 32);
        dlog("PairingSessionDeriveKey rc=%d info=%s", e, info);
        return 0;
    }
    dlog("111 chacha key derived info=%s saltLen=%d", info, n);
    return 1;
}

/* Open one VIDEO_FRAME in place. On success *plain_len is the AU length
 * (tag removed) and the 8-byte little-endian nonce advances by 1.
 * ProcessFrames 0x1bab4: AAD is the 128-byte header, tag is the last 16
 * body bytes, and the nonce advances only after verify succeeds. */
static int chacha_open(const uint8_t key[32], uint8_t nonce[8],
                       const ScreenHeader *hdr, uint8_t *body, uint32_t body_len,
                       uint32_t *plain_len) {
    ChaChaCtx ctx;
    OSStatus verr = -1;
    size_t ct, n1, n2 = 0;
    int i;
    /* stock decrypts when bodySize > 15 (0x1b80c): a tag-only frame (16 bytes,
     * empty AU) still verifies and advances the nonce; rejecting it would
     * desync every frame after it. */
    if (body_len < 16) return 0;
    ct = (size_t)body_len - 16;
    r_cc_init(&ctx, key, nonce);
    r_cc_aad(&ctx, hdr, HDR_SIZE);
    n1 = r_cc_dec(&ctx, body, ct, body);
    if (n1 <= ct) n2 = r_cc_verify(&ctx, body + n1, body + ct, &verr);
    if (verr != 0 || n1 > ct || n2 != ct - n1) {
        static __thread int logged;
        if (!logged) {
            logged = 1;
            dlog("111 chacha verify fail err=%d n=%u+%u ct=%u",
                 verr, (unsigned)n1, (unsigned)n2, (unsigned)ct);
        }
        return 0;
    }
    for (i = 0; i < 8; i++) if (++nonce[i]) break;
    *plain_len = (uint32_t)ct;
    return 1;
}
#endif

struct rx_arg { int listen_fd; unsigned gen; uint64_t cid; void *session; };

static void dump_au(int *dfd, size_t *dn, const uint8_t *p, size_t n) {
    if (!flag(DUMP_FILE)) return;
    if (*dfd < 0 && *dn == 0) *dfd = open(DUMP_PATH, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (*dfd >= 0 && *dn < DUMP_LIMIT) {
        ssize_t w = write(*dfd, p, n);
        if (w > 0) *dn += (size_t)w;
    }
}

/* ---- head-unit relay (shared) -------------------------------------------
 * The head-unit hook accepts ONE stream-111 connection per head-unit SETUP and
 * closes its listener when that connection ends. The relay socket therefore
 * belongs to the process, not to a receiver thread: it survives an iPhone 111
 * reconnect (new receiver thread) and is dropped only on a send failure or when
 * a new head-unit SETUP re-keys it (g_hu_gen). Only the current receiver
 * (rx_gen == g_rx_gen) may write; g_relay_lock serializes. */
static pthread_mutex_t g_relay_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    int fd;              /* -1 = no socket */
    int enc;             /* ctx valid (else plain, cluster_dual.plainhu) */
    AesCtx ctx;          /* head-unit AES-CTR, continuous for the socket's life */
    unsigned for_gen;    /* g_hu_gen the socket / tries belong to */
    int dead;            /* gave up for for_gen; revived by the next g_hu_gen */
    int tries;           /* connect attempts for for_gen */
    long next_try;       /* seconds, from time_plain() (32-bit time on the dongle) */
    int nokey_logged;
    unsigned long relayed, err, dropped;
    int backlog;             /* consecutive drops; 0 after a successful send */
    uint8_t bc[8];           /* back-channel accumulator (VIEW message) */
    int bc_len;
} g_relay = { .fd = -1 };

static void relay_drop_locked(int dead) {
    if (g_relay.fd >= 0) close(g_relay.fd);
    g_relay.fd = -1;
    if (g_relay.enc) { r_ctr_final(&g_relay.ctx); g_relay.enc = 0; }
    g_relay.dead = dead;
    g_relay.backlog = 0;
    g_relay.bc_len = 0;
}

/* Non-blocking read of the head-unit back-channel (VIEW + u32 index). Returns
 * the ViewArea index when a full message arrives, else -1. The head unit sends
 * it on the stream-111 socket when the cockpit layout changes. */
__attribute__((unused)) static int relay_poll_view(void) {
    int view = -1;
    pthread_mutex_lock(&g_relay_lock);
    if (g_relay.fd >= 0) {
        struct pollfd pf = { g_relay.fd, POLLIN, 0 };
        while (view < 0 && poll(&pf, 1, 0) == 1 && (pf.revents & POLLIN)) {
            ssize_t r = recv(g_relay.fd, g_relay.bc + g_relay.bc_len,
                             (size_t)(8 - g_relay.bc_len), MSG_DONTWAIT);
            if (r <= 0) break;
            g_relay.bc_len += (int)r;
            if (g_relay.bc_len == 8) {
                if (memcmp(g_relay.bc, BC_MAGIC, 4) == 0) {
                    view = g_relay.bc[4] | (g_relay.bc[5] << 8) |
                           (g_relay.bc[6] << 16) | (g_relay.bc[7] << 24);
                    g_relay.bc_len = 0;
                } else {                       /* resync: drop the first byte */
                    memmove(g_relay.bc, g_relay.bc + 1, 7);
                    g_relay.bc_len = 7;
                }
            }
            pf.revents = 0;
        }
    }
    pthread_mutex_unlock(&g_relay_lock);
    return view;
}

static int relay_current(unsigned rx_gen) {
    int cur;
    pthread_mutex_lock(&g_lock);
    cur = (rx_gen == g_rx_gen);
    pthread_mutex_unlock(&g_lock);
    return cur;
}

/* Forward a (plain) VIDEO_CONFIG to an open head-unit socket. Marks this
 * receiver as attached (so relay_frame does not replay it again) and returns 1
 * when the caller should request a key frame (first config on a reused socket). */
static int relay_config(unsigned rx_gen, int *thread_attached, const ScreenHeader *h, const uint8_t *body) {
    int want_kf = 0;
    if (!relay_current(rx_gen)) return 0;
    pthread_mutex_lock(&g_relay_lock);
    if (g_relay.fd >= 0) {
        if (send_full(g_relay.fd, h, sizeof *h) != 0 || send_full(g_relay.fd, body, h->bodySize) != 0) {
            g_relay.err++;
            relay_drop_locked(1);
            dlog("head-unit config forward failed; relay waits for the next head-unit SETUP");
        } else if (!*thread_attached) {
            *thread_attached = 1;
            want_kf = 1;
        }
    }
    pthread_mutex_unlock(&g_relay_lock);
    return want_kf;
}

/* Relay one decrypted VIDEO_FRAME (body is re-encrypted in place).
 * Returns 1 when the caller should request a key frame from the iPhone. */
static int relay_frame(unsigned rx_gen, int *thread_attached, ScreenHeader *h, uint8_t *body,
                       const ScreenHeader *cfg_hdr, const uint8_t *cfg, uint32_t cfg_len) {
    uint8_t key[16], iv[16];
    unsigned cur;
    int have, want_kf = 0;
    if (!relay_current(rx_gen)) return 0;
    pthread_mutex_lock(&g_lock);
    cur = g_hu_gen;
    have = g_hu_key_valid;
    if (have) { memcpy(key, g_hu_key, 16); memcpy(iv, g_hu_iv, 16); }
    pthread_mutex_unlock(&g_lock);

    pthread_mutex_lock(&g_relay_lock);
    if (cur != g_relay.for_gen) {           /* new head-unit SETUP: fresh key, fresh tries */
        if (g_relay.fd >= 0) dlog("head-unit re-keyed (hu_gen %u->%u); reconnecting", g_relay.for_gen, cur);
        relay_drop_locked(0);
        g_relay.for_gen = cur;
        g_relay.tries = 0;
        g_relay.next_try = 0;
    }
    if (!have && !g_relay.nokey_logged) {
        g_relay.nokey_logged = 1;
        dlog("relay: no head-unit screen key captured yet (111 not injected, or SPSSI caller check missed)");
    }
    if (g_relay.fd < 0 && have && !g_relay.dead) {
        long now = time_plain(0);
        if (now >= g_relay.next_try) {
            g_relay.next_try = now + HU_RETRY_S;
            g_relay.tries++;
            g_relay.fd = hu_connect();
            if (g_relay.fd >= 0) {
                /* Same AES-CTR context other dongles init in SetupProxyScreenSecurityInfo.
                 * plainhu is the only way a frame leaves unencrypted. */
                int plain = flag(PLAINHU_FILE);
                /* Fresh context. A reused socket has already been AES_CTR_Final'd
                 * in relay_drop_locked; init must start the counter at this IV. */
                memset(&g_relay.ctx, 0, sizeof g_relay.ctx);
                g_relay.enc = (!plain && r_ctr_init(&g_relay.ctx, key, iv) == 0);
                if (!plain && !g_relay.enc) {
                    relay_drop_locked(0);
                    dlog("head-unit aes init failed");
                } else if (cfg && cfg_len &&        /* replay the real config header + body */
                    (send_full(g_relay.fd, cfg_hdr, sizeof *cfg_hdr) != 0 ||
                     send_full(g_relay.fd, cfg, cfg_len) != 0)) {
                    g_relay.err++;
                    relay_drop_locked(1);
                    dlog("head-unit config replay failed; relay waits for the next head-unit SETUP");
                } else {
                    *thread_attached = 1;
                    want_kf = 1;
                    if (g_relay.enc) dlog("head-unit relay aes on");
                }
            } else if (g_relay.tries >= HU_MAX_TRIES) {
                /* a failed connect does not consume the HU accept, so retry is
                 * safe; but stop after ~1 min so a dead path stops costing frames */
                g_relay.dead = 1;
                dlog("head-unit connect gave up after %d tries for hu_gen=%u", g_relay.tries, cur);
            }
        }
    }
    memset(key, 0, sizeof key); memset(iv, 0, sizeof iv);
    if (g_relay.fd >= 0) {
        if (!*thread_attached) {                /* socket reused across an iPhone 111 reconnect */
            *thread_attached = 1;
            want_kf = 1;
            if (cfg && cfg_len &&
                (send_full(g_relay.fd, cfg_hdr, sizeof *cfg_hdr) != 0 ||
                 send_full(g_relay.fd, cfg, cfg_len) != 0)) {
                g_relay.err++;
                relay_drop_locked(1);
                dlog("head-unit config replay failed; relay waits for the next head-unit SETUP");
            }
        }
    }
    if (g_relay.fd >= 0) {
        /* Backpressure: this runs on the phone read path, so never block per
         * frame waiting for a slow head unit. If the socket is not writable
         * within HU_WRITE_MS, DROP this AU *before* encrypting - the AES-CTR
         * counter must not advance for a frame the head unit never receives, or
         * every later frame desyncs. A drop corrupts the decoder until the next
         * IDR, so ask the phone for a key frame. The common case (head unit
         * keeping up, as in the car test) sends immediately. */
        struct pollfd wp = { g_relay.fd, POLLOUT, 0 };
        if (poll(&wp, 1, HU_WRITE_MS) == 1 && (wp.revents & POLLOUT)) {
            /* One continuous CTR span over the AVCC AU, matching AES_CTR_Update
             * in SendProxyScreenFrame and the head unit's decrypt of bodySize
             * bytes. Config frames are not fed in, so they never advance it. */
            if (g_relay.enc) r_ctr_update(&g_relay.ctx, body, h->bodySize, body);
            if (send_full(g_relay.fd, h, sizeof *h) != 0 || send_full(g_relay.fd, body, h->bodySize) != 0) {
                g_relay.err++;
                relay_drop_locked(1);
                dlog("head-unit send failed; relay waits for the next head-unit SETUP (err=%lu)", g_relay.err);
            } else {
                g_relay.relayed++;
                g_relay.backlog = 0;
            }
        } else {
            /* not writable in time: drop, un-encrypted, counter untouched */
            g_relay.dropped++;
            if (++g_relay.backlog == 1 || g_relay.backlog % 60 == 0) {
                dlog("head-unit slow: dropped %lu AUs (backlog=%d), requesting key frame",
                     g_relay.dropped, g_relay.backlog);
                want_kf = 1;   /* recover the decoder once the head unit catches up */
            }
        }
    }
    pthread_mutex_unlock(&g_relay_lock);
    return want_kf;
}

static void relay_stats(unsigned long *relayed, unsigned long *err, unsigned long *dropped) {
    pthread_mutex_lock(&g_relay_lock);
    *relayed = g_relay.relayed;
    *err = g_relay.err;
    *dropped = g_relay.dropped;
    pthread_mutex_unlock(&g_relay_lock);
}

static void *rx_thread(void *varg) {
    struct rx_arg a = *(struct rx_arg *)varg;
    free(varg);
    int listen_fd = a.listen_fd, c = -1, dfd = -1;
    size_t dn = 0;
    int dec_ok = 0, attached = 0;           /* attached: this thread fed the shared relay */
    int nal_len = 4, cfg_w = 0, cfg_h = 0;
    unsigned long frames = 0, idr = 0, bad = 0, relayed = 0, send_err = 0;
    int log_now = 0;
#if STEP_DECRYPT
    int desync = 0, last_view = -1;
    uint8_t cc_key[32], cc_nonce[8];
#endif
    /* full cached VIDEO_CONFIG (header + body), replaced on every config */
    ScreenHeader cfg_hdr; uint8_t *cfg = 0; uint32_t cfg_len = 0;
    memset(&cfg_hdr, 0, sizeof cfg_hdr);

#if STEP_DECRYPT
    /* One key for this stream. The nonce starts at 0 and belongs to this
     * connection (a new SETUP starts a new thread and a new counter). */
    memset(cc_key, 0, sizeof cc_key);
    memset(cc_nonce, 0, sizeof cc_nonce);
    dec_ok = chacha_derive(a.session, a.cid, cc_key);
    if (!dec_ok) dlog("111 decrypt disabled");
#else
    dlog("transport phase: counting 111 frames, not decrypting (ChaCha)");
    (void)au_ok;   /* decrypt check; live only in the STEP_DECRYPT build */
#endif

    /* accept the iPhone 111 connection, then this thread owns and closes its
     * own listen fd (one connection per SETUP is enough). */
    for (;;) {
        struct pollfd pf = { listen_fd, POLLIN, 0 };
        int pr = poll(&pf, 1, 1000), stale;
        pthread_mutex_lock(&g_lock);
        stale = (a.gen != g_rx_gen);
        pthread_mutex_unlock(&g_lock);
        if (stale) goto done;
        if (pr < 0) { if (errno == EINTR) continue; goto done; }
        if (pr == 0) continue;
        c = accept(listen_fd, 0, 0);
        if (c < 0) { if (errno == EINTR) continue; goto done; }
        break;
    }
    close(listen_fd); listen_fd = -1;
    dlog("111 connected from iPhone cid=%llu", (unsigned long long)a.cid);

    for (;;) {
        ScreenHeader h;
        uint8_t *body = 0;
        int rr;
#if STEP_DECRYPT
        /* cockpit layout change from the head unit -> tell the phone */
        if (STEP_RELAY && !disabled() && !flag(NORELAY_FILE)) {
            int vw = relay_poll_view();
            if (vw >= 0 && vw != last_view) { last_view = vw; send_view(a.session, vw); }
        }
#endif
        rr = read_full(c, &h, sizeof h, a.gen);
        if (rr <= 0) break;
        if (h.bodySize > MAX_BODY) { dlog("reject bodySize=%u", h.bodySize); break; }
        if (h.bodySize) {
            body = malloc(h.bodySize);
            if (!body) break;
            if (read_full(c, body, h.bodySize, a.gen) <= 0) { free(body); break; }
        }

        if (h.opcode == OP_VIDEO_CONFIG) {
            if (body && h.bodySize) {
                uint8_t *nc = malloc(h.bodySize);   /* cache every config, not just the first */
                if (nc) {
                    memcpy(nc, body, h.bodySize);
                    free(cfg); cfg = nc; cfg_len = h.bodySize; cfg_hdr = h;
                    nal_len = avcc_parse(cfg, cfg_len, &cfg_w, &cfg_h);
                    dlog("111 video config body=%u sps=%dx%d nal_len=%d", h.bodySize, cfg_w, cfg_h, nal_len);
                }
                /* forward a config update to an already-open head-unit socket (plain) */
                if (STEP_RELAY && dec_ok && !disabled() && !flag(NORELAY_FILE) &&
                    relay_config(a.gen, &attached, &h, body))
                    ask_keyframe(a.session);
            }
        } else if (h.opcode == OP_VIDEO_FRAME) {
            if (body && h.bodySize) {
                int ok = 0, opened = 0;
#if STEP_DECRYPT
                if (dec_ok) {
                    uint32_t plain = 0;
                    int has_idr = 0;
                    if (!chacha_open(cc_key, cc_nonce, &h, body, h.bodySize, &plain)) {
                        bad++;
                        /* A failed verify does not advance the nonce, so the
                         * dongle and the phone are now one frame out of step and
                         * every later frame fails too. Stock tears the stream
                         * down here; do the same so iOS re-SETUPs 111 with a
                         * fresh counter. The shared head-unit relay socket stays
                         * open (its CTR is independent). */
                        if (++desync >= DESYNC_LIMIT) {
                            dlog("111 nonce desync (%d consecutive verify fails); closing stream", desync);
                            free(body);
                            break;
                        }
                    } else {
                        desync = 0;
                        opened = 1;
                        h.bodySize = plain;   /* tag stripped; AU is body[0..plain) */
                        ok = au_ok(body, h.bodySize, nal_len, &has_idr);
                        if (!ok) {
                            static __thread int au_logged;
                            bad++;
                            if (!au_logged) {
                                au_logged = 1;
                                dlog("111 chacha mac ok but au_ok=0 plain=%u nal_len=%d", plain, nal_len);
                            }
                        }
                        if (has_idr) idr++;
                    }
                }
#endif
                /* STEP_DECRYPT=0 leaves ok at 0: that is the "still encrypted" signal. */
                frames++;
                if (CLUSTER_DIAG && (opened || !STEP_DECRYPT)) dump_au(&dfd, &dn, body, h.bodySize);
                {   /* also every 10 s, so a slow (static-map) cluster stream still shows a rate */
                    static __thread long last_log;
                    long now = time_plain(0);
                    if (frames == 1 || frames % 300 == 0 || now - last_log >= 10) { last_log = now; log_now = 1; }
                }
                if (log_now) {
                    unsigned long dropped = 0;
                    log_now = 0;
                    relay_stats(&relayed, &send_err, &dropped);
                    dlog("111 frames=%lu bytes=%u nal_ok=%d idr=%lu bad=%lu relayed=%lu dropped=%lu send_err=%lu",
                         frames, h.bodySize, ok, idr, bad, relayed, dropped, send_err);
                }

                /* relay the authenticated AU; relay_frame re-encrypts with the HU AES key. */
                if (STEP_RELAY && dec_ok && opened && !disabled() && !flag(NORELAY_FILE) &&
                    relay_frame(a.gen, &attached, &h, body, &cfg_hdr, cfg, cfg_len))
                    ask_keyframe(a.session);           /* outside g_relay_lock */
            }
        }
        /* keepalive / forceKeyFrame / ignore: nothing */
        free(body);
    }

done:
    {
        unsigned long dropped = 0;
        relay_stats(&relayed, &send_err, &dropped);
        dlog("111 receiver exit frames=%lu idr=%lu bad=%lu relayed=%lu dropped=%lu send_err=%lu",
             frames, idr, bad, relayed, dropped, send_err);
    }
    if (listen_fd >= 0) close(listen_fd);   /* only set if we exited before accept */
    if (c >= 0) close(c);
    if (dfd >= 0) close(dfd);
    free(cfg);
#if STEP_DECRYPT
    memset(cc_key, 0, sizeof cc_key);
    memset(cc_nonce, 0, sizeof cc_nonce);
#endif
    /* the head-unit relay socket is shared and intentionally left open */
    if (a.session && cf_release) cf_release(a.session);   /* retained in start_receiver */
    return 0;
}

/* dual-stack IPv6 listener, prefer PHONE_PORT, else ephemeral */
static int open_listener(int *out_port) {
    int fd, one = 1, off = 0;
    struct sockaddr_in6 sa;
    socklen_t sl = sizeof sa;
    fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
    memset(&sa, 0, sizeof sa);
    sa.sin6_family = AF_INET6;
    sa.sin6_port = htons(PHONE_PORT);
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        sa.sin6_port = 0;
        if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0) { close(fd); return -1; }
    }
    if (listen(fd, 1) < 0) { close(fd); return -1; }
    if (getsockname(fd, (struct sockaddr *)&sa, &sl) == 0) *out_port = ntohs(sa.sin6_port);
    return fd;
}

/* Build a 111 stream entry in the response: clone of the requested desc +
 * dataPort + streamID=111 (same shape as the head-unit hook). */
static void append_response_111(CFRef response, CFRef requested_111, int port) {
    CFRef old, arr, sd;
    if (!is_type(response, cf_dict_type) || !requested_111) return;
    old = cf_dict_get(response, k_streams);
    arr = is_type(old, cf_array_type) ? cf_array_copy(0, 0, old) : cf_array_new(0, 0, array_cb);
    sd = cf_dict_copy(0, 0, requested_111);
    if (arr && sd) {
        set_i64(sd, "dataPort", port);
        set_i64(sd, "streamID", TYPE_CLUSTER);
        /* log the stock 110 entry's keys in this response: if iOS wants the 111
         * entry to carry more than dataPort/streamID, this shows what to add. */
        if (CLUSTER_DIAG && cf_dict_apply && is_type(old, cf_array_type)) {   /* 111 SETUP has no stock streams */
            long i, n = cf_array_count(old);
            for (i = 0; old && i < n; i++) {
                CFRef e = cf_array_get(old, i);
                if (is_type(e, cf_dict_type) && dict_type(e) == TYPE_MAIN) {
                    dlog("setup response stock 110 entry keys:");
                    cf_dict_apply(e, log_kv, (void *)"110resp");
                    break;
                }
            }
        }
        cf_array_append(arr, sd);
        cf_dict_set(response, k_streams, arr);
    }
    if (sd) cf_release(sd);
    if (arr) cf_release(arr);
}

static void start_receiver(uint64_t cid, void *session, CFRef response, CFRef req111) {
    int port = 0, lfd;
    struct rx_arg *arg;
    unsigned gen;
    pthread_t th;
    /* retire any previous receiver (it polls g_rx_gen and exits within ~1s,
     * closing its own listen fd; until then port 7100 may still be bound, so
     * the new listener falls back to an ephemeral port - fine, the actual port
     * is what we return in the response). */
    pthread_mutex_lock(&g_lock);
    gen = ++g_rx_gen;
    pthread_mutex_unlock(&g_lock);

    lfd = open_listener(&port);
    if (lfd < 0) { dlog("111 listener bind failed: %s", strerror(errno)); return; }
    arg = malloc(sizeof *arg);
    if (!arg) { close(lfd); return; }
    arg->listen_fd = lfd;
    arg->gen = gen;
    arg->cid = cid;
    arg->session = (session && cf_retain) ? cf_retain(session) : session;
    if (pthread_create(&th, 0, rx_thread, arg) != 0) {
        if (arg->session && cf_release) cf_release(arg->session);
        close(lfd); free(arg);
        dlog("111 receiver thread create failed");
        return;
    }
    pthread_detach(th);
    append_response_111(response, req111, port);
    dlog("111 receiver ready cid=%llu port=%d", (unsigned long long)cid, port);
}

/* ============ SESSION SETUP ============ */
OSStatus AirPlayReceiverSessionSetup(void *session, CFRef request, CFRef *response) {
    OSStatus e;
    CFRef req111;
    if (!ready()) return r_setup ? r_setup(session, request, response) : -1;
    if (t_busy) return r_setup(session, request, response);
    t_busy = 1;
    if (CLUSTER_DIAG && !disabled()) log_stream_types("setup request", request);
    e = r_setup(session, request, response);
    if (disabled()) { t_busy = 0; return e; }
    if (CLUSTER_DIAG || e != 0) dlog("setup rc=%d response=%s", e, (response && *response) ? "yes" : "no");
    if (e == 0 && response && *response) {
        /* iOS negotiates features: it proposes a FeatureKey list and validates
         * the cluster display against the features the RECEIVER accepts in the
         * SETUP response. Without altScreen there, iOS logs "altScreen not
         * defined but display structures found" and deactivates the endpoint
         * (iPhone log, v1144). The head-unit hook merges into every SETUP
         * response too (set_reference_enabled_features). Response is the
         * CFDictionaryCreateMutable dict from AirPlayReceiverSessionSetup. */
        if (ADVERT_FEATURES && ADVERT_DISPLAY) {
            merge_features(*response);
            if (CLUSTER_DIAG) dlog("setup response enabledFeatures += altScreen, viewAreas");
        }
        if (CLUSTER_DIAG) log_stream_types("setup response", *response);
        req111 = find_stream(request, TYPE_CLUSTER);
        if (req111) {
            uint64_t cid = dict_cid(req111);
            dlog("setup request has 111 cid=%llu", (unsigned long long)cid);
            if (!STEP_RECEIVE) dlog("step 1: observe only, not opening a 111 listener");
            else if (cid) start_receiver(cid, session, *response, req111);
            else dlog("111 request missing streamConnectionID; skip");
        }
    }
    t_busy = 0;
    return e;
}

void AirPlayReceiverSessionTearDown(void *session, CFRef request, OSStatus reason, void *done) {
    if (!ready()) { if (r_teardown) r_teardown(session, request, reason, done); return; }
    if (!disabled()) {
        if (CLUSTER_DIAG && request) log_stream_types("teardown request", request);
        dlog("teardown reason=%d request=%s", reason, request ? "dict" : "NULL");
    }
    /* a NULL request (whole session) or one that names 111 retires the receiver */
    if (!disabled() && (request == 0 || find_stream(request, TYPE_CLUSTER))) {
        pthread_mutex_lock(&g_lock);
        g_rx_gen++;
        pthread_mutex_unlock(&g_lock);
        dlog("teardown retires 111 receiver");
    }
    /* Whole phone session over: close the head-unit relay socket too instead of
     * leaving it to fail on the next send. CPAAProxyEx tears the head-unit
     * screen down when the phone leaves, and the next phone session makes the
     * head unit re-SETUP the screen (requestUI -> SetupScreen with the injected
     * 111), which advances g_hu_gen and lets relay_frame connect again. dead=1
     * so nothing retries against the old head-unit listener in between. */
    if (STEP_RELAY && !disabled() && request == 0) {
        unsigned long sent, err;
        int had;
        pthread_mutex_lock(&g_relay_lock);
        had = g_relay.fd >= 0;
        sent = g_relay.relayed; err = g_relay.err;
        relay_drop_locked(1);
        pthread_mutex_unlock(&g_relay_lock);
        if (had) dlog("session end: head-unit relay closed (relayed=%lu err=%lu)", sent, err);
    }
    r_teardown(session, request, reason, done);
}

__attribute__((constructor)) static void init(void) {
    dlog("LOADED pid=%d step=%d advert=%d", (int)getpid(), CLUSTER_STEP, CLUSTER_ADVERT);
}
