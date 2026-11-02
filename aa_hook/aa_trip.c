/* SPDX-License-Identifier: GPL-3.0-or-later
 * Rich AA navigation -> a bounded, checksummed snapshot for AaTripFeed.
 * Destination arrays are ordered along the trip: use the LAST entry in both
 * 0x8006.destinations and 0x8007.destination_distances (the final destination).
 * Each message replaces its part of the snapshot; absent fields clear it. Fresh estimates
 * also keep an unchanged destination name fresh.
 */
#include "aa_trip.h"
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct { const unsigned char *p; unsigned n; } trip_pb;

static int trip_varint(trip_pb *p, uint64_t *v)
{
    unsigned i;
    *v = 0;
    for (i = 0; i < 10 && p->n; i++) {
        unsigned c = *p->p++; p->n--;
        if (i == 9 && c > 1) return 0;
        *v |= (uint64_t)(c & 127) << (i * 7);
        if (!(c & 128)) return 1;
    }
    return 0;
}

/* The caller loops on remaining bytes, so a truncated field can never look like EOF. */
static int trip_field(trip_pb *p, unsigned *field, unsigned *wire, uint64_t *v, trip_pb *sub)
{
    uint64_t tag, len;
    if (!trip_varint(p, &tag) || !(tag >> 3) || (tag >> 3) > 0x1fffffff) return 0;
    *field = (unsigned)(tag >> 3); *wire = (unsigned)(tag & 7);
    if (*wire == 0) return trip_varint(p, v);
    if (*wire == 2) { if (!trip_varint(p, &len)) return 0; }
    else if (*wire == 1) len = 8;
    else if (*wire == 5) len = 4;
    else return 0;
    if (len > p->n) return 0;
    sub->p = p->p; sub->n = (unsigned)len;
    p->p += len; p->n -= (unsigned)len;
    return 1;
}

static void trip_text(char *out, trip_pb s)
{
    unsigned n = s.n < AA_TRIP_DEST_SIZE ? s.n : AA_TRIP_DEST_SIZE - 1;
    /* Never cut a UTF-8 codepoint at the byte limit. Java validates the result. */
    if (n < s.n) while (n && (s.p[n] & 0xc0) == 0x80) n--;
    memcpy(out, s.p, n); out[n] = 0;
    while (n) { n--; if ((unsigned char)out[n] < 32 || out[n] == 127) out[n] = ' '; }
}

static int trip_destination(trip_pb p, char *out)
{
    unsigned f, w; uint64_t v; trip_pb s;
    out[0] = 0;
    while (p.n) {
        if (!trip_field(&p, &f, &w, &v, &s)) return 0;
        if (f == 1) { if (w != 2) return 0; trip_text(out, s); }
    }
    return 1;
}

static int trip_distance(trip_pb p, int32_t *meters)
{
    unsigned f, w; uint64_t v; trip_pb s;
    *meters = -1;
    while (p.n) {
        if (!trip_field(&p, &f, &w, &v, &s)) return 0;
        if (f == 1) {
            if (w != 0) return 0;
            *meters = v <= INT32_MAX ? (int32_t)v : -1;
        }
    }
    return 1;
}

static int trip_estimate(trip_pb p, int32_t *meters, int32_t *seconds)
{
    unsigned f, w; uint64_t v; trip_pb s;
    *meters = *seconds = -1;
    while (p.n) {
        if (!trip_field(&p, &f, &w, &v, &s)) return 0;
        if (f == 1) { if (w != 2 || !trip_distance(s, meters)) return 0; }
        /* Field 2 is a localized arrival string, not a timestamp. */
        if (f == 3) { if (w != 0) return 0; *seconds = v <= INT32_MAX ? (int32_t)v : -1; }
    }
    return 1;
}

static void trip_clear(aa_trip_t *t)
{
    memset(t, 0, sizeof(*t)); t->meters = t->seconds = -1;
}

