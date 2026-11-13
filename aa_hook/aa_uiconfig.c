/*
 * Cluster VideoConfiguration UiConfig (field 11): content insets (the safe area Google lays
 * its car and turn card out in) + UI theme, and the per-view layout update (0x8009).
 *
 * Adapted from wasimlhr/mib2q-android-auto-cluster android_auto/hook/src/uiconfig.c, keeping
 * only its car-validated 1080p viewport mode. Technique and object offsets from
 * OneB1t/chopinwong01 video_sink_hook.c (GPL-3.0): the payload goes into the
 * VideoConfiguration's unknown-fields std::string (+0x08 buffer/pointer, size +0x18, reserve
 * +0x1c), on the sink after addSupportedConfiguration and again on the discovery-response
 * copy.
 *
 * Geometry: the stream is 1920x1080 with margins 480 x 540, so the phone lays out the
 * centred 1440x540 viewport = the cockpit terminal, which altscreen_render shows 1:1.
 * UiConfig (issue #53, NuiQuant's on-car test with Yandex and Google Maps on an S4 B9):
 *   field 1 margins        270 270 240 240: the frame around the viewport (cropped away, so
 *                          the black the phone draws there is never seen)
 *   field 2 content insets the view's safe area, relative to the viewport
 *   field 3 stable insets  the same
 * Before 1.5 only field 2 was sent, measured from the FULL 1920x1080 frame (viewport origin
 * added). Google Maps placed itself correctly from that, Yandex did not (off-centre in every
 * view, on a B9 too). The old encoding is kept behind aa_cluster.uiconfig_legacy, read at
 * phone connect. One preset per cockpit view, from the fork's on-car runs:
 *   full    large map view (centred wide)
 *   classic small window between the two dials
 *   sport   small window left of the centre dial (the B9Sport stage starts at x 476)
 * The view comes from Java (/tmp/aa_cluster_view, com.luka.carplay.aa.AaClusterView).
 *
 * The presets are B9 numbers. What they describe is a window on the cockpit panel, and the
 * cockpit shows the viewport from the stock layout's map origin row (B9 26, Q7 18) and, in the
 * Sport small stage, shifted by the layout's small-stage offset (B9Sport -476, Q7Sport -460).
 * Java adds both to the view record and the presets follow them, so a Q7 gets the same panel
 * window as a B9 (issue #53); a record without them (older jar) means the B9 values. Every
 * view ends on panel row 390, just above the cockpit's street-name bar: the phone anchors its
 * ETA pill to the bottom inset, and the fork's Sport value (146, panel row 420) put it behind
 * that bar (issues #41, #53).
 *
 * Theme: dark by default. The fork found that Automatic and Light on the cluster display crash
 * Android Auto on the phone. Opt-in marker aa_cluster.daynight (GEM "Android Auto cluster
 * day/night"): Light by day, Dark by night, from the MMI day/night design that Java writes to
 * /tmp/aa_cluster_theme (com.luka.carplay.aa.AaClusterTheme). Sent at connect only: a theme
 * change in a running session as 0x8009 drops the phone's Android Auto session about 2 s later
 * (issue #19; the same 0x8009 for a view change is fine), so every 0x8009 carries the theme the
 * session connected with and a day/night change applies from the next connect. Missing or
 * garbled record = dark.
 *
 * Optional file aa_cluster.insets: 12 numbers, "top bottom left right" in viewport pixels
 * (0..540 / 0..1440) for full, classic, sport, in that order. Read at phone connect and sent
 * as written (measured on the owner's car, so not moved with the layout).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "aa_hook.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define VIEW_FULL    0
#define VIEW_CLASSIC 1
#define VIEW_SPORT   2
#define UI_THEME_LIGHT 1u
#define UI_THEME_DARK 2u
#define VP_OX ((uint32_t)AA_MARGIN_W / 2u)
#define VP_OY ((uint32_t)AA_MARGIN_H / 2u)

/* The layout the presets were measured on (LayoutMIB2HighB9 / B9Sport). */
#define REF_MAP_Y    26
#define REF_STAGE_DX (-476)

