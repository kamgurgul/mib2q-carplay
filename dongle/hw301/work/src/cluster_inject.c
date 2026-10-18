/*
 * cluster_inject.c - HW301 shim, PHASE 1: advertise a 2nd (cluster) CarPlay
 * stream. Wraps libCoreUtils. CPAAProxyEx's SetupScreen builder does:
 *   CFArrayEnsureCreatedAndAppend(&streamsArray, streamDict)   (stream type 110)
 *   CFDictionarySetValue(params, "streams", streamsArray)
 * We interpose the LESS-hot CFArrayEnsureCreatedAndAppend: after the real append,
 * if the value is a stream dict (has "streamConnectionID"), we clone it, set
 * type=111 (cluster), and append the clone to the same array -> the phone is
 * offered a 2nd video stream. Detection uses only CFDictionaryGetValue (safe,
 * 2-arg). Everything else forwards unchanged.
 *
 * Build: WRAP_LIB=libCoreUtils.so CHECK_SYM=CFArrayEnsureCreatedAndAppend \
 *        CLUSTER_SRC=cluster_inject.c ./work/tools/build.sh 129
 * Toggle off: CLUSTER_INJECT=0.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

/* ---- dual-send: mirror the head-unit video stream to BOTH screens --------
 * The dongle sets up two head-unit video sockets (main 110 + cluster 111; the
 * CFArrayEnsureCreatedAndAppend inject + the CPAAProxyEx NOP patch) but only
 * writes frames to one of them, so the other screen is starved and the car
 * tears the session down. Both streams share the same crypto keys
 * (SetupProxyScreenSecurityInfo uses one global), so the exact ciphertext the
 * dongle writes to one socket is valid for the other if sent in lockstep.
 * We capture the two video fds via SocketConnect(fd,addr,timeout==10) (video
 * uses timeout 10) and, in write(), mirror every frame to the partner fd. */
static void ilog(const char *fmt, ...);   /* defined below */
static ssize_t (*real_write)(int, const void*, size_t);
static int     (*real_SocketConnect)(int, void*, int);
static volatile int vfd[2] = {-1,-1};   /* the two head-unit video sockets */
static __thread int in_mirror;           /* re-entrancy guard */

/* the video SocketConnect call site in CPAAProxyEx (bl @0x5cac0 -> ret 0x5cac4).
 * SocketConnect is also used for the control socket (@0x37d14); keying on the
 * caller offset registers ONLY the two video sockets. */
/* return address (module offset) of the video-stream SocketConnect call site
 * (bl @0x5cac0 in CPAAProxyEx; dladdr fbase is the 0x10000 link base, so the
 * reported offset is 0x4cac4). The control socket uses a different call site. */
#define VIDEO_CONNECT_OFF 0x4cac4

int SocketConnect(int fd, void *addr, int timeout){
    if (!real_SocketConnect) real_SocketConnect=(void*)dlsym(RTLD_NEXT,"SocketConnect");
    int rc = real_SocketConnect(fd, addr, timeout);
    if (rc == 0){
        void *ret = __builtin_return_address(0);
        Dl_info di; unsigned long off = ~0ul;
        if (dladdr(ret,&di) && di.dli_fbase) off = (unsigned long)((char*)ret - (char*)di.dli_fbase);
        if (off == VIDEO_CONNECT_OFF){           /* a head-unit video socket */
            vfd[0] = vfd[1]; vfd[1] = fd;        /* keep the two most recent */
            ilog("video socket fd=%d registered (pair %d,%d)", fd, vfd[0], vfd[1]);
        }
    }
    return rc;
}

static void full_write(int fd, const char *buf, size_t n){
    size_t off = 0;
    while (off < n){
        ssize_t w = real_write(fd, buf+off, n-off);
        if (w <= 0) break;                 /* give up on error; harmless */
        off += (size_t)w;
    }
}

