/*
 * cluster_screen.c — HW301 second CarPlay screen, phone-facing half.
 *
 * The dongle is the AirPlay receiver the iPhone talks to. libAirPlay calls
 * AirPlayCopyServerInfo and AirPlayReceiverSessionSetup through its PLT, so a
 * wrapper-rename shim sees them (same mechanism as cluster_shim.c). The
 * advertisement is the contract the head-unit hook already uses with a real
 * iPhone (altscreen_hook/libaltscreen111_gen2.c, AirPlayCopyServerInfo):
 *
 *   root enabledFeatures keeps every stock entry and gains "altScreen" and
 *   "viewAreas". displays[0] is cloned into a type-111 display:
 *     uuid b7e6c5a0-2222-4000-8000-000000000002
 *     initialURL maps:/car/instrumentcluster
 *     altScreenSuggestUIURLs: that URL, /map, /instructioncard
 *     1440x540, 30 fps, 290x108 mm, features 0, no primaryInputDevice
 *     two full-canvas view areas (wide and classic safe areas)
 *
 * Session setup calls stock first. Stock libAirPlay compares stream type 111
 * but its "Unsupported stream type" path can fail the whole request. If that
 * happens and the request carried 111, stock is retried with 111 removed so
 * stream 110 still comes up. A dataPort is not invented here: a port with no
 * listener would black the session. When stock itself accepts 111, its
 * response (and the screen-setup / frame callbacks) are logged so the sender
 * half can be wired to that stream.
 *
 * This does not touch the head-unit socket. The iPhone9,1 guard on the head
 * unit stays on. CLUSTER_SCREEN=0, or the file /mnt/UDISK/cluster_screen.off,
 * returns stock /info and stock setup.
 *
 * Not a flash target. The working cockpit image is cluster_dual.c
 * (./work/tools/build_dual.sh optA). This is the earlier libAirPlay
 * advertisement (v1131). /info still has to match cluster_dual.c: dereference
 * kCFLBooleanFalse, safe-area height 270, and uuid only. Building this in
 * its place reproduces the /info iOS rejected (v1143).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#define UTF8 0x08000100
#define CLUSTER_TYPE 111
#define CLUSTER_W 1440
#define CLUSTER_H 540
#define CLUSTER_FPS 30
#define CLUSTER_WMM 290
#define CLUSTER_HMM 108
#define CLUSTER_UUID "b7e6c5a0-2222-4000-8000-000000000002"
#define URL_CLUSTER "maps:/car/instrumentcluster"
#define URL_MAP "maps:/car/instrumentcluster/map"
#define URL_CARD "maps:/car/instrumentcluster/instructioncard"
#define OFF_FILE "/mnt/UDISK/cluster_screen.off"

typedef void *CFRef;
typedef int64_t (*get_i64_fn)(CFRef dict, CFRef key, int *err);
typedef CFRef (*create_mut_fn)(CFRef alloc, long cap, const void *key_cb, const void *val_cb);
typedef CFRef (*array_mut_fn)(CFRef alloc, long cap, const void *cb);

static CFRef (*real_info)(void *, void *, void *, int *);
static int (*real_setup)(void *, void *, void **);
static int (*real_screen_setup)(void *, void *);
static int (*real_frames)(void *, void *, void *, void *);

static CFRef (*cf_release)(CFRef);
static CFRef (*cf_string)(CFRef, const char *, uint32_t);
static unsigned long (*cf_get_type)(CFRef);
static unsigned long (*cf_dict_type)(void);
static unsigned long (*cf_array_type)(void);
static unsigned long (*cf_string_type)(void);
static CFRef (*cf_dict_get)(CFRef, CFRef);
static void (*cf_dict_set)(CFRef, CFRef, CFRef);
static void (*cf_dict_remove)(CFRef, CFRef);
static void (*cf_dict_set_i64)(CFRef, CFRef, int64_t);
static get_i64_fn cf_dict_get_i64;
static CFRef (*cf_dict_copy)(CFRef, long, CFRef);
static create_mut_fn cf_dict_new;
static array_mut_fn cf_array_new;
static CFRef (*cf_array_copy)(CFRef, long, CFRef);
static void (*cf_array_append)(CFRef, const void *);
static CFRef (*cf_array_get)(CFRef, long);
static long (*cf_array_count)(CFRef);
static const void *array_cb, *dict_key_cb, *dict_val_cb, *bool_false;

static CFRef k_displays, k_features, k_type, k_streams;
static __thread int in_info, in_setup;
static int cf_ok;

static void slog(const char *fmt, ...) {
    char b[240];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    FILE *k = fopen("/dev/kmsg", "w");
    if (k) { fprintf(k, "cluster_screen: %s\n", b); fclose(k); }
    FILE *f = fopen("/mnt/UDISK/logs/app.log", "a");
    if (f) { fprintf(f, "[cluster_screen] %s\n", b); fclose(f); }
}

static int disabled(void) {
    const char *e = getenv("CLUSTER_SCREEN");
    if (e && e[0] == '0') return 1;
    return access(OFF_FILE, F_OK) == 0;
}

static CFRef s_cf(const char *text) {
    return cf_string ? cf_string(0, text, UTF8) : 0;
}

static int is_type(CFRef v, unsigned long (*type_fn)(void)) {
    return v && cf_get_type && type_fn && cf_get_type(v) == type_fn();
}

static int cf_bind(void) {
    if (cf_ok) return 1;
#define NEED(dst, name) do { dst = (void *)dlsym(RTLD_DEFAULT, name); if (!(dst)) { slog("missing %s", name); return 0; } } while (0)
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
    NEED(cf_dict_get_i64, "CFDictionaryGetInt64");
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
    /* The symbol is a pointer variable. dlsym returns that variable's address;
     * v1143 stored the address in viewAreas and iOS rejected /info. */
    bool_false = *(const void *const *)bool_false;
    if (!bool_false) { slog("kCFLBooleanFalse is NULL"); return 0; }
    k_displays = s_cf("displays");
    k_features = s_cf("enabledFeatures");
    k_type = s_cf("type");
    k_streams = s_cf("streams");
    cf_ok = k_displays && k_features && k_type && k_streams;
    if (!cf_ok) slog("CF key alloc failed");
    return cf_ok;
}

