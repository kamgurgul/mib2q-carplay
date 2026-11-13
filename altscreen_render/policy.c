/*
 * altscreen_render — session-loop decisions, see policy.h. No OMX/GL/socket use.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "policy.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct hw_starts { unsigned starts, rounds; unsigned long long until_ms; };

static void starts_load(const char *record, struct hw_starts *st)
{
    FILE *f;
    memset(st, 0, sizeof(*st));
    f = fopen(record, "r");
    if (!f) return;
    if (fscanf(f, "%u %u %llu", &st->starts, &st->rounds, &st->until_ms) != 3)
        memset(st, 0, sizeof(*st));
    fclose(f);
}

static void starts_save(const char *record, const struct hw_starts *st)
{
    FILE *f = fopen(record, "w");
    if (!f) return;
    fprintf(f, "%u %u %llu\n", st->starts, st->rounds, st->until_ms);
    fclose(f);
}

int altr_hw_starts_begin(const char *record, const char *off_boot, uint64_t now_ms,
                         uint64_t *defer_until)
{
    struct hw_starts st;
    starts_load(record, &st);
    if (st.until_ms && now_ms < st.until_ms) {
        *defer_until = st.until_ms;
        return ALTR_HW_DEFER;
    }
    if (st.starts >= ALTR_HW_START_MAX) {
        st.starts = 0;
        if (++st.rounds >= ALTR_HW_ROUNDS_MAX) {
            int fd = open(off_boot, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) close(fd);
            starts_save(record, &st);
            return ALTR_HW_OFF;
        }
        st.until_ms = now_ms + ALTR_HW_COOLDOWN_MS;
        starts_save(record, &st);
        *defer_until = st.until_ms;
        return ALTR_HW_DEFER;
    }
    st.until_ms = 0;
    st.starts++;
    starts_save(record, &st);
    return ALTR_HW_GO;
}

void altr_hw_starts_clear(const char *record)
{
    unlink(record);
}

uint64_t altr_yield_parse(const char *text, uint64_t hold_ms)
{
    unsigned long long t = 0;
    if (!text || sscanf(text, "%llu", &t) != 1 || !t) return 0;
    return (uint64_t)t + hold_ms;
}

uint64_t altr_yield_until(const char *path, uint64_t hold_ms, uint64_t now_ms)
{
    char b[32];
    ssize_t n;
    uint64_t until;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    n = read(fd, b, sizeof(b) - 1);
    close(fd);
    if (n <= 0) return 0;
    b[n] = 0;
    until = altr_yield_parse(b, hold_ms);
    return until > now_ms ? until : 0;
}

int altr_hw_fail_persists(int source_keeps_hangs, int video_core_hung)
{
    return source_keeps_hangs && video_core_hung;
}

int altr_keyframe_due(uint64_t now_ms, uint64_t *last_ms, uint64_t min_gap_ms)
{
    if (*last_ms && now_ms - *last_ms < min_gap_ms) return 0;
    *last_ms = now_ms ? now_ms : 1;
    return 1;
}

int altr_freeze_on_connect(int frozen, int same_source)
{
    return frozen && !same_source ? ALTR_FREEZE_CLOSE : ALTR_FREEZE_KEEP;
}
