/*
 * Android Auto cluster-display hook for gal (libaa_cluster_hook.so).
 * Shared declarations; see aa_hook.c for the overview.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef AA_HOOK_H
#define AA_HOOK_H

#include <stddef.h>
#include <stdint.h>

/* Runtime switches, all optional files on the unit. */
#define AA_MARKER_DIR          "/mnt/app/root/"
#define AA_MARKER_OFF          AA_MARKER_DIR "aa_cluster.off"           /* no cluster display */
#define AA_MARKER_ABI_TRIAL    AA_MARKER_DIR "aa_cluster.abi_trial"     /* unknown receiver: try anyway */
#define AA_MARKER_NAVXLATE_OFF AA_MARKER_DIR "aa_cluster.navxlate_off"  /* leave 0x8006/0x8007 alone */
#define AA_MARKER_RELAYOUT_OFF AA_MARKER_DIR "aa_cluster.relayout_off"  /* no per-view 0x8009 */
#ifndef AA_MARKER_REFOCUS
#define AA_MARKER_REFOCUS      AA_MARKER_DIR "aa_cluster.keyframe_refocus" /* opt-in: focus bounce for a key frame */
#endif
#ifndef AA_MARKER_PAUSE_HIDDEN
#define AA_MARKER_PAUSE_HIDDEN AA_MARKER_DIR "cluster_video.pause_hidden" /* opt-in, shared with CarPlay */
#endif
#ifndef AA_HIDDEN_PATH
#define AA_HIDDEN_PATH         "/tmp/aa_cluster_hidden"                 /* Java: "1\n" hidden, "0\n" shown */
#endif
#ifndef AA_MARKER_DAYNIGHT
#define AA_MARKER_DAYNIGHT     AA_MARKER_DIR "aa_cluster.daynight"      /* opt-in: theme follows MMI */
#endif
#ifndef AA_MARKER_UICONFIG_LEGACY
/* 1.4 UiConfig: content insets only, in full-frame coordinates (fallback, issue #53) */
#define AA_MARKER_UICONFIG_LEGACY AA_MARKER_DIR "aa_cluster.uiconfig_legacy"
#endif
/* Files; overridable for host tests. */
#ifndef AA_FILE_DPI
#define AA_FILE_DPI            AA_MARKER_DIR "aa_cluster.dpi"           /* one number, 80..400 */
#endif
#ifndef AA_FILE_FPS
/* Cluster stream frame rate, shared with the CarPlay hook's override (MMI "Cluster Map Frame
 * Rate"): 60 = 60 fps, anything else or no file = 30. Android Auto knows only these two. */
#define AA_FILE_FPS            AA_MARKER_DIR "mibr-carplay111-fps"
#endif
#ifndef AA_FILE_INSETS
#define AA_FILE_INSETS         AA_MARKER_DIR "aa_cluster.insets"        /* see aa_uiconfig.c */
#endif
#define AA_LOG_PATH            "/tmp/aa_cluster_hook.log"
#ifndef AA_UNPINNED_PATH
#define AA_UNPINNED_PATH       "/tmp/aa_cluster.unpinned"               /* unpinned gal on trial */
#endif
#ifndef AA_LANES_PATH
#define AA_LANES_PATH          "/tmp/aa_lanes"
#endif
#ifndef AA_VIEW_PATH
#define AA_VIEW_PATH           "/tmp/aa_cluster_view"
#endif
#ifndef AA_THEME_PATH
#define AA_THEME_PATH          "/tmp/aa_cluster_theme"
#endif
#define AA_TEE_PORT_DEFAULT    19821
/* Main-screen video yield to altscreen_render: the same files as the CarPlay hook's. */
#ifndef AA_HW_YIELD_PATH
#define AA_HW_YIELD_PATH       "/tmp/altscreen_render.hw_yield"         /* uptime ms of the last change */
#endif
#ifndef AA_HW_ACTIVE_PATH
#define AA_HW_ACTIVE_PATH      "/tmp/altscreen_render.hw_active"        /* pid=<renderer> while it holds a decoder */
#endif
#ifndef AA_HW_YIELD_WAIT_MS
#define AA_HW_YIELD_WAIT_MS    1500u                                    /* longest wait for the renderer */
#endif

/* Cluster stream: 1920x1080 (codec resolution 3) with width/height margins 480 x 540,
 * so the phone lays its UI out in the centred 1440x540 viewport = the cockpit terminal. */
#define AA_CODEC_RES_1080P     3
#define AA_STREAM_W            1920
#define AA_STREAM_H            1080
#define AA_VIEW_W              1440
#define AA_VIEW_H              540
#define AA_MARGIN_W            (AA_STREAM_W - AA_VIEW_W)
#define AA_MARGIN_H            (AA_STREAM_H - AA_VIEW_H)
#define AA_DPI_DEFAULT         125   /* the 12.3" 1440x540 panel is ~125 PPI: 1:1 physical size */

void aa_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int aa_marker(const char *path);

/* aa_tee.c: loopback Annex-B tee for altscreen_render. Producer calls never block on
 * the consumer (one short mutex hold), so the receiver thread is never stalled. */
int aa_tee_start(int port);
void aa_tee_codec(const void *data, size_t size);
void aa_tee_frame(const void *data, size_t size);
void aa_tee_reset(const char *reason);
unsigned aa_nal_types(const void *data, size_t size);
/* 1 once (then cleared) when a renderer asked for a key frame (back-channel 'K') or needs
 * one the cache no longer holds. */
int aa_tee_keyframe_wanted(void);

/* aa_focus.c: the cluster sink's video focus, receiver thread only. Opt-in pause while the
 * cockpit hides the phone map (/mnt/app/root/cluster_video.pause_hidden + Java's
 * /tmp/aa_cluster_hidden) and opt-in key-frame refocus (aa_cluster.keyframe_refocus). */
typedef void (*aa_set_focus_fn)(void *sink, int mode, int unsolicited);
void aa_focus_tick(void *sink, aa_set_focus_fn set_focus);
/* The cluster sink finished its setup (PROJECTED focus sent): the tick may act on it now. */
void aa_focus_setup_done(void *sink);

/* aa_yield.c: tell altscreen_render before gal's main-screen decoder changes state, wait
 * (bounded) until it has released the video core. */
enum { AA_YIELD_NONE = 0, AA_YIELD_RELEASED = 1, AA_YIELD_TIMEOUT = 2 };
int aa_hw_yield(const char *why);

/* aa_navxlate.c */
void aa_nav_translate(unsigned char channel, const void *shared);
int aa_nav_state_to_legacy(const unsigned char *in, unsigned n, unsigned char *out, unsigned max,
                           const char *fallback_road);
int aa_nav_position_to_legacy(const unsigned char *in, unsigned n, unsigned char *out, unsigned max,
                              char *road_out, unsigned road_max);
int aa_nav_state_lanes(const unsigned char *in, unsigned n, char *out);
void aa_nav_set_channel(unsigned channel);

/* aa_uiconfig.c */
int aa_dpi(void);
int aa_fps(void);
void aa_uiconfig_sink(void *sink);
void aa_uiconfig_media(void *media);
void aa_relayout_tick(void *sink);
unsigned aa_relayout_message(unsigned char *out, unsigned max, int view, unsigned theme);
int aa_view_state(void);
unsigned aa_theme_state(void);

#endif
