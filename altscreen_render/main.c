/*
 * altscreen_render — phone cluster video renderer for Audi MHI2Q
 * (CarPlay AltScreen and the Android Auto cluster display).
 *
 * Pipeline:
 *   loopback Annex-B H.264 tee, one of:
 *     - CarPlay AltScreen hook (dio_manager)   127.0.0.1:19820, picture = window
 *     - Android Auto cluster hook (gal)        127.0.0.1:19821, 1920x1080 picture
 *       whose centred 1440x540 viewport is the cockpit terminal
 *     -> hw_decode.c (Qualcomm OMX, blit of the view 1:1)   [QNX, default]
 *        or decode.c (libavcodec) + video_gles.c          [host-tested / QNX]
 *     -> cluster_surface managed window, displayable 99   [QNX]
 *     -> DisplayManager composites ctx 81/82 -> MOST/LVDS -> Virtual Cockpit
 *
 * Only one phone session exists at a time, so this one process owns window 99
 * for both: while idle it tries the CarPlay tee, then the Android Auto tee.
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
 *   ALTR_TEE_PORT   CarPlay tee (default 19820)
 *   ALTR_AA_TEE_PORT Android Auto tee (default 19821, 0 = off)
 *   ALTR_AA_STREAM_WIDTH / ALTR_AA_STREAM_HEIGHT  AA picture (default 1920x1080)
 *   ALTR_AA_CROP_X / ALTR_AA_CROP_Y  AA view origin (default: centred)
 *   ALTR_WIDTH      cluster surface width  (default 1440)   [QNX]
 *   ALTR_HEIGHT     cluster surface height (default 540)    [QNX]
 *   ALTR_DISPLAYABLE_ID (default 99)                         [QNX]
 *   ALTR_LIVE_FILE  (default /tmp/altscreen_render.live)
 *   ALTR_STATUS_FILE(default /tmp/altscreen_render.status)
 *   ALTR_LIVE_TIMEOUT_MS (default 0 = while connected) — .live removed if no frame
 *                   within this. Apple Maps without a route sends pictures only when
 *                   the map changes (issue #39: 0 pictures in 5.5 s while connected,
 *                   North Up parked even longer), so a picture gap is not a lost
 *                   stream; the tee closing is (the hook closes it whenever stream
 *                   111 ends)
 *   ALTR_AA_LIVE_TIMEOUT_MS (default 0 = while connected) — same for Android Auto,
 *                   which sends almost no pictures while its map does not change
 *                   (car log 020: one picture every ~6 s)
 *   ALTR_AA_DECODE_THREADS (default 2, 1..4) — frame threads of the Android Auto
 *                   software decoder (fallback when hardware decode is off); 1 = the
 *                   low-delay single-thread decoder of 1.3.1 (issue #44)
 *   ALTR_AA_SW_FAST (default 0) — 1 = the Android Auto software decoder skips the deblocking
 *                   filter (about a third less CPU). Off by default: without the in-loop
 *                   filter the picture drifts from the encoder's until the next key frame,
 *                   and Android Auto sends one only every minute or two
 *   /mnt/app/root/altscreen_render.noasm (or /tmp/...) — FFmpeg's plain C code instead of its
 *                   ARM/NEON assembly (GEM "Software decoder plain C"). The renderer creates
 *                   the /tmp one itself after an illegal instruction, then restarts on C code
 *   ALTR_KEYFRAME_GAP_MS (default 2000) — at most one key-frame request per this many ms
 *                   over the tee back-channel (one byte 'K' to the hook) when the decoder
 *                   reports a damaged picture
 *   ALTR_HW_MIN_UPTIME_S (default 120) — with the marker
 *                   /mnt/app/root/altscreen_render.hwdecode.delayed (GEM toggle) only:
 *                   no hardware decoder before this uptime, software decode until then,
 *                   then one reconnect to hardware. CarPlay and Android Auto alike
 *   ALTR_HW_YIELD_HOLD_S (default 3, min 1) — after a hook announces a main-screen
 *                   video change (/tmp/altscreen_render.hw_yield: the CarPlay hook for
 *                   stock's stream 110, the Android Auto hook for gal's main sink) the
 *                   hardware decoder is suspended (last picture kept on the cockpit) and
 *                   none is opened until this long after the last one
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
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
#include "omx_probe.h"
#include "hw_decode.h"
#include "policy.h"

#include <libavutil/cpu.h>

#ifdef PLATFORM_QNX
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <sys/neutrino.h>   /* setprio() */
#include <sys/syspage.h>    /* includes arm/syspage.h: ARM_CPU_FLAG_NEON */
#include "cluster_surface.h"
#endif

static volatile int g_stop = 0;

/* One-shot hardware-decoder probe (GEM CarPlay-RGI -> "HW decoder probe"):
 * the marker is consumed and the result goes to altscreen_render.log. Checked
 * at most once a second, from both the idle and the connected loop. */
#define ALTR_OMX_PROBE_MARKER "/mnt/app/root/altscreen_render.omxprobe"
static void omx_probe_poll(int w, int h)
{
    static time_t last;
    time_t now = time(NULL);
    if (now == last) return;
    last = now;
    if (access(ALTR_OMX_PROBE_MARKER, F_OK) != 0) return;
    unlink(ALTR_OMX_PROBE_MARKER);
    altr_omx_probe(w, h);
}
static void on_signal(int s){ (void)s; g_stop = 1; }

/* FFmpeg's ARM/NEON assembly (1.5) has no run-time CPU detection on QNX. Two guards beside
 * the syspage check in main(): the marker selects FFmpeg's plain C code, and an illegal
 * instruction creates the /tmp marker and exits, so carplay_monitor.sh restarts the renderer
 * on C code instead of into the same fault. Async-signal-safe calls only. */
#ifdef PLATFORM_QNX
#define ALTR_NOASM_MARKER "/mnt/app/root/altscreen_render.noasm"
#define ALTR_NOASM_BOOT   "/tmp/altscreen_render.noasm"
static void on_sigill(int s)
{
    static const char msg[] = "altscreen_render: illegal instruction; plain C decoder from the next start ("
                              ALTR_NOASM_BOOT ")\n";
    int fd = open(ALTR_NOASM_BOOT, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    (void)s;
    if (fd >= 0) close(fd);
    (void)!write(2, msg, sizeof(msg) - 1);
    _exit(4);
}
#endif

static const char *env_s(const char *k, const char *d){ const char *v=getenv(k); return (v&&*v)?v:d; }
static int env_i(const char *k, int d){ const char *v=getenv(k); return (v&&*v)?atoi(v):d; }

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*1000u + (uint64_t)(ts.tv_nsec/1000000L);
}

