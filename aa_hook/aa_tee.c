/*
 * Loopback Annex-B tee: Android Auto cluster pictures -> altscreen_render.
 *
 * gal's receiver thread hands every access unit (AU) to aa_tee_frame(). It is
 * appended to a bounded in-memory list under one short mutex hold and the call
 * returns; it never waits for the consumer. A writer thread serves one TCP client
 * on 127.0.0.1:<port> (altscreen_render, which also reads the CarPlay tee).
 *
 * The list keeps every AU since the last keyframe (IDR, with the SPS/PPS from the
 * codec message prepended), so a renderer that connects mid-stream - or restarts -
 * starts on a keyframe at once instead of waiting for the phone's next one. When
 * the list exceeds its byte/AU budget the oldest AUs are dropped; a new client then
 * waits for the next keyframe, and a client that falls behind skips to it.
 *
 * aa_tee_reset() (new playback session, sink gone) drops the list and closes the
 * client, so the renderer reconnects with a fresh decoder for the next stream.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "aa_hook.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define TEE_AU_MAX        (2u * 1024u * 1024u)   /* one AU */
#define TEE_CACHE_BYTES   (8u * 1024u * 1024u)   /* ~8 s at the phone's 1080p rate */
#define TEE_CACHE_AUS     512u
#define TEE_CODEC_MAX     4096u

