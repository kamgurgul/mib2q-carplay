/*
 * Host test for aa_hook/aa_yield.c, the gal side of the main-screen video yield.
 *
 * The stamp must be the uptime (CLOCK_MONOTONIC ms) in the format altscreen_render reads
 * ("%llu"), written before the active file is checked. No renderer decoder, or an active file
 * naming a dead pid, returns at once; a live renderer that lets go ends the wait; one that does
 * not is waited for at most AA_HW_YIELD_WAIT_MS; a half-written active file counts as active.
 * Build with -DAA_HW_YIELD_PATH, -DAA_HW_ACTIVE_PATH (temp files) and a short -DAA_HW_YIELD_WAIT_MS.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "aa_hook.h"

#undef NDEBUG
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

void aa_log(const char *fmt, ...) { (void)fmt; }

static uint64_t now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)(t.tv_nsec / 1000000L);
}

static void put(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert(fd >= 0);
    assert(write(fd, text, strlen(text)) == (ssize_t)strlen(text));
    close(fd);
}

/* The stamp as the renderer's hw_yield_until() parses it. */
static unsigned long long stamp(void)
{
    char b[32];
    unsigned long long t = 0;
    ssize_t n;
    int fd = open(AA_HW_YIELD_PATH, O_RDONLY);
    assert(fd >= 0);
    n = read(fd, b, sizeof(b) - 1);
    close(fd);
    assert(n > 0);
    b[n] = 0;
    assert(sscanf(b, "%llu", &t) == 1 && t);
    return t;
}

static void *release_later(void *arg)
{
    (void)arg;
    usleep(150000);
    unlink(AA_HW_ACTIVE_PATH);           /* the renderer suspended its decoder */
    return NULL;
}

int main(void)
{
    char me[32];
    uint64_t t0, t1;
    pthread_t t;
    pid_t child;
    int rc;

    unlink(AA_HW_YIELD_PATH);
    unlink(AA_HW_ACTIVE_PATH);
    snprintf(me, sizeof(me), "pid=%d\n", (int)getpid());

    /* No renderer decoder: stamp written, no wait. */
    t0 = now();
    rc = aa_hw_yield("test");
    t1 = now();
    assert(rc == AA_YIELD_NONE && t1 - t0 < 100);
    assert(stamp() >= t0 && stamp() <= t1);

    /* Active file left by a renderer that is gone: stale, no wait. */
    child = fork();
    assert(child >= 0);
    if (!child) _exit(0);
    assert(waitpid(child, NULL, 0) == child);
    {
        char dead[32];
        snprintf(dead, sizeof(dead), "pid=%d\n", (int)child);
        put(AA_HW_ACTIVE_PATH, dead);
    }
    t0 = now();
    assert(aa_hw_yield("test") == AA_YIELD_NONE && now() - t0 < 100);

    /* A live renderer that lets go after 150 ms: the wait ends then, well before the bound. */
    put(AA_HW_ACTIVE_PATH, me);
    assert(pthread_create(&t, NULL, release_later, NULL) == 0);
    t0 = now();
    rc = aa_hw_yield("test");
    t1 = now();
    pthread_join(t, NULL);
    assert(rc == AA_YIELD_RELEASED);
    assert(t1 - t0 >= 120 && t1 - t0 < AA_HW_YIELD_WAIT_MS);
    assert(stamp() >= t0 && stamp() < t0 + 100);   /* stamped before the wait, not after it */

    /* A live renderer that keeps it: bounded, then gal goes on. */
    put(AA_HW_ACTIVE_PATH, me);
    t0 = now();
    rc = aa_hw_yield("test");
    t1 = now();
    assert(rc == AA_YIELD_TIMEOUT);
    assert(t1 - t0 >= AA_HW_YIELD_WAIT_MS && t1 - t0 < AA_HW_YIELD_WAIT_MS + 200);

    /* Being written (empty): treated as active, like the CarPlay hook does. */
    put(AA_HW_ACTIVE_PATH, "");
    assert(aa_hw_yield("test") == AA_YIELD_TIMEOUT);
    unlink(AA_HW_ACTIVE_PATH);

    printf("aa_yield_test: stamp format/order, no decoder, stale pid, release, bounded wait, "
           "half-written active file PASS\n");
    return 0;
}
