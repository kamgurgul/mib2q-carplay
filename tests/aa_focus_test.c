/*
 * Host test for aa_hook/aa_focus.c: nothing is sent without the opt-in markers; with the
 * pause marker a hidden cockpit map gives NATIVE focus and a shown one PROJECTED; with the
 * refocus marker a key-frame request bounces the focus (NATIVE, then PROJECTED), at most
 * once per gap and never while paused; a new sink starts from scratch.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#undef NDEBUG
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char pause_path[512], refocus_path[512], hidden_path[512];
#define AA_MARKER_PAUSE_HIDDEN pause_path
#define AA_MARKER_REFOCUS refocus_path
#define AA_HIDDEN_PATH hidden_path
#define FOCUS_CHECK_MS 20u
#define REFOCUS_GAP_MS 300u
#define REFOCUS_BACK_MS 40u
#include "../aa_hook/aa_focus.c"

void aa_log(const char *fmt, ...) { (void)fmt; }
int aa_marker(const char *path) { return access(path, F_OK) == 0; }
static int wanted;
int aa_tee_keyframe_wanted(void) { int w = wanted; wanted = 0; return w; }

static int calls, last_mode;
static void fake_focus(void *sink, int mode, int unsolicited)
{
    (void)sink;
    assert(unsolicited == 1);
    calls++;
    last_mode = mode;
}

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    assert(f && fputs(text, f) >= 0 && fclose(f) == 0);
}

/* Tick for ms milliseconds, every 5 ms, as the receiver thread would. */
static void run(void *sink, unsigned ms)
{
    unsigned t;
    for (t = 0; t < ms; t += 5) { aa_focus_tick(sink, fake_focus); usleep(5000); }
}

int main(int argc, char **argv)
{
    unsigned char sink[32] = {0}, sink2[32] = {0};
    assert(argc == 2);
    snprintf(pause_path, sizeof(pause_path), "%s/pause", argv[1]);
    snprintf(refocus_path, sizeof(refocus_path), "%s/refocus", argv[1]);
    snprintf(hidden_path, sizeof(hidden_path), "%s/hidden", argv[1]);
    unlink(pause_path); unlink(refocus_path); unlink(hidden_path);
    sink[4] = 1; sink2[4] = 1;          /* channel open */

    /* No markers: hidden map and key-frame requests change nothing. */
    aa_focus_setup_done(sink);
    put(hidden_path, "1\n");
    wanted = 1;
    run(sink, 100);
    assert(calls == 0);

    /* Pause marker: hidden -> NATIVE, shown -> PROJECTED, once each. */
    put(pause_path, "");
    run(sink, 100);
    assert(calls == 1 && last_mode == 2);
    wanted = 1;                         /* paused: no refocus even with the marker */
    put(refocus_path, "");
    run(sink, 100);
    assert(calls == 1);
    put(hidden_path, "0\n");
    run(sink, 100);
    assert(calls == 2 && last_mode == 1);

    /* Refocus: NATIVE, then PROJECTED after the bounce; a second request inside the gap waits. */
    calls = 0;
    wanted = 1;
    run(sink, 120);
    assert(calls == 2 && last_mode == 1);
    wanted = 1;
    run(sink, 60);
    assert(calls == 2);
    usleep(300000);                     /* gap over: the pending request is served, not lost */
    run(sink, 120);
    assert(calls == 4 && last_mode == 1);

    /* A new sink does nothing before its setup (its own PROJECTED would override a NATIVE
     * sent first), then starts from scratch. */
    calls = 0;
    put(hidden_path, "1\n");
    run(sink2, 60);
    assert(calls == 0);
    aa_focus_setup_done(sink2);
    run(sink2, 60);
    assert(calls == 1 && last_mode == 2);   /* paused on the new sink at its first check */
    /* The same sink set up again with no tick in between that saw its channel closed (the
     * next phone session): the phone is on PROJECTED again, so a still hidden map pauses it
     * once more instead of being taken for already paused. */
    aa_focus_setup_done(sink2);
    run(sink2, 60);
    assert(calls == 2 && last_mode == 2);
    sink2[4] = 0;
    run(sink2, 40);
    assert(calls == 2);                     /* closed channel: nothing sent */

    unlink(pause_path); unlink(refocus_path); unlink(hidden_path);
    printf("aa_focus_test: markers off = nothing sent, pause while hidden, refocus bounce, request "
           "kept across the gap, no refocus while paused, nothing before setup, a new setup starts over PASS\n");
    return 0;
}