struct au {
    struct au *next;
    uint64_t seq;
    size_t size;
    int key;
    unsigned char data[1];
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t more = PTHREAD_COND_INITIALIZER;
static struct au *head, *tail;      /* oldest .. newest retained AU */
static size_t cache_bytes;
static unsigned cache_aus;
static uint64_t next_seq = 1;
static uint64_t epoch = 1;          /* bumped by aa_tee_reset: the client is closed */
static int need_idr = 1;            /* nothing is kept until the stream (re)starts on a keyframe */
static unsigned char codec[TEE_CODEC_MAX];
static size_t codec_size;
static unsigned codec_types;
static unsigned long drops, sent_aus;
static int started;

unsigned aa_nal_types(const void *data, size_t size)
{
    const unsigned char *p = data;
    size_t i;
    unsigned types = 0;
    if (!p) return 0;
    for (i = 0; i + 3 < size; ++i)
        if (!p[i] && !p[i + 1] && p[i + 2] == 1) types |= 1u << (p[i + 3] & 31);
    return types;
}

/* caller holds lock */
static void drop_oldest_locked(void)
{
    struct au *a = head;
    if (!a) return;
    head = a->next;
    if (!head) tail = NULL;
    cache_bytes -= a->size;
    cache_aus--;
    free(a);
}

/* caller holds lock */
static void clear_locked(void)
{
    while (head) drop_oldest_locked();
}

void aa_tee_codec(const void *data, size_t size)
{
    unsigned types;
    if (!data || !size) return;
    types = aa_nal_types(data, size);
    pthread_mutex_lock(&lock);
    /* Codec messages normally carry SPS and PPS together. Keep split messages too,
     * starting over when a new SPS announces a new configuration. */
    if (types & (1u << 7)) { codec_size = 0; codec_types = 0; need_idr = 1; }
    if (size <= sizeof(codec) - codec_size) {
        memcpy(codec + codec_size, data, size);
        codec_size += size;
        codec_types |= types;
    } else {
        codec_size = 0; codec_types = 0; need_idr = 1;
    }
    pthread_mutex_unlock(&lock);
}

void aa_tee_frame(const void *data, size_t size)
{
    struct au *a;
    unsigned types;
    size_t extra;
    int key;
    if (!started || !data || !size) return;
    types = aa_nal_types(data, size);
    key = (types & (1u << 5)) != 0;
    pthread_mutex_lock(&lock);
    if (!key && need_idr) goto out;
    /* A keyframe is only decodable with its SPS (7) and PPS (8). */
    if (key && ((types | codec_types) & 0x180u) != 0x180u) { need_idr = 1; goto out; }
    extra = key && !(types & 0x180u) ? codec_size : 0;
    if (size > TEE_AU_MAX - extra) { drops++; need_idr = 1; goto out; }
    a = malloc(sizeof(*a) + extra + size);
    if (!a) { drops++; need_idr = 1; goto out; }
    if (extra) memcpy(a->data, codec, extra);
    memcpy(a->data + extra, data, size);
    a->size = extra + size;
    a->key = key;
    a->seq = next_seq++;
    a->next = NULL;
    /* A keyframe starts a new self-contained group: older AUs are no longer needed. */
    if (key) clear_locked();
    if (tail) tail->next = a; else head = a;
    tail = a;
    cache_bytes += a->size;
    cache_aus++;
    while (head != tail && (cache_bytes > TEE_CACHE_BYTES || cache_aus > TEE_CACHE_AUS))
        drop_oldest_locked();
    need_idr = 0;
    pthread_cond_broadcast(&more);
out:
    pthread_mutex_unlock(&lock);
}

void aa_tee_reset(const char *reason)
{
    pthread_mutex_lock(&lock);
    clear_locked();
    codec_size = 0;
    codec_types = 0;
    need_idr = 1;
    epoch++;
    pthread_cond_broadcast(&more);
    pthread_mutex_unlock(&lock);
    aa_log("tee.reset reason=%s", reason);
}

/* ---- writer thread ---- */

static int send_all(int fd, const unsigned char *p, size_t n)
{
    while (n) {
        ssize_t w = send(fd, p, n, 0);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

/* Next AU for a client whose last sent seq is *cursor (0 = new client). Copies it into
 * *buf. Returns its size, 0 when there is nothing yet. caller holds lock. */
static size_t next_au_locked(uint64_t *cursor, unsigned char **buf, size_t *cap)
{
    struct au *a = head;
    if (!a) return 0;
    if (*cursor == 0 || *cursor + 1 < a->seq) {
        /* New, or fell behind the retained list: resume on the first kept keyframe. */
        while (a && !a->key) a = a->next;
    } else {
        while (a && a->seq <= *cursor) a = a->next;
    }
    if (!a) return 0;
    if (*cap < a->size) {
        unsigned char *n = realloc(*buf, a->size);
        if (!n) return 0;
        *buf = n;
        *cap = a->size;
    }
    memcpy(*buf, a->data, a->size);
    *cursor = a->seq;
    return a->size;
}

static int accept_client(int lfd)
{
    struct timeval tv = { 2, 0 };
    int fd = accept(lfd, NULL, NULL);
    if (fd < 0) return -1;
    /* A stuck consumer must not hold the writer forever: give up the client instead. */
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return fd;
}

static void *writer(void *arg)
{
    int lfd = (int)(intptr_t)arg, cfd = -1;
    uint64_t cursor = 0, client_epoch = 0;
    unsigned char *buf = NULL;
    size_t cap = 0;
    for (;;) {
        fd_set rd;
        struct timeval tv;
        size_t n = 0;
        struct timespec until;

        /* A newer client (renderer restart) replaces the current one. */
        FD_ZERO(&rd);
        FD_SET(lfd, &rd);
        tv.tv_sec = 0;
        tv.tv_usec = cfd < 0 ? 200000 : 0;
        if (select(lfd + 1, &rd, NULL, NULL, &tv) > 0 && FD_ISSET(lfd, &rd)) {
            int nfd = accept_client(lfd);
            if (nfd >= 0) {
                if (cfd >= 0) close(cfd);
                cfd = nfd;
                cursor = 0;
                pthread_mutex_lock(&lock);
                client_epoch = epoch;
                pthread_mutex_unlock(&lock);
                aa_log("tee.client connected");
            }
        }
        if (cfd < 0) continue;

        pthread_mutex_lock(&lock);
        if (client_epoch != epoch) {
            pthread_mutex_unlock(&lock);
            close(cfd);
            cfd = -1;
            aa_log("tee.client closed (new stream)");
            continue;
        }
        n = next_au_locked(&cursor, &buf, &cap);
        if (!n) {
            clock_gettime(CLOCK_REALTIME, &until);
            until.tv_nsec += 100000000L;
            if (until.tv_nsec >= 1000000000L) { until.tv_sec++; until.tv_nsec -= 1000000000L; }
            pthread_cond_timedwait(&more, &lock, &until);
            pthread_mutex_unlock(&lock);
            continue;
        }
        pthread_mutex_unlock(&lock);
        if (send_all(cfd, buf, n) != 0) {
            close(cfd);
            cfd = -1;
            aa_log("tee.client lost errno=%d sent=%lu drops=%lu", errno, sent_aus, drops);
            continue;
        }
        sent_aus++;
    }
    return NULL;
}

int aa_tee_start(int port)
{
    struct sockaddr_in sa;
    pthread_t t;
    int one = 1, fd;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   /* never leaves the head unit */
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0 || listen(fd, 2) != 0) {
        aa_log("tee.listen port=%d failed errno=%d", port, errno);
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    if (pthread_create(&t, NULL, writer, (void *)(intptr_t)fd) != 0) {
        close(fd);
        return -1;
    }
    pthread_detach(t);
    started = 1;
    aa_log("tee.listen 127.0.0.1:%d", port);
    return 0;
}
