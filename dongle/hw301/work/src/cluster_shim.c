/*
 * cluster_shim.c — HW301 dongle, Virtual Cockpit experiment, STEP 1.
 *
 * LD_PRELOAD shim that interposes libAirPlay's
 *   AirPlayInfoArrayAddScreenDisplay(holder, uuid, features, primaryInputDevice,
 *       maxFPS, widthPixels, heightPixels, widthPhysical, heightPhysical)
 * which CPAAProxyEx calls (via PLT) to advertise its single CarPlay display to
 * the phone. We forward the original (main) display unchanged, then append a
 * SECOND display describing the Virtual Cockpit cluster, so the iPhone offers a
 * second video stream for it.
 *
 * Signature recovered by decoding the dict-key CFStrings libAirPlay sets (order:
 * uuid, features, primaryInputDevice, maxFPS, widthPixels, heightPixels,
 * widthPhysical, heightPhysical) and confirmed at the CPAAProxyEx call site
 * (v1.29 @0x5afa4; libAirPlay.so is byte-identical in v1.26 and v1.29). r0 holder,
 * r1 uuid, r2 features, r3 primaryInputDevice, then 5 stack args
 * maxFPS,widthPixels,heightPixels,widthPhysical,heightPhysical. uintptr args are
 * passed opaque so we never depend on exact CF types.
 *
 * STEP 1 only proves the phone will advertise/open the second stream. Relaying
 * that stream to the car (type 111) is a later step. Safe: pure userspace
 * interposition, no binary/partition changes, removed by dropping the preload.
 *
 * Build:  ./work/tools/xcc.sh gcc -shared -fPIC -Os -o work/out/cluster_shim.so \
 *             work/src/cluster_shim.c -ldl
 * Tunables via env: CLUSTER_W, CLUSTER_H (default 1440x540), CLUSTER_FPS (30),
 *   CLUSTER_SHIM=0 disables the second display, CLUSTER_REUSE_UUID=1 reuses the
 *   main uuid, CLUSTER_LOG=<path> changes the log file. Logs every call to
 *   /mnt/UDISK/logs/app.log (the file `getlogs.py` retrieves) since stderr is
 *   not captured on-device.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

/* CPAAProxyEx's stderr is NOT captured, so log through EVERY channel getlogs can
 * reach, so we cannot miss it:
 *   - /dev/kmsg  -> shows in dmesg -> getlogs' dmesg.log  (independent of the app
 *                   logger; most reliable)
 *   - the app log file (CLUSTER_LOG or /mnt/UDISK/logs/app.log) -> getlogs app.log
 *   - stderr (in case it is ever kept)
 * All writes are best-effort; failures are ignored. */
#define CLUSTER_LOG_DEFAULT "/mnt/UDISK/logs/app.log"
static void shimlog(const char *fmt, ...) {
    char buf[256]; va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);

    FILE *k = fopen("/dev/kmsg", "w");
    if (k) { fprintf(k, "cluster_shim: %s\n", buf); fclose(k); }

    const char *p = getenv("CLUSTER_LOG"); if (!p || !*p) p = CLUSTER_LOG_DEFAULT;
    FILE *f = fopen(p, "a");
    if (f) { fprintf(f, "[cluster_shim] %s\n", buf); fclose(f); }

    fprintf(stderr, "[cluster_shim] %s\n", buf);
}

typedef int (*add_screen_fn)(void *holder, void *uuid, uintptr_t features,
    uintptr_t primaryInputDevice, uintptr_t maxFPS, uintptr_t widthPixels,
    uintptr_t heightPixels, uintptr_t widthPhysical, uintptr_t heightPhysical);

/* Fires the instant the library is loaded, before any interposed call. If this
 * does NOT appear in dmesg.log/app.log after a flash, the shim is not being
 * loaded at all (injection problem) — as opposed to the interposed function just
 * never being called. */
__attribute__((constructor))
static void shim_loaded(void) {
    void *real = dlsym(RTLD_NEXT, "AirPlayInfoArrayAddScreenDisplay");
    shimlog("LOADED pid=%d real_AirPlayInfoArrayAddScreenDisplay=%p", (int)getpid(), real);
}

static add_screen_fn real_add(void) {
    static add_screen_fn f;
    if (!f) f = (add_screen_fn)dlsym(RTLD_NEXT, "AirPlayInfoArrayAddScreenDisplay");
    return f;
}

static long envl(const char *k, long dflt) {
    const char *v = getenv(k); return v && *v ? strtol(v, 0, 0) : dflt;
}

int AirPlayInfoArrayAddScreenDisplay(void *holder, void *uuid, uintptr_t features,
    uintptr_t primaryInputDevice, uintptr_t maxFPS, uintptr_t widthPixels,
    uintptr_t heightPixels, uintptr_t widthPhysical, uintptr_t heightPhysical)
{
    add_screen_fn real = real_add();
    if (!real) { /* should never happen; fail open */ return -1; }

    shimlog("ENTER holder=%p uuid=%p features=0x%lx prim=%lu fps=%lu w=%lu h=%lu wPhys=%lu hPhys=%lu",
         holder, uuid, (unsigned long)features, (unsigned long)primaryInputDevice,
         (unsigned long)maxFPS, (unsigned long)widthPixels, (unsigned long)heightPixels,
         (unsigned long)widthPhysical, (unsigned long)heightPhysical);

    /* 1) main display, exactly as CPAAProxyEx intended */
    int rc = real(holder, uuid, features, primaryInputDevice, maxFPS,
                  widthPixels, heightPixels, widthPhysical, heightPhysical);
    shimlog("main display w=%lu h=%lu fps=%lu rc=%d",
         (unsigned long)widthPixels, (unsigned long)heightPixels,
         (unsigned long)maxFPS, rc);

    if (!envl("CLUSTER_SHIM", 1)) { shimlog("CLUSTER_SHIM=0, not adding 2nd display"); return rc; }

    /* 2) second display = Virtual Cockpit cluster, same holder (appended).
     * uuid=NULL: if the SDK rejects a NULL uuid the cluster add simply fails and
     * is swallowed below, leaving the main display (rc) intact. CLUSTER_UUID can
     * pass the main uuid object again if a distinct one is not required. */
    uintptr_t cw = (uintptr_t)envl("CLUSTER_W", 1440);
    uintptr_t ch = (uintptr_t)envl("CLUSTER_H", 540);
    uintptr_t cf = (uintptr_t)envl("CLUSTER_FPS", 30);
    void *cuuid = envl("CLUSTER_REUSE_UUID", 0) ? uuid : NULL;
    int rc2 = real(holder, cuuid, features, primaryInputDevice,
                   cf, cw, ch, widthPhysical, heightPhysical);
    shimlog("CLUSTER display w=%lu h=%lu fps=%lu uuid=%p rc=%d",
         (unsigned long)cw, (unsigned long)ch, (unsigned long)cf, cuuid, rc2);
    return rc;  /* report main display's status to the caller */
}
