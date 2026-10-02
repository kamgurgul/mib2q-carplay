/*
 * altscreen_render — CarPlay cluster video renderer for Audi MHI2Q.
 *
 * Pipeline:
 *   AltScreen hook tee (127.0.0.1:19820, Annex-B H.264)
 *     -> decode.c (libavcodec)               [host-tested]
 *     -> video_gles.c (YUV->RGB, GLES2)       [QNX]
 *     -> cluster_surface managed window, displayable 99   [QNX]
 *     -> DisplayManager composites ctx 82 -> MOST/LVDS -> Virtual Cockpit
 *
 * Java (mib2q-carplay-rgi) owns cluster CONTEXT selection. This process only
 * owns the pixels of displayable 99 and reports liveness so Java can switch to
 * the CarPlay-video context. Liveness signal: /tmp/altscreen_render.live holds
 * "pid=<pid>" and is rewritten while frames are swapped to the screen. Java
 * requires a fresh mtime and a live pid, so a crash cannot pin ctx 81/82.
 * /tmp/altscreen_render.status carries detail.
 *
 * On a non-QNX host this builds without GL/EGL and runs as a headless tee sink
 * (connect + decode + frame count), which is useful over an SSH port-forward.
 *
 * Env:
 *   ALTR_TEE_HOST   (default 127.0.0.1)
 *   ALTR_TEE_PORT   (default 19820)
 *   ALTR_WIDTH      cluster surface width  (default 1440)   [QNX]
 *   ALTR_HEIGHT     cluster surface height (default 540)    [QNX]
 *   ALTR_DISPLAYABLE_ID (default 99)                         [QNX]
 *   ALTR_LIVE_FILE  (default /tmp/altscreen_render.live)
 *   ALTR_STATUS_FILE(default /tmp/altscreen_render.status)
 *   ALTR_LIVE_TIMEOUT_MS (default 500) — .live removed if no frame within this
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "decode.h"
#include "video_gles.h"

#ifdef PLATFORM_QNX
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "cluster_surface.h"
#endif

static volatile int g_stop = 0;
static void on_signal(int s){ (void)s; g_stop = 1; }

static const char *env_s(const char *k, const char *d){ const char *v=getenv(k); return (v&&*v)?v:d; }
static int env_i(const char *k, int d){ const char *v=getenv(k); return (v&&*v)?atoi(v):d; }

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*1000u + (uint64_t)(ts.tv_nsec/1000000L);
}

/* ---- liveness / status files (read by the Java cluster context module) ---- */
static const char *g_live_file;
static const char *g_status_file;
static int g_live_written;
static uint64_t g_live_touch_ms;

static void write_status(const char *state, int w, int h, uint64_t frames)
{
    int fd = open(g_status_file, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd < 0) return;
    char b[192];
    int n = snprintf(b, sizeof(b), "state=%s\nwidth=%d\nheight=%d\nframes=%llu\npid=%d\n",
                     state, w, h, (unsigned long long)frames, (int)getpid());
    if (n > 0) (void)!write(fd, b, (size_t)n);
    close(fd);
}

/* Heartbeat, not a create-once flag. set_live(0) always unlinks, including a
 * file left by a previous process that this one did not create. */
static void set_live(int live)
{
    if (live) {
        char b[32];
        int n, fd;
        uint64_t now = now_ms();
        if (g_live_written && (now - g_live_touch_ms) < 200) return;
        n = snprintf(b, sizeof(b), "pid=%d\n", (int)getpid());
        fd = open(g_live_file, O_WRONLY|O_CREAT|O_TRUNC, 0644);
        if (fd >= 0) {
            if (n > 0) (void)!write(fd, b, (size_t)n);
            close(fd);
            g_live_written = 1;
            g_live_touch_ms = now;
        }
    } else {
        if (g_live_file) unlink(g_live_file);
        g_live_written = 0;
    }
}

/* ---- decode callback ---- */
struct app {
    int surface_w, surface_h;
    int last_w, last_h;
    uint64_t frames;
    uint64_t last_frame_ms;
#ifdef PLATFORM_QNX
    altr_gles_t *gl;
    EGLDisplay dpy;
    EGLSurface surf;
    EGLContext ctx;
    EGLConfig ecfg;
    cluster_surface_t *cs;
    int have_gl;
    int egl_ready;
    uint64_t last_probe_ms;   /* throttle for cluster_surface_lost() */
    int probe_now;            /* force a probe after a failed present */
#endif
};

#ifdef PLATFORM_QNX
static int present_frame(struct app *a, const altr_frame_t *f);
static void gl_shutdown(struct app *a);
#endif

static void on_frame(void *user, const altr_frame_t *f)
{
    struct app *a = (struct app *)user;
    a->frames++;
    a->last_w = f->width; a->last_h = f->height;
    a->last_frame_ms = now_ms();
#ifdef PLATFORM_QNX
    if (a->have_gl && a->gl && present_frame(a, f) == 0)
        set_live(1);
#else
    set_live(1);
#endif
    if (a->frames <= 2 || (a->frames % 120) == 0)
        write_status("decoding", a->last_w, a->last_h, a->frames);
}

