/*
 * Main-screen video yield: gal's own decoder versus the cluster map's hardware decoder.
 *
 * Both run on the head unit's single Venus video core, and two decoders changing state at the
 * same moment hung it: issue #14 (the cluster decoder opened as an Android Auto session started
 * with a route running, i.e. together with gal's main-screen decoder: VCD_HWTimeoutHandler, MMI
 * freeze, watchdog reboot) and car logs 2026-10-30 save 011 (CarPlay). The CarPlay hook has
 * announced stock's main-screen setup and teardown to altscreen_render since 1.4; this is the
 * same protocol for gal. It replaces the 120 s software-decode wait after boot for Android Auto,
 * whose planned reconnect cost the whole session on QNX (issue #48).
 *
 * gal's renderer (CVideoRendererJob_InitRenderer / _ConfigAvailable / _ShutdownRenderer) is
 * driven by the stock VideoSink callbacks, so aa_hook.c calls aa_hw_yield() before the main
 * sink's setup, codec configuration, playback start and stop, and before the session shutdown.
 * It stamps AA_HW_YIELD_PATH with the uptime (ms) and then waits, at most AA_HW_YIELD_WAIT_MS,
 * while AA_HW_ACTIVE_PATH names a live renderer. The renderer reads the stamp every 40 ms while
 * it decodes in hardware, suspends its decoder (the cockpit keeps the last picture), and opens
 * none until ALTR_HW_YIELD_HOLD_S (3 s) after the last stamp. Ordering as in the CarPlay hook:
 * the stamp is written before the active file is read, and the renderer writes the active file
 * before it reads the stamp, so a hardware start cannot slip past a yield.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "aa_hook.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* CLOCK_MONOTONIC = uptime on QNX: the renderer compares the stamp with its own clock. */
static uint64_t mono_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)(t.tv_nsec / 1000000L);
}

/* pid in the renderer's active file: 0 = none or stale, -1 = present but not readable yet. */
static int active_pid(void)
{
    char b[32];
    int fd, pid = 0;
    ssize_t n;
    fd = open(AA_HW_ACTIVE_PATH, O_RDONLY);
    if (fd < 0) return 0;
    n = read(fd, b, sizeof(b) - 1u);
    close(fd);
    if (n <= 0) return -1;                                    /* being written: treat as active */
    b[n] = 0;
    if (sscanf(b, "pid=%d", &pid) != 1 || pid <= 0) return -1;
    if (kill((pid_t)pid, 0) != 0 && errno == ESRCH) return 0;  /* stale: that renderer is gone */
    return pid;
}

int aa_hw_yield(const char *why)
{
    char b[32];
    int fd, n, pid;
    uint64_t t0 = mono_ms();

    n = snprintf(b, sizeof(b), "%llu\n", (unsigned long long)t0);
    fd = open(AA_HW_YIELD_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        aa_log("hw.yield why=%s stamp_failed errno=%d", why, errno);
    } else {
        if (n > 0) (void)!write(fd, b, (size_t)n);
        close(fd);
    }
    pid = active_pid();
    if (!pid) {
        aa_log("hw.yield why=%s renderer=no_hw_decoder", why);
        return AA_YIELD_NONE;
    }
    while (pid && mono_ms() - t0 < AA_HW_YIELD_WAIT_MS) {
        usleep(20000);
        pid = active_pid();
    }
    if (pid) {
        aa_log("hw.yield why=%s renderer=%d still holds the decoder after %u ms; continuing",
               why, pid, (unsigned)AA_HW_YIELD_WAIT_MS);
        return AA_YIELD_TIMEOUT;
    }
    aa_log("hw.yield why=%s renderer released the decoder in %llu ms", why,
           (unsigned long long)(mono_ms() - t0));
    return AA_YIELD_RELEASED;
}