#define PRESET_DEFAULT {                                                              \
    { 97, 176, 350, 370 },   /* full: card just left of the Audi arrow tile frame */ \
    { 97, 176, 510, 510 },   /* classic: between the dials */                       \
    { 78, 176, 484, 394 },   /* sport: the B9Sport small-stage window */            \
}
static const uint32_t preset_default[3][4] = PRESET_DEFAULT;
static uint32_t preset[3][4] = PRESET_DEFAULT;   /* aa_cluster.insets replaces it at connect */
static int preset_file;      /* preset[] is the insets file: sent as written */
static int geo_dx = REF_STAGE_DX, geo_y = REF_MAP_Y;   /* layout the next UiConfig is built for */
static int conn_view;        /* layout sent at connect */
static unsigned conn_theme = UI_THEME_DARK;   /* theme sent at connect */
static int conn_legacy;      /* 1.4 encoding for this session (aa_cluster.uiconfig_legacy) */

static uint32_t rd(const void *p, unsigned off) { uint32_t v; memcpy(&v, (const char *)p + off, 4); return v; }
static void wr(void *p, unsigned off, uint32_t v) { memcpy((char *)p + off, &v, 4); }

static unsigned varint(unsigned char *b, uint32_t v)
{
    unsigned n = 0;
    while (v >= 0x80u) { b[n++] = (unsigned char)(v | 0x80u); v >>= 7; }
    b[n++] = (unsigned char)v;
    return n;
}

static void load_insets(void)
{
    unsigned v[12];
    int i, j;
    FILE *f;
    memcpy(preset, preset_default, sizeof(preset));
    preset_file = 0;
    f = fopen(AA_FILE_INSETS, "r");
    if (!f) return;
    for (i = 0; i < 12; i++) if (fscanf(f, "%u", &v[i]) != 1) break;
    fclose(f);
    if (i != 12) { aa_log("uiconfig.insets file ignored (need 12 numbers)"); return; }
    for (i = 0; i < 3; i++) {
        if (v[i * 4] + v[i * 4 + 1] >= AA_VIEW_H || v[i * 4 + 2] + v[i * 4 + 3] >= AA_VIEW_W) {
            aa_log("uiconfig.insets file ignored (view %d leaves no area)", i);
            return;
        }
    }
    for (i = 0; i < 3; i++) for (j = 0; j < 4; j++) preset[i][j] = v[i * 4 + j];
    preset_file = 1;
    aa_log("uiconfig.insets from file");
}

/* Cockpit view record from Java: 0 full, 1 classic, 2 sport; -1 unknown (no file yet /
 * mid-write). The record is the view name padded to 8, then the layout's Sport small-stage
 * shift and its map origin row, a fixed 22 bytes ending in a newline. dx / y get those two;
 * they are the B9 values for the 8-byte record of a jar before 1.5 and for numbers no layout
 * has. */
static int view_read(int *dx, int *y)
{
    char v[32] = {0};
    int view, a, b;
    size_t n;
    FILE *f = fopen(AA_VIEW_PATH, "r");
    *dx = REF_STAGE_DX;
    *y = REF_MAP_Y;
    if (!f) return -1;
    if (!fgets(v, sizeof(v), f)) v[0] = 0;
    fclose(f);
    if (!strncmp(v, "full", 4)) view = VIEW_FULL;
    else if (!strncmp(v, "classic", 7)) view = VIEW_CLASSIC;
    else if (!strncmp(v, "sport", 5)) view = VIEW_SPORT;
    else return -1;
    n = strlen(v);
    if (n <= 8) return view;                            /* name only */
    if (v[n - 1] != '\n' || sscanf(v + 8, "%d %d", &a, &b) != 2) return -1;   /* mid-write */
    if (a >= -AA_VIEW_W && a <= AA_VIEW_W && b >= 0 && b < AA_VIEW_H) { *dx = a; *y = b; }
    return view;
}

int aa_view_state(void)
{
    int dx, y;
    return view_read(&dx, &y);
}

/* Insets for a view on a layout, in viewport pixels. A built-in preset is a panel window
 * measured on the B9: the same window sits (26 - map origin) rows lower in the viewport, and
 * in Sport (small-stage shift + 476) px further left. Each axis moves only if it stays on the
 * viewport, so the area can never invert. */