static void set_i64(CFRef dict, const char *key, int64_t value) {
    CFRef k = s_cf(key);
    if (k) { cf_dict_set_i64(dict, k, value); cf_release(k); }
}

static void set_str(CFRef dict, const char *key, const char *value) {
    CFRef k = s_cf(key), v = s_cf(value);
    if (k && v) cf_dict_set(dict, k, v);
    if (v) cf_release(v);
    if (k) cf_release(k);
}

static void remove_key(CFRef dict, const char *key) {
    CFRef k = s_cf(key);
    if (k) { cf_dict_remove(dict, k); cf_release(k); }
}

static int ascii_eq(CFRef value, const char *want) {
    char buf[64];
    const char *p;
    static const char *(*ptr)(CFRef, uint32_t);
    static int (*get)(CFRef, char *, long, uint32_t);
    if (!ptr) ptr = (void *)dlsym(RTLD_DEFAULT, "CFStringGetCStringPtr");
    if (!get) get = (void *)dlsym(RTLD_DEFAULT, "CFStringGetCString");
    if (!is_type(value, cf_string_type)) return 0;
    p = ptr ? ptr(value, UTF8) : 0;
    if (!p && get && get(value, buf, (long)sizeof buf, UTF8)) p = buf;
    return p && strcmp(p, want) == 0;
}

static int dict_type(CFRef dict) {
    int err = 0;
    int64_t t;
    if (!is_type(dict, cf_dict_type) || !k_type) return -1;
    t = cf_dict_get_i64(dict, k_type, &err);
    return err ? -1 : (int)t;
}

/* Keep stock entries. Append altScreen and viewAreas when they are absent. */
static void merge_features(CFRef info) {
    CFRef existing, copy = 0, alt = 0, va = 0;
    long i, n = 0;
    int have_alt = 0, have_va = 0;
    existing = cf_dict_get(info, k_features);
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
    if (!have_va && (va = s_cf("viewAreas"))) cf_array_append(copy, va);
    cf_dict_set(info, k_features, copy);
    if (va) cf_release(va);
    if (alt) cf_release(alt);
    cf_release(copy);
}

static void add_view(CFRef areas, int x, int y, int w, int h) {
    CFRef view = cf_dict_new(0, 0, dict_key_cb, dict_val_cb);
    CFRef safe = cf_dict_new(0, 0, dict_key_cb, dict_val_cb);
    CFRef k;
    if (!view || !safe) { if (view) cf_release(view); if (safe) cf_release(safe); return; }
    set_i64(view, "widthPixels", CLUSTER_W);
    set_i64(view, "heightPixels", CLUSTER_H);
    set_i64(view, "originXPixels", 0);
    set_i64(view, "originYPixels", 0);
    set_i64(safe, "widthPixels", w);
    set_i64(safe, "heightPixels", h);
    set_i64(safe, "originXPixels", x);
    set_i64(safe, "originYPixels", y);
    k = s_cf("drawUIOutsideSafeArea");
    if (k) { cf_dict_set(view, k, (CFRef)bool_false); cf_release(k); }
    k = s_cf("viewAreaTransitionControl");
    if (k) { cf_dict_set(view, k, (CFRef)bool_false); cf_release(k); }
    k = s_cf("safeArea");
    if (k) { cf_dict_set(view, k, safe); cf_release(k); }
    cf_array_append(areas, view);
    cf_release(safe);
    cf_release(view);
}

