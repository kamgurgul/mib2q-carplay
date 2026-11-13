/*
 * Host test for altscreen_hook/src/alt111_video.c: the CarPlay stream-111 video core. A
 * consumer that falls behind is no longer reset (1.4: 16 AUs, then the renderer had to
 * reconnect): the unsent backlog is dropped, a partly written AU is finished, pictures are
 * skipped until the next IDR on the same consumer, and ALT111_RESYNC tells the adapter to
 * ask the phone for one. A consumer still priming starts over as before.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "alt111.h"

#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* avcC: version 1, profile/compat/level, 4-byte lengths, one SPS, one PPS. */
static const uint8_t AVCC[] = {
    1, 0x64, 0, 0x1f, 0xff, 0xe1, 0, 4, 0x67, 0x64, 0, 0x1f, 1, 0, 2, 0x68, 0xee
};
static const uint8_t IDR[] = { 0, 0, 0, 3, 0x65, 0x88, 0x84 };
static const uint8_t P[] = { 0, 0, 0, 3, 0x41, 0x9a, 0x02 };

/* Deliver everything queued, in one write per AU; returns the AU count and the NAL type
 * of the first delivered picture. */
static unsigned drain(struct alt111_video *v, unsigned *first_type)
{
    unsigned n = 0;
    *first_type = 0;
    for (;;) {
        const uint8_t *p;
        size_t len, i;
        struct alt111_output_ticket t;
        if (alt111_video_peek(v, &p, &len, &t) != ALT111_OK) return n;
        for (i = 0; i + 4 < len && !*first_type; i++)
            if (!p[i] && !p[i + 1] && !p[i + 2] && p[i + 3] == 1 &&
                ((p[i + 4] & 31u) == 5 || (p[i + 4] & 31u) == 1))
                *first_type = p[i + 4] & 31u;
        assert(alt111_video_advance(v, &t, len) == ALT111_OK);
        n++;
    }
}

int main(void)
{
    struct alt111_video v;
    uint64_t s;
    unsigned i, first, n;
    int rc;

    alt111_video_init(&v);
    s = alt111_video_begin(&v, 1);
    assert(alt111_video_config(&v, s, AVCC, sizeof(AVCC)) == ALT111_OK);
    assert(alt111_video_attach(&v) == ALT111_OK);

    /* Priming: config + IDR, then live P frames. */
    assert(alt111_video_submit(&v, s, IDR, sizeof(IDR), 1) == ALT111_OK);
    assert(drain(&v, &first) == 1 && first == 5 && v.consumer_primed);

    /* Fill the queue, then one more picture: backlog dropped, RESYNC, consumer kept. */
    for (i = 0; i < ALT111_QUEUE_CAP; i++)
        assert(alt111_video_submit(&v, s, P, sizeof(P), 1) == ALT111_OK);
    {
        /* Half-write the head AU: it must survive the drop. */
        const uint8_t *p; size_t len; struct alt111_output_ticket t;
        assert(alt111_video_peek(&v, &p, &len, &t) == ALT111_OK);
        assert(alt111_video_advance(&v, &t, 2) == ALT111_OK);
    }
    rc = alt111_video_submit(&v, s, P, sizeof(P), 1);
    assert(rc == ALT111_RESYNC);
    assert(v.attached && v.consumer_primed && v.skip_to_idr);
    assert(v.count == 1 && v.queue[v.head].offset == 2);   /* only the partly written head */
    assert(v.resyncs == 1);

    /* Until the next IDR: pictures are dropped, nothing new queued. */
    assert(alt111_video_submit(&v, s, P, sizeof(P), 1) == ALT111_WAIT);
    assert(v.count == 1);
    assert(alt111_video_submit(&v, s, IDR, sizeof(IDR), 1) == ALT111_OK);
    assert(!v.skip_to_idr && v.count == 2);
    assert(alt111_video_submit(&v, s, P, sizeof(P), 1) == ALT111_OK);
    n = drain(&v, &first);
    assert(n == 3);                     /* rest of the head P, the IDR, a live P */

    /* Overflow with the IDR itself as the newest AU: queued at once, no request. */
    for (i = 0; i < ALT111_QUEUE_CAP; i++)
        assert(alt111_video_submit(&v, s, P, sizeof(P), 1) == ALT111_OK);
    assert(alt111_video_submit(&v, s, IDR, sizeof(IDR), 1) == ALT111_OK);
    assert(!v.skip_to_idr && v.count == 1 && v.resyncs == 2);
    assert(drain(&v, &first) == 1 && first == 5);

    /* A consumer that stopped reading: the first overflow skips, the second one, with not
     * one AU delivered in between, resets it (no key-frame request every second for a
     * renderer that is gone). */
    for (i = 0; i < ALT111_QUEUE_CAP; i++)
        assert(alt111_video_submit(&v, s, P, sizeof(P), 1) == ALT111_OK);
    assert(alt111_video_submit(&v, s, P, sizeof(P), 1) == ALT111_RESYNC);
    assert(alt111_video_submit(&v, s, IDR, sizeof(IDR), 1) == ALT111_OK);
    for (i = 1; i < ALT111_QUEUE_CAP; i++)
        assert(alt111_video_submit(&v, s, P, sizeof(P), 1) == ALT111_OK);
    assert(alt111_video_submit(&v, s, P, sizeof(P), 1) == ALT111_RESTART_CONSUMER);
    assert(!v.attached && v.count == 0);
    /* ... while one that delivers in between may skip again. */
    assert(alt111_video_attach(&v) == ALT111_OK);
    assert(alt111_video_submit(&v, s, IDR, sizeof(IDR), 1) == ALT111_OK);
    assert(drain(&v, &first) == 1);
    for (n = 0; n < 2; n++) {
        for (i = 0; i < ALT111_QUEUE_CAP; i++)
            assert(alt111_video_submit(&v, s, P, sizeof(P), 1) == ALT111_OK);
        assert(alt111_video_submit(&v, s, P, sizeof(P), 1) == ALT111_RESYNC);
        assert(alt111_video_submit(&v, s, IDR, sizeof(IDR), 1) == ALT111_OK);
        assert(drain(&v, &first) == 1 && first == 5);
    }
    assert(v.attached);

    /* A consumer still priming (its IDR not yet sent) is reset as before. */
    alt111_video_detach(&v);
    assert(alt111_video_attach(&v) == ALT111_OK);
    assert(alt111_video_submit(&v, s, IDR, sizeof(IDR), 1) == ALT111_OK);   /* priming queued */
    for (i = 1; i < ALT111_QUEUE_CAP; i++)
        assert(alt111_video_submit(&v, s, P, sizeof(P), 1) == ALT111_OK);
    assert(alt111_video_submit(&v, s, P, sizeof(P), 1) == ALT111_RESTART_CONSUMER);
    assert(!v.attached);

    /* A new consumer starts clean: no leftover skip state. */
    assert(alt111_video_attach(&v) == ALT111_OK && !v.skip_to_idr);

    alt111_video_destroy(&v);
    printf("alt111_video_test: overflow skips to the next IDR on the same consumer, partial head "
           "kept, IDR-at-overflow queued, stalled consumer reset, priming consumer reset PASS\n");
    return 0;
}