/* ---- tee client ---- */
static int tee_connect(const char *host, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = inet_addr(host);
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) { close(fd); return -1; }
    return fd;
}

#ifdef PLATFORM_QNX
/* Bring up EGL on the managed cluster window (displayable 99). Mirrors the
 * proven maneuver_render sequence: create the screen context (cluster_surface)
 * BEFORE eglGetDisplay, because the Adreno/GSL libEGL derefs a screen context
 * that must already exist in the process. */
static int gl_init(struct app *a, int displayable_id)
{
    cluster_surface_cfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.id = displayable_id;
    cfg.width = a->surface_w;
    cfg.height = a->surface_h;
    cfg.format = 8;      /* SCREEN_FORMAT_RGBA8888 */
    cfg.usage = 0x20;    /* SCREEN_USAGE_OPENGL_ES2 */
    cfg.nbuffers = 2;
    cfg.transparent = 0; /* opaque video plane */
    a->cs = cluster_surface_create(&cfg);
    if (!a->cs) { fprintf(stderr, "altscreen_render: cluster_surface_create failed\n"); return -1; }

    a->dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (a->dpy == EGL_NO_DISPLAY) { fprintf(stderr, "altscreen_render: eglGetDisplay failed\n"); return -1; }
    EGLint maj, min;
    if (!eglInitialize(a->dpy, &maj, &min)) { fprintf(stderr, "altscreen_render: eglInitialize failed\n"); return -1; }
    a->egl_ready = 1;

    EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig ec; EGLint ncfg = 0;
    if (!eglChooseConfig(a->dpy, cfg_attr, &ec, 1, &ncfg) || ncfg == 0) {
        fprintf(stderr, "altscreen_render: eglChooseConfig failed\n"); return -1;
    }
    a->ecfg = ec;
    EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    eglBindAPI(EGL_OPENGL_ES_API);
    a->ctx = eglCreateContext(a->dpy, ec, EGL_NO_CONTEXT, ctx_attr);
    if (a->ctx == EGL_NO_CONTEXT) { fprintf(stderr, "altscreen_render: eglCreateContext failed\n"); return -1; }
    a->surf = eglCreateWindowSurface(a->dpy, ec,
                (EGLNativeWindowType)cluster_surface_window(a->cs), NULL);
    if (a->surf == EGL_NO_SURFACE) { fprintf(stderr, "altscreen_render: eglCreateWindowSurface failed\n"); return -1; }
    if (!eglMakeCurrent(a->dpy, a->surf, a->surf, a->ctx)) {
        fprintf(stderr, "altscreen_render: eglMakeCurrent failed\n"); return -1;
    }
    eglSwapInterval(a->dpy, 1);
    a->gl = altr_gles_create(a->surface_w, a->surface_h);
    if (!a->gl) { fprintf(stderr, "altscreen_render: gles create failed\n"); return -1; }
    a->have_gl = 1;
    fprintf(stderr, "altscreen_render: GL up on displayable %d %dx%d (EGL %d.%d)\n",
            displayable_id, a->surface_w, a->surface_h, maj, min);
    return 0;
}

/* Drop the EGL surface bound to a window the display manager has disowned,
 * open a new managed window, and rebind. cluster_surface_recreate rate-limits
 * itself to one attempt per 100 ms. */
