/*
 * Cluster VideoConfiguration UiConfig (field 11): content insets (the safe area Google lays
 * its car and turn card out in) + UI theme, and the per-view layout update (0x8009).
 *
 * Adapted from wasimlhr/mib2q-android-auto-cluster android_auto/hook/src/uiconfig.c, keeping
 * only its car-validated 1080p viewport mode. Technique and object offsets from
 * OneB1t/chopinwong01 video_sink_hook.c (GPL-3.0): the payload goes into the
 * VideoConfiguration's unknown-fields std::string (+0x08 buffer/pointer, size +0x18, reserve
 * +0x1c), on the sink after addSupportedConfiguration and again on the discovery-response
 * copy. UiConfig field 1 (margins) is NOT used: it makes the phone draw black borders.
 *
 * Geometry: the stream is 1920x1080 with margins 480 x 540, so the phone lays out the
 * centred 1440x540 viewport = the cockpit terminal, which altscreen_render shows 1:1.
 * Insets are measured by the phone from the FULL 1920x1080 frame, so every preset adds the
 * viewport origin (240, 270). One preset per cockpit view, from the fork's on-car runs:
 *   full    large map view (centred wide)
 *   classic small window between the two dials
 *   sport   small window left of the centre dial (the B9Sport stage starts at x 476)
 * The view comes from Java (/tmp/aa_cluster_view, com.luka.carplay.aa.AaClusterView).
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
 * (0..540 / 0..1440) for full, classic, sport, in that order. Read at phone connect.

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

#define PRESET_DEFAULT {                                                              \
    { 97, 167, 350, 370 },   /* full: card just left of the Audi arrow tile frame */ \
    { 97, 167, 510, 510 },   /* classic: between the dials */                       \
    { 78, 146, 484, 394 },   /* sport: the B9Sport small-stage window */            \
}
static const uint32_t preset_default[3][4] = PRESET_DEFAULT;
static uint32_t preset[3][4] = PRESET_DEFAULT;   /* aa_cluster.insets replaces it at connect */
static int conn_view;        /* layout sent at connect */
static unsigned conn_theme = UI_THEME_DARK;   /* theme sent at connect */
static int native_card_visible = -1, conn_native_card = -1;

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
    aa_log("uiconfig.insets from file");
}

