/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual rich-message parser and C -> Java record. Optional argv[1] writes the
 * cross-language fixture consumed by AaTripDetailsTest. */
#include "../aa_hook/aa_trip.c"
#include <assert.h>
#include <stdlib.h>

typedef struct { unsigned char b[2048]; unsigned n; } buf;
static void var(buf *b, uint64_t v)
{
    while (v >= 128) { b->b[b->n++] = (unsigned char)(v | 128); v >>= 7; }
    b->b[b->n++] = (unsigned char)v;
}
static void num(buf *b, unsigned f, uint64_t v) { var(b, f * 8); var(b, v); }
static void bytes(buf *b, unsigned f, const void *p, unsigned n)
{
    var(b, f * 8 + 2); var(b, n); memcpy(b->b + b->n, p, n); b->n += n;
}
static void msg(buf *b, unsigned f, const buf *v) { bytes(b, f, v->b, v->n); }
static void dest(buf *b, const char *text)
{
    buf d = {{0}, 0}; bytes(&d, 1, text, (unsigned)strlen(text)); msg(b, 2, &d);
}
static void estimate(buf *b, int meters, int seconds)
{
    buf d = {{0}, 0}, e = {{0}, 0};
    num(&d, 1, (uint64_t)(int64_t)meters); msg(&e, 1, &d);
    bytes(&e, 2, "15:45", 5); num(&e, 3, (uint64_t)(int64_t)seconds); msg(b, 2, &e);
}
static void same(const aa_trip_t *a, const aa_trip_t *b)
{
    assert(!strcmp(a->destination, b->destination));
    assert(a->meters == b->meters && a->seconds == b->seconds);
    assert(a->state_ms == b->state_ms && a->position_ms == b->position_ms);
}