static void add_views(CFRef alt) {
    CFRef areas = cf_array_new(0, 0, array_cb);
    CFRef k;
    if (!areas) return;
    add_view(areas, 360, 87, 720, 270);   /* wide; 297 sits in the VC bar */
    add_view(areas, 520, 87, 400, 270);   /* classic */
    k = s_cf("viewAreas");
    if (k) { cf_dict_set(alt, k, areas); cf_release(k); }
    cf_release(areas);
    set_i64(alt, "initialViewArea", 0);
}

static void add_urls(CFRef alt) {
    CFRef urls = cf_array_new(0, 0, array_cb);
    CFRef a = s_cf(URL_CLUSTER), b = s_cf(URL_MAP), c = s_cf(URL_CARD), k;
    if (urls && a && b && c) {
        cf_array_append(urls, a);
        cf_array_append(urls, b);
        cf_array_append(urls, c);
        k = s_cf("altScreenSuggestUIURLs");
        if (k) { cf_dict_set(alt, k, urls); cf_release(k); }
    }
    if (c) cf_release(c);
    if (b) cf_release(b);
    if (a) cf_release(a);
    if (urls) cf_release(urls);
}

static int already_cluster(CFRef displays) {
    long i, n = cf_array_count(displays);
    CFRef ukey = s_cf("uuid");
    for (i = 0; i < n; i++) {
        CFRef d = cf_array_get(displays, i);
        if (!is_type(d, cf_dict_type)) continue;
        if (dict_type(d) == CLUSTER_TYPE) { if (ukey) cf_release(ukey); return 1; }
        if (ukey && ascii_eq(cf_dict_get(d, ukey), CLUSTER_UUID)) {
            cf_release(ukey);
            return 1;
        }
    }
    if (ukey) cf_release(ukey);
    return 0;
}

/* Return a new info dict, or NULL to keep base. Caller releases base on success. */
static CFRef augment(CFRef base) {
    CFRef info, displays, old, alt, stock;
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
        slog("cluster display clone failed");
        return 0;
    }
    remove_key(alt, "primaryInputDevice");
    remove_key(alt, "edid");
    remove_key(alt, "platformLayer");
    remove_key(alt, "avcc");
    set_i64(alt, "type", CLUSTER_TYPE);
    set_i64(alt, "maxFPS", CLUSTER_FPS);
    set_i64(alt, "features", 0);
    set_i64(alt, "widthPixels", CLUSTER_W);
    set_i64(alt, "heightPixels", CLUSTER_H);
    set_i64(alt, "widthPhysical", CLUSTER_WMM);
    set_i64(alt, "heightPhysical", CLUSTER_HMM);
    set_str(alt, "uuid", CLUSTER_UUID);
    set_str(alt, "initialURL", URL_CLUSTER);
    add_urls(alt);
    add_views(alt);
    cf_array_append(displays, alt);
    cf_dict_set(info, k_displays, displays);
    {
        static int announced;
        if (announced < 4) {
            announced++;
            slog("enabledFeatures+display type=%d %dx%d @%d uuid=%s displays=%ld",
                 CLUSTER_TYPE, CLUSTER_W, CLUSTER_H, CLUSTER_FPS, CLUSTER_UUID,
                 cf_array_count(displays));
        }
    }
    cf_release(alt);
    cf_release(displays);
    return info;
}

static CFRef streams_of(CFRef dict) {
    CFRef s;
    if (!is_type(dict, cf_dict_type)) return 0;
    s = cf_dict_get(dict, k_streams);
    return is_type(s, cf_array_type) ? s : 0;
}

static void log_streams(const char *tag, CFRef dict) {
    CFRef s = streams_of(dict);
    char b[160];
    long i, n;
    int used;
    if (!s) { slog("%s streams=<none>", tag); return; }
    n = cf_array_count(s);
    used = snprintf(b, sizeof b, "%s streams=%ld types=", tag, n);
    for (i = 0; i < n && i < 8 && used > 0 && used < (int)sizeof b - 8; i++)
        used += snprintf(b + used, sizeof b - (size_t)used, "%s%d",
                         i ? "," : "", dict_type(cf_array_get(s, i)));
    slog("%s", b);
}