static void view_insets(int view, int dx, int y, uint32_t in[4])
{
    int t = (int)preset[view][0], b = (int)preset[view][1];
    int l = (int)preset[view][2], r = (int)preset[view][3];
    if (!preset_file) {
        int down = REF_MAP_Y - y;
        int left = view == VIEW_SPORT ? dx - REF_STAGE_DX : 0;
        if (t + down >= 0 && b - down >= 0) { t += down; b -= down; }
        if (l - left >= 0 && r + left >= 0) { l -= left; r += left; }
    }
    in[0] = (uint32_t)t; in[1] = (uint32_t)b; in[2] = (uint32_t)l; in[3] = (uint32_t)r;
}

/* Cluster theme: UI_THEME_DARK unless aa_cluster.daynight is set and Java's record says day.
 * The record is one fixed 6-byte line, "day  \n" or "night\n". */
unsigned aa_theme_state(void)
{
    char v[7] = {0};
    FILE *f;
    if (!aa_marker(AA_MARKER_DAYNIGHT)) return UI_THEME_DARK;
    f = fopen(AA_THEME_PATH, "r");
    if (!f) return UI_THEME_DARK;
    if (!fgets(v, sizeof(v), f)) v[0] = 0;
    fclose(f);
    return strncmp(v, "day", 3) ? UI_THEME_DARK : UI_THEME_LIGHT;
}

static const char *theme_name(unsigned theme) { return theme == UI_THEME_LIGHT ? "light" : "dark"; }

/* One Insets field: tag, len, {0x08 top, 0x10 bottom, 0x18 left, 0x20 right}. Values stay
 * below 16384 (2-byte varints), so a field is at most 14 bytes. */
static unsigned insets_field(unsigned char *out, unsigned char tag,
                             uint32_t t, uint32_t b, uint32_t l, uint32_t r)
{
    unsigned char in[20];
    unsigned ni = 0, n = 0;
    in[ni++] = 0x08; ni += varint(in + ni, t);
    in[ni++] = 0x10; ni += varint(in + ni, b);
    in[ni++] = 0x18; ni += varint(in + ni, l);
    in[ni++] = 0x20; ni += varint(in + ni, r);
    out[n++] = tag; n += varint(out + n, ni);
    memcpy(out + n, in, ni);
    return n + ni;
}

#define UICONFIG_MAX (3u * 14u + 2u)

/* UiConfig body: 0x0a margins, 0x12 content insets, 0x1a stable insets (viewport-relative),
 * then 0x20 theme. Legacy (1.4): only 0x12, in frame coordinates, and the theme. */
static unsigned uiconfig_body(unsigned char *ui, unsigned max, int view, unsigned theme)
{
    unsigned nu = 0;
    uint32_t vi[4];
    if (max < UICONFIG_MAX) return 0;
    view_insets(view, geo_dx, geo_y, vi);
    if (conn_legacy) {
        nu += insets_field(ui + nu, 0x12, vi[0] + VP_OY, vi[1] + VP_OY, vi[2] + VP_OX, vi[3] + VP_OX);
    } else {
        nu += insets_field(ui + nu, 0x0a, VP_OY, VP_OY, VP_OX, VP_OX);
        nu += insets_field(ui + nu, 0x12, vi[0], vi[1], vi[2], vi[3]);
        nu += insets_field(ui + nu, 0x1a, vi[0], vi[1], vi[2], vi[3]);
    }
    ui[nu++] = 0x20; ui[nu++] = (unsigned char)theme;
    return nu;
}

int aa_dpi(void)
{
    int v, d = AA_DPI_DEFAULT;
    FILE *f = fopen(AA_FILE_DPI, "r");
    if (f) {
        if (fscanf(f, "%d", &v) == 1 && v >= 80 && v <= 400) d = v;
        fclose(f);
    }
    aa_log("uiconfig.dpi %d%s", d, d == AA_DPI_DEFAULT ? "" : " (file)");
    return d;
}

/* 30 unless the shared frame-rate file says 60 (read at phone connect). The cockpit captures
 * the map at 30 fps either way; 60 doubles the decoder's work. */
