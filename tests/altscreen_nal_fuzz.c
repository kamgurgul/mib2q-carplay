/*
 * Fuzz test of the Annex-B scanners that see every byte the phone sends:
 *   - altr_config_prefix() (altscreen_render/nal.h): splits each AU into CODECCONFIG
 *     (SPS/PPS) and picture for the hardware decoder.
 *   - aa_nal_types() (aa_hook/aa_tee.c): the Android Auto tee's key-frame gate.
 *
 * Structured AUs (known NAL list) check the exact result; random byte soup, dense in
 * zero bytes so start codes are frequent, checks the invariants. Every buffer is a
 * fresh heap block of exactly n bytes, so ASan catches any read past the end.
 * Run with SANITIZE=1 scripts/run_tests.sh for ASan/UBSan.
 *
 *   ALTR_FUZZ_ITERS (default 200000) per generator.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "nal.h"
#include "aa_hook.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void aa_log(const char *fmt, ...) { (void)fmt; }   /* aa_tee.c logging: silent */

static uint32_t rng = 0x9e3779b9u;
static uint32_t next(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static unsigned below(unsigned n) { return n ? next() % n : 0; }

static void fail(const char *what, const uint8_t *p, size_t n, size_t got)
{
    size_t i;
    printf("FAIL: %s (n=%zu got=%zu)\n  ", what, n, got);
    for (i = 0; i < n && i < 48; i++) printf("%02x ", p[i]);
    printf("\n");
    exit(1);
}

static size_t start_code_at(const uint8_t *p, size_t n, size_t i)
{
    if (i + 3 <= n && p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1) return 3;
    if (i + 4 <= n && p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 0 && p[i + 3] == 1) return 4;
    return 0;
}

/* AU from a NAL list: start code (3 or 4 bytes), header byte, payload without zero
 * bytes (so no start code appears inside a NAL). Returns the expected prefix. */
static uint8_t *structured(size_t *len, size_t *expect)
{
    static const unsigned types[] = { 1, 5, 6, 7, 8, 9, 7, 8, 1, 5 };
    unsigned count = 1 + below(6), k;
    size_t cap = 0, n = 0, prefix = 0;
    int cfg = 0, picture = 0;
    uint8_t *p;
    for (k = 0; k < count; k++) cap += 4 + 1 + 40;
    p = malloc(cap);
    for (k = 0; k < count; k++) {
        unsigned type = types[below(sizeof(types) / sizeof(types[0]))];
        unsigned payload = below(41), j;
        size_t at = n;
        if (below(2)) p[n++] = 0;
        p[n++] = 0; p[n++] = 0; p[n++] = 1;
        p[n++] = (uint8_t)((below(4) << 5) | type);     /* nal_ref_idc, type */
        for (j = 0; j < payload; j++) p[n++] = (uint8_t)(1 + below(255));
        if (!picture) {
            if (type == 7 || type == 8) cfg = 1;
            else if (type != 6 && type != 9) { picture = 1; prefix = cfg ? at : 0; }
        }
    }
    if (!picture) prefix = cfg ? n : 0;
    /* A trailing 3-byte start code with no header byte is not a NAL. */
    *len = n;
    *expect = prefix;
    return p;
}

static void check_invariants(const uint8_t *p, size_t n, size_t r)
{
    if (r > n) fail("prefix past the AU", p, n, r);
    if (r == 0 || r == n) return;
    {
        size_t sc = start_code_at(p, n, r);
        unsigned type;
        if (!sc || r + sc >= n) fail("split not on a start code", p, n, r);
        type = p[r + sc] & 31u;
        if (type == 6 || type == 7 || type == 8 || type == 9)
            fail("split before a config/AUD/SEI NAL", p, n, r);
    }
}

int main(void)
{
    const char *e = getenv("ALTR_FUZZ_ITERS");
    long iters = e ? atol(e) : 200000L, it;
    size_t structured_cfg = 0, random_splits = 0;
    if (iters <= 0) iters = 200000L;

    /* Fixed cases from the field: SPS+PPS+IDR, picture only, config only, empty. */
    {
        static const uint8_t idr[] = { 0,0,0,1,0x67,0x42,0,0,0,1,0x68,0xce,0,0,1,0x65,0x88 };
        static const uint8_t pic[] = { 0,0,0,1,0x41,0x9a };
        static const uint8_t cfg[] = { 0,0,0,1,0x09,0x10,0,0,0,1,0x67,0x42,0,0,1,0x68,0xce };
        if (altr_config_prefix(idr, sizeof(idr)) != 12) fail("SPS+PPS+IDR", idr, sizeof(idr), altr_config_prefix(idr, sizeof(idr)));
        if (altr_config_prefix(pic, sizeof(pic)) != 0) fail("picture only", pic, sizeof(pic), 0);
        if (altr_config_prefix(cfg, sizeof(cfg)) != sizeof(cfg)) fail("config only", cfg, sizeof(cfg), 0);
        if (altr_config_prefix(idr, 0) != 0 || altr_config_prefix(idr, 3) != 0)
            fail("short input", idr, 3, 0);
        if (aa_nal_types(idr, sizeof(idr)) != ((1u << 7) | (1u << 8) | (1u << 5)))
            fail("aa_nal_types SPS+PPS+IDR", idr, sizeof(idr), 0);
    }

    for (it = 0; it < iters; it++) {
        size_t n, expect, r;
        uint8_t *p = structured(&n, &expect);
        uint8_t *exact = malloc(n ? n : 1);
        memcpy(exact, p, n);
        free(p);
        r = altr_config_prefix(exact, n);
        if (r != expect) fail("structured AU", exact, n, r);
        check_invariants(exact, n, r);
        if (r) structured_cfg++;
        (void)aa_nal_types(exact, n);
        free(exact);
    }

    for (it = 0; it < iters; it++) {
        size_t n = below(97), i, r;
        uint8_t *p = malloc(n ? n : 1);
        uint32_t bits;
        for (i = 0; i < n; i++) {
            unsigned roll = below(8);
            p[i] = roll < 4 ? 0 : roll < 6 ? 1 : (uint8_t)next();
        }
        r = altr_config_prefix(p, n);
        check_invariants(p, n, r);
        if (r && r < n) random_splits++;
        bits = aa_nal_types(p, n);
        (void)bits;
        free(p);
    }

    printf("altscreen_nal_fuzz: %ld structured AUs (%zu with config) and %ld random buffers "
           "(%zu split), exact sizes, config_prefix + aa_nal_types PASS\n",
           iters, structured_cfg, iters, random_splits);
    return 0;
}
