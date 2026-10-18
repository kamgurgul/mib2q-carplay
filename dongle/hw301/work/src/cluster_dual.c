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
 *   - key: master captured at AirPlayReceiverSessionSetSecurityInfo; per-stream
 *     key = AirPlay_DeriveAESKeySHA512ForScreen(master, 16, cid).
 *
 * Head-unit side (dongle = CarPlay client; NO CPAAProxyEx patch):
 *   - CFArrayEnsureCreatedAndAppend: clone the SetupScreen stream 110 dict as
 *     type 111 (cluster_inject.c). Stock CPAAProxyEx then skips 111, so the
 *     single head-unit socket slot is never clobbered. The clone keeps 110's
 *     streamConnectionID, so the head unit derives the same key for 111 as 110.
 *   - that key is captured where CPAAProxyEx derives it: AES_CTR_Init called
 *     from SetupProxyScreenSecurityInfo.
 *   - the head-unit 111 port defaults to 7100 (the hook's MHI2Q default), or
 *     /mnt/UDISK/cluster_hu.port. The head-unit address is the peer of the
 *     dongle's stream-110 data socket (peer port 6030).
 *
 * Relay: phone 111 VIDEO_FRAME bodies are AES-CTR decrypted with the phone key
 * and re-encrypted with the head-unit key; VIDEO_CONFIG and other opcodes pass
 * through. One head-unit connection is kept across phone reconnects.
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
 *   cluster_viewareas.on   advertise viewAreas feature + areas
 *   cluster_dual.plainhu   send plain (unencrypted) frames to the head unit
 *   cluster_dump.on        dump decrypted 111 AUs to /mnt/UDISK/c111.h264 (~2MB)
 * Port override: cluster_hu.port
 * To disable without a shell, flash stock fw/hw301_v129.tar.
 *
 * Logs -> /dev/kmsg (getlogs.py pulls dmesg.log; app.log loses late appends).
 *
 * Build (produces an image, does not flash). build_cluster.sh wraps these:
 *   WRAP_LIB=libCoreUtils.so CHECK_SYM=AirPlayReceiverSessionSetup \
 *   CLUSTER_CFLAGS=-DCLUSTER_STEP=1 CLUSTER_SRC=cluster_dual.c \
 *   ./work/tools/build.sh 129 1132
 */
#ifndef CLUSTER_STEP
#define CLUSTER_STEP 3
#endif
#define STEP_RECEIVE (CLUSTER_STEP >= 2)   /* listen, append dataPort, decrypt */
#define STEP_RELAY   (CLUSTER_STEP >= 3)   /* inject 111 to the HU and forward */
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
#include <unistd.h>

#define UTF8 0x08000100
#define TYPE_MAIN 110
#define TYPE_CLUSTER 111
#define CLUSTER_W 1440
#define CLUSTER_H 540
#define CLUSTER_FPS 30
#define CLUSTER_WMM 290
#define CLUSTER_HMM 108
#define CLUSTER_UUID "b7e6c5a0-2222-4000-8000-000000000002"
#define URL_CLUSTER "maps:/car/instrumentcluster"
#define URL_MAP "maps:/car/instrumentcluster/map"
#define URL_CARD "maps:/car/instrumentcluster/instructioncard"

#define OFF_FILE "/mnt/UDISK/cluster_dual.off"
#define NOINJECT_FILE "/mnt/UDISK/cluster_dual.noinject"
#define NORELAY_FILE "/mnt/UDISK/cluster_dual.norelay"
#define VIEWAREAS_FILE "/mnt/UDISK/cluster_viewareas.on"
#define PLAINHU_FILE "/mnt/UDISK/cluster_dual.plainhu"
#define DUMP_FILE "/mnt/UDISK/cluster_dump.on"
#define DUMP_PATH "/mnt/UDISK/c111.h264"
#define HU_PORT_FILE "/mnt/UDISK/cluster_hu.port"

#define PHONE_PORT 7100          /* preferred phone-facing listener port */
#define HU_PORT_DEFAULT 7100     /* head-unit altscreen hook default (MHI2Q) */
#define HU_MAIN_PEER_PORT 6030   /* dongle's stream-110 data socket peer port */
#define SPSSI_SIZE 0x2b8         /* SetupProxyScreenSecurityInfo span, v129 */

#define HDR_SIZE 128
#define OP_VIDEO_FRAME 0
#define OP_VIDEO_CONFIG 1
#define MAX_BODY (8u * 1024u * 1024u)
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

static void (*cf_release)(CFRef);
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
static unsigned g_gen;                      /* bumps to retire the receiver thread */
static int g_listen_fd = -1;
static pthread_t g_thread;
static int g_thread_live;

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

static void *next_sym(const char *name) {
    void *p = dlsym(RTLD_NEXT, name);
    return p ? p : dlsym(RTLD_DEFAULT, name);
}

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
#undef NEED
    *(void **)&cf_cstr_ptr = next_sym("CFStringGetCStringPtr");
    *(void **)&cf_cstr_get = next_sym("CFStringGetCString");

    /* SetupProxyScreenSecurityInfo is a CPAAProxyEx (executable) export */
    spssi = dlsym(RTLD_DEFAULT, "SetupProxyScreenSecurityInfo");
    if (spssi && dladdr(spssi, &di)) {
        g_spssi_lo = (uintptr_t)spssi;
        g_spssi_hi = (uintptr_t)spssi + SPSSI_SIZE;
        g_cpaa_base = di.dli_fbase;
    }

    k_type = s_cf("type");
    k_conn = s_cf("streamConnectionID");
    k_streams = s_cf("streams");
    k_displays = s_cf("displays");
    k_features = s_cf("enabledFeatures");
    g_ok = k_type && k_conn && k_streams && k_displays && k_features;
    if (!g_ok) dlog("CF key alloc failed");
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
    int have_alt = 0, have_va = 0, want_va = flag(VIEWAREAS_FILE);
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

/* Return a new info dict, or 0 to keep base. Caller releases base on success. */
static CFRef augment_info(CFRef base) {
    CFRef info, old, displays, stock, alt;
    if (!is_type(base, cf_dict_type)) return 0;
    old = cf_dict_get(base, k_displays);
    if (!is_type(old, cf_array_type) || cf_array_count(old) < 1) return 0;
    if (already_cluster(old)) return 0;
    info = cf_dict_copy(0, 0, base);
    if (!info) return 0;
    merge_features(info);
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
    set_str(alt, "displayUUID", CLUSTER_UUID);
    set_str(alt, "initialURL", URL_CLUSTER);
    add_urls(alt);
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

/* Head-unit 111 key: AES_CTR_Init called from SetupProxyScreenSecurityInfo.
 * The clone keeps 110's streamConnectionID, so 110's key == 111's key. */
OSStatus AES_CTR_Init(void *ctx, const uint8_t *key, const uint8_t *iv) {
    OSStatus e;
    void *ra = __builtin_return_address(0);
    if (!ready()) return r_ctr_init ? r_ctr_init(ctx, key, iv) : -1;
    e = r_ctr_init(ctx, key, iv);
    if (e == 0 && key && iv && !disabled() && g_spssi_lo &&
        (uintptr_t)ra >= g_spssi_lo && (uintptr_t)ra < g_spssi_hi) {
        pthread_mutex_lock(&g_lock);
        memcpy(g_hu_key, key, 16);
        memcpy(g_hu_iv, iv, 16);
        g_hu_key_valid = 1;
        g_gen++;                 /* new head-unit key -> drop any open relay socket */
        pthread_mutex_unlock(&g_lock);
        dlog("head-unit 111 key captured gen=%u", g_gen);
    }
    return e;
}

/* ============ HEAD-UNIT SIDE: inject 111 into the SetupScreen streams ============ */
void CFArrayEnsureCreatedAndAppend(void **ioArray, const void *value) {
    void *ra = __builtin_return_address(0);
    CFRef clone;
    if (!r_ensure) { r_ensure = (void (*)(void **, const void *))next_sym("CFArrayEnsureCreatedAndAppend"); }
    r_ensure(ioArray, value);                 /* normal append (main stream) */
    if (!STEP_RELAY) return;                   /* 111->HU injection is step 3 */
    if (!ready() || disabled() || flag(NOINJECT_FILE)) return;
    if (t_busy || !ioArray || !*ioArray || !value) return;
    if (!caller_is_cpaa(ra)) return;
    if (!is_type((CFRef)value, cf_dict_type)) return;
    if (dict_type((CFRef)value) != TYPE_MAIN) return;      /* only clone the main stream */
    if (!cf_dict_get((CFRef)value, k_conn)) return;        /* must be a stream dict */
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
            pthread_mutex_lock(&g_lock);
            int stale = (gen != g_gen);
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

/* Find the head-unit address = peer of the dongle's stream-110 data socket. */
static int hu_addr(struct sockaddr_in6 *out) {
    int fd, found = 0;
    for (fd = 3; fd < 1024 && !found; fd++) {
        struct sockaddr_in6 sa;
        socklen_t sl = sizeof sa;
        if (getpeername(fd, (struct sockaddr *)&sa, &sl) != 0) continue;
        if (sa.sin6_family != AF_INET6) continue;
        if (ntohs(sa.sin6_port) != HU_MAIN_PEER_PORT) continue;
        *out = sa;                      /* keeps the %usb0 scope id */
        found = 1;
    }
    return found;
}

static int hu_connect(void) {
    struct sockaddr_in6 sa;
    int fd, one = 1;
    long fl;
    struct pollfd pf;
    if (!hu_addr(&sa)) { dlog("head-unit addr not found (no peer :6030)"); return -1; }
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
    if (poll(&pf, 1, 3000) <= 0) { close(fd); dlog("head-unit connect timeout"); return -1; }
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

static void ask_keyframe(void *session) {
    CFRef req, params;
    if (!session || !r_send_cmd) return;
    req = cf_dict_new(0, 0, dict_key_cb, dict_val_cb);
    params = cf_dict_new(0, 0, dict_key_cb, dict_val_cb);
    if (req && params) {
        set_str(req, "type", "forceKeyFrame");
        set_str(params, "uuid", CLUSTER_UUID);
        set_value(req, "params", params);
        r_send_cmd(session, req, 0, 0);
        dlog("forceKeyFrame requested");
    }
    if (params) cf_release(params);
    if (req) cf_release(req);
}

/* ============ PHONE 111 RECEIVER + RELAY ============ */
struct rx_arg { int listen_fd; unsigned gen; uint64_t cid; void *session; };

static void dump_au(int *dfd, size_t *dn, const uint8_t *p, size_t n) {
    if (!flag(DUMP_FILE)) return;
    if (*dfd < 0 && *dn == 0) *dfd = open(DUMP_PATH, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (*dfd >= 0 && *dn < DUMP_LIMIT) {
        ssize_t w = write(*dfd, p, n);
        if (w > 0) *dn += (size_t)w;
    }
}

static void *rx_thread(void *varg) {
    struct rx_arg a = *(struct rx_arg *)varg;
    free(varg);
    int listen_fd = a.listen_fd, c = -1, hu = -1, dfd = -1;
    size_t dn = 0;
    AesCtx phone_ctx, hu_ctx;
    int phone_valid = 0, hu_valid = 0, config_seen = 0;
    unsigned long frames = 0, idr = 0, send_err = 0;
    uint8_t key[16], iv[16];
    uint8_t *cfg = 0; uint32_t cfg_len = 0;

    /* derive the phone-side per-stream key */
    pthread_mutex_lock(&g_lock);
    if (g_master_valid) {
        r_derive(g_master, 16, a.cid, key, iv);
        if (r_ctr_init(&phone_ctx, key, iv) == 0) phone_valid = 1;
    }
    pthread_mutex_unlock(&g_lock);
    memset(key, 0, sizeof key); memset(iv, 0, sizeof iv);
    if (!phone_valid) dlog("no phone key; decrypt disabled (U2: ChaCha?)");

    /* accept the iPhone 111 connection */
    for (;;) {
        struct pollfd pf = { listen_fd, POLLIN, 0 };
        int pr = poll(&pf, 1, 1000);
        pthread_mutex_lock(&g_lock);
        int stale = (a.gen != g_gen);
        pthread_mutex_unlock(&g_lock);
        if (stale) goto done;
        if (pr < 0) { if (errno == EINTR) continue; goto done; }
        if (pr == 0) continue;
        c = accept(listen_fd, 0, 0);
        if (c < 0) { if (errno == EINTR) continue; goto done; }
        break;
    }
    dlog("111 connected from iPhone cid=%llu", (unsigned long long)a.cid);

    for (;;) {
        ScreenHeader h;
        uint8_t *body = 0;
        int rr = read_full(c, &h, sizeof h, a.gen);
        if (rr <= 0) break;
        if (h.bodySize > MAX_BODY) { dlog("reject bodySize=%u", h.bodySize); break; }
        if (h.bodySize) {
            body = malloc(h.bodySize);
            if (!body) break;
            if (read_full(c, body, h.bodySize, a.gen) <= 0) { free(body); break; }
        }

        if (h.opcode == OP_VIDEO_CONFIG) {
            if (body && h.bodySize && !config_seen) {
                config_seen = 1;
                free(cfg);
                cfg = malloc(h.bodySize);
                if (cfg) { memcpy(cfg, body, h.bodySize); cfg_len = h.bodySize; }
                dlog("111 video config body=%u", h.bodySize);
            }
        } else if (h.opcode == OP_VIDEO_FRAME) {
            if (body && h.bodySize) {
                if (phone_valid && r_ctr_update(&phone_ctx, body, h.bodySize, body) != 0) {
                    dlog("phone AES-CTR decrypt failed"); free(body); break;
                }
                frames++;
                /* length-prefixed NAL sanity + IDR count (nal type 5) */
                if (h.bodySize >= 5) {
                    uint8_t nt = body[4] & 0x1f;
                    if (nt == 5) idr++;
                }
                dump_au(&dfd, &dn, body, h.bodySize);
                if (frames == 1 || frames % 300 == 0)
                    dlog("111 frames=%lu idr=%lu bytes=%u", frames, idr, h.bodySize);

                if (STEP_RELAY && !disabled() && !flag(NORELAY_FILE)) {
                    /* lazy head-unit connect on the first frame */
                    if (hu < 0) {
                        pthread_mutex_lock(&g_lock);
                        int have = g_hu_key_valid;
                        if (have) { memcpy(key, g_hu_key, 16); memcpy(iv, g_hu_iv, 16); }
                        pthread_mutex_unlock(&g_lock);
                        if (!have) {
                            if (frames == 1) dlog("waiting for head-unit 111 key");
                        } else {
                            hu = hu_connect();
                            if (hu >= 0) {
                                hu_valid = 0;
                                if (!flag(PLAINHU_FILE) && r_ctr_init(&hu_ctx, key, iv) == 0) hu_valid = 1;
                                memset(key, 0, sizeof key); memset(iv, 0, sizeof iv);
                                /* send cached config, then ask for a keyframe */
                                if (cfg && cfg_len) {
                                    ScreenHeader ch; memset(&ch, 0, sizeof ch);
                                    ch.bodySize = cfg_len; ch.opcode = OP_VIDEO_CONFIG;
                                    if (send_full(hu, &ch, sizeof ch) == 0) send_full(hu, cfg, cfg_len);
                                }
                                ask_keyframe(a.session);
                            }
                        }
                    }
                    if (hu >= 0) {
                        ScreenHeader oh = h;
                        uint8_t *out = body;
                        if (hu_valid) {
                            out = malloc(h.bodySize);
                            if (out && r_ctr_update(&hu_ctx, body, h.bodySize, out) != 0) {
                                free(out); out = 0;
                            }
                        }
                        if (out) {
                            if (send_full(hu, &oh, sizeof oh) != 0 ||
                                send_full(hu, out, oh.bodySize) != 0) {
                                send_err++;
                                close(hu); hu = -1;
                                if (hu_valid) { r_ctr_final(&hu_ctx); hu_valid = 0; }
                                dlog("head-unit send failed; dropping relay (send_err=%lu)", send_err);
                            }
                            if (out != body) free(out);
                        }
                    }
                }
            }
        }
        /* keepalive / forceKeyFrame / ignore: nothing */
        free(body);
    }

done:
    dlog("111 receiver exit frames=%lu idr=%lu send_err=%lu", frames, idr, send_err);
    if (c >= 0) close(c);
    if (hu >= 0) close(hu);
    if (dfd >= 0) close(dfd);
    free(cfg);
    if (phone_valid) r_ctr_final(&phone_ctx);
    if (hu_valid) r_ctr_final(&hu_ctx);
    pthread_mutex_lock(&g_lock);
    if (g_listen_fd == listen_fd) { close(g_listen_fd); g_listen_fd = -1; }
    else close(listen_fd);
    if (a.gen == g_gen) g_thread_live = 0;
    pthread_mutex_unlock(&g_lock);
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
        cf_array_append(arr, sd);
        cf_dict_set(response, k_streams, arr);
    }
    if (sd) cf_release(sd);
    if (arr) cf_release(arr);
}

static void start_receiver(uint64_t cid, void *session, CFRef response, CFRef req111) {
    int port = 0, lfd;
    struct rx_arg *arg;
    pthread_t th;
    /* retire any previous receiver */
    pthread_mutex_lock(&g_lock);
    g_gen++;
    if (g_listen_fd >= 0) { close(g_listen_fd); g_listen_fd = -1; }
    pthread_mutex_unlock(&g_lock);

    lfd = open_listener(&port);
    if (lfd < 0) { dlog("111 listener bind failed: %s", strerror(errno)); return; }
    arg = malloc(sizeof *arg);
    if (!arg) { close(lfd); return; }
    pthread_mutex_lock(&g_lock);
    g_listen_fd = lfd;
    arg->listen_fd = lfd;
    arg->gen = g_gen;
    arg->cid = cid;
    arg->session = session;
    if (pthread_create(&th, 0, rx_thread, arg) != 0) {
        g_listen_fd = -1;
        pthread_mutex_unlock(&g_lock);
        close(lfd); free(arg);
        dlog("111 receiver thread create failed");
        return;
    }
    pthread_detach(th);
    g_thread = th;
    g_thread_live = 1;
    pthread_mutex_unlock(&g_lock);
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
    if (!disabled()) log_stream_types("setup request", request);
    e = r_setup(session, request, response);
    if (disabled()) { t_busy = 0; return e; }
    dlog("setup rc=%d response=%s", e, (response && *response) ? "yes" : "no");
    if (e == 0 && response && *response) {
        log_stream_types("setup response", *response);
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
    /* a NULL request or one that names 111 retires the receiver */
    if (!disabled() && (request == 0 || find_stream(request, TYPE_CLUSTER))) {
        pthread_mutex_lock(&g_lock);
        g_gen++;
        if (g_listen_fd >= 0) { close(g_listen_fd); g_listen_fd = -1; }
        pthread_mutex_unlock(&g_lock);
        dlog("teardown retires 111 receiver");
    }
    r_teardown(session, request, reason, done);
}

__attribute__((constructor)) static void init(void) {
    dlog("LOADED pid=%d", (int)getpid());
}
