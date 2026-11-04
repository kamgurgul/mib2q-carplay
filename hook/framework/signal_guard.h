/* Preserve process-owned signal dispositions around an injected hook policy. */
#ifndef CARPLAY_SIGNAL_GUARD_H
#define CARPLAY_SIGNAL_GUARD_H

#include <sys/types.h>   /* size_t; QNX 6.5's <stddef.h> after <signal.h> does not provide it */
#include <signal.h>

#define SPA_SIGNAL_GUARD_CAPACITY 8u
#define SPA_SIGNAL_NOTE_MAX 160u

typedef struct {
    int signalNumber;
    struct sigaction previous;
    volatile sig_atomic_t installed;
} spa_signal_guard_entry_t;

typedef struct {
    spa_signal_guard_entry_t entries[SPA_SIGNAL_GUARD_CAPACITY];
    unsigned count;
} spa_signal_guard_t;

int spa_signal_guard_install(spa_signal_guard_t *guard, int signalNumber,
                             const struct sigaction *replacement);
void spa_signal_guard_restore(spa_signal_guard_t *guard);
void spa_signal_guard_chain(spa_signal_guard_t *guard, int signalNumber);

/* Fatal-handler helpers: async-signal-safe calls only (clock_gettime, getpid,
 * open, write, close), no stdio or locale.  format builds
 * "t=<CLOCK_MONOTONIC s.ms> <msg> pid=<pid>\n" (the clock of altscreen111.log
 * and aa_cluster_hook.log), truncating msg to fit; returns the length, 0 if
 * out is unusable.  append adds one line to path; 0 when all of it was written. */
size_t spa_signal_format_note(char *out, size_t cap, const char *msg);
int spa_signal_append_note(const char *path, const char *line, size_t len);

#endif /* CARPLAY_SIGNAL_GUARD_H */
