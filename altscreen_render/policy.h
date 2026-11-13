/*
 * altscreen_render — decisions of the renderer's session loop that need no OMX, GL or
 * socket: hardware-start records, the main-screen yield stamp, when a hardware failure
 * is remembered across boots, keyframe-request pacing and what a new tee connection
 * does to a frozen picture. Host-tested by tests/altscreen_policy_test.c.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ALTR_POLICY_H
#define ALTR_POLICY_H

#include <stdint.h>

/* Bounded hardware-decoder starts across renderer restarts (idea from
 * luka-dev/mib2q-carplay-rgi, sink_omx_qnx.c). A start that never produced a picture
 * and never closed cleanly is counted in a /tmp record; after ALTR_HW_START_MAX of
 * them hardware waits ALTR_HW_COOLDOWN_MS, after ALTR_HW_ROUNDS_MAX such rounds it
 * stays off for this boot. */
#define ALTR_HW_START_MAX     3u
#define ALTR_HW_COOLDOWN_MS   30000u
#define ALTR_HW_ROUNDS_MAX    2u

enum { ALTR_HW_GO = 0, ALTR_HW_DEFER = 1, ALTR_HW_OFF = -1 };

/* Before a hardware start. record = the start record file, off_boot = the file that
 * switches hardware off for this boot (created on ALTR_HW_OFF). ALTR_HW_GO = counted,
 * go ahead; ALTR_HW_DEFER = software until *defer_until; ALTR_HW_OFF = off for this boot. */
int altr_hw_starts_begin(const char *record, const char *off_boot, uint64_t now_ms,
                         uint64_t *defer_until);

/* A picture came out, or the decoder closed cleanly: the start record is dropped. */
void altr_hw_starts_clear(const char *record);

/* Main-screen yield stamp (uptime ms written by a hook): the uptime until which the
 * hardware decoder stays off, 0 for an empty, garbled or zero stamp. */
uint64_t altr_yield_parse(const char *text, uint64_t hold_ms);

/* The same from the file; 0 when it is missing or the hold is over at now_ms. */
uint64_t altr_yield_until(const char *path, uint64_t hold_ms, uint64_t now_ms);

/* Is a hardware failure remembered after a reboot (persistent off marker)? Only a hung
 * video core (a call that never returned, a decoder that never left Executing), and only
 * for a source that keeps such failures: a hang can take the HMI down with it, so the next
 * boot must not walk into it again. A decoder that merely failed (setup refused, no
 * picture, port change) is off for this boot only and tried again after the next one. */
int altr_hw_fail_persists(int source_keeps_hangs, int video_core_hung);

/* Keyframe requests over the tee back-channel: at most one per min_gap_ms. 1 = send
 * now (*last_ms updated), 0 = too soon. */
int altr_keyframe_due(uint64_t now_ms, uint64_t *last_ms, uint64_t min_gap_ms);

/* A tee connection came up while window 99 still holds the last picture of a finished
 * stream (disconnect freeze). The connection alone is no new stream (the hooks listen
 * all the time): the same source keeps the frozen picture and resumes the decoder on
 * that window when its first bytes arrive; another source starts from scratch. */
enum { ALTR_FREEZE_KEEP = 0, ALTR_FREEZE_CLOSE = 1 };
int altr_freeze_on_connect(int frozen, int same_source);

#endif /* ALTR_POLICY_H */