static int rebind_window(struct app *a)
{
    fprintf(stderr, "altscreen_render: recreating cluster window\n");
    if (a->dpy != EGL_NO_DISPLAY && a->surf != EGL_NO_SURFACE) {
        eglMakeCurrent(a->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(a->dpy, a->surf);
        a->surf = EGL_NO_SURFACE;
    }
    if (!a->cs || cluster_surface_recreate(a->cs) != 0) return -1;
    a->surf = eglCreateWindowSurface(a->dpy, a->ecfg,
                (EGLNativeWindowType)cluster_surface_window(a->cs), NULL);
    if (a->surf == EGL_NO_SURFACE) return -1;
    if (!eglMakeCurrent(a->dpy, a->surf, a->surf, a->ctx)) {
        eglDestroySurface(a->dpy, a->surf);
        a->surf = EGL_NO_SURFACE;
        return -1;
    }
    eglSwapInterval(a->dpy, 1);
    return 0;
}

/* cluster_surface.h documents cluster_surface_lost() as a ~5 s probe: it is two
 * synchronous Screen IPC round-trips. Calling it per frame stalls this renderer
 * (the hook's AU queue then overflows and it resets our consumer) AND hammers the
 * DisplayManager that also serves the main display. Probe on the documented
 * cadence, plus immediately when the surface is already gone or the previous
 * present failed - a failed draw/swap is the real signal of a disowned window. */
#define ALTR_LOST_PROBE_MS 5000u

/* Calibration ruler toggle (mods/altscreen_grid). Checked once a second, not per
 * frame, so it costs nothing in normal use. */
#define ALTR_GRID_MARKER "/mnt/app/root/altscreen_render.grid"
static int grid_enabled(uint64_t now)
{
    static uint64_t checked_ms;
    static int on;
    if (!checked_ms || now - checked_ms >= 1000u) {
        checked_ms = now;
        on = access(ALTR_GRID_MARKER, F_OK) == 0;
    }
    return on;
}

static int present_frame(struct app *a, const altr_frame_t *f)
{
    uint64_t now = now_ms();
    int lost = 0;

    if (a->surf == EGL_NO_SURFACE || a->probe_now
        || (now - a->last_probe_ms) >= ALTR_LOST_PROBE_MS) {
        a->last_probe_ms = now;
        a->probe_now = 0;
        lost = (a->surf == EGL_NO_SURFACE) || cluster_surface_lost(a->cs);
    }
    if (lost && rebind_window(a) != 0) { a->probe_now = 1; return -1; }
    if (altr_gles_draw(a->gl, f) != 0)  { a->probe_now = 1; return -1; }
    if (grid_enabled(now)) altr_gles_draw_grid(a->gl);
    if (!eglSwapBuffers(a->dpy, a->surf)) { a->probe_now = 1; return -1; }
    return 0;
}

static void gl_shutdown(struct app *a)
{
    if (a->gl) { altr_gles_destroy(a->gl); a->gl = NULL; }
    if (a->dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(a->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (a->surf != EGL_NO_SURFACE) eglDestroySurface(a->dpy, a->surf);
        if (a->ctx != EGL_NO_CONTEXT) eglDestroyContext(a->dpy, a->ctx);
        if (a->egl_ready) eglTerminate(a->dpy);
    }
    a->surf = EGL_NO_SURFACE;
    a->ctx = EGL_NO_CONTEXT;
    a->dpy = EGL_NO_DISPLAY;
    a->egl_ready = 0;
    if (a->cs) { cluster_surface_destroy(a->cs); a->cs = NULL; }
    a->have_gl = 0;
}
#endif

int main(void)
{
    struct app a;
    memset(&a, 0, sizeof(a));
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    const char *host = env_s("ALTR_TEE_HOST", "127.0.0.1");
    int port = env_i("ALTR_TEE_PORT", 19820);
    g_live_file   = env_s("ALTR_LIVE_FILE",   "/tmp/altscreen_render.live");
    g_status_file = env_s("ALTR_STATUS_FILE", "/tmp/altscreen_render.status");
    unlink(g_live_file); /* a predecessor crash must not pin ctx 81/82 */
    int live_timeout = env_i("ALTR_LIVE_TIMEOUT_MS", 500);
    a.surface_w = env_i("ALTR_WIDTH", 1440);
    a.surface_h = env_i("ALTR_HEIGHT", 540);

#ifdef PLATFORM_QNX
    a.dpy = EGL_NO_DISPLAY;
    a.surf = EGL_NO_SURFACE;
    a.ctx = EGL_NO_CONTEXT;
    if (gl_init(&a, env_i("ALTR_DISPLAYABLE_ID", 99)) != 0) {
        write_status("gl_error", 0, 0, 0);
        gl_shutdown(&a);
        set_live(0);
        return 1;
    }
#else
    fprintf(stderr, "altscreen_render: host build (headless tee sink, no GL)\n");
#endif

    altr_decoder_t *dec = altr_decode_create(on_frame, &a);
    if (!dec) {
        write_status("decoder_error", 0, 0, 0);
        set_live(0);
#ifdef PLATFORM_QNX
        gl_shutdown(&a);
#endif
        return 1;
    }

    write_status("connecting", 0, 0, 0);

    uint8_t buf[32768];
    while (!g_stop) {
        int fd = tee_connect(host, port);
        if (fd < 0) {
            set_live(0);
            write_status("waiting_tee", a.last_w, a.last_h, a.frames);
            sleep(1);
            continue;
        }
        fprintf(stderr, "altscreen_render: connected to tee %s:%d\n", host, port);
        write_status("connected", a.last_w, a.last_h, a.frames);

        /* Recv loop with a poll so we can drop .live when frames stall and exit
         * on signal. */
        for (;;) {
            if (g_stop) { close(fd); goto done; }
            fd_set rd; struct timeval tv;
            FD_ZERO(&rd); FD_SET(fd, &rd);
            tv.tv_sec = 0; tv.tv_usec = 200000;
            int r = select(fd + 1, &rd, NULL, NULL, &tv);
            if (r > 0 && FD_ISSET(fd, &rd)) {
                ssize_t n = recv(fd, buf, sizeof(buf), 0);
                if (n <= 0) break;
                altr_decode_feed(dec, buf, (size_t)n);
            } else if (r < 0 && errno != EINTR) {
                break;
            }
            if (a.last_frame_ms && (now_ms() - a.last_frame_ms) > (uint64_t)live_timeout)
                set_live(0);
        }

        close(fd);
        altr_decode_flush(dec);
        set_live(0);
        write_status("tee_closed", a.last_w, a.last_h, a.frames);
        fprintf(stderr, "altscreen_render: tee closed; frames so far=%llu\n",
                (unsigned long long)a.frames);
    }

done:
    set_live(0);
    write_status("stopped", a.last_w, a.last_h, a.frames);
    altr_decode_destroy(dec);
#ifdef PLATFORM_QNX
    gl_shutdown(&a);
#endif
    return 0;
}