int main(int argc, char **argv)
{
    aa_trip_t t, saved;
    buf s = {{0}, 0}, p = {{0}, 0};
    unsigned char rec[AA_TRIP_RECORD_SIZE];
    static const unsigned char captured[] =
        "\x0a\x30\x0a\x02\x08\x01\x12\x14\x0a\x12toward Goodyear St\x22\x14\x0a\x12toward Goodyear St"
        "\x12\x1a\x0a\x18" "5751 Pacific Center Blvd";
    trip_clear(&t);
    assert(trip_parse(&t, 0x8006, captured, sizeof(captured) - 1, 90000));
    assert(!strcmp(t.destination, "5751 Pacific Center Blvd"));
    dest(&s, "Intermediate stop"); dest(&s, "Krak\xc3\xb3w: Dworzec G\xc5\x82\xc3\xb3wny");
    estimate(&p, 3000, 120); estimate(&p, 12000, 900);
    assert(trip_parse(&t, 0x8006, s.b, s.n, 100000));
    assert(trip_parse(&t, 0x8007, p.b, p.n, 100500));
    assert(t.meters == 12000 && t.seconds == 900 && t.position_ms == 100500);
    assert(!strcmp(t.destination, "Krak\xc3\xb3w: Dworzec G\xc5\x82\xc3\xb3wny"));
    trip_record(&t, 42, rec);
    if (argc == 2) {
        FILE *f = fopen(argv[1], "wb"); assert(f);
        assert(fwrite(rec, 1, sizeof(rec), f) == sizeof(rec)); assert(fclose(f) == 0);
    }
    /* Repeated names do not discard estimates or refresh their sample time. */
    assert(trip_parse(&t, 0x8006, s.b, s.n, 101000));
    assert(t.position_ms == 100500 && t.seconds == 900);
    assert(trip_parse(&t, 0x8007, p.b, p.n, 102000) && t.position_ms == 102000);
    assert(t.state_ms == 102000);   /* estimates keep the unchanged destination fresh */
    saved = t;
    /* Every truncated final-destination message rejects all its partial fields. */
    { buf single = {{0}, 0}; unsigned i; estimate(&single, 12000, 900);
      for (i = 1; i < single.n; i++) {
          assert(!trip_parse(&t, 0x8007, single.b, i, 103000)); same(&t, &saved);
      }
    }
    { unsigned char bad[][12] = {
        {0x12, 0x02, 0x18, 0x80},       /* unterminated duration */
        {0x12, 0x03, 0x0a, 0x01, 0x08},/* truncated distance varint */
        {0x12, 0x02, 0x1a, 0x00},       /* duration has wrong wire type */
        {0x12, 0x01, 0x00},             /* field zero */
        {0x12, 0x01, 0x0f}              /* unknown wire type */
      }; unsigned sizes[] = {4,5,4,3,3}, i;
      for (i = 0; i < 5; i++) { assert(!trip_parse(&t, 0x8007, bad[i], sizes[i], 103000)); same(&t, &saved); }
    }
    { buf bad = {{0}, 0}; unsigned i; var(&bad, 0x10);
      for (i = 0; i < 9; i++) bad.b[bad.n++] = 0xff;
      bad.b[bad.n++] = 2; /* a varint cannot overflow 64 bits */
      assert(!trip_parse(&t, 0x8007, bad.b, bad.n, 103000)); same(&t, &saved);
    }
    /* Unknown fields are skipped without losing a valid final estimate. */
    num(&p, 99, 7); assert(trip_parse(&t, 0x8007, p.b, p.n, 103000));
    assert(t.seconds == 900);
    /* A different destination invalidates the old destination's distance/ETA. */
    memset(&s, 0, sizeof(s)); dest(&s, "New destination");
    assert(trip_parse(&t, 0x8006, s.b, s.n, 104000));
    assert(t.meters == -1 && t.seconds == -1 && t.position_ms == 0);
    trip_record(&t, 43, rec);
    { unsigned i; for (i = 32 + (unsigned)strlen(t.destination); i < AA_TRIP_RECORD_SIZE - 4; i++) assert(rec[i] == 0); }
    /* Zero is valid arrival; negative / oversized values are unknown. */
    memset(&p, 0, sizeof(p)); estimate(&p, 0, 0);
    assert(trip_parse(&t, 0x8007, p.b, p.n, 105000) && t.meters == 0 && t.seconds == 0);
    memset(&p, 0, sizeof(p)); estimate(&p, -1, -1);
    assert(trip_parse(&t, 0x8007, p.b, p.n, 106000) && t.meters == -1 && t.seconds == -1);
    assert(t.state_ms == 105000);   /* no estimate, no destination refresh */
    /* No estimates / only a formatted ETA must never invent numeric values. */
    { buf d = {{0}, 0}; memset(&p, 0, sizeof(p)); bytes(&d, 2, "3:45 PM", 7); msg(&p, 2, &d);
      assert(trip_parse(&t, 0x8007, p.b, p.n, 107000) && t.seconds == -1 && t.meters == -1); }
    assert(trip_parse(&t, 0x8007, NULL, 0, 108000) && t.seconds == -1);
    assert(trip_parse(&t, 0x8006, NULL, 0, 109000) && !t.destination[0]);
    /* Length cap preserves UTF-8; text cannot inject a bus line. */
    { char name[270]; memset(name, 'x', 254); memcpy(name + 254, "\xc5\x82\nend", 6); name[260] = 0;
      memset(&s, 0, sizeof(s)); dest(&s, name);
      assert(trip_parse(&t, 0x8006, s.b, s.n, 110000) && strlen(t.destination) == 254);
      memset(&s, 0, sizeof(s)); dest(&s, "A\nB\rC");
      assert(trip_parse(&t, 0x8006, s.b, s.n, 110000) && !strcmp(t.destination, "A B C")); }
    /* Status end/unavailable/rerouting clear details; ACTIVE does not. */
    { unsigned status;
      for (status = 0; status < 4; status++) {
          buf st = {{0}, 0}; t = saved; num(&st, 1, status);
          assert(trip_parse(&t, 0x8003, st.b, st.n, 111000) == (status != 1));
          if (status == 1) same(&t, &saved);
          else assert(!t.destination[0] && t.meters == -1 && t.seconds == -1 && t.state_ms == 111000);
      }
    }
    /* Random untrusted bytes exercise bounded parsing under ASan/UBSan. */
    { unsigned i, j; uint32_t rng = 17; unsigned char junk[128];
      for (i = 0; i < 10000; i++) {
          for (j = 0; j < sizeof(junk); j++) { rng = rng * 1664525u + 1013904223u; junk[j] = (unsigned char)(rng >> 24); }
          t = saved; (void)trip_parse(&t, 0x8006 + (i & 1), junk, i % sizeof(junk), 112000);
      }
    }
    puts("aa_trip_test: PASS captured destination, final stop, estimates, replacement, malformed input, UTF-8, route status, wire record");
    return 0;
}