int aa_fps(void)
{
    int v = 0, f = 30;
    FILE *file = fopen(AA_FILE_FPS, "r");
    if (file) {
        if (fscanf(file, "%d", &v) == 1 && v == 60) f = 60;
        fclose(file);
    }
    aa_log("uiconfig.fps %d%s", f, f == 30 ? "" : " (file)");
    return f;
}

/* The payload (~45 bytes, ~20 legacy) is longer than the 15-byte SSO buffer, so it goes into a heap
 * string from the library's own operator new (heap mode when reserve >= 16, freed by the
 * receiver with operator delete) - confirmed on the car by the fork. */
typedef void *(*opnew_fn)(unsigned);
static char *heap_copy(const unsigned char *p, unsigned n)
{
    static opnew_fn opnew;
    char *b;
    if (!opnew) opnew = (opnew_fn)dlsym(RTLD_DEFAULT, "_Znwj");
    if (!opnew) return NULL;
    b = (char *)opnew(n + 1u);
    if (b) { memcpy(b, p, n); b[n] = 0; }
    return b;
}

static void apply(void *vconf, const char *where)
{
    unsigned char ui[UICONFIG_MAX], p[UICONFIG_MAX + 8];
    unsigned nu, n = 0;
    uint32_t vi[4];
    char *heap;
    int view;
    if (!vconf) { aa_log("uiconfig.skipped where=%s reason=no_vconf", where); return; }
    if (rd(vconf, 0x28) != AA_CODEC_RES_1080P) {
        aa_log("uiconfig.skipped where=%s reason=resolution res=%u", where, rd(vconf, 0x28));
        return;
    }
    if (rd(vconf, 0x1c) > 15u || rd(vconf, 0x18) != 0u) {
        aa_log("uiconfig.skipped where=%s reason=unknown_fields_in_use len=%u cap=%u",
               where, rd(vconf, 0x18), rd(vconf, 0x1c));
        return;
    }
    load_insets();
    view = view_read(&geo_dx, &geo_y);
    if (view < 0) view = VIEW_FULL;
    conn_view = view;
    conn_theme = aa_theme_state();
    conn_legacy = aa_marker(AA_MARKER_UICONFIG_LEGACY);
    nu = uiconfig_body(ui, sizeof(ui), view, conn_theme);
    if (!nu) return;
    p[n++] = 0x5a;                      /* VideoConfiguration field 11 = UiConfig */
    n += varint(p + n, nu);
    memcpy(p + n, ui, nu);
    n += nu;
    heap = heap_copy(p, n);
    if (!heap) { aa_log("uiconfig.skipped where=%s reason=no_operator_new", where); return; }
    wr(vconf, 0x08, (uint32_t)(uintptr_t)heap);
    wr(vconf, 0x18, n);
    wr(vconf, 0x1c, n);
    view_insets(view, geo_dx, geo_y, vi);
    aa_log("uiconfig.set where=%s view=%d top=%u bottom=%u left=%u right=%u %s stage_dx=%d map_y=%d"
           " theme=%s encoding=%s bytes=%u", where, view, vi[0], vi[1], vi[2], vi[3],
           preset_file ? "file" : "preset", geo_dx, geo_y, theme_name(conn_theme),
           conn_legacy ? "legacy" : "viewport", n);
}

void aa_uiconfig_sink(void *sink)
{
    uint32_t vec = sink ? rd(sink, 0x40) : 0;
    apply(vec ? (void *)(uintptr_t)rd((void *)(uintptr_t)vec, 4) : NULL, "sink");
}

void aa_uiconfig_media(void *media)
{
    void **arr = media ? (void **)(uintptr_t)rd(media, 0x40) : NULL;
    apply(media && rd(media, 0x44) > 0u && arr ? arr[0] : NULL, "discovery");
}

/* 0x8009 UpdateUiConfigRequest (HU -> phone, video channel): 2-byte big-endian type, then
 * field 1 = the same UiConfig as VideoConfiguration field 11. */
unsigned aa_relayout_message(unsigned char *out, unsigned max, int view, unsigned theme)
{
    unsigned char ui[UICONFIG_MAX];
    unsigned nu, n = 0;
    if (view < 0 || view > 2) return 0;
    nu = uiconfig_body(ui, sizeof(ui), view, theme);
    if (!nu || max < nu + 5u) return 0;
    out[n++] = 0x80; out[n++] = 0x09;
    out[n++] = 0x0a; n += varint(out + n, nu);
    memcpy(out + n, ui, nu);
    return n + nu;
}

