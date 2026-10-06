/*
 * Host test for aa_hook/aa_tee.c over real loopback sockets: nothing before the first
 * keyframe, SPS/PPS prepended to it, live delivery, a client joining mid-stream starts on
 * the cached keyframe, a newer client replaces the old one, reset closes the client, and an
 * overflowed cache makes a new client wait for the next keyframe.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "aa_hook.h"

#undef NDEBUG
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

void aa_log(const char *fmt, ...) { (void)fmt; }
int aa_marker(const char *path) { (void)path; return 0; }

static int port;

static int client(void)
{
    struct sockaddr_in sa;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(connect(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0);
    usleep(300000);   /* the writer accepts within its 200 ms poll */
    return fd;
}

/* Read exactly n bytes within ms; returns bytes read (n on success, 0 on EOF). */
static size_t read_n(int fd, unsigned char *buf, size_t n, int ms)
{
    size_t got = 0;
    while (got < n) {
        struct pollfd p = { fd, POLLIN, 0 };
        ssize_t r;
        if (poll(&p, 1, ms) <= 0) break;
        r = recv(fd, buf + got, n - got, 0);
        if (r <= 0) break;
        got += (size_t)r;
    }
    return got;
}

static int nothing_for(int fd, int ms)
{
    struct pollfd p = { fd, POLLIN, 0 };
    return poll(&p, 1, ms) == 0;
}

static int closed_within(int fd, int ms)
{
    unsigned char b;
    struct pollfd p = { fd, POLLIN, 0 };
    if (poll(&p, 1, ms) <= 0) return 0;
    return recv(fd, &b, 1, 0) == 0;
}

static const unsigned char SPS_PPS[] = { 0, 0, 0, 1, 0x67, 1, 2, 3, 0, 0, 0, 1, 0x68, 4, 5 };
static const unsigned char IDR[] = { 0, 0, 0, 1, 0x65, 0xaa, 0xbb, 0xcc };
static const unsigned char P1[] = { 0, 0, 0, 1, 0x41, 0x11 };
static const unsigned char P2[] = { 0, 0, 0, 1, 0x41, 0x22 };
static const unsigned char P3[] = { 0, 0, 0, 1, 0x41, 0x33 };

static void expect(int fd, const unsigned char *a, size_t an, const unsigned char *b, size_t bn,
                   const char *what)
{
    unsigned char buf[64];
    assert(an + bn <= sizeof(buf));
    if (read_n(fd, buf, an + bn, 1500) != an + bn ||
        memcmp(buf, a, an) != 0 || (bn && memcmp(buf + an, b, bn) != 0)) {
        printf("FAIL: %s\n", what);
        exit(1);
    }
}

int main(void)
{
    int a, b, c, i;
    static unsigned char big[300 * 1024];

    for (i = 0; i < 50; i++) {
        port = 41000 + (int)(getpid() % 9000) + i;
        if (aa_tee_start(port) == 0) break;
    }
    assert(i < 50);

    /* No keyframe yet: a P frame (and the codec message alone) send nothing. */
    a = client();
    aa_tee_codec(SPS_PPS, sizeof(SPS_PPS));
    aa_tee_frame(P1, sizeof(P1));
    assert(nothing_for(a, 300));

    /* Keyframe goes out with SPS/PPS in front, then live P frames. */
    aa_tee_frame(IDR, sizeof(IDR));
    expect(a, SPS_PPS, sizeof(SPS_PPS), IDR, sizeof(IDR), "keyframe with SPS/PPS");
    aa_tee_frame(P1, sizeof(P1));
    expect(a, P1, sizeof(P1), NULL, 0, "live P frame");

    /* A newer client replaces the old one and starts on the cached keyframe. */
    b = client();
    assert(closed_within(a, 1500));
    close(a);
    expect(b, SPS_PPS, sizeof(SPS_PPS), IDR, sizeof(IDR), "join: cached keyframe");
    expect(b, P1, sizeof(P1), NULL, 0, "join: cached P frame");
    aa_tee_frame(P2, sizeof(P2));
    expect(b, P2, sizeof(P2), NULL, 0, "join: then live");

    /* Reset (new stream) closes the client; the renderer reconnects for a fresh decoder. */
    aa_tee_reset("test");
    assert(closed_within(b, 1500));
    close(b);

    /* After reset: codec + keyframe again, then overflow the 8 MB cache with P frames. */
    aa_tee_codec(SPS_PPS, sizeof(SPS_PPS));
    aa_tee_frame(IDR, sizeof(IDR));
    memset(big, 0x5a, sizeof(big));
    memcpy(big, P1, sizeof(P1));
    for (i = 0; i < 40; i++) aa_tee_frame(big, sizeof(big));   /* 12 MB > budget */
    c = client();
    assert(nothing_for(c, 400));        /* the keyframe was dropped: wait for the next one */
    aa_tee_frame(IDR, sizeof(IDR));
    expect(c, SPS_PPS, sizeof(SPS_PPS), IDR, sizeof(IDR), "after overflow: next keyframe");
    aa_tee_frame(P3, sizeof(P3));
    expect(c, P3, sizeof(P3), NULL, 0, "after overflow: live");
    close(c);

    /* NAL scan used by the producer. */
    assert(aa_nal_types(IDR, sizeof(IDR)) == (1u << 5));
    assert(aa_nal_types(SPS_PPS, sizeof(SPS_PPS)) == ((1u << 7) | (1u << 8)));

    printf("aa_tee_test: keyframe gating, SPS/PPS prefix, live, mid-stream join, client replace, "
           "reset, overflow PASS\n");
    return 0;
}