/* ---- tee sources ---- */

/* Hardware decode is the DEFAULT (log set 018: 3120 pictures in one session at
 * ~2.9 ms renderer CPU per picture vs ~24 ms in software). A marker turns it
 * OFF (GEM CarPlay-RGI -> "HW decoder ON / OFF"); a real hardware failure
 * creates it, so later sessions stay on the software path until switched back
 * on. Read per tee connection. Each source has its own pair, so an Android Auto
 * (1080p) decoder failure never switches CarPlay's proven path off. An Android
 * Auto failure writes only the /tmp twin: software decode cannot keep up with its
 * 1080p30 stream (no NEON in this FFmpeg build), the tee falls behind and the
 * cockpit picture freezes until the phone's next key frame (issue #44), so every
 * boot tries hardware again. */
#define ALTR_HWDEC_OFF_MARKER "/mnt/app/root/altscreen_render.hwdecode.off"
/* /mnt/app is normally read-only to us; the /tmp twin always works and keeps a
 * failed hardware path off for the rest of this boot either way. */
#define ALTR_HWDEC_OFF_BOOT   "/tmp/altscreen_render.hwdecode.off"
#define ALTR_AA_HWDEC_OFF_MARKER "/mnt/app/root/altscreen_render.aa.hwdecode.off"
#define ALTR_AA_HWDEC_OFF_BOOT   "/tmp/altscreen_render.aa.hwdecode.off"

struct source {
    const char *name;           /* status/log name */
    int port;
    altr_view_t view;           /* what this stream shows in window 99 */
    int live_timeout_ms;        /* .live removed this long after the last shown picture; 0 = never while connected */
    const char *hw_off_marker;  /* persistent "hardware decode off" switch */
    const char *hw_off_boot;    /* the same for this boot (/tmp) */
    const char *hw_settle_marker; /* present => wait ALTR_HW_MIN_UPTIME_S before hardware (NULL = always) */
    int hw_hang_persists;       /* a hung video core also writes hw_off_marker (else this boot only) */
    int sw_threads;             /* software decoder frame threads; 1 = low delay */
    int sw_fast;                /* software decoder skips deblocking (altr_decode_create_opts) */
};

/* Delayed hardware start (GEM "HW decoder delayed start"): both sources start on hardware at
 * once (CarPlay as upstream mib2q-carplay-rgi), the marker brings back the boot settle time for
 * units that still freeze when the decoder opens early (issues #10, #14). Android Auto waited
 * always until 1.4: its 1080p decoder, opened together with gal's main-screen decoder at a
 * session start, hung the video core (#14). The gal hook's main-screen yield now keeps the two
 * apart, and the settle time's reconnect cost the whole session (#48). */
#define ALTR_HW_SETTLE_MARKER "/mnt/app/root/altscreen_render.hwdecode.delayed"

/* ---- liveness / status files (read by the Java cluster context module) ---- */
static const char *g_live_file;
static const char *g_status_file;
static int g_live_written;
static uint64_t g_live_touch_ms;

static const char *g_source_name = "none";
/* Status detail for the GEM status page (mods/altscreen_status): which decoder runs and why
 * hardware is off, if it is. */
static const char *decoder_state(void);      /* hardware | software | suspended | frozen | none */
static char g_hw_off_reason[96];             /* marker that keeps hardware off, "" = none */
static unsigned long g_connections;          /* tee connections this process */
static unsigned long g_keyframe_requests;    /* 'K' sent over the tee back-channel */
static unsigned long g_damaged;              /* damaged pictures reported by a decoder */

static void write_status(const char *state, int w, int h, uint64_t frames)
{
    int fd = open(g_status_file, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd < 0) return;
    char b[512];
    int n = snprintf(b, sizeof(b), "state=%s\nsource=%s\nwidth=%d\nheight=%d\nframes=%llu\npid=%d\n"
                     "decoder=%s\nhw_off=%s\nconnections=%lu\nkeyframe_requests=%lu\ndamaged=%lu\n",
                     state, g_source_name, w, h, (unsigned long long)frames, (int)getpid(),
                     decoder_state(), g_hw_off_reason[0] ? g_hw_off_reason : "none", g_connections,
                     g_keyframe_requests, g_damaged);
    if (n >= (int)sizeof(b)) n = (int)sizeof(b) - 1;
    if (n > 0) (void)!write(fd, b, (size_t)n);
    close(fd);
}

/* Heartbeat, not a create-once flag. set_live(0) always unlinks, including a
 * file left by a previous process that this one did not create. */
