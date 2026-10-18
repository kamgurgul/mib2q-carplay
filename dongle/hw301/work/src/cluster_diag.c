/*
 * cluster_diag.c - DIAGNOSTIC HW301 shim (wraps libCoreUtils). Does NOT change
 * behaviour; it only logs UNCONDITIONALLY to answer the open question: do the
 * libCoreUtils interposes actually fire for CPAAProxyEx's internal calls, and at
 * what caller offsets? Build exactly like cluster_inject.c:
 *   WRAP_LIB=libCoreUtils.so CHECK_SYM=CFArrayEnsureCreatedAndAppend \
 *   CLUSTER_SRC=cluster_diag.c ./work/tools/build.sh 129   (NO CPAA gate patch)
 *
 * What it logs (prefix [cluster_diag] in /mnt/UDISK/logs/app.log + /dev/kmsg):
 *   - constructor: LOADED + resolved real fn pointers.
 *   - EVERY SocketConnect: fd, rc, caller module-offset (hex) -> tells us the
 *     REAL video-socket call site offset (expected ~0x4cac4) and proves the
 *     interpose fires for CPAAProxyEx at all.
 *   - EVERY CFArrayEnsureCreatedAndAppend: whether the value is a stream dict
 *     (has streamConnectionID) and the running array count -> proves whether the
 *     stream-inject guard would ever match.
 * No frame mirroring, no stream injection: pure observation, low risk.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

#define UTF8 0x08000100
typedef void* CFRef;
typedef void (*ensure_fn)(void** ioArray, const void* value);

static ensure_fn p_real_ensure;
static CFRef (*p_DictGetValue)(CFRef, CFRef);
static long  (*p_ArrayGetCount)(CFRef);
static CFRef (*p_StrNew)(CFRef, const char*, uint32_t);
static int   (*real_SocketConnect)(int, void*, int);
static CFRef k_conn;
static int sc_calls, ens_calls;

static void dlog(const char *fmt, ...) {
    char b[240]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    FILE *k = fopen("/dev/kmsg","w"); if (k){ fprintf(k,"cluster_diag: %s\n",b); fclose(k);}
    FILE *f = fopen("/mnt/UDISK/logs/app.log","a"); if (f){ fprintf(f,"[cluster_diag] %s\n",b); fclose(f);}
}

int SocketConnect(int fd, void *addr, int timeout){
    if (!real_SocketConnect) real_SocketConnect=(void*)dlsym(RTLD_NEXT,"SocketConnect");
    int rc = real_SocketConnect(fd, addr, timeout);
    void *ret = __builtin_return_address(0);
    Dl_info di; unsigned long off = ~0ul; const char *mod = "?";
    if (dladdr(ret,&di) && di.dli_fbase){
        off = (unsigned long)((char*)ret - (char*)di.dli_fbase);
        if (di.dli_fname) mod = di.dli_fname;
    }
    dlog("SocketConnect #%d fd=%d timeout=%d rc=%d ret=%p off=0x%lx mod=%s",
         ++sc_calls, fd, timeout, rc, ret, off, mod);
    return rc;
}

void CFArrayEnsureCreatedAndAppend(void **ioArray, const void *value){
    if (!p_real_ensure) p_real_ensure=(ensure_fn)dlsym(RTLD_NEXT,"CFArrayEnsureCreatedAndAppend");
    p_real_ensure(ioArray, value);
    int is_stream = 0; long cnt = -1;
    if (p_DictGetValue && k_conn && value) is_stream = (p_DictGetValue((CFRef)value,k_conn) != 0);
    if (p_ArrayGetCount && ioArray && *ioArray) cnt = p_ArrayGetCount(*ioArray);
    dlog("CFArrayEnsureCreatedAndAppend #%d value=%p stream_dict=%d arr_count=%ld",
         ++ens_calls, value, is_stream, cnt);
}

__attribute__((constructor)) static void init(void){
    p_real_ensure =(ensure_fn)dlsym(RTLD_NEXT,"CFArrayEnsureCreatedAndAppend");
    p_DictGetValue=(void*)dlsym(RTLD_NEXT,"CFDictionaryGetValue");
    p_ArrayGetCount=(void*)dlsym(RTLD_NEXT,"CFArrayGetCount");
    p_StrNew      =(void*)dlsym(RTLD_NEXT,"CFStringCreateWithCString");
    real_SocketConnect=(void*)dlsym(RTLD_NEXT,"SocketConnect");
    if (p_StrNew) k_conn=p_StrNew(0,"streamConnectionID",UTF8);
    dlog("LOADED pid=%d ensure=%p getval=%p arrcount=%p SocketConnect=%p k_conn=%p",
         (int)getpid(), p_real_ensure, p_DictGetValue, p_ArrayGetCount, real_SocketConnect, k_conn);
}
