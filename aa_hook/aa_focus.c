/*
 * Video focus of the Android Auto cluster sink, two opt-in uses (both experimental,
 * car test pending):
 *
 *   Pause while hidden (marker cluster_video.pause_hidden, shared with the CarPlay hook):
 *   Java writes /tmp/aa_cluster_hidden ("1\n" while the cockpit shows no phone map: another
 *   VC tab, or the Audi map picked with a long roller press). The sink then reports NATIVE
 *   focus, so the phone stops drawing and encoding the cluster display, and PROJECTED again
 *   when the map is back. The renderer keeps the last picture meanwhile.
 *
 *   Key-frame refocus (marker aa_cluster.keyframe_refocus): Android Auto sends a key frame
 *   only every minute or two. When the renderer needs one the tee no longer holds (it asked
 *   over the back-channel, or reconnected after the cached key frame was dropped for the
 *   byte budget), the sink reports NATIVE and 200 ms later PROJECTED focus, which should
 *   make the phone restart the stream with a key frame. At most once every 10 s.
 *
 * Without either marker nothing here sends anything. Receiver thread only (the cluster
 * frame callback and routeMessage), the thread the receiver itself sends from; the
 * stock setVideoFocus(mode, unsolicited) queues the notification. Theme changes in a
 * running session dropped the phone's session (issue #19); focus is a different message,
 * but untested on the cluster display, hence opt-in.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "aa_hook.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define FOCUS_PROJECTED 1
#define FOCUS_NATIVE 2
#ifndef FOCUS_CHECK_MS              /* host tests run these faster */
#define FOCUS_CHECK_MS 300u
#endif
#ifndef REFOCUS_GAP_MS
#define REFOCUS_GAP_MS 10000u
#endif
#ifndef REFOCUS_BACK_MS
#define REFOCUS_BACK_MS 200u
#endif

static uint64_t focus_now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)(t.tv_nsec / 1000000L);
}

/* Java's record: "1\n" hidden, "0\n" (or no file) shown. */
static int focus_hidden(void)
{
    char v[4] = {0};
    FILE *f = fopen(AA_HIDDEN_PATH, "r");
    if (!f) return 0;
    if (!fgets(v, sizeof(v), f)) v[0] = 0;
    fclose(f);
    return v[0] == '1';
}

/* The sink answered the phone's setup and reported PROJECTED focus (secondary_setup). Until
 * then the tick sends nothing: a NATIVE sent first would be overridden by that PROJECTED
 * while this module still believed the stream paused. */
static void *focus_ready_sink;
/* Session state, receiver thread only. */
static void *cur;
static uint64_t next_check, back_at, last_refocus;
static int paused, pending;

/* A setup is the start of a session whatever the tick saw of the last one's end (the sink
 * object is reused, and a closed channel is only noticed by a tick that runs while it is
 * closed): the phone is on PROJECTED now, so nothing is paused or in a bounce. */
void aa_focus_setup_done(void *sink)
{
    focus_ready_sink = sink;
    cur = sink;
    next_check = back_at = 0;
    paused = pending = 0;
}

void aa_focus_tick(void *sink, aa_set_focus_fn set_focus)
{
    static unsigned skipped_logged;
    uint64_t now;
    int want_pause;
    if (!sink || !set_focus) return;
    if (sink != cur || !((const unsigned char *)sink)[4]) {   /* new session or channel closed */
        cur = ((const unsigned char *)sink)[4] ? sink : NULL;
        next_check = back_at = 0;
        paused = pending = 0;             /* a new sink starts PROJECTED (secondary_setup) */
        if (!cur) { focus_ready_sink = NULL; return; }
    }
    if (focus_ready_sink != sink) return; /* before its setup */
    now = focus_now_ms();
    if (back_at && now >= back_at) {
        back_at = 0;
        if (!paused) {
            set_focus(sink, FOCUS_PROJECTED, 1);
            aa_log("focus.refocus projected (key frame expected)");
        }
    }
    if (now < next_check) return;
    next_check = now + FOCUS_CHECK_MS;

    want_pause = aa_marker(AA_MARKER_PAUSE_HIDDEN) && focus_hidden();
    if (want_pause != paused) {
        paused = want_pause;
        back_at = 0;
        set_focus(sink, paused ? FOCUS_NATIVE : FOCUS_PROJECTED, 1);
        aa_log("focus.%s cluster map %s", paused ? "pause" : "resume",
               paused ? "hidden on the cockpit -> native" : "shown again -> projected");
    }
    if (aa_tee_keyframe_wanted()) pending = 1;
    if (!pending) return;
    if (paused || back_at) { pending = 0; return; }   /* a resume / bounce is under way: it brings one */
    if (!aa_marker(AA_MARKER_REFOCUS)) {
        pending = 0;
        if (skipped_logged++ < 8)
            aa_log("focus.keyframe_wanted (refocus off: %s absent, waiting for the phone's next one)",
                   AA_MARKER_REFOCUS);
        return;
    }
    /* Inside the gap the request stays pending and is served when the gap is over. */
    if (last_refocus && now - last_refocus < REFOCUS_GAP_MS) return;
    pending = 0;
    last_refocus = now;
    back_at = now + REFOCUS_BACK_MS;
    set_focus(sink, FOCUS_NATIVE, 1);
    aa_log("focus.refocus native for %u ms (renderer needs a key frame)", REFOCUS_BACK_MS);
}
