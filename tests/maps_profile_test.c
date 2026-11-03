/*
 * Host test for hook/mapsprofile: the Maps 30 FPS PPID is appended to a valid Identify
 * exactly once, never next to an existing top-level PPID (a nested parameter 34 does not
 * count), never to a malformed message, and not with the off marker.
 * Build with -DMAPS_PROFILE_OFF_PATH pointing at a temp file.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../hook/mapsprofile/maps_profile.h"
#include "../hook/framework/common.h"
#include "../hook/framework/iap2_protocol.h"

#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static uint8_t grown[8192];
static int grow_calls;
uint8_t* hook_identify_grow(hook_context_t* ctx, size_t len, size_t new_len, size_t* out_cap) {
    (void)ctx;
    grow_calls++;
    if (new_len > sizeof(grown)) return NULL;
    (void)len;   /* the caller already built the frame in grown[] */
    if (out_cap) *out_cap = sizeof(grown);
    return grown;
}

/* An Identify with a name parameter (id 0) and, optionally, a component holding id 34. */
static size_t identify(uint8_t* b, int nested34, int top34) {
    size_t n = 6;
    b[0] = IAP2_SYNC1; b[1] = IAP2_SYNC2; write_be16(b + 4, IAP2_MSG_IDENTIFY);
    write_be16(b + n, 8); write_be16(b + n + 2, 0); memcpy(b + n + 4, "Car", 4); n += 8;
    if (nested34) {   /* component 17 containing a sub-parameter 34 */
        write_be16(b + n, 10); write_be16(b + n + 2, 17);
        write_be16(b + n + 4, 6); write_be16(b + n + 6, 34); write_be16(b + n + 8, 1); n += 10;
    }
    if (top34) { write_be16(b + n, 6); write_be16(b + n + 2, 34); write_be16(b + n + 4, 7); n += 6; }
    write_be16(b + 2, (uint16_t)n);
    return n;
}

int main(void)
{
    uint8_t b[512];
    size_t n, out;
    unlink(MAPS_PROFILE_OFF_PATH);

    /* Valid Identify, room in place: +21 bytes, PPID string with NUL, header length. */
    n = identify(b, 0, 0);
    out = maps_profile_identify(NULL, b, n, sizeof(b));
    assert(out == n + 21 && read_be16(b + 2) == out);
    assert(read_be16(b + n) == 21 && read_be16(b + n + 2) == 34);
    assert(memcmp(b + n + 4, MAPS_PROFILE_PPID, 17) == 0);
    /* Already carrying it (a second Identify on the same buffer): unchanged. */
    assert(maps_profile_identify(NULL, b, out, sizeof(b)) == out);

    /* A nested parameter 34 inside a component does not count as the PPID. */
    n = identify(b, 1, 0);
    assert(maps_profile_identify(NULL, b, n, sizeof(b)) == n + 21);
    /* A stock top-level PPID is kept, nothing appended. */
    n = identify(b, 0, 1);
    assert(maps_profile_identify(NULL, b, n, sizeof(b)) == n);

    /* No room in the Cinemo array: grown through the framework helper. */
    n = identify(grown, 0, 0);
    grow_calls = 0;
    out = maps_profile_identify(NULL, grown, n, n);
    assert(grow_calls == 1 && out == n + 21 && read_be16(grown + 2) == out);

    /* Malformed: bad header length, truncated parameter, wrong message, bad sync. */
    n = identify(b, 0, 0); write_be16(b + 2, (uint16_t)(n + 1));
    assert(maps_profile_identify(NULL, b, n, sizeof(b)) == n);
    n = identify(b, 0, 0); write_be16(b + 6, 200);
    assert(maps_profile_identify(NULL, b, n, sizeof(b)) == n);
    n = identify(b, 0, 0); write_be16(b + 4, 0x1D02);
    assert(maps_profile_identify(NULL, b, n, sizeof(b)) == n);
    n = identify(b, 0, 0); b[0] = 0;
    assert(maps_profile_identify(NULL, b, n, sizeof(b)) == n);

    /* Off marker. */
    { FILE* f = fopen(MAPS_PROFILE_OFF_PATH, "w"); assert(f); fclose(f); }
    n = identify(b, 0, 0);
    assert(maps_profile_identify(NULL, b, n, sizeof(b)) == n);
    unlink(MAPS_PROFILE_OFF_PATH);

    puts("maps_profile_test: PPID appended once, nested 34 ignored, stock PPID kept, grow, "
         "malformed and off marker untouched PASS");
    return 0;
}
