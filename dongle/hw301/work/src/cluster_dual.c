/* HW301 native second-screen proxy replacement (stock v129 ABI).
 * Stock CPAAProxyEx keeps its main display/socket. This shim owns stream 111:
 * it consumes the car's normal /info display, adds a separate native SETUP,
 * hides 111 from the single-screen stock parser, and relays authenticated
 * phone video with the car session's independently derived stream key.
 * Controls travel through /command in both directions, without a private
 * head-unit protocol. See FINDINGS.md for ABI evidence and validation limits.
 */
#ifndef CLUSTER_STEP
#define CLUSTER_STEP 3
#endif
#define STEP_RECEIVE (CLUSTER_STEP >= 2)   /* listen, append dataPort, count frames */
#define STEP_RELAY   (CLUSTER_STEP >= 3)   /* negotiate native car 111 and relay video */

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
/* The car owns the complete display descriptor. No local geometry fallback:
 * an older/malformed head-unit profile leaves normal CarPlay available. */
#define MAX_VIEWS 64
#define URL_CAP 1024

#define OFF_FILE "/mnt/UDISK/cluster_dual.off"
#define NORELAY_FILE "/mnt/UDISK/cluster_dual.norelay"
#define DUMP_FILE "/mnt/UDISK/cluster_dump.on"
#define DUMP_PATH "/mnt/UDISK/c111.h264"

#define PHONE_PORT 7100          /* preferred phone-facing listener port */
#define HU_CONNECT_MS 1000       /* runs on the phone read path: keep it short */
#define HU_RETRY_S 5             /* pre-connect retry interval */
#define HU_MAX_TRIES 12          /* per head-unit key generation (~1 min) */
#define HU_WRITE_MS 60           /* max per-frame wait for the head-unit socket; else drop */
#define KF_LATE_S 3              /* one more key frame this long after the relay attaches */
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
static int64_t (*r_get_i64)(CFRef, CFRef, OSStatus *);
static OSStatus (*r_ctr_init)(void *, const uint8_t *, const uint8_t *);
static OSStatus (*r_ctr_update)(void *, const void *, size_t, void *);
static void (*r_ctr_final)(void *);
static CFRef (*r_info)(void *, void *, void *, OSStatus *);
static OSStatus (*r_setup)(void *, CFRef, CFRef *);
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
static int64_t (*cf_get_i64)(CFRef, OSStatus *);
static unsigned long (*cf_number_type)(void);
static unsigned long (*cf_boolean_type)(void);
static unsigned long (*cf_data_type)(void);

static CFRef k_type, k_conn, k_streams, k_displays, k_features;
static CFRef (*r_plist_read)(const void *, size_t, OSStatus *);
static CFRef g_car_display;             /* owned immutable snapshot, under g_lock */
static int g_car_port;
static void *(*r_plist_write)(CFRef, size_t *, OSStatus *);
static void (*r_client_invalidate)(void *);
static OSStatus (*r_peer_address)(void *, void *, size_t, void *);
static void **r_car_client;
static const uint8_t *r_car_master;
static void **r_car_pair;
static struct sockaddr_storage g_car_addr;
static socklen_t g_car_addr_len;
static uint64_t g_car_cid, g_car_main_cid;
static uint8_t g_car_master[16];
static int g_car_pending;
static void car_reset(int forget_display);
static int car_needs_setup(void);
static void capture_car_command(CFRef request);
static void service_car_commands(void *session, unsigned gen);


/* address range of CPAAProxyEx's SetupProxyScreenSecurityInfo, for caller test */
static uintptr_t g_spssi_lo, g_spssi_hi;
static uintptr_t g_createplist_lo, g_createplist_hi;
static void *g_cpaa_base;        /* dli_fbase of a CPAAProxyEx symbol */

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static int g_ok;
static __thread int t_busy;

/* ---- shared session state ---------------------------------------------- */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t g_hu_key[16], g_hu_iv[16];   /* car stream-111 key: independent connection ID */
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
#ifdef HW301_HOST_TEST
#define dlsym_plain dlsym
#define time_plain time
#else
void *dlsym_plain(void *__restrict, const char *__restrict) __asm__("dlsym");
long time_plain(long *) __asm__("time");
#endif

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
    NEED(r_get_i64, "CFDictionaryGetInt64");
    NEED(r_ctr_init, "AES_CTR_Init");
    NEED(r_ctr_update, "AES_CTR_Update");
    NEED(r_ctr_final, "AES_CTR_Final");
    NEED(r_info, "AirPlayCopyServerInfo");
    NEED(r_setup, "AirPlayReceiverSessionSetup");
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

    *(void **)&r_peer_address = next_sym("HTTPClientGetPeerAddress");
    r_car_client = dlsym_plain(RTLD_DEFAULT, "gAirPlayClient");
    r_car_master = dlsym_plain(RTLD_DEFAULT, "gSessionAesKey");
    r_car_pair = dlsym_plain(RTLD_DEFAULT, "gPairVerifySessionHomeKit");

    /* SetupProxyScreenSecurityInfo is a CPAAProxyEx (executable) export */
    spssi = dlsym_plain(RTLD_DEFAULT, "SetupProxyScreenSecurityInfo");
    if (spssi && dladdr(spssi, &di)) {
        g_spssi_lo = (uintptr_t)spssi;
        g_spssi_hi = (uintptr_t)spssi + SPSSI_SIZE;
        g_cpaa_base = di.dli_fbase;
    }
    spssi = dlsym_plain(RTLD_DEFAULT, "CreateHTTPRequestMsgWithPList");
    g_createplist_lo = (uintptr_t)spssi;
    g_createplist_hi = g_createplist_lo + 0x190; /* verified v129 symbol size */

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

/* ---- car-owned profile, captured at CPAAProxyEx's /info plist decode ---- */
static CFRef get_value(CFRef d, const char *name) {
    CFRef k, v = 0;
    if (!is_type(d, cf_dict_type)) return 0;
    k = s_cf(name);
    if (k) { v = cf_dict_get(d, k); cf_release(k); }
    return v;
}

static int get_int(CFRef d, const char *name, int64_t lo, int64_t hi, int64_t *out) {
    CFRef k, v = get_value(d, name);
    OSStatus e = -1;
    int64_t n;
    if (!is_type(v, cf_number_type)) return 0;
    k = s_cf(name);
    if (!k) return 0;
    n = r_get_i64(d, k, &e);
    cf_release(k);
    if (e || n < lo || n > hi) return 0;
    *out = n;
    return 1;
}

static int get_string(CFRef d, const char *name, char *out, size_t cap) {
    CFRef v = get_value(d, name);
    out[0] = 0;
    return is_type(v, cf_string_type) && cf_cstr_get &&
           cf_cstr_get(v, out, (long)cap, UTF8) && out[0];
}

static int valid_rect(CFRef d, int64_t w, int64_t h) {
    int64_t x, y, rw, rh;
    return get_int(d, "originXPixels", 0, w - 1, &x) &&
           get_int(d, "originYPixels", 0, h - 1, &y) &&
           get_int(d, "widthPixels", 1, w - x, &rw) &&
           get_int(d, "heightPixels", 1, h - y, &rh);
}

/* Validate the fields we consume; preserve every other car-provided field.
 * viewAreas and initialURL are optional in the native display descriptor. */