/* Transactional parsing: malformed messages leave the last complete snapshot alone. */
static int trip_parse(aa_trip_t *t, unsigned id, const unsigned char *body, unsigned n, uint64_t now)
{
    aa_trip_t next = *t;
    trip_pb p = { body, n }, s;
    unsigned f, w, status = 1; uint64_t v;
    int have_status = 0;
    if (!body && n) return 0;
    if (id == 0x8006) next.destination[0] = 0;
    else if (id == 0x8007) next.meters = next.seconds = -1;
    else if (id != 0x8003) return 0;
    while (p.n) {
        if (!trip_field(&p, &f, &w, &v, &s)) return 0;
        if (id == 0x8003 && f == 1) {
            if (w != 0 || v > 3) return 0;
            status = (unsigned)v; have_status = 1;
        } else if (f == 2 && id == 0x8006) {
            if (w != 2 || !trip_destination(s, next.destination)) return 0;
        } else if (f == 2 && id == 0x8007) {
            if (w != 2 || !trip_estimate(s, &next.meters, &next.seconds)) return 0;
        }
    }
    if (id == 0x8003) {
        if (!have_status || status == 1) return 0;
        trip_clear(&next);               /* unavailable, inactive or recalculating */
        next.state_ms = next.position_ms = now;
    } else if (id == 0x8006) {
        if (strcmp(next.destination, t->destination)) {
            next.meters = next.seconds = -1; next.position_ms = 0;
        }
        next.state_ms = now;
    } else {
        next.position_ms = now;
        /* Estimates for the route keep its destination alive: apps may send 0x8006 only on
         * step changes. A different destination still arrives as a new 0x8006. */
        if (next.destination[0] && (next.meters >= 0 || next.seconds >= 0)) next.state_ms = now;
    }
    *t = next;
    return 1;
}

static void trip_u32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
}

static void trip_record(const aa_trip_t *t, uint32_t seq, unsigned char *out)
{
    unsigned i; uint32_t hash = 2166136261u;
    memset(out, 0, AA_TRIP_RECORD_SIZE);
    memcpy(out, "AAT1", 4); trip_u32(out + 4, seq);
    trip_u32(out + 8, (uint32_t)(t->state_ms >> 32)); trip_u32(out + 12, (uint32_t)t->state_ms);
    trip_u32(out + 16, (uint32_t)(t->position_ms >> 32)); trip_u32(out + 20, (uint32_t)t->position_ms);
    trip_u32(out + 24, (uint32_t)t->meters); trip_u32(out + 28, (uint32_t)t->seconds);
    memcpy(out + 32, t->destination, strlen(t->destination));
    for (i = 0; i < AA_TRIP_RECORD_SIZE - 4; i++) hash = (hash ^ out[i]) * 16777619u;
    trip_u32(out + AA_TRIP_RECORD_SIZE - 4, hash);
}

static pthread_mutex_t trip_lock = PTHREAD_MUTEX_INITIALIZER;
static aa_trip_t trip = { "", -1, -1, 0, 0 };
static uint32_t trip_seq;

static uint64_t trip_now(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* /tmp is QNX shared memory: overwrite one fixed-size record, without rename/truncate.
 * A checksum rejects partial/torn reads. Write every sample, even identical values:
 * a fresh '600 seconds remaining' means a later ETA than an old sample of 600. */
static void trip_publish(void)
{
    unsigned char record[AA_TRIP_RECORD_SIZE];
    int fd = open(AA_TRIP_PATH, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) return;
    trip_record(&trip, ++trip_seq, record);
    (void)pwrite(fd, record, sizeof(record), 0);
    close(fd);
}

void aa_trip_reset(void)
{
    pthread_mutex_lock(&trip_lock);
    trip_clear(&trip);
    trip.state_ms = trip.position_ms = trip_now();
    trip_publish();
    pthread_mutex_unlock(&trip_lock);
}

void aa_trip_update(unsigned id, const unsigned char *body, unsigned size)
{
    pthread_mutex_lock(&trip_lock);
    if (trip_parse(&trip, id, body, size, trip_now())) trip_publish();
    pthread_mutex_unlock(&trip_lock);
}
