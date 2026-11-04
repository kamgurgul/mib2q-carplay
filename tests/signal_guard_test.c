#include "framework/signal_guard.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static spa_signal_guard_t guard;
static volatile sig_atomic_t hookCalls;
static volatile sig_atomic_t ownerCalls;

static void owner_handler(int signalNumber)
{
    (void)signalNumber;
    ownerCalls++;
}

static void hook_handler(int signalNumber)
{
    hookCalls++;
    spa_signal_guard_chain(&guard, signalNumber);
}

static int set_handler(void (*handler)(int))
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    return sigaction(SIGUSR1, &action, NULL);
}

/* "t=<digits>.<3 digits> <msg> pid=<pid>\n", or a cut msg with the pid tail kept. */
static int note_format(void)
{
    char line[SPA_SIGNAL_NOTE_MAX], tail[32], small[24];
    const char *p;
    size_t n, tailLen;

    tailLen = (size_t)snprintf(tail, sizeof(tail), " pid=%ld\n", (long)getpid());
    n = spa_signal_format_note(line, sizeof(line), "[hook] FATAL SIGSEGV in dio_manager");
    if (n == 0 || n >= sizeof(line)) return 10;
    line[n] = '\0';
    if (strncmp(line, "t=", 2) != 0) return 11;
    p = line + 2;
    while (*p >= '0' && *p <= '9') p++;
    if (p == line + 2 || p[0] != '.' || p[1] < '0' || p[1] > '9' || p[2] < '0' || p[2] > '9'
        || p[3] < '0' || p[3] > '9' || p[4] != ' ') return 12;
    if (strncmp(p + 5, "[hook] FATAL SIGSEGV in dio_manager", 35) != 0) return 13;
    if (n < tailLen || strcmp(line + n - tailLen, tail) != 0) return 14;

    /* A long message is cut, the pid and newline stay. */
    n = spa_signal_format_note(small, sizeof(small),
                               "a message far longer than the twenty-four byte buffer");
    if (n == 0 || n > sizeof(small)) return 15;
    if (tailLen < sizeof(small) - 8u
        && (n < tailLen || memcmp(small + n - tailLen, tail, tailLen) != 0)) return 16;
    if (small[n - 1] != '\n') return 17;

    /* Too small for the tail: still one terminated line; unusable buffers give 0. */
    n = spa_signal_format_note(small, 4, "xyz");
    if (n == 0 || n > 4 || small[n - 1] != '\n') return 18;
    if (spa_signal_format_note(small, 1, "x") != 0) return 19;
    if (spa_signal_format_note(NULL, 8, "x") != 0) return 20;
    return 0;
}

/* Two appends give two lines; a missing directory fails without crashing. */
static int note_append(void)
{
    char path[] = "/tmp/signal_note_XXXXXX";
    char buf[64];
    FILE *f;
    size_t n;
    int fd = mkstemp(path);

    if (fd < 0) return 30;
    close(fd);
    if (spa_signal_append_note(path, "one\n", 4) != 0) return 31;
    if (spa_signal_append_note(path, "two\n", 4) != 0) return 32;
    f = fopen(path, "r");
    if (!f) return 33;
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    unlink(path);
    buf[n] = '\0';
    if (strcmp(buf, "one\ntwo\n") != 0) return 34;
    if (spa_signal_append_note("/nonexistent-dir/x.log", "x\n", 2) != -1) return 35;
    if (spa_signal_append_note(NULL, "x\n", 2) != -1) return 36;
    return 0;
}

int main(void)
{
    struct sigaction action;
    int rc;

    if ((rc = note_format()) != 0) return rc;
    if ((rc = note_append()) != 0) return rc;

    if (set_handler(owner_handler) != 0) return 1;
    memset(&action, 0, sizeof(action));
    action.sa_handler = hook_handler;
    sigemptyset(&action.sa_mask);
    if (spa_signal_guard_install(&guard, SIGUSR1, &action) != 0) return 2;
    if (raise(SIGUSR1) != 0) return 3;
    if (hookCalls != 1 || ownerCalls != 1) return 4;

    if (spa_signal_guard_install(&guard, SIGUSR1, &action) != 0) return 5;
    spa_signal_guard_restore(&guard);
    if (raise(SIGUSR1) != 0) return 6;
    if (hookCalls != 1 || ownerCalls != 2) return 7;

    puts("signal_guard_test: all tests passed");
    return 0;
}