static void set_live(int live)
{
    if (live) {
        char b[48];
        int n, fd;
        uint64_t now = now_ms();
        static unsigned seq;
        if (g_live_written && (now - g_live_touch_ms) < 200) return;
        /* seq changes on every rewrite: Java counts its own polls since the last change, so
         * a wall-clock step (or a coarse mtime) cannot make a live renderer look stale. */
        n = snprintf(b, sizeof(b), "pid=%d\nseq=%u\n", (int)getpid(), ++seq);
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
    uint64_t last_shown_ms;     /* last picture that reached the window; drives .live */
    const struct source *src;   /* connected tee, NULL while idle */
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
    int disp_id;              /* displayable (99) */
    altr_hwdec_t *hw;         /* hardware decode session, NULL = software path */
    uint64_t hw_frames_seen;
    int hw_tried;             /* hardware attempted on this tee connection */
    int hw_deferred;          /* software decode for now: boot settle time or start cooldown */
    int hw_waiting;           /* main-screen yield: no decoding; hw (if any) suspended, last picture kept */
    uint64_t hw_min_uptime_ms;    /* configured boot settle time (ALTR_HW_MIN_UPTIME_S) */
    uint64_t hw_upgrade_at_ms;    /* deferred: reconnect to hardware at this uptime */
    uint64_t live_hold_until_ms;  /* planned switch to hardware: keep .live fresh until the first picture */
    uint64_t freeze_until_ms;     /* session over: decoder suspended, window 99 keeps the last picture until then */
    const struct source *hw_src;  /* source the hardware decoder (and a frozen picture) belongs to */
#endif
    int tee_fd;                   /* connected tee, -1 while idle (keyframe back-channel) */
    uint64_t keyframe_ms;         /* last key-frame request on this connection */
    uint64_t keyframe_gap_ms;
    unsigned long errors_seen;    /* software decoder error count at the last check */
    unsigned damaged_seen;        /* hardware damaged-picture count at the last check */
};

#ifdef PLATFORM_QNX
static int present_frame(struct app *a, const altr_frame_t *f);
static void gl_shutdown(struct app *a);
#endif

static struct app *g_app;

static const char *decoder_state(void)
{
    struct app *a = g_app;
    if (!a) return "none";
#ifdef PLATFORM_QNX
    if (a->hw && a->freeze_until_ms) return "frozen";
    if (a->hw && a->hw_waiting) return "suspended";
    if (a->hw) return "hardware";
#endif
    return a->src ? "software" : "none";
}

static void on_frame(void *user, const altr_frame_t *f)
{
    struct app *a = (struct app *)user;
    altr_frame_t shown;
    if (a->src) altr_frame_crop(f, &a->src->view, &shown);
    else shown = *f;
    a->frames++;
    a->last_w = f->width; a->last_h = f->height;
    a->last_frame_ms = now_ms();
#ifdef PLATFORM_QNX
    if (a->have_gl && a->gl && present_frame(a, &shown) == 0) {
        a->last_shown_ms = a->last_frame_ms;
        set_live(1);
    }
#else
    a->last_shown_ms = a->last_frame_ms;
    set_live(1);
#endif
    if (a->frames <= 2 || (a->frames % 120) == 0)
        write_status("decoding", a->last_w, a->last_h, a->frames);
}

/* ---- tee client ---- */

#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif

/* Key-frame request over the tee back-channel: one byte 'K' to the hook. The CarPlay hook
 * answers with forceKeyFrame to the iPhone (its serialized command queue); the Android
 * Auto hook re-sends its cached key frame group and, opt-in, asks the phone for one. A
 * hook that predates the back-channel never reads its socket: the byte just waits there
 * (at most one per ALTR_KEYFRAME_GAP_MS). */
static void request_keyframe(struct app *a, const char *why)
{
    static const char k = 'K';
    if (a->tee_fd < 0 || !altr_keyframe_due(now_ms(), &a->keyframe_ms, a->keyframe_gap_ms)) return;
    if (send(a->tee_fd, &k, 1, MSG_DONTWAIT) == 1) {
        g_keyframe_requests++;
        fprintf(stderr, "altscreen_render: key frame requested from %s: %s\n",
                a->src ? a->src->name : "?", why);
    }
}

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

#ifdef PLATFORM_QNX
/* Hardware decode markers: see struct source. */
static int hw_wanted(const struct source *s)
{
    return access(s->hw_off_marker, F_OK) != 0 && access(s->hw_off_boot, F_OK) != 0;
}

/* Hardware decode off after a failure: always for this boot (/tmp); also after a reboot
 * only when the video core hung (altr_hw_fail_persists): until 1.5 any CarPlay failure
 * wrote the persistent marker, so one bad session kept software decode for good. */
static void hw_switch_off(const struct source *s, int video_core_hung)
{
    int fd = open(s->hw_off_boot, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) close(fd);
    snprintf(g_hw_off_reason, sizeof(g_hw_off_reason), "%s", s->hw_off_boot);
    if (!altr_hw_fail_persists(s->hw_hang_persists, video_core_hung)) {
        fprintf(stderr, "altscreen_render: %s hardware decode off for this boot (%s)\n",
                s->name, s->hw_off_boot);
        return;
    }
    fd = open(s->hw_off_marker, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        close(fd);
        snprintf(g_hw_off_reason, sizeof(g_hw_off_reason), "%s", s->hw_off_marker);
        fprintf(stderr, "altscreen_render: %s video hardware hung; hardware decode off also after "
                "a reboot (%s; GEM HW decoder ON clears it)\n", s->name, s->hw_off_marker);
    } else {
        fprintf(stderr, "altscreen_render: %s not writable (errno %d); off for this boot only\n",
                s->hw_off_marker, errno);
    }
}

/* Main-screen video yield (car logs 2026-10-30, save 011). Stock's main-screen
 * decoder and ours share the one Venus video core; stock starting it right after
 * a reverse-camera / CarPlay switch burst, with ours open, hung the core
 * (VCD_HWTimeoutHandler, MMKIVdecDriver_Start TimeOut): black main screen, then
 * the issue #14 freeze and watchdog reboot. The CarPlay hook in dio_manager
 * stamps ALTR_HW_YIELD_FILE with the uptime before stock sets up or tears down
 * the main screen stream, the Android Auto hook in gal before gal's main sink is
 * set up, configured, started or stopped and before the session shuts down; both
 * wait (bounded) while ALTR_HW_ACTIVE_FILE names a live renderer. We suspend the
 * hardware decoder at once (window 99 keeps the
 * last picture, so nothing blinks), drop the stream until the main screen has
 * been quiet for ALTR_HW_YIELD_HOLD_S (stock's decoder is up ~0.5 s after its
 * SETUP), then reconnect for a key frame and resume. Order: we write the active file before reading the yield file, the
 * hook writes the yield file before reading the active file. */
#define ALTR_HW_YIELD_FILE    "/tmp/altscreen_render.hw_yield"
#define ALTR_HW_ACTIVE_FILE   "/tmp/altscreen_render.hw_active"
#define ALTR_HW_YIELD_POLL_MS 40u

static uint64_t g_hw_yield_hold_ms = 3000u;

/* Uptime until which hardware decode stays off, 0 = no hold. Both sides use
 * CLOCK_MONOTONIC (uptime). The file is read at most every 40 ms unless forced. */
static uint64_t hw_yield_until(int force)
{
    static uint64_t checked_ms, until;
    uint64_t now = now_ms();
    if (force || !checked_ms || now - checked_ms >= ALTR_HW_YIELD_POLL_MS) {
        checked_ms = now;
        until = altr_yield_until(ALTR_HW_YIELD_FILE, g_hw_yield_hold_ms, now);
    }
    return until > now ? until : 0;
}

static void hw_active_mark(int on)
{
    if (on) {
        char b[32];
        int n = snprintf(b, sizeof(b), "pid=%d\n", (int)getpid());
        int fd = open(ALTR_HW_ACTIVE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            if (n > 0) (void)!write(fd, b, (size_t)n);
            close(fd);
        }
    } else {
        unlink(ALTR_HW_ACTIVE_FILE);
    }
}

/* Claim the video core before a hardware start: 0 = go ahead (active file
 * written), 1 = a main-screen change keeps hardware off until *until. */
static int hw_yield_claim(uint64_t *until)
{
    hw_active_mark(1);
    *until = hw_yield_until(1);
    if (!*until) return 0;
    hw_active_mark(0);
    return 1;
}

/* Bounded hardware-decoder starts across renderer restarts (policy.c). Failures this
 * process sees already switch hardware decode off (hw_give_up, the watchdog); what they
 * miss is a renderer that dies inside the decoder start or before the first picture:
 * carplay_monitor.sh restarts it and it would try hardware again, and each failed
 * OMX_GetHandle can hold a decoder slot that dio_manager's own decoder then lacks. */
static void hw_starts_path(const struct source *s, char *out, size_t cap)
{
    snprintf(out, cap, "%s.starts", s->hw_off_boot);
}

static void hw_starts_clear(const struct source *s)
{
    char p[160];
    hw_starts_path(s, p, sizeof(p));
    altr_hw_starts_clear(p);
}

/* 0 = go ahead (counted), 1 = software until *defer_until, -1 = hardware off for this boot. */
static int hw_starts_begin(const struct source *s, uint64_t now, uint64_t *defer_until)
{
    char p[160];
    int rc;
    hw_starts_path(s, p, sizeof(p));
    rc = altr_hw_starts_begin(p, s->hw_off_boot, now, defer_until);
    if (rc == ALTR_HW_OFF) {
        snprintf(g_hw_off_reason, sizeof(g_hw_off_reason), "%s", s->hw_off_boot);
        fprintf(stderr, "altscreen_render: %s hardware decoder never produced a picture in "
                "%u starts; hardware decode off for this boot\n", s->name,
                ALTR_HW_START_MAX * ALTR_HW_ROUNDS_MAX);
    } else if (rc == ALTR_HW_DEFER && *defer_until > now) {
        fprintf(stderr, "altscreen_render: %s hardware decoder: %u starts without a picture; "
                "software decode for %llu s to protect the decoder pool\n", s->name,
                ALTR_HW_START_MAX, (unsigned long long)((*defer_until - now + 999u) / 1000u));
    }
    return rc;
}

/* Watchdog for calls into the OMX component. They are IPC to videoCore, which
 * the stock decoders share; once the Venus core hangs (issue #14: slog
 * VCD_HWTimeoutHandler) a call can block for good, and with this process stuck
 * inside videoCore the HMI and the cockpit map froze until the unit rebooted.
 * A call that outlives its deadline switches hardware decode OFF and exits;
 * carplay_monitor.sh then starts a clean renderer on the software path. */
static pthread_mutex_t g_wd_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_wd_deadline_ms;     /* 0 = disarmed */
static const struct source *g_wd_src;
static const char *g_wd_what;
static uint64_t g_wd_live_until_ms;  /* keep .live fresh while blocked in the call, until then */

static void *wd_main(void *arg)
{
    (void)arg;
    for (;;) {
        uint64_t deadline, live_until, now;
        const struct source *s;
        const char *what;
        usleep(250000);
        pthread_mutex_lock(&g_wd_lock);
        deadline = g_wd_deadline_ms;
        live_until = g_wd_live_until_ms;
        s = g_wd_src;
        what = g_wd_what;
        pthread_mutex_unlock(&g_wd_lock);
        now = now_ms();
        /* Java drops ctx 81/82 when .live is >1 s old; decoder setup can take that
         * long on the planned software -> hardware switch (issue #9: 0.3 s Audi map). */
        if (deadline && now < live_until) set_live(1);
        if (!deadline || now < deadline) continue;
        fprintf(stderr, "altscreen_render: %s hardware decoder blocked in %s; HW decoder switched OFF, "
                "exiting for a clean restart\n", s->name, what);
        hw_switch_off(s, 1);
        set_live(0);
        write_status("hw_stuck", 0, 0, 0);
        _exit(3);
    }
    return NULL;
}

static void wd_arm(const struct source *s, const char *what, unsigned ms, uint64_t live_until_ms)
{
    static int started;
    if (!started) {
        pthread_t t;
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        started = pthread_create(&t, &at, wd_main, NULL) == 0;
        pthread_attr_destroy(&at);
        if (!started) fprintf(stderr, "altscreen_render: hardware decoder watchdog not started\n");
    }
    pthread_mutex_lock(&g_wd_lock);
    g_wd_src = s;
    g_wd_what = what;
    g_wd_live_until_ms = live_until_ms;
    g_wd_deadline_ms = now_ms() + ms;
    pthread_mutex_unlock(&g_wd_lock);
}

static void wd_disarm(void)
{
    pthread_mutex_lock(&g_wd_lock);
    g_wd_deadline_ms = 0;
    pthread_mutex_unlock(&g_wd_lock);
}

/* Setup waits up to 2 s for Idle and 2 s for Executing, teardown 2 s each for
 * Idle and Loaded, one feed at most ~1 s of busy input; the deadlines leave margin. */
#define ALTR_WD_CREATE_MS  10000u
#define ALTR_WD_DESTROY_MS 8000u
#define ALTR_WD_FEED_MS    4000u
/* Planned software -> hardware reconnect: .live stays fresh this long without a picture. */
#define ALTR_UPGRADE_LIVE_HOLD_MS 3000u

/* Disconnect freeze. Window 99 is on the cockpit until Java has switched back to the
 * stock context, which it does only after .live has been missing for three 250 ms polls
 * (AltScreenModule MISS_DEBOUNCE). Closing the hardware decoder destroys its window, so
 * the cockpit went black for up to ~1 s at every session end. Now the decoder is
 * suspended (the video core is free for stock at once, as before), .live is dropped,
 * and the window with the last picture is destroyed only after this hold. The software
 * path keeps its GL window anyway. */
#define ALTR_DISCONNECT_FREEZE_MS 1500u

static void hw_close(struct app *a);
static void hw_suspend(struct app *a);

/* Session over on the hardware path: free the video core, keep the last picture. */
static void hw_freeze(struct app *a)
{
    if (!a->hw || a->freeze_until_ms) return;
    hw_suspend(a);
    a->hw_waiting = 0;
    a->freeze_until_ms = now_ms() + ALTR_DISCONNECT_FREEZE_MS;
    fprintf(stderr, "altscreen_render: session over; last picture kept %u ms while the cockpit "
            "returns to the stock map\n", ALTR_DISCONNECT_FREEZE_MS);
}

/* End a disconnect freeze: now (another phone type connected) or once the hold is over
 * with no new stream from the same source (idle, or a tee connection that carries
 * nothing yet: both hooks listen the whole time their process lives). A new stream from
 * the same source resumes the suspended decoder on this window instead (main loop). */
static void hw_unfreeze(struct app *a, int force)
{
    if (!a->freeze_until_ms) return;
    if (!force && now_ms() < a->freeze_until_ms) return;
    hw_close(a);
    a->freeze_until_ms = 0;
    fprintf(stderr, "altscreen_render: frozen picture released%s\n", force ? " (other source)" : "");
}

/* Close the hardware session. A decoder stuck in Executing keeps our buffers:
 * switch hardware decode OFF and exit, so carplay_monitor.sh restarts a clean
 * renderer (software path) instead of this one hanging or reusing them. */
static void hw_close(struct app *a)
{
    int rc;
    if (!a->hw) return;
    /* hw_src, not src: a frozen decoder is closed after its connection is gone (a->src
     * NULL), and hw_starts_clear(NULL) crashed the renderer at every session end. */
    wd_arm(a->hw_src, "teardown", ALTR_WD_DESTROY_MS, 0);
    rc = altr_hwdec_destroy(a->hw);
    wd_disarm();
    if (rc != 0) {
        hw_switch_off(a->hw_src, 1);
        set_live(0);
        fprintf(stderr, "altscreen_render: stuck hardware decoder; HW decoder switched OFF, "
                "exiting for a clean restart\n");
        _exit(3);
    }
    hw_starts_clear(a->hw_src); /* started and stopped cleanly: not a failing start */
    hw_active_mark(0);          /* the video core is free for stock's decoder */
    a->hw = NULL;
    a->hw_src = NULL;
}

/* Main-screen yield: close only the decoder, window 99 keeps the last picture.
 * Stuck is handled as in hw_close. */
static void hw_suspend(struct app *a)
{
    int rc;
    if (!a->hw) return;
    wd_arm(a->hw_src, "suspend", ALTR_WD_DESTROY_MS, 0);
    rc = altr_hwdec_suspend(a->hw);
    wd_disarm();
    if (rc != 0) {
        hw_switch_off(a->hw_src, 1);
        set_live(0);
        fprintf(stderr, "altscreen_render: stuck hardware decoder; HW decoder switched OFF, "
                "exiting for a clean restart\n");
        _exit(3);
    }
    hw_active_mark(0);          /* the video core is free for stock's decoder */
}

/* A decoder that failed but did not hang the video core: off for this boot only. */
static void hw_give_up(struct app *a, const char *why)
{
    fprintf(stderr, "altscreen_render: %s hardware decode %s; back to software decode "
            "(HW decoder switched OFF)\n", a->hw_src->name, why);
    hw_switch_off(a->hw_src, 0);
    hw_close(a);
}

/* The software path draws with GLES on its own window 99; bring it back if the
 * hardware path had it. */
static int ensure_gl(struct app *a)
{
    if (a->have_gl) return 0;
    gl_shutdown(a);
    if (gl_init(a, a->disp_id) != 0) {
        fprintf(stderr, "altscreen_render: GL re-init failed\n");
        gl_shutdown(a);
        return -1;
    }
    return 0;
}
#endif

/* Defaults for the two tee sources. */
static void sources_init(struct source *src, int *nsrc, int surface_w, int surface_h)
{
    int sw, sh;
    *nsrc = 0;

    src[*nsrc].name = "carplay";
    src[*nsrc].port = env_i("ALTR_TEE_PORT", 19820);
    src[*nsrc].view.stream_w = src[*nsrc].view.view_w = surface_w;
    src[*nsrc].view.stream_h = src[*nsrc].view.view_h = surface_h;
    src[*nsrc].view.crop_x = src[*nsrc].view.crop_y = 0;
    src[*nsrc].live_timeout_ms = env_i("ALTR_LIVE_TIMEOUT_MS", 0);
    src[*nsrc].hw_off_marker = ALTR_HWDEC_OFF_MARKER;
    src[*nsrc].hw_off_boot = ALTR_HWDEC_OFF_BOOT;
    src[*nsrc].hw_settle_marker = ALTR_HW_SETTLE_MARKER;
    src[*nsrc].hw_hang_persists = 1;             /* a hung core must not hit the next boot */
    /* Apple Maps without a route sends a picture only when the map changes: a frame
     * threaded decoder would hold the last one back, so CarPlay stays low delay. */
    src[*nsrc].sw_threads = 1;
    src[*nsrc].sw_fast = 0;
    (*nsrc)++;

    if (env_i("ALTR_AA_TEE_PORT", 19821) <= 0) return;
    sw = env_i("ALTR_AA_STREAM_WIDTH", 1920);
    sh = env_i("ALTR_AA_STREAM_HEIGHT", 1080);
    if (sw < surface_w || sh < surface_h) {
        fprintf(stderr, "altscreen_render: Android Auto stream %dx%d smaller than the %dx%d window; "
                "Android Auto tee off\n", sw, sh, surface_w, surface_h);
        return;
    }
    src[*nsrc].name = "android-auto";
    src[*nsrc].port = env_i("ALTR_AA_TEE_PORT", 19821);
    src[*nsrc].view.stream_w = sw;
    src[*nsrc].view.stream_h = sh;
    src[*nsrc].view.view_w = surface_w;
    src[*nsrc].view.view_h = surface_h;
    /* The phone lays its UI out in the centred viewport (width/height margins split
     * evenly), so the default crop is the centre. */
    src[*nsrc].view.crop_x = env_i("ALTR_AA_CROP_X", (sw - surface_w) / 2);
    src[*nsrc].view.crop_y = env_i("ALTR_AA_CROP_Y", (sh - surface_h) / 2);
    if (src[*nsrc].view.crop_x < 0 || src[*nsrc].view.crop_x > sw - surface_w)
        src[*nsrc].view.crop_x = (sw - surface_w) / 2;
    if (src[*nsrc].view.crop_y < 0 || src[*nsrc].view.crop_y > sh - surface_h)
        src[*nsrc].view.crop_y = (sh - surface_h) / 2;
    src[*nsrc].live_timeout_ms = env_i("ALTR_AA_LIVE_TIMEOUT_MS", 0);
    src[*nsrc].hw_off_marker = ALTR_AA_HWDEC_OFF_MARKER;
    src[*nsrc].hw_off_boot = ALTR_AA_HWDEC_OFF_BOOT;
    src[*nsrc].hw_settle_marker = ALTR_HW_SETTLE_MARKER;   /* issue #48; the yield covers #14 */
    src[*nsrc].hw_hang_persists = 0;             /* issue #44: this boot only, hung or not */
    /* Software fallback only (the hardware decoder is the default). On one core a
     * 1080p30 drive falls behind and freezes until the next key frame (issue #44).
     * Frame threads keep up; the cost is one picture of delay, so while the map is
     * still (a picture every few seconds) the cockpit shows the one before the last. */
    src[*nsrc].sw_threads = env_i("ALTR_AA_DECODE_THREADS", 2);
    if (src[*nsrc].sw_threads < 1) src[*nsrc].sw_threads = 1;
    if (src[*nsrc].sw_threads > 4) src[*nsrc].sw_threads = 4;
    src[*nsrc].sw_fast = env_i("ALTR_AA_SW_FAST", 0) != 0;
    (*nsrc)++;
}

int main(void)
{
    struct app a;
    struct source src[2];
    int nsrc, k;
    memset(&a, 0, sizeof(a));
    memset(src, 0, sizeof(src));
    g_app = &a;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

#ifdef PLATFORM_QNX
    /* The compositor's workers starve a default-priority renderer on this unit:
     * every thread (socket, decoder callback, presenter, main) can lose 300-650 ms
     * at once several times per minute (measured in luka-dev/mib2q-carplay-rgi,
     * 2026-10-09).  maneuver_render runs at 15 for the same reason.  Set it before
     * any thread exists so they all inherit it. */
    if (setprio(0, 15) < 0)
        fprintf(stderr, "altscreen_render: setprio(15) failed: %s; running at default\n",
                strerror(errno));
    /* The FFmpeg build has NEON (software fallback, issue #44) and picks it from build
     * flags, not the CPU, on QNX. The MHI2Q Krait cores have NEON; a CPU whose startup
     * does not report it gets FFmpeg's plain ARM code instead of SIGILL. */
    {
        int neon = (SYSPAGE_ENTRY(cpuinfo)->flags & ARM_CPU_FLAG_NEON) != 0;
        int noasm = access(ALTR_NOASM_MARKER, F_OK) == 0 || access(ALTR_NOASM_BOOT, F_OK) == 0;
        signal(SIGILL, on_sigill);
        if (noasm)
            av_force_cpu_flags(0);          /* every FFmpeg DSP table keeps its C functions */
        else if (!neon && (av_get_cpu_flags() & AV_CPU_FLAG_NEON))
            av_force_cpu_flags(av_get_cpu_flags() & ~AV_CPU_FLAG_NEON);
        fprintf(stderr, "altscreen_render: CPU NEON %s; software decoder uses %s\n",
                neon ? "yes" : "not reported",
                noasm ? "plain C (altscreen_render.noasm)" :
                (av_get_cpu_flags() & AV_CPU_FLAG_NEON) ? "NEON" : "ARM assembly without NEON");
    }
#endif
    a.tee_fd = -1;
    a.keyframe_gap_ms = (uint64_t)env_i("ALTR_KEYFRAME_GAP_MS", 2000);
    if (a.keyframe_gap_ms < 500u) a.keyframe_gap_ms = 500u;

    const char *host = env_s("ALTR_TEE_HOST", "127.0.0.1");
    g_live_file   = env_s("ALTR_LIVE_FILE",   "/tmp/altscreen_render.live");
    g_status_file = env_s("ALTR_STATUS_FILE", "/tmp/altscreen_render.status");
    unlink(g_live_file); /* a predecessor crash must not pin ctx 81/82 */
    a.surface_w = env_i("ALTR_WIDTH", 1440);
    a.surface_h = env_i("ALTR_HEIGHT", 540);
    sources_init(src, &nsrc, a.surface_w, a.surface_h);
    for (k = 0; k < nsrc; k++)
        fprintf(stderr, "altscreen_render: source %s port %d stream %dx%d view %dx%d at %d,%d\n",
                src[k].name, src[k].port, src[k].view.stream_w, src[k].view.stream_h,
                src[k].view.view_w, src[k].view.view_h, src[k].view.crop_x, src[k].view.crop_y);

#ifdef PLATFORM_QNX
    a.dpy = EGL_NO_DISPLAY;
    a.surf = EGL_NO_SURFACE;
    a.ctx = EGL_NO_CONTEXT;
    a.disp_id = env_i("ALTR_DISPLAYABLE_ID", 99);
    {
        int s = env_i("ALTR_HW_MIN_UPTIME_S", 120);
        a.hw_min_uptime_ms = s > 0 ? (uint64_t)s * 1000u : 0;   /* CLOCK_MONOTONIC = uptime on QNX */
        s = env_i("ALTR_HW_YIELD_HOLD_S", 3);
        g_hw_yield_hold_ms = (uint64_t)(s > 0 ? s : 1) * 1000u;   /* the hook waits for our release */
    }
    hw_active_mark(0);          /* a predecessor's marker; it no longer holds a decoder */
    {
        int any_hw = 0;
        for (k = 0; k < nsrc; k++) any_hw |= hw_wanted(&src[k]);
        if (any_hw) {
            fprintf(stderr, "altscreen_render: hardware decode (default); GL window deferred\n");
        } else if (gl_init(&a, a.disp_id) != 0) {
            write_status("gl_error", 0, 0, 0);
            gl_shutdown(&a);
            set_live(0);
            return 1;
        }
    }
#else
    fprintf(stderr, "altscreen_render: host build (headless tee sink, no GL)\n");
#endif

    /* One software decoder per source: each keeps its own threading mode. */
    altr_decoder_t *decs[2] = { NULL, NULL };
    int dec_ok = 1;
    for (k = 0; k < nsrc; k++) {
        decs[k] = altr_decode_create_opts(on_frame, &a, src[k].sw_threads, src[k].sw_fast);
        if (!decs[k]) dec_ok = 0;
        else if (src[k].sw_threads > 1 || src[k].sw_fast)
            fprintf(stderr, "altscreen_render: %s software decoder: %d frame thread(s)%s\n",
                    src[k].name, src[k].sw_threads, src[k].sw_fast ? ", no deblocking" : "");
    }
    if (!dec_ok) {
        for (k = 0; k < nsrc; k++) altr_decode_destroy(decs[k]);
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
        const struct source *s = NULL;
        int fd = -1;
        int upgrade = 0;            /* planned reconnect from software to hardware decode */
        int dropped = 0;            /* waiting: stream discarded or decoder state lost, resume needs a reconnect */
        omx_probe_poll(a.surface_w, a.surface_h);
        /* Only one phone session exists at a time, so at most one tee listens. */
        for (k = 0; k < nsrc && fd < 0; k++) {
            s = &src[k];
            fd = tee_connect(host, s->port);
        }
        if (fd < 0) {
#ifdef PLATFORM_QNX
            /* The session is gone (a decoder suspended for the main screen included):
             * keep its last picture until the cockpit is back on the stock map. */
            hw_freeze(&a);
            hw_unfreeze(&a, 0);
#endif
            set_live(0);
            g_source_name = "none";
            write_status("waiting_tee", a.last_w, a.last_h, a.frames);
            sleep(1);
            continue;
        }
#ifdef PLATFORM_QNX
        /* A connection is not a new stream yet: the frozen picture of the same source stays
         * until that source sends bytes (then its decoder resumes on this window) or the
         * hold ends. Another phone type drops it at once. */
        if (altr_freeze_on_connect(a.freeze_until_ms != 0, a.hw_src == s) == ALTR_FREEZE_CLOSE)
            hw_unfreeze(&a, 1);
#endif
        a.src = s;
        a.last_shown_ms = 0;
        a.tee_fd = fd;
        a.keyframe_ms = 0;
        a.errors_seen = (unsigned long)altr_decode_error_count(decs[s - src]);
        g_connections++;
        g_source_name = s->name;
        fprintf(stderr, "altscreen_render: connected to %s tee %s:%d\n", s->name, host, s->port);
        g_hw_off_reason[0] = 0;
#ifdef PLATFORM_QNX
        if (!hw_wanted(s)) {
            const char *why = access(s->hw_off_boot, F_OK) == 0 ? s->hw_off_boot : s->hw_off_marker;
            snprintf(g_hw_off_reason, sizeof(g_hw_off_reason), "%s", why);
            fprintf(stderr, "altscreen_render: %s hardware decode OFF (%s present); software decode\n",
                    s->name, why);
        }
#endif
        write_status("connected", a.last_w, a.last_h, a.frames);

        /* Recv loop with a poll so we can drop .live when frames stall and exit
         * on signal. */
        for (;;) {
            uint64_t now;
            if (g_stop) { close(fd); goto done; }
            omx_probe_poll(a.surface_w, a.surface_h);
#ifdef PLATFORM_QNX
            /* Frozen picture, and the hold ended before this source sent a new stream. */
            if (a.freeze_until_ms) hw_unfreeze(&a, 0);
            if (a.hw) altr_hwdec_set_grid(a.hw, grid_enabled(now_ms()));
            if (a.hw && !a.hw_waiting && !a.freeze_until_ms && hw_yield_until(0)) {
                /* Stock is about to start or stop its main-screen decoder (the
                 * hook waits for us): free the video core now. The cockpit keeps
                 * the last picture until the main screen is quiet. */
                fprintf(stderr, "altscreen_render: main screen video changing; %s hardware decoder "
                        "suspended, last picture kept\n", s->name);
                hw_suspend(&a);
                a.hw_waiting = 1;
                dropped = 1;                        /* mid-stream: resume only after a reconnect */
            }
            if (a.hw_waiting) {
                if (a.hw) set_live(1);              /* the frozen picture stays on the cockpit */
                if (dropped && !hw_yield_until(0)) {
                    /* The decoder needs SPS/PPS + IDR again: the tee replays from
                     * the last key frame on a new connection. */
                    fprintf(stderr, "altscreen_render: main screen quiet; reconnecting %s for a "
                            "key frame\n", s->name);
                    upgrade = 1;
                    a.live_hold_until_ms = now_ms() + ALTR_UPGRADE_LIVE_HOLD_MS;
                    break;
                }
            }
            if (a.hw_deferred && now_ms() >= a.hw_upgrade_at_ms) {
                /* The tee replays from the last keyframe, so the reconnect costs
                 * one picture; .live is kept so the cockpit does not switch away. */
                fprintf(stderr, "altscreen_render: hardware wait over; reconnecting %s for "
                        "hardware decode\n", s->name);
                upgrade = 1;
                a.live_hold_until_ms = now_ms() + ALTR_UPGRADE_LIVE_HOLD_MS;
                break;
            }
#endif
            fd_set rd; struct timeval tv;
            FD_ZERO(&rd); FD_SET(fd, &rd);
            tv.tv_sec = 0; tv.tv_usec = 200000;
#ifdef PLATFORM_QNX
            if (a.hw && !a.hw_waiting) tv.tv_usec = 40000;   /* see a yield quickly: the hook is waiting */
#endif
            int r = select(fd + 1, &rd, NULL, NULL, &tv);
            if (r > 0 && FD_ISSET(fd, &rd)) {
                ssize_t n = recv(fd, buf, sizeof(buf), 0);
                if (n <= 0) break;
#ifdef PLATFORM_QNX
                if (a.freeze_until_ms) {
                    /* A new stream from the source whose last picture is frozen (stream 111
                     * restarted, the hook reset its consumer, Android Auto's next playback):
                     * resume its suspended decoder on window 99 instead of closing it, so the
                     * cockpit goes from the old picture straight to the new one. The stream
                     * starts with SPS/PPS and an IDR (hook priming / tee replay). */
                    fprintf(stderr, "altscreen_render: new %s stream; decoder resumes on the "
                            "frozen window\n", s->name);
                    a.freeze_until_ms = 0;
                    a.hw_waiting = 1;
                }
                if (a.hw_waiting) {
                    uint64_t until;
                    /* Once anything was dropped only a reconnect brings a key frame. */
                    if (dropped || hw_yield_until(0)) { dropped = 1; continue; }
                    if (hw_yield_claim(&until) != 0) { dropped = 1; continue; }
                    a.hw_waiting = 0;
                    if (a.hw) {
                        int stuck = 0;
                        if (a.live_hold_until_ms && now_ms() < a.live_hold_until_ms) set_live(1);
                        wd_arm(s, "resume", ALTR_WD_CREATE_MS, a.live_hold_until_ms);
                        if (altr_hwdec_resume(a.hw, &stuck) != 0) {
                            wd_disarm();
                            if (stuck) {
                                hw_switch_off(s, 1);
                                set_live(0);
                                fprintf(stderr, "altscreen_render: video hardware not answering; "
                                        "HW decoder switched OFF, exiting for a clean restart\n");
                                _exit(3);
                            }
                            hw_give_up(&a, "resume failed");
                            ensure_gl(&a);
                            break;
                        }
                        wd_disarm();
                        fprintf(stderr, "altscreen_render: %s hardware decoder resumed\n", s->name);
                    } else {
                        hw_active_mark(0);           /* the gate below claims it again */
                    }
                }
                if (!a.hw && !a.hw_tried && !a.hw_deferred && hw_wanted(s)) {
                    int hw_gate;
                    uint64_t hw_defer_until = 0;
                    /* Only with the GEM "delayed start" marker (CarPlay and Android Auto). */
                    int settle = !s->hw_settle_marker || access(s->hw_settle_marker, F_OK) == 0;
                    if (settle && now_ms() < a.hw_min_uptime_ms) {
                        /* Issue #14: a 1080p Android Auto decoder opened ~64 s after
                         * boot (phone route already active, so the cluster stream
                         * starts with the session) hung the shared Venus core
                         * (VCD_HWTimeoutHandler) while stock was still bringing up
                         * its own video sessions. Car logs 020/021 opened it at
                         * ~100 s without trouble. The main-screen yield below now
                         * keeps it apart from gal's decoder; this wait remains for
                         * units that still freeze. */
                        fprintf(stderr, "altscreen_render: uptime %llu s < %llu s: %s on software "
                                "decode until the video hardware has settled\n",
                                (unsigned long long)(now_ms() / 1000u),
                                (unsigned long long)(a.hw_min_uptime_ms / 1000u), s->name);
                        a.hw_upgrade_at_ms = a.hw_min_uptime_ms;
                        a.hw_deferred = 1;
                    } else if (hw_yield_claim(&hw_defer_until) != 0) {
                        /* The main screen video changed moments ago: wait for the video
                         * core instead of a software start and a later swap. */
                        fprintf(stderr, "altscreen_render: main screen video changed; %s waits %llu ms "
                                "for the video core\n", s->name,
                                (unsigned long long)(hw_defer_until - now_ms()));
                        a.hw_waiting = 1;
                        dropped = 1;
                        continue;
                    } else if ((hw_gate = hw_starts_begin(s, now_ms(), &hw_defer_until)) != 0) {
                        hw_active_mark(0);
                        if (hw_gate > 0) {
                            /* Same path as the boot settle time: software now, one
                             * reconnect to hardware when the cooldown is over. */
                            a.hw_upgrade_at_ms = hw_defer_until;
                            a.hw_deferred = 1;
                        } else {
                            a.hw_tried = 1;          /* off for this boot: software only */
                        }
                    } else {
                        int stuck = 0;
                        a.hw_tried = 1;
                        gl_shutdown(&a);             /* frees displayable 99 for the decoder */
                        if (a.live_hold_until_ms && now_ms() < a.live_hold_until_ms) set_live(1);
                        wd_arm(s, "setup", ALTR_WD_CREATE_MS, a.live_hold_until_ms);
                        a.hw_src = s;
                        a.hw = altr_hwdec_create(&s->view, a.disp_id, &stuck);
                        wd_disarm();
                        a.hw_frames_seen = 0;
                        a.damaged_seen = 0;
                        if (!a.hw) {
                            if (stuck) {
                                /* A hung core: off also after a reboot. Exiting: the hook
                                 * sees the pid gone, so the active file needs no clearing. */
                                hw_switch_off(s, 1);
                                set_live(0);
                                fprintf(stderr, "altscreen_render: video hardware not answering; "
                                        "exiting for a clean restart\n");
                                _exit(3);
                            }
                            hw_give_up(&a, "setup failed");
                            a.hw_src = NULL;        /* no decoder: hw_close had nothing to clear */
                            hw_active_mark(0);
                            ensure_gl(&a);
                            break;                  /* reconnect: the hook re-primes with a fresh IDR */
                        }
                    }
                }
                if (a.hw) {
                    uint64_t hf;
                    int rc;
                    wd_arm(s, "decode", ALTR_WD_FEED_MS, a.live_hold_until_ms);
                    rc = altr_hwdec_feed(a.hw, buf, (size_t)n);
                    wd_disarm();
                    if (rc < 0 || altr_hwdec_failed(a.hw)) {
                        hw_give_up(&a, "failed");
                        ensure_gl(&a);
                        break;
                    }
                    {
                        unsigned dm = altr_hwdec_damaged(a.hw);
                        if (dm != a.damaged_seen) {
                            g_damaged += dm - a.damaged_seen;
                            a.damaged_seen = dm;
                            request_keyframe(&a, "damaged picture (hardware decoder)");
                        }
                    }
                    hf = altr_hwdec_frames(a.hw);
                    if (hf > a.hw_frames_seen) {
                        if (a.hw_frames_seen == 0) hw_starts_clear(s);   /* decoder works */
                        a.frames += hf - a.hw_frames_seen;
                        a.hw_frames_seen = hf;
                        a.last_w = s->view.view_w; a.last_h = s->view.view_h;
                        a.last_frame_ms = a.last_shown_ms = now_ms();
                        set_live(1);
                        if (a.frames <= 2 || (a.frames % 120) == 0)
                            write_status("decoding_hw", a.last_w, a.last_h, a.frames);
                    }
                    continue;
                }
                if (!a.have_gl && ensure_gl(&a) != 0) continue;
#endif
                altr_decode_feed(decs[s - src], buf, (size_t)n);
                {
                    unsigned long e = (unsigned long)altr_decode_error_count(decs[s - src]);
                    if (e != a.errors_seen) {
                        g_damaged += e - a.errors_seen;
                        a.errors_seen = e;
                        request_keyframe(&a, "damaged picture (software decoder)");
                    }
                }
            } else if (r < 0 && errno != EINTR) {
                break;
            }
            /* Heartbeat while the last picture is recent (or, timeout 0, for the
             * whole connection): Android Auto and Apple Maps without a route send
             * almost no pictures while the map is still, and that must not drop
             * ctx 81/82. */
            now = now_ms();
#ifdef PLATFORM_QNX
            if (a.live_hold_until_ms) {
                if (!a.last_shown_ms && now < a.live_hold_until_ms) set_live(1);
                else a.live_hold_until_ms = 0;     /* first picture shown, or hold over */
            }
#endif
            if (a.last_shown_ms && (s->live_timeout_ms <= 0 ||
                                    (now - a.last_shown_ms) <= (uint64_t)s->live_timeout_ms))
                set_live(1);
            else if (a.last_shown_ms)
                set_live(0);
        }

        close(fd);
        a.tee_fd = -1;
#ifdef PLATFORM_QNX
        if (a.hw && a.hw_waiting) {
            /* Suspended for the main screen: keep window 99 and its last picture
             * across the reconnect (the hook also resets us when iOS re-sends the
             * cluster config right after a main-screen change). */
            upgrade = 1;
            a.live_hold_until_ms = now_ms() + ALTR_UPGRADE_LIVE_HOLD_MS;
        } else if (a.hw && !upgrade) {
            hw_freeze(&a);          /* stream over: last picture kept, see ALTR_DISCONNECT_FREEZE_MS */
        } else {
            hw_close(&a);
            a.hw_waiting = 0;
        }
        a.hw_tried = 0;
        a.hw_deferred = 0;
        if (!upgrade) a.live_hold_until_ms = 0;
#endif
        altr_decode_flush(decs[s - src]);
        if (!upgrade) set_live(0);
        a.last_shown_ms = 0;
        write_status("tee_closed", a.last_w, a.last_h, a.frames);
        fprintf(stderr, "altscreen_render: %s tee closed; frames so far=%llu\n",
                s->name, (unsigned long long)a.frames);
        a.src = NULL;
        g_source_name = "none";
    }

done:
#ifdef PLATFORM_QNX
    hw_close(&a);
#endif
    set_live(0);
    write_status("stopped", a.last_w, a.last_h, a.frames);
    for (k = 0; k < nsrc; k++) altr_decode_destroy(decs[k]);
#ifdef PLATFORM_QNX
    gl_shutdown(&a);
#endif
    return 0;
}