/* Cockpit view from Java: 0 full, 1 classic, 2 sport; -1 unknown (no file yet / mid-write). */
int aa_view_state(void)
{
    char v[9] = {0};
    FILE *f = fopen(AA_VIEW_PATH, "r");
    if (!f) return -1;
    if (!fgets(v, sizeof(v), f)) v[0] = 0;
    fclose(f);
    if (!strncmp(v, "full", 4)) return VIEW_FULL;
    if (!strncmp(v, "classic", 7)) return VIEW_CLASSIC;
    if (!strncmp(v, "sport", 5)) return VIEW_SPORT;
    return -1;
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

/* Accepted Fct44 from Java; missing or incomplete records leave the state unknown. */
static int native_card_state(void)
{
    unsigned char record[3];
    size_t n;
    int error;
    FILE *f = fopen(AA_NATIVE_CARD_PATH, "r");
    if (!f) return -1;
    n = fread(record, 1, sizeof(record), f);
    error = ferror(f);
    fclose(f);
    return !error && n == 2u && record[1] == '\n' && (record[0] == '0' || record[0] == '1')
        ? record[0] - '0' : -1;
}

/* UiConfig body: 0x12 len {insets 0x08 top, 0x10 bottom, 0x18 left, 0x20 right}, 0x20 theme. */
static unsigned uiconfig_body(unsigned char *ui, unsigned max, int view, unsigned theme)
{
    unsigned char in[20];
    unsigned ni = 0, nu = 0;
    uint32_t t = preset[view][0] + VP_OY, b = preset[view][1] + VP_OY;
    uint32_t l = preset[view][2] + VP_OX, r = preset[view][3] + VP_OX;
    in[ni++] = 0x08; ni += varint(in + ni, t);
    in[ni++] = 0x10; ni += varint(in + ni, b);
    in[ni++] = 0x18; ni += varint(in + ni, l);
    in[ni++] = 0x20; ni += varint(in + ni, r);
    if (max < ni + 4u + (native_card_visible >= 0 ? 2u : 0u)) return 0;
    ui[nu++] = 0x12; nu += varint(ui + nu, ni);
    memcpy(ui + nu, in, ni); nu += ni;
    ui[nu++] = 0x20; ui[nu++] = (unsigned char)theme;
    if (native_card_visible >= 0) {
        /* UiConfig field 5: NAVIGATION_TURN_DATA_AVAILABLE=5; empty list clears it. */
        ui[nu++] = native_card_visible ? 0x28 : 0x2a;
        ui[nu++] = native_card_visible ? 5u : 0u;
    }
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

/* The payload (~20 bytes) is longer than the 15-byte SSO buffer, so it goes into a heap
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
    unsigned char ui[40], p[48];
    unsigned nu, n = 0;
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
    view = aa_view_state();
    if (view < 0) view = VIEW_FULL;
    conn_view = view;
    conn_theme = aa_theme_state();
    native_card_visible = conn_native_card = native_card_state();
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
    aa_log("uiconfig.set where=%s view=%d top=%u bottom=%u left=%u right=%u theme=%s bytes=%u",
           where, view, preset[view][0], preset[view][1], preset[view][2], preset[view][3],
           theme_name(conn_theme), n);
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
    unsigned char ui[40];
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
    unsigned char msg[64];
    unsigned n;
    if (!queue) queue = (queue_fn)dlsym(RTLD_DEFAULT, "_ZN13MessageRouter13queueOutgoingEhPvj");
    n = aa_relayout_message(msg, sizeof(msg), view, theme);
    if (!queue || !n || !rd(sink, 8) || !((const unsigned char *)sink)[4]) return 0;
    queue((void *)(uintptr_t)rd(sink, 8), ((const unsigned char *)sink)[5], msg, n);
    aa_log("relayout.sent view=%d theme=%s channel=%u bytes=%u", view, theme_name(theme),
           ((const unsigned char *)sink)[5], n);
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
 * up on one call is sent on the next one. The theme never changes mid-session (issue #19):
 * every 0x8009 carries conn_theme, and a new day/night state is only logged, once. */
void aa_relayout_tick(void *sink)
{
    static void *cur_sink;
    static uint64_t next_check;
    static int sent_view, sent_native_card, pending_view = -1, off;
    static unsigned deferred_theme;
    uint64_t now;
    int view, native_card;
    unsigned theme;
    if (!sink) return;
    if (sink != cur_sink || !((const unsigned char *)sink)[4]) {   /* new session or channel closed */
        cur_sink = ((const unsigned char *)sink)[4] ? sink : NULL;
        next_check = 0;
        sent_view = conn_view;
        sent_native_card = conn_native_card;
        deferred_theme = conn_theme;
        pending_view = -1;
        off = aa_marker(AA_MARKER_RELAYOUT_OFF);
        if (!cur_sink) return;
    }
    if (off) return;
    if (pending_view >= 0) {
        int v = pending_view;
        pending_view = -1;
        if (relayout_send(sink, v, conn_theme)) {
            sent_view = v;
            sent_native_card = native_card_visible;
        }
        return;
    }
    now = mono_ms();
    if (now < next_check) return;
    next_check = now + 300u;
    view = aa_view_state();
    if (view < 0) view = sent_view;                     /* mid-write / no record: keep */
    native_card = native_card_state();
    if (native_card >= 0) native_card_visible = native_card;
    if (view != sent_view || native_card_visible != sent_native_card) pending_view = view;
    theme = aa_theme_state();
    if (theme != deferred_theme) {
        deferred_theme = theme;
        aa_log("theme.deferred theme=%s session=%s (applies at the next phone connect)",
               theme_name(theme), theme_name(conn_theme));
    }
}