static int has_type(CFRef dict, int type) {
    CFRef s = streams_of(dict);
    long i, n;
    if (!s) return 0;
    n = cf_array_count(s);
    for (i = 0; i < n; i++)
        if (dict_type(cf_array_get(s, i)) == type) return 1;
    return 0;
}

/* Mutable copy of request whose streams array has no `type` entry. */
static CFRef without_type(CFRef request, int type) {
    CFRef copy, old, neu;
    long i, n, kept = 0;
    if (!is_type(request, cf_dict_type)) return 0;
    old = streams_of(request);
    if (!old) return 0;
    copy = cf_dict_copy(0, 0, request);
    neu = cf_array_new(0, 0, array_cb);
    if (!copy || !neu) { if (copy) cf_release(copy); if (neu) cf_release(neu); return 0; }
    n = cf_array_count(old);
    for (i = 0; i < n; i++) {
        CFRef sd = cf_array_get(old, i);
        if (dict_type(sd) == type) continue;
        cf_array_append(neu, sd);
        kept++;
    }
    cf_dict_set(copy, k_streams, neu);
    cf_release(neu);
    slog("setup retry keeps %ld of %ld streams", kept, n);
    return copy;
}

CFRef AirPlayCopyServerInfo(void *session, void *properties, void *mac, int *err) {
    CFRef base, edited;
    if (!real_info) return 0;
    if (in_info) return real_info(session, properties, mac, err);
    in_info = 1;
    base = real_info(session, properties, mac, err);
    in_info = 0;
    if (!base || disabled() || !cf_bind()) return base;
    edited = augment(base);
    if (!edited) return base;
    cf_release(base);
    return edited;
}

int AirPlayReceiverSessionSetup(void *session, void *request, void **response) {
    int e;
    if (!real_setup) return -1;
    if (in_setup) return real_setup(session, request, response);
    in_setup = 1;
    if (cf_bind() && !disabled()) log_streams("setup request", request);
    e = real_setup(session, request, response);
    if (!disabled() && cf_bind() && e != 0 && has_type(request, CLUSTER_TYPE)) {
        CFRef stripped = without_type(request, CLUSTER_TYPE);
        slog("stock setup rejected a type-%d request rc=%d", CLUSTER_TYPE, e);
        if (response && *response) { cf_release(*response); *response = 0; }
        if (stripped) {
            e = real_setup(session, stripped, response);
            cf_release(stripped);
            slog("stock setup retry without %d rc=%d", CLUSTER_TYPE, e);
        }
    } else if (!disabled() && cf_bind() && e == 0 && response && *response) {
        log_streams("setup response", *response);
    }
    in_setup = 0;
    return e;
}

int AirPlayReceiverSessionScreen_Setup(void *screen, void *desc) {
    static void *seen[4];
    static int n;
    int i;
    if (!real_screen_setup) return -1;
    for (i = 0; i < n; i++) if (seen[i] == screen) break;
    if (i == n && n < 4) {
        seen[n++] = screen;
        slog("screen setup #%d screen=%p desc=%p", n, screen, desc);
    }
    return real_screen_setup(screen, desc);
}

int AirPlayReceiverSessionScreen_ProcessFrames(void *screen, void *a, void *b, void *c) {
    static void *seen[4];
    static unsigned long count[4];
    int i;
    if (!real_frames) return -1;
    for (i = 0; i < 4; i++) {
        if (seen[i] == screen || !seen[i]) break;
    }
    if (i < 4) {
        if (!seen[i]) { seen[i] = screen; slog("frames begin screen[%d]=%p", i, screen); }
        count[i]++;
        if (count[i] == 1 || count[i] % 300 == 0)
            slog("frames screen[%d]=%p n=%lu", i, screen, count[i]);
    }
    return real_frames(screen, a, b, c);
}

__attribute__((constructor)) static void init(void) {
    real_info = (void *)dlsym(RTLD_NEXT, "AirPlayCopyServerInfo");
    real_setup = (void *)dlsym(RTLD_NEXT, "AirPlayReceiverSessionSetup");
    real_screen_setup = (void *)dlsym(RTLD_NEXT, "AirPlayReceiverSessionScreen_Setup");
    real_frames = (void *)dlsym(RTLD_NEXT, "AirPlayReceiverSessionScreen_ProcessFrames");
    slog("LOADED pid=%d info=%p setup=%p screen_setup=%p frames=%p off=%d",
         (int)getpid(), real_info, real_setup, real_screen_setup, real_frames, disabled());
}