static int valid_car_display(CFRef d) {
    int64_t w, h, initial;
    CFRef areas;
    char uuid[64];
    long i, count;
    if (dict_type(d) != TYPE_CLUSTER ||
        !get_int(d, "widthPixels", 1, 16384, &w) ||
        !get_int(d, "heightPixels", 1, 16384, &h) ||
        !get_string(d, "uuid", uuid, sizeof uuid) || strlen(uuid) != 36) return 0;
    areas = get_value(d, "viewAreas");
    if (!areas) return 1;
    if (!is_type(areas, cf_array_type)) return 0;
    count = cf_array_count(areas);
    if (count < 1 || count > MAX_VIEWS ||
        !get_int(d, "initialViewArea", 0, count - 1, &initial)) return 0;
    for (i = 0; i < count; ++i) {
        CFRef area = cf_array_get(areas, i), safe = get_value(area, "safeArea");
        int64_t aw, ah;
        if (!valid_rect(area, w, h) ||
            !get_int(area, "widthPixels", 1, w, &aw) ||
            !get_int(area, "heightPixels", 1, h, &ah) ||
            (safe && !valid_rect(safe, aw, ah))) return 0;
    }
    return 1;
}

/* Own the returned copy; never mutate a caller's array/dictionary. In particular,
 * an empty streams array must not accidentally become a full stock TEARDOWN. */
static CFRef without_cluster(CFRef root, const char *field) {
    CFRef old = get_value(root, field), copy, arr;
    long i;
    if (!is_type(old, cf_array_type)) return 0;
    copy = cf_dict_copy(0, 0, root);
    arr = cf_array_new(0, 0, array_cb);
    if (!copy || !arr) { if (copy) cf_release(copy); if (arr) cf_release(arr); return 0; }
    for (i = 0; i < cf_array_count(old); ++i) {
        CFRef item = cf_array_get(old, i);
        if (dict_type(item) != TYPE_CLUSTER) cf_array_append(arr, item);
    }
    set_value(copy, field, arr);
    cf_release(arr);
    return copy;
}

static void capture_car_info(CFRef info) {
    CFRef displays = get_value(info, "displays"), copy = 0, old;
    long i;
    if (!is_type(displays, cf_array_type)) return;
    for (i = 0; i < cf_array_count(displays); ++i) {
        CFRef d = cf_array_get(displays, i);
        if (valid_car_display(d)) { copy = cf_dict_copy(0, 0, d); break; }
    }
    pthread_mutex_lock(&g_lock);
    old = g_car_display;
    g_car_display = copy;
    pthread_mutex_unlock(&g_lock);
    if (old) cf_release(old);
    if (!copy) car_reset(0);
    dlog("native car display %s", copy ? "accepted" : "unavailable");
}

static void capture_car_setup(CFRef response) {
    CFRef sd = find_stream(response, TYPE_CLUSTER);
    int64_t port;
    uint64_t cid;
    struct sockaddr_storage sa;
    int peer_ok = 0;
    if (!sd) return;
    memset(&sa, 0, sizeof sa);
    if (r_peer_address && r_car_client && *r_car_client)
        peer_ok = r_peer_address(*r_car_client, &sa, sizeof sa, 0) == 0;
    cid = dict_cid(sd);
    pthread_mutex_lock(&g_lock);
    if (g_car_pending && (!cid || cid == g_car_cid)) {
        g_car_pending = 0;
        if (get_int(sd, "dataPort", 1, 65535, &port) && peer_ok &&
            (sa.ss_family == AF_INET || sa.ss_family == AF_INET6)) {
            g_car_port = (int)port;
            g_car_addr = sa;
            g_car_addr_len = sa.ss_family == AF_INET6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
            dlog("native car SETUP 111 cid=%llu port=%d", (unsigned long long)g_car_cid, g_car_port);
        } else {
            /* No usable transport was established. Release the pending ID so
             * a later main SETUP negotiates 111 again with a fresh ID/key. */
            g_car_cid = 0; g_car_main_cid = 0;
            g_car_port = 0; g_car_addr_len = 0; g_hu_key_valid = 0;
            memset(g_car_master, 0, sizeof g_car_master);
            memset(g_hu_key, 0, sizeof g_hu_key); memset(g_hu_iv, 0, sizeof g_hu_iv);
            dlog("native car SETUP 111 invalid port/peer; retry on next screen SETUP");
        }
    }
    pthread_mutex_unlock(&g_lock);
}

CFRef CFBinaryPlistV0CreateWithData(const void *data, size_t len, OSStatus *err) {
    CFRef result, filtered = 0;
    void *ra = __builtin_return_address(0);
    if (!r_plist_read) *(void **)&r_plist_read = next_sym("CFBinaryPlistV0CreateWithData");
    if (!r_plist_read) { if (err) *err = -1; return 0; }
    result = r_plist_read(data, len, err);
    if (result && !t_busy && ready() && !disabled() && caller_is_cpaa(ra)) {
        t_busy = 1;
        if (is_type(get_value(result, "displays"), cf_array_type)) {
            capture_car_info(result);
            filtered = without_cluster(result, "displays");
        } else if (find_stream(result, TYPE_CLUSTER)) {
            capture_car_setup(result);
            filtered = without_cluster(result, "streams");
        } else capture_car_command(result);
        /* Never expose 111 to the stock parser on an allocation failure. */
        if (is_type(get_value(result, "displays"), cf_array_type) || find_stream(result, TYPE_CLUSTER)) {
            cf_release(result);
            result = filtered;
            if (!result && err) *err = -1;
        }
        t_busy = 0;
    }
    return result;
}

static int car_available(void) {
    int have;
    pthread_mutex_lock(&g_lock);
    have = g_car_display != 0;
    pthread_mutex_unlock(&g_lock);
    return have;
}

static int car_view_count(void) {
    int count = 0;
    pthread_mutex_lock(&g_lock);
    if (g_car_display && is_type(get_value(g_car_display, "viewAreas"), cf_array_type))
        count = (int)cf_array_count(get_value(g_car_display, "viewAreas"));
    pthread_mutex_unlock(&g_lock);
    return count;
}

static void set_car_uuid(CFRef params) {
    pthread_mutex_lock(&g_lock);
    if (g_car_display) set_value(params, "uuid", get_value(g_car_display, "uuid"));
    pthread_mutex_unlock(&g_lock);
}

