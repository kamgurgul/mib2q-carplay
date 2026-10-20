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
 *
 * Not the working cockpit image. Injecting type 111 into CPAAProxyEx drops
 * the session. Flash ./work/tools/build_dual.sh optA instead.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* ---- dual-send: mirror the head-unit video stream to BOTH screens --------
 * The dongle sets up two head-unit video sockets (main 110 + cluster 111; the
 * CFArrayEnsureCreatedAndAppend inject + the CPAAProxyEx NOP patch) but only
 * writes frames to one of them, so the other screen is starved and the car
 * tears the session down. Both streams share the same crypto keys
 * (SetupProxyScreenSecurityInfo uses one global), so the exact ciphertext the
 * dongle writes to one socket is valid for the other if sent in lockstep.
 * We capture the two video fds via SocketConnect(fd,addr,timeout==10) (video
 * uses timeout 10) and, in write(), mirror the accepted bytes to the partner
 * fd. The mirror never blocks the caller and never sends bytes the primary
 * write did not accept. close() forgets the fd so a reused number is not mirrored. */
static void ilog(const char *fmt, ...);   /* defined below */
static ssize_t (*real_write)(int, const void*, size_t);
static int     (*real_close)(int);
static int     (*real_SocketConnect)(int, void*, int);
static volatile int vfd[2] = {-1,-1};   /* the two head-unit video sockets */
static __thread int in_mirror;           /* re-entrancy guard */
#define MIRROR_QCAP (256u * 1024u)
static uint8_t *qbuf[2];
static size_t qlen[2];

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
            free(qbuf[0]);
            qbuf[0] = qbuf[1]; qlen[0] = qlen[1];
            qbuf[1] = NULL; qlen[1] = 0;
            vfd[0] = vfd[1]; vfd[1] = fd;        /* keep the two most recent */
            ilog("video socket fd=%d registered (pair %d,%d)", fd, vfd[0], vfd[1]);
        }
    }
    return rc;
}

static void q_clear(int slot){
    free(qbuf[slot]);
    qbuf[slot] = NULL;
    qlen[slot] = 0;
}

static void drop_vfd(int fd){
    if (vfd[0] == fd){ vfd[0] = -1; q_clear(0); }
    if (vfd[1] == fd){ vfd[1] = -1; q_clear(1); }
}

static int vslot(int fd){
    if (fd >= 0 && fd == vfd[0]) return 0;
    if (fd >= 0 && fd == vfd[1]) return 1;
    return -1;
}

/* Nonblocking. *hard is set on a real socket error. EAGAIN returns the
 * short count and leaves the unsent tail with the caller. */
static ssize_t send_nb(int fd, const void *buf, size_t n, int *hard){
    int flags = fcntl(fd, F_GETFL, 0);
    int restore = 0;
    size_t off = 0;
    *hard = 0;
    if (flags >= 0 && !(flags & O_NONBLOCK))
        restore = fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
    while (off < n){
        ssize_t w = send(fd, (const char*)buf + off, n - off, MSG_NOSIGNAL);
        if (w > 0){ off += (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        if (!(w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) *hard = errno ? errno : EIO;
        break;
    }
    if (restore) fcntl(fd, F_SETFL, flags);
    return (ssize_t)off;
}

static int q_append(int slot, const void *buf, size_t n){
    uint8_t *p;
    if (n > MIRROR_QCAP || qlen[slot] > MIRROR_QCAP - n) return -1;
    p = realloc(qbuf[slot], qlen[slot] + n);
    if (!p) return -1;
    memcpy(p + qlen[slot], buf, n);
    qbuf[slot] = p;
    qlen[slot] += n;
    return 0;
}

/* Partner send of exactly the accepted bytes. A stuck cluster is queued up
 * to MIRROR_QCAP and then dropped, so the main-screen write is never blocked
 * and a short primary write is not copied twice. */
static void mirror_once(int fd, const void *buf, size_t n){
    int slot = vslot(fd);
    int hard = 0;
    ssize_t sent;
    if (slot < 0) return;
    if (qlen[slot]){
        sent = send_nb(fd, qbuf[slot], qlen[slot], &hard);
        if (hard){
            ilog("mirror dropped fd=%d errno=%d", fd, hard);
            drop_vfd(fd);
            return;
        }
        if ((size_t)sent < qlen[slot]){
            memmove(qbuf[slot], qbuf[slot] + sent, qlen[slot] - (size_t)sent);
            qlen[slot] -= (size_t)sent;
        } else {
            qlen[slot] = 0;
        }
    }
    if (vslot(fd) < 0) return;
    if (qlen[slot]){
        if (q_append(slot, buf, n) != 0){
            ilog("mirror queue full fd=%d", fd);
            drop_vfd(fd);
        }
        return;
    }
    sent = send_nb(fd, buf, n, &hard);
    if (hard){
        ilog("mirror dropped fd=%d errno=%d", fd, hard);
        drop_vfd(fd);
        return;
    }
    if ((size_t)sent < n && q_append(slot, (const char*)buf + sent, n - (size_t)sent) != 0){
        ilog("mirror queue full fd=%d", fd);
        drop_vfd(fd);
    }
}

ssize_t write(int fd, const void *buf, size_t n){
    if (!real_write) real_write=(void*)dlsym(RTLD_NEXT,"write");
    ssize_t rc = real_write(fd, buf, n);
    if (!in_mirror && rc > 0 && (fd==vfd[0] || fd==vfd[1])){
        int other = (fd==vfd[0]) ? vfd[1] : vfd[0];
        if (other >= 0 && other != fd){
            in_mirror = 1;                 /* our mirror writes must not re-mirror */
            mirror_once(other, buf, (size_t)rc);
            in_mirror = 0;
        }
    }
    return rc;
}

int close(int fd){
    if (!real_close) real_close=(void*)dlsym(RTLD_NEXT,"close");
    drop_vfd(fd);                          /* a reused fd must not inherit the mirror */
    if (!real_close){ errno = ENOSYS; return -1; }
    return real_close(fd);
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
    real_close        =(void*)dlsym(RTLD_NEXT,"close");
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