typedef void (*queue_fn)(void *, unsigned char, void *, unsigned);

/* The 2016 receiver has no sender for 0x8009, so it is queued like the receiver's own
 * VideoFocusNotification: MessageRouter::queueOutgoing(router, channel, data, len) copies it;
 * the sink (ProtocolEndpointBase) holds open flag +4, channel +5, router +8. */
static int relayout_send(void *sink, int view, unsigned theme)
{
    static queue_fn queue;
    unsigned char msg[UICONFIG_MAX + 8];
    unsigned n;
    if (!queue) queue = (queue_fn)dlsym(RTLD_DEFAULT, "_ZN13MessageRouter13queueOutgoingEhPvj");
    n = aa_relayout_message(msg, sizeof(msg), view, theme);
    if (!queue || !n || !rd(sink, 8) || !((const unsigned char *)sink)[4]) return 0;
    queue((void *)(uintptr_t)rd(sink, 8), ((const unsigned char *)sink)[5], msg, n);
    {
        uint32_t vi[4];
        view_insets(view, geo_dx, geo_y, vi);
        aa_log("relayout.sent view=%d top=%u bottom=%u left=%u right=%u stage_dx=%d map_y=%d"
               " theme=%s channel=%u bytes=%u", view, vi[0], vi[1], vi[2], vi[3], geo_dx, geo_y,
               theme_name(theme), ((const unsigned char *)sink)[5], n);
    }
    return 1;
}

static uint64_t mono_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)(t.tv_nsec / 1000000L);
}

/* Receiver thread only (the thread the receiver itself sends from): the cluster sink's frame
 * callback and every routed message. The phone sends almost no pictures while its map is
 * still, so messages drive it too. The view file is read at most every 300 ms; a change picked
 * up on one call is sent on the next one. A change is another view, or the same view with other
 * insets: the stock layout arriving after a connect that still had the fallback one. The theme
 * never changes mid-session (issue #19): every 0x8009 carries conn_theme, and a new day/night
 * state is only logged, once. */
void aa_relayout_tick(void *sink)
{
    static void *cur_sink;
    static uint64_t next_check;
    static int sent_view, pending_view = -1, pending_dx, pending_y, off;
    static uint32_t sent_in[4];
    static unsigned deferred_theme;
    uint64_t now;
    uint32_t in[4];
    int view, dx, y;
    unsigned theme;
    if (!sink) return;
    if (sink != cur_sink || !((const unsigned char *)sink)[4]) {   /* new session or channel closed */
        cur_sink = ((const unsigned char *)sink)[4] ? sink : NULL;
        next_check = 0;
        sent_view = conn_view;
        view_insets(conn_view, geo_dx, geo_y, sent_in);
        deferred_theme = conn_theme;
        pending_view = -1;
        off = aa_marker(AA_MARKER_RELAYOUT_OFF);
        if (!cur_sink) return;
    }
    if (off) return;
    if (pending_view >= 0) {
        int v = pending_view;
        pending_view = -1;
        geo_dx = pending_dx;
        geo_y = pending_y;
        if (relayout_send(sink, v, conn_theme)) {
            sent_view = v;
            view_insets(v, geo_dx, geo_y, sent_in);
        }
        return;
    }
    now = mono_ms();
    if (now < next_check) return;
    next_check = now + 300u;
    view = view_read(&dx, &y);
    if (view < 0) { view = sent_view; dx = geo_dx; y = geo_y; }   /* mid-write / no record: keep */
    view_insets(view, dx, y, in);
    if (view != sent_view || memcmp(in, sent_in, sizeof(in))) {
        pending_view = view;
        pending_dx = dx;
        pending_y = y;
    }
    theme = aa_theme_state();
    if (theme != deferred_theme) {
        deferred_theme = theme;
        aa_log("theme.deferred theme=%s session=%s (applies at the next phone connect)",
               theme_name(theme), theme_name(conn_theme));
    }
}
