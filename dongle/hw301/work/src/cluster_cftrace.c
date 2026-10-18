/*
 * cluster_cftrace.c - HW301 shim (DISCOVERY). Wraps libCoreUtils to find where
 * CPAAProxyEx builds the display dict. For each CFDictionarySetInt64 we log the
 * value and the CALLER's offset inside its module (via dladdr) - when val is a
 * display dimension (1024/480, or 1440/540 if the head unit offers a cluster)
 * the caller offset pins the builder function in CPAAProxyEx: the binary-patch
 * target for injecting a second display. Also traces CFArrayAppendValue callers.
 * All wrappers forward to the real libCoreUtilsReal.so.
 *
 * Build: WRAP_LIB=libCoreUtils.so CHECK_SYM=CFDictionarySetInt64 \
 *        CLUSTER_SRC=cluster_cftrace.c ./work/tools/build.sh 129
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>

static void tlog(const char *fmt, ...) {
    char b[220]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    FILE *k = fopen("/dev/kmsg", "w"); if (k) { fprintf(k, "cftrace: %s\n", b); fclose(k); }
    FILE *f = fopen("/mnt/UDISK/logs/app.log", "a"); if (f) { fprintf(f, "[cftrace] %s\n", b); fclose(f); }
}

/* describe a code address as module+offset (offset matches the static binary) */
static void caller_desc(void *ret, char *out, int n) {
    Dl_info di;
    if (dladdr(ret, &di) && di.dli_fbase) {
        const char *base = di.dli_fname ? di.dli_fname : "?";
        const char *slash = base; for (const char *p = base; *p; p++) if (*p == '/') slash = p + 1;
        snprintf(out, n, "%s+0x%lx", slash, (unsigned long)((char*)ret - (char*)di.dli_fbase));
    } else snprintf(out, n, "ret=%p", ret);
}

typedef void (*setint64_fn)(void*, void*, int64_t);
typedef void (*append_fn)(void*, const void*);

void CFDictionarySetInt64(void *dict, void *key, int64_t val) {
    static setint64_fn real; if (!real) real = (setint64_fn)dlsym(RTLD_NEXT, "CFDictionarySetInt64");
    static int n;
    void *ret = __builtin_return_address(0);
    /* log dimension-like values always; cap the rest so we don't flood */
    int dim = (val==1024||val==480||val==1440||val==540||val==800||val==1280||val==720||val==600||val==1920||val==1080);
    if (dim || n++ < 80) {
        char c[96]; caller_desc(ret, c, sizeof c);
        tlog("SetInt64 val=%lld %s%s", (long long)val, c, dim ? "  <-- DIMENSION" : "");
    }
    real(dict, key, val);
}

void CFArrayAppendValue(void *arr, const void *val) {
    static append_fn real; if (!real) real = (append_fn)dlsym(RTLD_NEXT, "CFArrayAppendValue");
    static int n;
    if (n++ < 60) { char c[96]; caller_desc(__builtin_return_address(0), c, sizeof c);
                    tlog("ArrayAppend arr=%p val=%p %s", arr, val, c); }
    real(arr, val);
}

__attribute__((constructor)) static void init(void) {
    tlog("LOADED pid=%d (libCoreUtils wrapper)", (int)getpid());
}
