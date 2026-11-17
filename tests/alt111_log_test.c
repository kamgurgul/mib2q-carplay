/* altscreen111.log: callers never do file I/O once the writer runs (the AirPlay
 * control path, car logs 2026-11-17), and rotation still works where rename() is
 * refused (/tmp is /dev/shmem on the unit). */
#define ALT111_TARGET_MHI2Q 1
#include <errno.h>
#include <stdio.h>
/* Route the hook's rename() to a stub that can refuse, as /dev/shmem does. */
static int g_refuse_rename;
static int test_rename(const char *from, const char *to);
#define rename test_rename
#include "../altscreen_hook/libaltscreen111_gen2.c"
#undef rename
#include <assert.h>
#include <sys/time.h>

static int test_rename(const char *from, const char *to)
{
    if (g_refuse_rename) { errno = ENOSYS; return -1; }
    return rename(from, to);
}

static size_t slurp(const char *path, char *out, size_t cap)
{
    FILE *f = fopen(path, "r");
    size_t n;
    if (!f) { out[0] = 0; return 0; }
    n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = 0;
    return n;
}

static uint64_t now_test_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000u + (uint64_t)tv.tv_usec / 1000u;
}

static char big[2 * 1024 * 1024];

int main(int argc, char **argv)
{
    char logp[512], oldp[512], fifo[512];
    int i, fd;
    uint64_t t0;
    size_t n, got = 0;
    assert(argc == 2);
    alarm(20);
    snprintf(logp, sizeof(logp), "%s/altscreen111.log", argv[1]);
    snprintf(oldp, sizeof(oldp), "%s/altscreen111.log.1", argv[1]);
    snprintf(fifo, sizeof(fifo), "%s/fifo.log", argv[1]);
    unlink(logp); unlink(oldp); unlink(fifo);
    g_log_path = logp;

    /* Before the writer exists (ELF constructor time) a line is written in place. */
    logf_u2("before writer %d", 1);
    slurp(logp, big, sizeof(big));
    assert(strstr(big, "[altscreen111] before writer 1\n"));

    /* Past the cap with rename refused: the old content moves to .1, the file restarts. */
    fd = open(logp, O_WRONLY | O_APPEND);
    assert(fd >= 0);
    memset(big, 'x', U2_LOG_MAX_BYTES);
    big[U2_LOG_MAX_BYTES - 1] = '\n';
    assert(write(fd, big, U2_LOG_MAX_BYTES) == U2_LOG_MAX_BYTES);
    close(fd);
    g_refuse_rename = 1;
    logf_u2("after rotation");
    g_refuse_rename = 0;
    n = slurp(oldp, big, sizeof(big));
    assert(n > U2_LOG_MAX_BYTES && strstr(big, "before writer 1"));
    n = slurp(logp, big, sizeof(big));
    assert(n < 128 && strstr(big, "after rotation") && !strstr(big, "xxxx"));

    /* With the writer running, a log target that blocks every open() (a FIFO with no
     * reader) must not block the caller: 300 lines return at once, the oldest beyond
     * the queue are dropped and counted. */
    assert(mkfifo(fifo, 0644) == 0);
    g_log_path = fifo;
    u2_log_start_writer();
    assert(g_u2_log_writer_pid == getpid());
    t0 = now_test_ms();
    for (i = 0; i < 300; i++) logf_u2("queued line %03d", i);
    assert(now_test_ms() - t0 < 1000);

    fd = open(fifo, O_RDONLY | O_NONBLOCK);
    assert(fd >= 0);
    t0 = now_test_ms();
    big[0] = 0;
    while (now_test_ms() - t0 < 5000 && !strstr(big, "queued line 299\n")) {
        ssize_t r = read(fd, big + got, sizeof(big) - 1 - got);
        if (r > 0) { got += (size_t)r; big[got] = 0; continue; }
        usleep(2000);   /* EOF between batches (the writer closes after each) or EAGAIN */
    }
    close(fd);
    assert(strstr(big, "queued line 299\n"));
    assert(strstr(big, "log queue full: ") && strstr(big, " lines dropped\n"));
    {   /* survivors arrive in order */
        int last = -1;
        const char *p = big;
        while ((p = strstr(p, "queued line ")) != NULL) {
            int v = atoi(p + 12);
            assert(v > last);
            last = v;
            p += 12;
        }
        assert(last == 299);
    }
    unlink(fifo); unlink(logp); unlink(oldp);
    alarm(0);
    puts("altscreen111.log: in place before the writer, rotation without rename, no caller I/O after PASS");
    return 0;
}