/* ============ PHONE SIDE: /info advertisement ============ */
static void merge_features(CFRef info, int want_va) {
    CFRef existing = cf_dict_get(info, k_features), copy, alt = 0, va = 0;
    long i, n = 0;
    int have_alt = 0, have_va = 0;
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
        snprintf(out, cap, "%lld", (long long)(cf_get_i64 ? cf_get_i64(v, 0) : 0));
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

static int apply_car_view_locked(CFRef alt);

/* Return a new info dict, or 0 to keep base. Caller releases base on success. */
static CFRef augment_info(CFRef base) {
    CFRef info, old, displays, alt;
    int have_profile, view = -1;
    if (!ADVERT_FEATURES && !ADVERT_DISPLAY) return 0;   /* stock /info (log only) */
    if (!is_type(base, cf_dict_type)) return 0;
    old = cf_dict_get(base, k_displays);
    if (!is_type(old, cf_array_type) || cf_array_count(old) < 1) return 0;
    if (already_cluster(old)) return 0;
    pthread_mutex_lock(&g_lock);
    have_profile = g_car_display != 0;
    pthread_mutex_unlock(&g_lock);
    if (!have_profile) return 0;
    info = cf_dict_copy(0, 0, base);
    if (!info) return 0;
    if (ADVERT_FEATURES) merge_features(info, car_view_count() > 0);
    if (!ADVERT_DISPLAY) {
        dlog("/info enabledFeatures merged, display not added (ADVERT=%d)", CLUSTER_ADVERT);
        return info;
    }
    displays = cf_array_copy(0, 0, old);
    pthread_mutex_lock(&g_lock);
    alt = g_car_display ? cf_dict_copy(0, 0, g_car_display) : 0;
    if (alt) view = apply_car_view_locked(alt);
    pthread_mutex_unlock(&g_lock);
    if (!displays || !alt) {
        if (displays) cf_release(displays);
        if (alt) cf_release(alt);
        cf_release(info);
        return 0;
    }
    cf_array_append(displays, alt);
    cf_dict_set(info, k_displays, displays);
    dlog("/info cluster display added displays=%ld", cf_array_count(displays));
    if (view >= 0) dlog("/info initialViewArea=%d from the car's last updateViewArea", view);
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

/* ============ SCREEN SECURITY ============ */
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

/* Confirm the car session master against stock's main-screen derivation before
 * using it. gSessionAesKey is the SPSSI SHA512 input (v129 GOT 0x17e1a8).
 * Reusing stream 110's *derived* key or connection ID would reuse CTR keystream. */
static void capture_car_key(const uint8_t *key, const uint8_t *iv) {
    uint8_t check_key[16], check_iv[16];
    pthread_mutex_lock(&g_lock);
    if (g_car_cid && g_car_main_cid && !g_hu_key_valid) {
        r_derive(g_car_master, 16, g_car_main_cid, check_key, check_iv);
        if (!memcmp(key, check_key, 16) && !memcmp(iv, check_iv, 16)) {
            r_derive(g_car_master, 16, g_car_cid, g_hu_key, g_hu_iv);
            g_hu_key_valid = 1;
            dlog("native car 111 independent AES ready gen=%u", g_hu_gen);
        } else dlog("car master verification failed; cluster remains closed");
        memset(check_key, 0, sizeof check_key); memset(check_iv, 0, sizeof check_iv);
    }
    pthread_mutex_unlock(&g_lock);
}

OSStatus AES_CTR_Init(void *ctx, const uint8_t *key, const uint8_t *iv) {
    OSStatus e;
    void *ra = __builtin_return_address(0);
    if (!ready()) return r_ctr_init ? r_ctr_init(ctx, key, iv) : -1;
    e = r_ctr_init(ctx, key, iv);
    if (!e && key && iv && !disabled() && g_spssi_lo &&
        (uintptr_t)ra >= g_spssi_lo && (uintptr_t)ra < g_spssi_hi) capture_car_key(key, iv);
    return e;
}

static int random_cid(uint64_t *cid) {
    size_t off = 0;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    while (off < sizeof *cid) {
        ssize_t n = read(fd, (uint8_t *)cid + off, sizeof *cid - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); return 0; }
        off += (size_t)n;
    }
    close(fd);
    return *cid != 0;
}

/* Called only for CPAA's HTTP plist serialization. Add 111 to the first screen
 * SETUP. Main-only teardown/re-setup does not own the independent car stream. */
static CFRef car_request(CFRef request) {
    CFRef main = find_stream(request, TYPE_MAIN), out = 0, streams = 0, alt = 0, display = 0;
    uint64_t cid, main_cid = dict_cid(main);
    if (get_value(request, "model")) {
        car_reset(1);
        out = cf_dict_copy(0, 0, request);
        if (out) {
            /* Only the model changes: the head unit's legacy guard needs both
             * iPhone9,1 and 18D70, and the car keeps the stock iOS build string. */
            set_str(out, "model", "HW301");
            /* Both features are implemented; actual display data arrives in /info. */
            merge_features(out, 1);
        }
        return out;
    }
    if (!main_cid || find_stream(request, TYPE_CLUSTER) || !car_needs_setup()) return 0;
    pthread_mutex_lock(&g_lock);
    if (g_car_display) display = cf_retain(g_car_display);
    pthread_mutex_unlock(&g_lock);
    if (!display) return 0;
    /* This USB transport uses stock AES. Do not silently downgrade a paired
     * car transport to AES if a future firmware enables HomeKit here. */
    if (!r_car_master || !r_car_pair || *r_car_pair) goto done;
    out = cf_dict_copy(0, 0, request);
    streams = cf_array_copy(0, 0, streams_of(request));
    alt = cf_dict_new(0, 0, dict_key_cb, dict_val_cb);
    if (!out || !streams || !alt) goto fail;
    set_i64(alt, "type", TYPE_CLUSTER);
    set_value(alt, "uuid", get_value(display, "uuid"));
    if (!random_cid(&cid) || cid == main_cid) goto fail;
    car_reset(0);
    set_i64(alt, "streamConnectionID", (int64_t)cid);
    if (get_value(display, "maxFPS")) set_value(alt, "maxFPS", get_value(display, "maxFPS"));
    pthread_mutex_lock(&g_lock);
    g_car_cid = cid; g_car_main_cid = main_cid; g_car_pending = 1;
    memcpy(g_car_master, r_car_master, 16);
    pthread_mutex_unlock(&g_lock);
    dlog("native SETUP main=%llu cluster=%llu", (unsigned long long)main_cid, (unsigned long long)cid);
    cf_array_append(streams, alt);
    set_value(out, "streams", streams);
    goto done;
fail:
    if (out) cf_release(out);
    out = 0;
done:
    if (alt) cf_release(alt);
    if (streams) cf_release(streams);
    cf_release(display);
    return out;
}

void *CFBinaryPlistV0Create(CFRef request, size_t *len, OSStatus *err) {
    CFRef edited = 0;
    void *data, *ra = __builtin_return_address(0);
    if (!r_plist_write) *(void **)&r_plist_write = next_sym("CFBinaryPlistV0Create");
    if (!r_plist_write) { if (err) *err = -1; return 0; }
    if (!t_busy && ready() && !disabled() && caller_is_cpaa(ra) && g_createplist_hi &&
        (uintptr_t)ra >= g_createplist_lo && (uintptr_t)ra < g_createplist_hi) {
        t_busy = 1; edited = car_request(request); t_busy = 0;
    }
    data = r_plist_write(edited ? edited : request, len, err);
    if (edited) cf_release(edited);
    return data;
}

/* ---- socket helpers ---------------------------------------------------- */
static int read_full(int fd, void *buf, size_t len, unsigned gen, int idle_return) {
    uint8_t *p = buf;
    size_t off = 0;
    while (off < len) {
        int stale;
        pthread_mutex_lock(&g_lock);
        stale = gen != g_rx_gen;
        pthread_mutex_unlock(&g_lock);
        if (stale) return -1;
        struct pollfd pf = { fd, POLLIN, 0 };
        ssize_t r;
        int pr = poll(&pf, 1, 100);
        if (pr < 0) { if (errno == EINTR) continue; return -1; }
        if (pr == 0) {
            if (off == 0 && idle_return) return 2;
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
        if (w <= 0) { if (w < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue; return -1; }
        off += (size_t)w;
    }
    return 0;
}

static int hu_port(void) {
    int port;
    pthread_mutex_lock(&g_lock);
    port = g_car_port;
    pthread_mutex_unlock(&g_lock);
    return port;
}

/* The control client's negotiated peer preserves IPv6 scope IDs and supports
 * IPv4 too. No socket scan, guessed address or fixed car-side port. */
static int hu_connect(void) {
    struct sockaddr_storage sa;
    socklen_t len;
    int fd, one = 1, port, err = 0;
    socklen_t el = sizeof err;
    long fl;
    struct pollfd pf;
    pthread_mutex_lock(&g_lock);
    sa = g_car_addr; len = g_car_addr_len; port = g_car_port;
    pthread_mutex_unlock(&g_lock);
    if (!len || port <= 0) return -1;
    if (sa.ss_family == AF_INET6) ((struct sockaddr_in6 *)&sa)->sin6_port = htons((uint16_t)port);
    else if (sa.ss_family == AF_INET) ((struct sockaddr_in *)&sa)->sin_port = htons((uint16_t)port);
    else return -1;
    fd = socket(sa.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return -1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) { close(fd); return -1; }
    if (connect(fd, (struct sockaddr *)&sa, len) < 0 && errno != EINPROGRESS) { close(fd); return -1; }
    pf.fd = fd; pf.events = POLLOUT; pf.revents = 0;
    if (poll(&pf, 1, HU_CONNECT_MS) <= 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) < 0 || err) {
        close(fd); return -1;
    }
    dlog("native car 111 connected port=%d", port);
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
        set_car_uuid(params);
        set_value(req, "params", params);
        e = r_send_cmd(session, req, (void *)kf_done, 0);
        dlog("forceKeyFrame requested rc=%d", e);
    }
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
    memcpy(&pair, (char *)session + 0x3ec, sizeof pair);
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

struct rx_arg { int listen_fd; unsigned gen; uint64_t cid; void *session; uint8_t key[32]; };

static void dump_au(int *dfd, size_t *dn, const uint8_t *p, size_t n) {
    if (!flag(DUMP_FILE)) return;
    if (*dfd < 0 && *dn == 0) *dfd = open(DUMP_PATH, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (*dfd >= 0 && *dn < DUMP_LIMIT) {
        ssize_t w = write(*dfd, p, n);
        if (w > 0) *dn += (size_t)w;
    }
}

/* ---- car relay (shared) -------------------------------------------------
 * A native streamConnectionID owns one socket and continuous CTR state. The
 * relay survives phone reconnects and main-screen-only teardown/re-setup;
 * replacing its socket requires a fresh native SETUP and independent key/IV.
 * Only the current phone receiver (rx_gen == g_rx_gen) may write, serialized by
 * g_relay_lock. Car invalidation or a new 111 SETUP advances g_hu_gen. */
static pthread_mutex_t g_relay_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    int fd;              /* -1 = no socket */
    int enc;             /* independent AES context valid */
    AesCtx ctx;          /* head-unit AES-CTR, continuous for the socket's life */
    unsigned for_gen;    /* g_hu_gen the socket / tries belong to */
    int dead;            /* gave up for for_gen; revived by the next g_hu_gen */
    int tries;           /* connect attempts for for_gen */
    long next_try;       /* seconds, from time_plain() (32-bit time on the dongle) */
    int nokey_logged;
    unsigned long relayed, err, dropped;
    int backlog;             /* consecutive drops; 0 after a successful send */
} g_relay = { .fd = -1 };

static void relay_drop_locked(int dead) {
    if (g_relay.fd >= 0) close(g_relay.fd);
    g_relay.fd = -1;
    if (g_relay.enc) { r_ctr_final(&g_relay.ctx); g_relay.enc = 0; }
    g_relay.dead = dead;
    g_relay.backlog = 0;
}

static int car_needs_setup(void) {
    int need;
    pthread_mutex_lock(&g_relay_lock);
    pthread_mutex_lock(&g_lock);
    need = !g_car_cid || (g_relay.for_gen == g_hu_gen && g_relay.dead);
    pthread_mutex_unlock(&g_lock);
    pthread_mutex_unlock(&g_relay_lock);
    return need;
}

/* Preserve the complete native command, including UUID, adjacency and any
 * future fields. UI/view intent survives a phone reconnect, but never crosses
 * a car session. Stock's generic phone->car delegate already forwards suggestUI.
 * Acknowledgments below belong to the phone command completion, not submission. */
static struct car_control {
    CFRef request;
    unsigned version, delivered_gen, delivered_version, inflight;
    long next_try;
} g_controls[3]; /* showUI/stopUI, updateViewArea, forceKeyFrame */
/* Issue #68: the phone must start in the car's current view, the state its VIEW presses
 * leave. The car display is captured once per car session, but a phone can reconnect
 * within it; its /info then gets the car's latest updateViewArea (index and adjacency) as
 * initialViewArea/adjacentViewAreas instead of the view the car started in. The head unit
 * declares both in /info from 1.6. Edits alt (a copy) only; the car snapshot is untouched.
 * An adjacency that is not a list of other views of this display is dropped, not sent.
 * Returns the view applied, or -1 (no car view command yet, or one that does not fit this
 * display). Caller holds g_lock. */
static int apply_car_view_locked(CFRef alt) {
    CFRef req = g_controls[1].request, params, areas, adj, k;
    int64_t index;
    long count, i;
    if (!req) return -1;
    areas = get_value(alt, "viewAreas");
    if (!is_type(areas, cf_array_type) || (count = cf_array_count(areas)) < 1) return -1;
    params = get_value(req, "params");
    if (!get_int(params, "viewAreaIndex", 0, count - 1, &index)) return -1;
    adj = get_value(params, "adjacentViewAreas");
    if (!is_type(adj, cf_array_type)) adj = 0;
    for (i = 0; adj && i < cf_array_count(adj); ++i) {
        CFRef n = cf_array_get(adj, i);
        OSStatus e = 0;
        int64_t v;
        if (!is_type(n, cf_number_type)) { adj = 0; break; }
        if (!cf_get_i64) continue;          /* optional symbol: keep the car's list as sent */
        v = cf_get_i64(n, &e);
        if (e || v < 0 || v >= count || v == index) adj = 0;
    }
    set_i64(alt, "initialViewArea", index);
    if (adj) set_value(alt, "adjacentViewAreas", adj);
    else if ((k = s_cf("adjacentViewAreas")) != 0) { cf_dict_remove(alt, k); cf_release(k); }
    return (int)index;
}

/* The completion owns its ticket. Do not free it on teardown: stock may still
 * call back, and command_done must safely reject the stale generation. If stock
 * discards the completion, this allocation remains (at most three per abandoned
 * generation, not a process-wide bound across repeated reconnects). */
struct command_ticket { unsigned slot, version, gen, car_gen, ui_version; };

static int cluster_command_slot(CFRef request) {
    CFRef type = get_value(request, "type"), params = get_value(request, "params");
    char uuid[64], expected[64];
    int match;
    if (!get_string(params, "uuid", uuid, sizeof uuid)) return -1;
    pthread_mutex_lock(&g_lock);
    match = get_string(g_car_display, "uuid", expected, sizeof expected) && !strcmp(uuid, expected);
    pthread_mutex_unlock(&g_lock);
    if (!match) return -1;
    if (ascii_eq(type, "showUI")) {
        char url[URL_CAP];
        return get_string(params, "url", url, sizeof url) ? 0 : -1;
    }
    if (ascii_eq(type, "stopUI")) return 0;
    if (ascii_eq(type, "updateViewArea")) return 1;
    if (ascii_eq(type, "forceKeyFrame")) return 2;
    return -1;
}

static void capture_car_command(CFRef request) {
    int slot = cluster_command_slot(request);
    CFRef copy, old;
    if (slot < 0 || !(copy = cf_dict_copy(0, 0, request))) return;
    pthread_mutex_lock(&g_lock);
    old = g_controls[slot].request;
    g_controls[slot].request = copy;
    ++g_controls[slot].version;
    pthread_mutex_unlock(&g_lock);
    if (old) cf_release(old);
}

static void command_done(OSStatus err, CFRef response, void *context) {
    struct command_ticket *t = context;
    struct car_control *c;
    (void)response;
    pthread_mutex_lock(&g_lock);
    c = &g_controls[t->slot];
    if (t->car_gen == g_hu_gen && c->inflight == t->gen) {
        c->inflight = 0;
        if (!err && (t->slot != 1 || t->ui_version == g_controls[0].version)) {
            c->delivered_gen = t->gen; c->delivered_version = t->version;
            /* A successful UI change may reset the phone's layout. Reapply the
             * car's exact view command once; failed UI attempts never do this. */
            if (t->slot == 0 && t->version == c->version) g_controls[1].delivered_gen = 0;
        }
    }
    pthread_mutex_unlock(&g_lock);
    if (err) dlog("car command slot=%u completion=%d; retry after 1s", t->slot, err);
    free(t);
}

static void service_car_commands(void *session, unsigned gen) {
    unsigned i;
    long now = time_plain(0);
    for (i = 0; i < 3; ++i) {
        struct command_ticket *t;
        struct car_control *c;
        CFRef request;
        OSStatus e;
        pthread_mutex_lock(&g_lock);
        c = &g_controls[i];
        if (gen != g_rx_gen || !c->request || c->inflight || now < c->next_try ||
            (c->delivered_gen == gen && c->delivered_version == c->version) ||
            (i == 1 && g_controls[0].request &&
             (g_controls[0].delivered_gen != gen ||
              g_controls[0].delivered_version != g_controls[0].version))) {
            pthread_mutex_unlock(&g_lock);
            continue;
        }
        t = malloc(sizeof *t);
        if (!t) { pthread_mutex_unlock(&g_lock); return; }
        t->slot = i; t->version = c->version; t->gen = gen; t->car_gen = g_hu_gen;
        t->ui_version = g_controls[0].version;
        request = cf_retain(c->request);
        c->next_try = now + 1; c->inflight = gen;
        pthread_mutex_unlock(&g_lock);
        e = r_send_cmd(session, request, (void *)command_done, t);
        cf_release(request);
        if (e) command_done(e, 0, t);
    }
}

OSStatus AirPlayReceiverSessionSendCommand(void *session, CFRef request, void *completion, void *context) {
    void *ra = __builtin_return_address(0);
    if (!ready()) return r_send_cmd ? r_send_cmd(session, request, completion, context) : -1;
    /* The CPAA parser already cached these commands at decode. Bypass only its
     * fire-and-forget call: our worker supplies a completion and retries errors. */
    if (!disabled() && !completion && caller_is_cpaa(ra) && cluster_command_slot(request) >= 0) return 0;
    return r_send_cmd(session, request, completion, context);
}

static void car_reset(int forget_display) {
    unsigned i;
    pthread_mutex_lock(&g_relay_lock);
    relay_drop_locked(1);
    pthread_mutex_lock(&g_lock);
    ++g_hu_gen;
    g_car_port = 0; g_car_addr_len = 0; g_car_pending = 0;
    g_car_cid = 0; g_car_main_cid = 0; g_hu_key_valid = 0;
    memset(g_car_master, 0, sizeof g_car_master);
    memset(g_hu_key, 0, sizeof g_hu_key); memset(g_hu_iv, 0, sizeof g_hu_iv);
    if (forget_display) {
        if (g_car_display) cf_release(g_car_display);
        g_car_display = 0;
    }
    for (i = 0; i < 3; ++i) {
        if (g_controls[i].request) cf_release(g_controls[i].request);
        memset(&g_controls[i], 0, sizeof g_controls[i]);
    }
    pthread_mutex_unlock(&g_lock);
    pthread_mutex_unlock(&g_relay_lock);
}

void HTTPClientInvalidate(void *client) {
    if (!r_client_invalidate) *(void **)&r_client_invalidate = next_sym("HTTPClientInvalidate");
    if (ready() && !disabled() && r_car_client && client == *r_car_client) car_reset(1);
    if (r_client_invalidate) r_client_invalidate(client);
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
    int want_kf = 0, current;
    if (!relay_current(rx_gen)) return 0;
    pthread_mutex_lock(&g_relay_lock);
    pthread_mutex_lock(&g_lock);
    current = rx_gen == g_rx_gen && g_relay.for_gen == g_hu_gen;
    pthread_mutex_unlock(&g_lock);
    if (g_relay.fd >= 0 && current) {
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
    pthread_mutex_lock(&g_relay_lock);
    pthread_mutex_lock(&g_lock);
    if (rx_gen != g_rx_gen) {
        pthread_mutex_unlock(&g_lock);
        pthread_mutex_unlock(&g_relay_lock);
        return 0;
    }
    cur = g_hu_gen;
    have = g_hu_key_valid && g_car_port > 0;
    if (have) { memcpy(key, g_hu_key, 16); memcpy(iv, g_hu_iv, 16); }
    pthread_mutex_unlock(&g_lock);

    if (cur != g_relay.for_gen) {           /* new head-unit SETUP: fresh key, fresh tries */
        if (g_relay.fd >= 0) dlog("head-unit re-keyed (hu_gen %u->%u); reconnecting", g_relay.for_gen, cur);
        relay_drop_locked(0);
        g_relay.for_gen = cur;
        g_relay.tries = 0;
        g_relay.next_try = 0;
    }
    if (!have && !g_relay.nokey_logged) {
        g_relay.nokey_logged = 1;
        dlog("relay: waiting for native car SETUP port/key");
    }
    if (g_relay.fd < 0 && have && !g_relay.dead) {
        long now = time_plain(0);
        if (now >= g_relay.next_try) {
            g_relay.next_try = now + HU_RETRY_S;
            g_relay.tries++;
            g_relay.fd = hu_connect();
            if (g_relay.fd >= 0) {
                memset(&g_relay.ctx, 0, sizeof g_relay.ctx);
                g_relay.enc = r_ctr_init(&g_relay.ctx, key, iv) == 0;
                if (!g_relay.enc) {
                    relay_drop_locked(1);
                    dlog("car stream AES init failed; no video sent");
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
            if (!g_relay.enc || r_ctr_update(&g_relay.ctx, body, h->bodySize, body) != 0) {
                g_relay.err++;
                relay_drop_locked(1);
                pthread_mutex_unlock(&g_relay_lock);
                return 0;
            }
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
    int desync = 0;
    long kf_late = 0;
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
    memcpy(cc_key, a.key, sizeof cc_key);
    memset(a.key, 0, sizeof a.key);
    dec_ok = 1; /* key derivation succeeded before SETUP was acknowledged */
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
        if (STEP_RELAY && !disabled() && !flag(NORELAY_FILE)) {
            long now = time_plain(0);
            service_car_commands(a.session, a.gen);
            if (kf_late && now >= kf_late) {
                kf_late = 0;
                ask_keyframe(a.session);
            }
        }
#endif
        rr = read_full(c, &h, sizeof h, a.gen, 1);
        if (rr == 2) continue;
        if (rr <= 0) break;
        if (h.bodySize > MAX_BODY) { dlog("reject bodySize=%u", h.bodySize); break; }
        if (h.bodySize) {
            body = malloc(h.bodySize);
            if (!body) break;
            if (read_full(c, body, h.bodySize, a.gen, 0) <= 0) { free(body); break; }
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
                    relay_config(a.gen, &attached, &h, body)) {
                    ask_keyframe(a.session);
#if STEP_DECRYPT
                    kf_late = time_plain(0) + KF_LATE_S;
#endif
                }
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
                    relay_frame(a.gen, &attached, &h, body, &cfg_hdr, cfg, cfg_len)) {
                    ask_keyframe(a.session);           /* outside g_relay_lock */
#if STEP_DECRYPT
                    if (!kf_late) kf_late = time_plain(0) + KF_LATE_S;
#endif
                }
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
static int append_response_111(CFRef response, CFRef requested_111, int port) {
    CFRef old, arr, sd;
    int ok = 0;
    if (!is_type(response, cf_dict_type) || !requested_111) return -1;
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
        ok = 1;
    }
    if (sd) cf_release(sd);
    if (arr) cf_release(arr);
    return ok ? 0 : -1;
}

static int start_receiver(uint64_t cid, void *session, CFRef response, CFRef req111) {
    int port = 0, lfd;
    struct rx_arg *arg;
    unsigned gen, i;
    pthread_t th;
    uint8_t key[32] = {0};
#if STEP_DECRYPT
    if (!chacha_derive(session, cid, key)) return -1;
#endif
    /* retire any previous receiver (it polls g_rx_gen and exits within ~1s,
     * closing its own listen fd; until then port 7100 may still be bound, so
     * the new listener falls back to an ephemeral port - fine, the actual port
     * is what we return in the response). */
    pthread_mutex_lock(&g_lock);
    gen = ++g_rx_gen;
    for (i = 0; i < 3; ++i) {
        g_controls[i].inflight = 0;
        g_controls[i].next_try = 0;
    }
    pthread_mutex_unlock(&g_lock);

    lfd = open_listener(&port);
    if (lfd < 0) { memset(key, 0, sizeof key); dlog("111 listener bind failed: %s", strerror(errno)); return -1; }
    arg = malloc(sizeof *arg);
    if (!arg) { memset(key, 0, sizeof key); close(lfd); return -1; }
    arg->listen_fd = lfd;
    arg->gen = gen;
    arg->cid = cid;
    arg->session = (session && cf_retain) ? cf_retain(session) : session;
    memcpy(arg->key, key, sizeof key);
    memset(key, 0, sizeof key);
    if (pthread_create(&th, 0, rx_thread, arg) != 0) {
        if (arg->session && cf_release) cf_release(arg->session);
        close(lfd); free(arg);
        dlog("111 receiver thread create failed");
        return -1;
    }
    pthread_detach(th);
    if (append_response_111(response, req111, port) != 0) {
        pthread_mutex_lock(&g_lock); ++g_rx_gen; pthread_mutex_unlock(&g_lock);
        return -1;
    }
    dlog("111 receiver ready cid=%llu port=%d", (unsigned long long)cid, port);
    return 0;
}

/* ============ SESSION SETUP ============ */
OSStatus AirPlayReceiverSessionSetup(void *session, CFRef request, CFRef *response) {
    OSStatus e;
    CFRef req111, stock = 0;
    int cluster_only = 0;
    if (!ready()) return r_setup ? r_setup(session, request, response) : -1;
    if (t_busy || disabled()) return r_setup(session, request, response);
    req111 = find_stream(request, TYPE_CLUSTER);
    if (req111) {
        stock = without_cluster(request, "streams");
        if (!stock) return -1;
        cluster_only = cf_array_count(streams_of(stock)) == 0;
        if (cluster_only && (!response || !dict_cid(req111) || !car_available())) {
            cf_release(stock);
            return -1;
        }
    }
    t_busy = 1;
    if (cluster_only) {
        *response = cf_dict_new(0, 0, dict_key_cb, dict_val_cb);
        e = *response ? 0 : -1;
    } else e = r_setup(session, stock ? stock : request, response);
    if (stock) cf_release(stock);
    if (!e && response && *response) {
        if (car_available()) merge_features(*response, car_view_count() > 0);
        if (req111 && STEP_RECEIVE) {
            OSStatus cluster_e = -1;
            if (dict_cid(req111) && car_available())
                cluster_e = start_receiver(dict_cid(req111), session, *response, req111);
            /* A rejected optional stream must not undo stock's successful
             * main/audio SETUP. Failed starts never append a 111 response. */
            if (cluster_only) e = cluster_e;
            else if (cluster_e) dlog("phone SETUP 111 unavailable rc=%d; preserving stock streams", cluster_e);
        }
    }
    t_busy = 0;
    return e;
}

void AirPlayReceiverSessionTearDown(void *session, CFRef request, OSStatus reason, void *done) {
    int full;
    CFRef stock = 0;
    if (!ready()) { if (r_teardown) r_teardown(session, request, reason, done); return; }
    if (disabled()) { r_teardown(session, request, reason, done); return; }
    full = !streams_of(request) || cf_array_count(streams_of(request)) == 0;
    if (full || find_stream(request, TYPE_CLUSTER)) {
        pthread_mutex_lock(&g_lock);
        ++g_rx_gen;
        pthread_mutex_unlock(&g_lock);
    }
    if (!full && find_stream(request, TYPE_CLUSTER)) {
        stock = without_cluster(request, "streams");
        /* Failure to allocate a filtered request must never tear down stock 110. */
        if (!stock || cf_array_count(streams_of(stock)) == 0) {
            if (stock) cf_release(stock);
            if (done) *(uint8_t *)done = 0;
            return;
        }
    }
    /* Phone teardown does not own the car key/socket. CPAA's full car teardown
     * calls HTTPClientInvalidate before clearing gAirPlayClient; that boundary
     * retires the independent stream if the car session also ends. */
    r_teardown(session, stock ? stock : request, reason, done);
    if (stock) cf_release(stock);
}

/* ============ ROUTE GUIDANCE (iAP2 0x5200-0x5204) ============
 * Stock CPAAProxyEx relays iAP2 between the car (USB link, the dongle is the
 * device) and the phone (iAP over CarPlay, the dongle is the accessory) from
 * fixed tables. The phone Identify (CPSendIAP2MsgIds / CPRecvIAP2MsgIds) has no
 * route guidance, and both relays drop 0x52xx. With the head-unit hook the car
 * Identify carries RouteGuidanceDisplayComponent (0x1E) and the car keeps
 * sending StartRouteGuidanceUpdate (0x5200), which never reaches the phone.
 * The bridge copies the car's 0x1E component and the five message IDs into the
 * phone Identify, relays the car's start/stop to the phone and the phone's
 * updates to the car. It wraps libiAP2Link imports only; every message still
 * reaches the stock handler, which drops 0x52xx itself. */
#ifndef CLUSTER_RGD
#define CLUSTER_RGD 1
#endif
#define RGD_OFF_FILE "/mnt/UDISK/cluster_rgd.off"

#define IAP_ID_START 0x1d00
#define IAP_ID_INFO 0x1d01
#define IAP_ID_REJECTED 0x1d03
#define RGD_START 0x5200
#define RGD_UPDATE 0x5201
#define RGD_MANEUVER 0x5202
#define RGD_STOP 0x5203
#define RGD_LANE 0x5204
#define ID_SENT_BY_ACC 6         /* Identify list: messages the accessory sends */
#define ID_RECV_BY_ACC 7         /* Identify list: messages the accessory receives */
#define ID_RGD_COMPONENT 0x1e
#define RGD_COMP_CAP 512
#define RGD_START_CAP 256
#define IAP_MSG_CAP 65535u
#define ID_OUT_CAP (4096 + RGD_COMP_CAP + 16)   /* stock builds the Identify in 4 KiB */
#define RGD_LOG_EVERY 500

typedef int (*iap_ready_fn)(void *link, uint8_t *data, uint32_t len, uint8_t session);
typedef int (*iap_send_fn)(void *link, const uint8_t *data, uint32_t len, uint8_t session,
                           void *ctx, void *done);
/* libiAP2Link v129: type 1 = device link (car, via iAP2LinkRunLoopCreateDevice),
 * 0 = accessory link (phone). The data callback is the sixth argument for both. */
typedef void *(*iap_rl_create_fn)(int type, void *ctx, void *syn, void *send, void *send2,
                                  iap_ready_fn ready, void *connected, void *detect,
                                  int validate, int max_packets, void *buf);

static iap_rl_create_fn r_rl_create;
static iap_send_fn r_queue_send;
static iap_ready_fn r_car_ready, r_phone_ready;  /* iap2_dev_recv_data, CarPlayIAP2Session::iAP2LinkDataReadyCB */
static void (*r_to_car)(const uint8_t *, uint32_t, int);  /* iap2_send_message_to_proxy_usb */
static void *const *r_cp_link;                   /* gOverCarplayLink: set after the phone's 0x4E09, NULL at stop */
static const uint8_t *r_cp_ctrl;                 /* gOverCarPlayCtrlSession */

static pthread_once_t g_rgd_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_rgd_lock = PTHREAD_MUTEX_INITIALIZER;
static __thread int t_rgd_phone;                 /* inside the stock phone data callback */
static struct {
    int off;                                     /* build flag, off file, missing symbol or a phone rejection */
    uint8_t comp[RGD_COMP_CAP];                  /* car's 0x1E parameter, verbatim */
    size_t comp_len;
    uint8_t start[RGD_START_CAP];                /* car's last 0x5200, replayed to a new phone link */
    size_t start_len;
    int phone_ok;                                /* the current phone Identify carried the component */
    unsigned logged, to_car;
    /* Phone updates can span link packets. Only the phone callback (one stock
     * run loop thread) touches these. */
    uint8_t rx[IAP_MSG_CAP];
    uint32_t rx_have, rx_want;
    uint8_t rx_session;
} g_rgd;

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static void put_be16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

/* ID of one complete iAP2 control message (0x4040, BE16 length, BE16 ID), else 0. */
static unsigned iap_msg_id(const uint8_t *m, uint32_t n) {
    if (!m || n < 6 || m[0] != 0x40 || m[1] != 0x40 || be16(m + 2) != n) return 0;
    return be16(m + 4);
}

/* Parameter `id` of a well-formed message, header included; NULL otherwise. */
static const uint8_t *iap_param(const uint8_t *m, size_t n, unsigned id, size_t *len) {
    size_t off = 6;
    while (off + 4 <= n) {
        size_t pl = be16(m + off);
        if (pl < 4 || off + pl > n) return NULL;
        if (be16(m + off + 2) == id) { *len = pl; return m + off; }
        off += pl;
    }
    return NULL;
}

static int list_has(const uint8_t *p, size_t pl, unsigned id) {
    size_t i;
    for (i = 4; i + 2 <= pl; i += 2) if (be16(p + i) == id) return 1;
    return 0;
}

/* The phone Identify `m` plus the car's route guidance component and message
 * IDs, written to `out`. 0 sends the stock Identify unchanged. */
static size_t rgd_patch_identify(const uint8_t *m, size_t n, const uint8_t *comp, size_t comp_len,
                                 uint8_t *out, size_t cap) {
    static const uint16_t sent[] = { RGD_START, RGD_STOP };
    static const uint16_t recv[] = { RGD_UPDATE, RGD_MANEUVER, RGD_LANE };
    size_t off = 6, o = 6, i;
    int lists = 0;
    if (iap_msg_id(m, (uint32_t)n) != IAP_ID_INFO || comp_len < 4) return 0;
    if (n + comp_len + 2 * 5 > cap || n + comp_len + 2 * 5 > IAP_MSG_CAP) return 0;
    memcpy(out, m, 6);
    while (off < n) {
        const uint16_t *add = NULL;
        size_t nadd = 0, pl, grown;
        unsigned id;
        if (off + 4 > n) return 0;
        pl = be16(m + off);
        id = be16(m + off + 2);
        if (pl < 4 || off + pl > n || id == ID_RGD_COMPONENT) return 0;
        if (id == ID_SENT_BY_ACC) { add = sent; nadd = 2; lists |= 1; }
        if (id == ID_RECV_BY_ACC) { add = recv; nadd = 3; lists |= 2; }
        memcpy(out + o, m + off, pl);
        for (grown = pl, i = 0; i < nadd; ++i)
            if (!list_has(m + off, pl, add[i])) { put_be16(out + o + grown, add[i]); grown += 2; }
        put_be16(out + o, (unsigned)grown);
        o += grown;
        off += pl;
    }
    if (lists != 3) return 0;
    memcpy(out + o, comp, comp_len);
    o += comp_len;
    put_be16(out + 2, (unsigned)o);
    return o;
}

static int rgd_send_phone(const uint8_t *m, size_t n) {
    void *link = r_cp_link ? *r_cp_link : NULL;
    if (!link || !r_queue_send) return 0;
    r_queue_send(link, m, (uint32_t)n, *r_cp_ctrl, NULL, NULL);
    return 1;
}

static void rgd_car_identify(const uint8_t *m, uint32_t n) {
    size_t pl = 0;
    const uint8_t *p = iap_param(m, n, ID_RGD_COMPONENT, &pl);
    unsigned cid = 0;
    if (p && pl > RGD_COMP_CAP) p = NULL;
    if (p && pl >= 10 && be16(p + 4) == 6 && be16(p + 6) == 0) cid = be16(p + 8);
    pthread_mutex_lock(&g_rgd_lock);
    g_rgd.comp_len = p ? pl : 0;
    if (p) memcpy(g_rgd.comp, p, pl);
    g_rgd.start_len = 0;
    pthread_mutex_unlock(&g_rgd_lock);
    if (p) dlog("rgd car component=0x%04x len=%u", cid, (unsigned)pl);
    else dlog("rgd car Identify has no route guidance component");
}

/* Car start/stop: cached for the next phone link, sent to a phone that
 * accepted the component. */
static void rgd_car_route(unsigned id, const uint8_t *m, uint32_t n) {
    int send;
    pthread_mutex_lock(&g_rgd_lock);
    g_rgd.start_len = 0;
    if (id == RGD_START && n <= RGD_START_CAP) { memcpy(g_rgd.start, m, n); g_rgd.start_len = n; }
    send = g_rgd.phone_ok && !g_rgd.off;
    pthread_mutex_unlock(&g_rgd_lock);
    send = send && rgd_send_phone(m, n);
    dlog("rgd car 0x%04x %s", id, send ? "to phone" : "kept for the phone");
}

static void rgd_replay_start(void) {
    uint8_t m[RGD_START_CAP];
    size_t n;
    pthread_mutex_lock(&g_rgd_lock);
    n = g_rgd.phone_ok && !g_rgd.off ? g_rgd.start_len : 0;
    memcpy(m, g_rgd.start, n);
    pthread_mutex_unlock(&g_rgd_lock);
    if (n && rgd_send_phone(m, n)) dlog("rgd car 0x%04x replayed to the new phone link", RGD_START);
}

static void rgd_to_car(const uint8_t *m, uint32_t n, unsigned id) {
    unsigned bit = 1u << (id - RGD_START);
    r_to_car(m, n, 0);
    ++g_rgd.to_car;
    if (!(g_rgd.logged & bit) || g_rgd.to_car % RGD_LOG_EVERY == 0) {
        g_rgd.logged |= bit;
        dlog("rgd phone 0x%04x len=%u to car (total %u)", id, (unsigned)n, g_rgd.to_car);
    }
}

/* Phone updates, reassembled like the stock handler does: a packet that starts
 * with 0x4040 opens a message, the rest of it follows on the same session. */
static void rgd_phone_update(const uint8_t *d, uint32_t n, uint8_t session) {
    uint32_t take;
    if (n >= 6 && d[0] == 0x40 && d[1] == 0x40) {
        unsigned want = be16(d + 2), id = be16(d + 4);
        g_rgd.rx_want = 0;
        if ((id != RGD_UPDATE && id != RGD_MANEUVER && id != RGD_LANE) || want < 6) return;
        if (n >= want) { rgd_to_car(d, want, id); return; }
        memcpy(g_rgd.rx, d, n);
        g_rgd.rx_have = n;
        g_rgd.rx_want = want;
        g_rgd.rx_session = session;
        return;
    }
    if (!g_rgd.rx_want || session != g_rgd.rx_session) return;
    take = g_rgd.rx_want - g_rgd.rx_have;
    if (take > n) take = n;
    memcpy(g_rgd.rx + g_rgd.rx_have, d, take);
    g_rgd.rx_have += take;
    if (g_rgd.rx_have == g_rgd.rx_want) {
        g_rgd.rx_want = 0;
        rgd_to_car(g_rgd.rx, g_rgd.rx_have, be16(g_rgd.rx + 4));
    }
}

static int rgd_car_ready(void *link, uint8_t *data, uint32_t len, uint8_t session) {
    unsigned id = iap_msg_id(data, len);
    if (id == IAP_ID_INFO) rgd_car_identify(data, len);
    else if (id == RGD_START || id == RGD_STOP) rgd_car_route(id, data, len);
    return r_car_ready(link, data, len, session);
}

static int rgd_phone_ready(void *link, uint8_t *data, uint32_t len, uint8_t session) {
    unsigned id = iap_msg_id(data, len);
    void *before = r_cp_link ? *r_cp_link : NULL;
    int rc;
    if (id == IAP_ID_START) {
        pthread_mutex_lock(&g_rgd_lock);
        g_rgd.phone_ok = 0;
        g_rgd.logged = 0;
        pthread_mutex_unlock(&g_rgd_lock);
    } else if (id == IAP_ID_REJECTED) {
        int was;
        pthread_mutex_lock(&g_rgd_lock);
        was = g_rgd.phone_ok;
        if (was) g_rgd.off = 1;
        g_rgd.phone_ok = 0;
        pthread_mutex_unlock(&g_rgd_lock);
        dlog("rgd phone rejected the Identify%s", was ? "; bridge off until restart" : "");
    }
    rgd_phone_update(data, len, session);
    t_rgd_phone = 1;
    rc = r_phone_ready(link, data, len, session);
    t_rgd_phone = 0;
    if (!before && r_cp_link && *r_cp_link) rgd_replay_start();
    return rc;
}

static void rgd_bind(void) {
    *(void **)&r_rl_create = dlsym_plain(RTLD_NEXT, "iAP2LinkRunLoopCreate");
    *(void **)&r_queue_send = dlsym_plain(RTLD_NEXT, "iAP2LinkQueueSendData");
    *(void **)&r_car_ready = dlsym_plain(RTLD_DEFAULT, "iap2_dev_recv_data");
    *(void **)&r_phone_ready = dlsym_plain(RTLD_DEFAULT,
        "_ZN18CarPlayIAP2Session19iAP2LinkDataReadyCBEP11iAP2Link_stPhjh");
    *(void **)&r_to_car = dlsym_plain(RTLD_DEFAULT, "iap2_send_message_to_proxy_usb");
    r_cp_link = dlsym_plain(RTLD_DEFAULT, "gOverCarplayLink");
    r_cp_ctrl = dlsym_plain(RTLD_DEFAULT, "gOverCarPlayCtrlSession");
    g_rgd.off = !CLUSTER_RGD || disabled() || flag(RGD_OFF_FILE) || !r_queue_send ||
                !r_car_ready || !r_phone_ready || !r_to_car || !r_cp_link || !r_cp_ctrl;
    dlog("rgd bridge %s create=%d send=%d car=%d phone=%d", g_rgd.off ? "off" : "on",
         r_rl_create != 0, r_queue_send != 0, r_car_ready != 0, r_phone_ready != 0);
}

void *iAP2LinkRunLoopCreate(int type, void *ctx, void *syn, void *send, void *send2,
                            iap_ready_fn ready, void *connected, void *detect,
                            int validate, int max_packets, void *buf) {
    pthread_once(&g_rgd_once, rgd_bind);
    if (!r_rl_create) return NULL;
    if (!g_rgd.off && ready && ready == r_car_ready) ready = rgd_car_ready;
    else if (!g_rgd.off && ready && ready == r_phone_ready) ready = rgd_phone_ready;
    return r_rl_create(type, ctx, syn, send, send2, ready, connected, detect, validate, max_packets, buf);
}

int iAP2LinkQueueSendData(void *link, const uint8_t *data, uint32_t len, uint8_t session,
                          void *ctx, void *done) {
    static uint8_t out[ID_OUT_CAP];   /* phone run loop thread only (t_rgd_phone) */
    pthread_once(&g_rgd_once, rgd_bind);
    if (!r_queue_send) return 0;
    if (t_rgd_phone && iap_msg_id(data, len) == IAP_ID_INFO) {
        size_t n = 0, comp_len;
        pthread_mutex_lock(&g_rgd_lock);
        comp_len = g_rgd.comp_len;
        if (!g_rgd.off) n = rgd_patch_identify(data, len, g_rgd.comp, comp_len, out, sizeof out);
        g_rgd.phone_ok = n != 0;
        pthread_mutex_unlock(&g_rgd_lock);
        if (n) {
            dlog("rgd phone Identify +route guidance %u -> %u bytes", (unsigned)len, (unsigned)n);
            return r_queue_send(link, out, (uint32_t)n, session, ctx, done);
        }
        dlog("rgd phone Identify unchanged (%s)", g_rgd.off ? "bridge off" :
             comp_len ? "unexpected layout" : "no car component");
    }
    return r_queue_send(link, data, len, session, ctx, done);
}

__attribute__((constructor)) static void init(void) {
    dlog("LOADED pid=%d step=%d advert=%d", (int)getpid(), CLUSTER_STEP, CLUSTER_ADVERT);
}
