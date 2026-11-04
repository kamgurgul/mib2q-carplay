#include "signal_guard.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int spa_signal_guard_install(spa_signal_guard_t *guard, int signalNumber,
                             const struct sigaction *replacement)
{
    spa_signal_guard_entry_t *entry;
    struct sigaction previous;
    unsigned i;

    if (!guard || !replacement || signalNumber <= 0) {
        errno = EINVAL;
        return -1;
    }
    for (i = 0; i < guard->count; ++i) {
        if (guard->entries[i].installed &&
            guard->entries[i].signalNumber == signalNumber) {
            errno = EBUSY;
            return -1;
        }
    }
    if (guard->count >= SPA_SIGNAL_GUARD_CAPACITY) {
        errno = ENOSPC;
        return -1;
    }
    entry = &guard->entries[guard->count++];
    memset(entry, 0, sizeof(*entry));
    entry->signalNumber = signalNumber;
    if (sigaction(signalNumber, NULL, &previous) != 0) {
        guard->count--;
        return -1;
    }
    entry->previous = previous;
    /* Publish the predecessor before installing a handler which can call
     * spa_signal_guard_chain from another thread. */
    entry->installed = 1;
    if (sigaction(signalNumber, replacement, NULL) != 0) {
        entry->installed = 0;
        guard->count--;
        return -1;
    }
    return 0;
}

void spa_signal_guard_restore(spa_signal_guard_t *guard)
{
    unsigned i;

    if (!guard) return;
    i = guard->count;
    while (i != 0) {
        spa_signal_guard_entry_t *entry = &guard->entries[--i];

        if (entry->installed) {
            (void)sigaction(entry->signalNumber, &entry->previous, NULL);
            entry->installed = 0;
        }
    }
    guard->count = 0;
}

/* Called from the hook's fatal signal handler.  sigaction() and raise() are
 * async-signal-safe.  The current signal remains blocked until our handler
 * returns, so the raised instance is delivered to the restored dio_manager
 * disposition afterwards (including SA_SIGINFO/default/ignore semantics). */
void spa_signal_guard_chain(spa_signal_guard_t *guard, int signalNumber)
{
    size_t i;

    if (guard) {
        for (i = 0; i < guard->count; ++i) {
            spa_signal_guard_entry_t *entry = &guard->entries[i];

            if (entry->installed && entry->signalNumber == signalNumber) {
                (void)sigaction(signalNumber, &entry->previous, NULL);
                entry->installed = 0;
                (void)raise(signalNumber);
                return;
            }
        }
    }
    /* This is reachable only if the guard was corrupted or called for a
     * signal it never installed. Do not invoke non-async-safe recovery code
     * from a fatal handler. */
    _exit(128 + (signalNumber & 0x7f));
}

/* Decimal digits of v, at least minDigits (zero-padded), into out[pos..cap-1). */
static size_t note_put_dec(char *out, size_t cap, size_t pos,
                           unsigned long v, unsigned minDigits)
{
    char digits[20];
    unsigned n = 0;

    do {
        digits[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u && n < sizeof(digits));
    while (n < minDigits && n < sizeof(digits)) digits[n++] = '0';
    while (n != 0u && pos < cap) out[pos++] = digits[--n];
    return pos;
}

static size_t note_put_str(char *out, size_t cap, size_t pos, const char *s)
{
    while (s && *s && pos < cap) out[pos++] = *s++;
    return pos;
}

size_t spa_signal_format_note(char *out, size_t cap, const char *msg)
{
    static const char pidPart[] = " pid=";
    struct timespec ts;
    size_t pos = 0, tail;
    char pidText[24];
    size_t pidLen;

    if (!out || cap < 2u) return 0;
    /* Reserve the " pid=<n>\n" tail first so a long msg cannot push it out. */
    pidLen = note_put_dec(pidText, sizeof(pidText), 0, (unsigned long)getpid(), 1);
    tail = (sizeof(pidPart) - 1u) + pidLen + 1u;
    if (tail >= cap) tail = 1u;            /* no room: keep only the newline */
    cap -= tail;

    pos = note_put_str(out, cap, pos, "t=");
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        pos = note_put_dec(out, cap, pos, (unsigned long)ts.tv_sec, 1);
        pos = note_put_str(out, cap, pos, ".");
        pos = note_put_dec(out, cap, pos, (unsigned long)(ts.tv_nsec / 1000000L), 3);
    } else {
        pos = note_put_str(out, cap, pos, "?");
    }
    pos = note_put_str(out, cap, pos, " ");
    pos = note_put_str(out, cap, pos, msg);

    cap += tail;
    if (tail > 1u) {
        size_t i;
        pos = note_put_str(out, cap, pos, pidPart);
        for (i = 0; i < pidLen && pos < cap; ++i) out[pos++] = pidText[i];
    }
    if (pos < cap) out[pos++] = '\n';
    return pos;
}

int spa_signal_append_note(const char *path, const char *line, size_t len)
{
    size_t done = 0;
    int fd, rc = 0;

    if (!path || !line) return -1;
    fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return -1;
    while (done < len) {
        ssize_t n = write(fd, line + done, len - done);
        if (n > 0) { done += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        rc = -1;
        break;
    }
    if (close(fd) != 0) rc = -1;
    return rc;
}
