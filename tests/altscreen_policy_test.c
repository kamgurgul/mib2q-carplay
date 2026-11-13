/*
 * Host test for altscreen_render/policy.c: bounded hardware starts (3 without a picture ->
 * 30 s cooldown, a second round -> off for this boot), the main-screen yield stamp, which
 * failures survive a reboot, key-frame request pacing, and what a new tee connection does
 * to a frozen picture.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "policy.h"

#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    assert(f && fputs(text, f) >= 0 && fclose(f) == 0);
}

int main(int argc, char **argv)
{
    char rec[512], off[512], yield[512];
    uint64_t until = 0, now = 1000000;
    unsigned i;
    assert(argc == 2);
    snprintf(rec, sizeof(rec), "%s/hw.starts", argv[1]);
    snprintf(off, sizeof(off), "%s/hw.off", argv[1]);
    snprintf(yield, sizeof(yield), "%s/hw_yield", argv[1]);
    unlink(rec); unlink(off);

    /* Three starts are allowed; the fourth waits out the cooldown in software. */
    for (i = 0; i < ALTR_HW_START_MAX; i++) assert(altr_hw_starts_begin(rec, off, now, &until) == ALTR_HW_GO);
    assert(altr_hw_starts_begin(rec, off, now, &until) == ALTR_HW_DEFER);
    assert(until == now + ALTR_HW_COOLDOWN_MS);
    assert(altr_hw_starts_begin(rec, off, now + 1000, &until) == ALTR_HW_DEFER);   /* still cooling */
    /* After the cooldown, a second round of three; then off for this boot. */
    now += ALTR_HW_COOLDOWN_MS;
    for (i = 0; i < ALTR_HW_START_MAX; i++) assert(altr_hw_starts_begin(rec, off, now, &until) == ALTR_HW_GO);
    assert(access(off, F_OK) != 0);
    assert(altr_hw_starts_begin(rec, off, now, &until) == ALTR_HW_OFF);
    assert(access(off, F_OK) == 0);
    /* A picture (or a clean close) clears the record. */
    altr_hw_starts_clear(rec);
    assert(altr_hw_starts_begin(rec, off, now, &until) == ALTR_HW_GO);
    put(rec, "garbage");                                 /* unreadable record = fresh start */
    assert(altr_hw_starts_begin(rec, off, now, &until) == ALTR_HW_GO);

    /* Yield stamp: uptime ms + hold, nothing for missing, empty, zero or garbled stamps. */
    assert(altr_yield_parse("12345\n", 3000) == 15345);
    assert(altr_yield_parse("", 3000) == 0 && altr_yield_parse("0\n", 3000) == 0);
    assert(altr_yield_parse("abc", 3000) == 0 && altr_yield_parse(NULL, 3000) == 0);
    unlink(yield);
    assert(altr_yield_until(yield, 3000, 5000) == 0);
    put(yield, "4000\n");
    assert(altr_yield_until(yield, 3000, 5000) == 7000);
    assert(altr_yield_until(yield, 3000, 7000) == 0);    /* hold over */

    /* Only a hung video core is remembered after a reboot, and only for CarPlay's source. */
    assert(altr_hw_fail_persists(1, 1) == 1);
    assert(altr_hw_fail_persists(1, 0) == 0);           /* setup refused, no picture, ... */
    assert(altr_hw_fail_persists(0, 1) == 0);           /* Android Auto: this boot only */

    /* Key-frame requests: at most one per gap. */
    {
        uint64_t last = 0;
        assert(altr_keyframe_due(10000, &last, 2000) == 1 && last == 10000);
        assert(altr_keyframe_due(11999, &last, 2000) == 0);
        assert(altr_keyframe_due(12000, &last, 2000) == 1);
        last = 0;
        assert(altr_keyframe_due(0, &last, 2000) == 1 && last != 0);   /* uptime 0 still counts */
    }

    /* A frozen picture survives a connection from its own source, not from another one. */
    assert(altr_freeze_on_connect(1, 1) == ALTR_FREEZE_KEEP);
    assert(altr_freeze_on_connect(1, 0) == ALTR_FREEZE_CLOSE);
    assert(altr_freeze_on_connect(0, 0) == ALTR_FREEZE_KEEP);

    unlink(rec); unlink(off); unlink(yield);
    printf("altscreen_policy_test: bounded hardware starts, yield stamp, hang-only persistence, "
           "key-frame pacing, freeze on connect PASS\n");
    return 0;
}