ssize_t write(int fd, const void *buf, size_t n){
    if (!real_write) real_write=(void*)dlsym(RTLD_NEXT,"write");
    ssize_t rc = real_write(fd, buf, n);
    if (!in_mirror && n > 0 && (fd==vfd[0] || fd==vfd[1])){
        int other = (fd==vfd[0]) ? vfd[1] : vfd[0];
        if (other >= 0 && other != fd){
            in_mirror = 1;                 /* our mirror writes must not re-mirror */
            full_write(other, (const char*)buf, (size_t)n);
            in_mirror = 0;
        }
    }
    return rc;
}
/* -------------------------------------------------------------------------- */

#define UTF8 0x08000100
#define STREAM_TYPE_CLUSTER 111

typedef void* CFRef;
typedef void (*ensure_fn)(void** ioArray, const void* value);

static ensure_fn p_real_ensure;
static CFRef (*p_DictGetValue)(CFRef, CFRef);
static CFRef (*p_DictCopy)(CFRef, long, CFRef);
static void  (*p_DictSetInt64)(CFRef, CFRef, int64_t);
static CFRef (*p_StrNew)(CFRef, const char*, uint32_t);
static void  (*p_Release)(CFRef);
static CFRef k_type, k_conn;
static int   active;

static void ilog(const char *fmt, ...) {
    char b[200]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    FILE *k = fopen("/dev/kmsg","w"); if (k){ fprintf(k,"cluster_inject: %s\n",b); fclose(k);}
    FILE *f = fopen("/mnt/UDISK/logs/app.log","a"); if (f){ fprintf(f,"[cluster_inject] %s\n",b); fclose(f);}
}

__attribute__((constructor)) static void init(void){
    p_real_ensure=(ensure_fn)dlsym(RTLD_NEXT,"CFArrayEnsureCreatedAndAppend");
    p_DictGetValue=(void*)dlsym(RTLD_NEXT,"CFDictionaryGetValue");
    p_DictCopy    =(void*)dlsym(RTLD_NEXT,"CFDictionaryCreateMutableCopy");
    p_DictSetInt64=(void*)dlsym(RTLD_NEXT,"CFDictionarySetInt64");
    p_StrNew      =(void*)dlsym(RTLD_NEXT,"CFStringCreateWithCString");
    p_Release     =(void*)dlsym(RTLD_NEXT,"CFRelease");
    real_write        =(void*)dlsym(RTLD_NEXT,"write");
    real_SocketConnect=(void*)dlsym(RTLD_NEXT,"SocketConnect");
    if (p_StrNew){ k_type=p_StrNew(0,"type",UTF8); k_conn=p_StrNew(0,"streamConnectionID",UTF8); }
    ilog("LOADED pid=%d ensure=%p getval=%p write=%p SocketConnect=%p", (int)getpid(),
         p_real_ensure, p_DictGetValue, real_write, real_SocketConnect);
}

void CFArrayEnsureCreatedAndAppend(void **ioArray, const void *value){
    if (!p_real_ensure){ return; }           /* should never happen */
    p_real_ensure(ioArray, value);           /* 1) normal append (main stream) */

    const char *off=getenv("CLUSTER_INJECT");
    if (off && off[0]=='0') return;
    if (active) return;                       /* re-entrancy guard */
    if (!value || !ioArray || !*ioArray) return;
    if (!p_DictGetValue || !p_DictCopy || !p_DictSetInt64 || !k_conn || !k_type) return;

    /* only act on a stream dict: it has a "streamConnectionID" key */
    if (!p_DictGetValue((CFRef)value, k_conn)) return;

    active=1;
    CFRef cluster=p_DictCopy(0,0,(CFRef)value);
    if (cluster){
        p_DictSetInt64(cluster,k_type,STREAM_TYPE_CLUSTER);
        p_real_ensure(ioArray, cluster);      /* 2) append cluster to same array */
        if (p_Release) p_Release(cluster);
        ilog("appended cluster stream (type=%d) alongside main", STREAM_TYPE_CLUSTER);
    }
    active=0;
}
