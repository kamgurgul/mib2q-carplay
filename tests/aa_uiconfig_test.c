/*
 * Host test for aa_hook/aa_uiconfig.c: the per-view 0x8009 UpdateUiConfigRequest bytes (margins
 * around the 1440x540 viewport, then the B9 viewport presets as content and stable insets,
 * issue #53; the 1.4 encoding behind aa_cluster.uiconfig_legacy), the connect-time UiConfig in
 * the VideoConfiguration, the view record Java writes with the stock layout's geometry and how
 * the presets follow it (Q7, issue #53), the density file, the insets override file and the
 * opt-in day/night theme.
 * Build with -DAA_VIEW_PATH / -DAA_FILE_INSETS / -DAA_FILE_DPI / -DAA_THEME_PATH pointing at a
 * temp dir.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../aa_hook/aa_uiconfig.c"

#undef NDEBUG
#include <assert.h>
#include <stdio.h>

void aa_log(const char *fmt, ...) { (void)fmt; }
static int daynight;          /* aa_cluster.daynight present */
static int legacy;            /* aa_cluster.uiconfig_legacy present */
int aa_marker(const char *path)
{
    return (daynight && !strcmp(path, AA_MARKER_DAYNIGHT))
        || (legacy && !strcmp(path, AA_MARKER_UICONFIG_LEGACY));
}

#define V2(v) (unsigned char)(0x80 | ((v) & 0x7f)), (unsigned char)((v) >> 7)

/* The receiver's operator new, found by heap_copy through dlsym like on the car. */
static unsigned char new_buf[128];
static unsigned new_len;
void *_Znwj(unsigned n) { assert(n <= sizeof(new_buf)); new_len = n; return new_buf; }

/* Independent encoder for the expected bytes. */
static unsigned enc_varint(unsigned char *b, unsigned v)
{
    unsigned n = 0;
    while (v >= 0x80u) { b[n++] = (unsigned char)(v | 0x80u); v >>= 7; }
    b[n++] = (unsigned char)v;
    return n;
}
static unsigned enc_insets(unsigned char *o, unsigned char tag, unsigned t, unsigned bo, unsigned l, unsigned r)
{
    unsigned char in[20];
    unsigned ni = 0;
    in[ni++] = 0x08; ni += enc_varint(in + ni, t);
    in[ni++] = 0x10; ni += enc_varint(in + ni, bo);
    in[ni++] = 0x18; ni += enc_varint(in + ni, l);
    in[ni++] = 0x20; ni += enc_varint(in + ni, r);
    o[0] = tag; o[1] = (unsigned char)ni;
    memcpy(o + 2, in, ni);
    return ni + 2;
}
/* Expected UiConfig body for viewport insets t/b/l/r. */
static unsigned want_body(unsigned char *o, int old, unsigned t, unsigned b, unsigned l, unsigned r, unsigned theme)
{
    unsigned n = 0;
    if (old) {
        n += enc_insets(o + n, 0x12, t + 270, b + 270, l + 240, r + 240);
    } else {
        n += enc_insets(o + n, 0x0a, 270, 270, 240, 240);
        n += enc_insets(o + n, 0x12, t, b, l, r);
        n += enc_insets(o + n, 0x1a, t, b, l, r);
    }
    o[n++] = 0x20; o[n++] = (unsigned char)theme;
    return n;
}

/* MessageRouter::queueOutgoing, found by relayout_send through dlsym like on the car. */
static int queued;
static unsigned char queued_msg[96];
static unsigned queued_len, queued_channel;
void _ZN13MessageRouter13queueOutgoingEhPvj(void *router, unsigned char channel, void *data, unsigned len)
{
    (void)router;
    queued++;
    queued_channel = channel;
    queued_len = len < sizeof(queued_msg) ? len : (unsigned)sizeof(queued_msg);
    memcpy(queued_msg, data, queued_len);
}

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    assert(f);
    fputs(text, f);
    fclose(f);
}

/* 0x8009 for a view = header + the UiConfig for viewport insets t/b/l/r, dark theme, in the
 * encoding the session connected with (conn_legacy). */
static void check_view(int view, unsigned t, unsigned b, unsigned l, unsigned r)
{
    unsigned char p[96], want[64];
    unsigned wn = want_body(want, conn_legacy, t, b, l, r, UI_THEME_DARK);
    unsigned n = aa_relayout_message(p, sizeof(p), view, UI_THEME_DARK);
    assert(n == 4 + wn);
    assert(p[0] == 0x80 && p[1] == 0x09 && p[2] == 0x0a && p[3] == wn);   /* 0x8009, field 1 = UiConfig */
    assert(!memcmp(p + 4, want, wn));
}
static int queued_is(unsigned t, unsigned b, unsigned l, unsigned r)
{
    unsigned char want[64];
    unsigned wn = want_body(want, conn_legacy, t, b, l, r, UI_THEME_DARK);
    return queued_len == 4 + wn && !memcmp(queued_msg + 4, want, wn);
}

int main(void)
{
    /* The exact wire bytes of the B9 full view, pinned: margins (field 1) 270 270 240 240 around
     * the viewport, then the preset 97 176 350 370 relative to it as content (field 2) and
     * stable (field 3) insets, then the dark theme (field 4). */
    static const unsigned char full_wire[] = {
        0x80, 0x09, 0x0a, 42,
        0x0a, 12, 0x08, V2(270), 0x10, V2(270), 0x18, V2(240), 0x20, V2(240),
        0x12, 11, 0x08, 97, 0x10, V2(176), 0x18, V2(350), 0x20, V2(370),
        0x1a, 11, 0x08, 97, 0x10, V2(176), 0x18, V2(350), 0x20, V2(370),
        0x20, 2 };
    unsigned char p[96];

    /* Presets are valid before any connect-time UiConfig ran. */
    assert(aa_relayout_message(p, sizeof(p), 0, UI_THEME_DARK) == sizeof(full_wire)
        && !memcmp(p, full_wire, sizeof(full_wire)));
    check_view(0, 97, 176, 350, 370);
    check_view(1, 97, 176, 510, 510);
    check_view(2, 78, 176, 484, 394);
    assert(aa_relayout_message(p, sizeof(p), 3, UI_THEME_DARK) == 0
        && aa_relayout_message(p, sizeof(p), -1, UI_THEME_DARK) == 0);
    assert(aa_relayout_message(p, 10, 0, UI_THEME_DARK) == 0);      /* no room: nothing */
    assert(aa_relayout_message(p, sizeof(full_wire) - 1, 0, UI_THEME_DARK) == 0);   /* never truncated */

    /* Light theme: same message, last byte 1. */
    {
        unsigned n = aa_relayout_message(p, sizeof(p), 0, UI_THEME_LIGHT);
        assert(n == sizeof(full_wire) && p[n - 2] == 0x20 && p[n - 1] == 1);
    }

    /* aa_cluster.uiconfig_legacy: the 1.4 bytes, only content insets in frame coordinates. */
    {
        static const unsigned char legacy_full[] = {
            0x80, 0x09, 0x0a, 16,
            0x12, 12, 0x08, V2(97 + 270), 0x10, V2(176 + 270), 0x18, V2(350 + 240), 0x20, V2(370 + 240),
            0x20, 2 };
        conn_legacy = 1;
        assert(aa_relayout_message(p, sizeof(p), 0, UI_THEME_DARK) == sizeof(legacy_full)
            && !memcmp(p, legacy_full, sizeof(legacy_full)));
        check_view(2, 78, 176, 484, 394);
        conn_legacy = 0;
    }

    /* Day/night record from Java (fixed 6 bytes), honoured only with the opt-in marker. */
    unlink(AA_THEME_PATH);
    daynight = 0;
    put(AA_THEME_PATH, "day  \n");    assert(aa_theme_state() == UI_THEME_DARK);   /* no marker */
    daynight = 1;
    assert(aa_theme_state() == UI_THEME_LIGHT);
    put(AA_THEME_PATH, "night\n");    assert(aa_theme_state() == UI_THEME_DARK);
    put(AA_THEME_PATH, "da");         assert(aa_theme_state() == UI_THEME_DARK);   /* mid-write */
    unlink(AA_THEME_PATH);            assert(aa_theme_state() == UI_THEME_DARK);   /* no record */
    daynight = 0;

    /* View record from Java; missing or garbled = unknown. The 8-byte record of a jar before
     * 1.5 has no geometry: the B9 values. */
    {
        int dx = 0, y = 0;
        unlink(AA_VIEW_PATH);
        assert(aa_view_state() == -1);
        put(AA_VIEW_PATH, "full   \n");  assert(aa_view_state() == 0);
        put(AA_VIEW_PATH, "classic\n");  assert(aa_view_state() == 1);
        put(AA_VIEW_PATH, "sport  \n");  assert(view_read(&dx, &y) == 2 && dx == -476 && y == 26);
        put(AA_VIEW_PATH, "spo");        assert(aa_view_state() == -1);
        /* 22 bytes, as AaClusterView.record writes them (tests/AaRouteStateTest.java). */
        put(AA_VIEW_PATH, "sport     -460     18\n");
        assert(view_read(&dx, &y) == 2 && dx == -460 && y == 18);
        put(AA_VIEW_PATH, "classic      0     26\n");
        assert(view_read(&dx, &y) == 1 && dx == 0 && y == 26);
        put(AA_VIEW_PATH, "full      -476     26\n");
        assert(view_read(&dx, &y) == 0 && dx == -476 && y == 26);
        put(AA_VIEW_PATH, "sport     -460    ");            /* mid-write: unknown, B9 values */
        assert(view_read(&dx, &y) == -1 && dx == -476 && y == 26);
        put(AA_VIEW_PATH, "sport     -9999   9999\n");      /* numbers no layout has: B9 values */
        assert(view_read(&dx, &y) == 2 && dx == -476 && y == 26);
    }

    /* The presets follow the layout; geo_dx / geo_y are what a connect or a view change read.
     * Q7 / Q7Sport: map origin row 18 (B9 26) = every window 8 rows lower in the viewport;
     * Sport small stage -460 (B9Sport -476) = the Sport window 16 px further left. */
    geo_dx = -460; geo_y = 18;
    check_view(0, 105, 168, 350, 370);
    check_view(2, 86, 168, 468, 410);
    geo_dx = 0; geo_y = 26;                    /* B9 classic skin: no Sport shift to apply */
    check_view(0, 97, 176, 350, 370);
    check_view(1, 97, 176, 510, 510);
    geo_dx = -476; geo_y = 300;                /* would push an inset below 0: that axis stays */
    check_view(2, 78, 176, 484, 394);
    geo_dx = 1000; geo_y = 26;
    check_view(2, 78, 176, 484, 394);
    geo_dx = -476; geo_y = 26;

    /* Connect: the VideoConfiguration gets field 11 = UiConfig in its unknown-fields string
     * (a heap string from the receiver's operator new), in the encoding the marker picks. */
    {
        unsigned char vconf[0x40] = {0}, want[64];
        unsigned wn;
        wr(vconf, 0x28, AA_CODEC_RES_1080P);
        put(AA_VIEW_PATH, "full      -476     26\n");
        apply(vconf, "test");
        wn = want_body(want, 0, 97, 176, 350, 370, UI_THEME_DARK);
        assert(!conn_legacy && rd(vconf, 0x18) == 2 + wn && rd(vconf, 0x1c) == 2 + wn);
        assert(new_len == 2 + wn + 1 && new_buf[0] == 0x5a && new_buf[1] == wn
            && !memcmp(new_buf + 2, want, wn));
        memset(vconf, 0, sizeof(vconf));
        wr(vconf, 0x28, AA_CODEC_RES_1080P);
        legacy = 1;
        apply(vconf, "test");
        wn = want_body(want, 1, 97, 176, 350, 370, UI_THEME_DARK);
        assert(conn_legacy && rd(vconf, 0x18) == 2 + wn && new_buf[0] == 0x5a && new_buf[1] == wn
            && !memcmp(new_buf + 2, want, wn));
        check_view(0, 97, 176, 350, 370);     /* the session's view changes keep its encoding */
        legacy = 0;
        memset(vconf, 0, sizeof(vconf));
        wr(vconf, 0x28, AA_CODEC_RES_1080P);
        apply(vconf, "test");
        assert(!conn_legacy);
        unlink(AA_VIEW_PATH);
    }

    /* Density: default 125 (1:1 physical size on the 125 PPI panel); file 80..400. */
    unlink(AA_FILE_DPI);
    assert(aa_dpi() == 125);
    put(AA_FILE_DPI, "140\n");       assert(aa_dpi() == 140);
    put(AA_FILE_DPI, "999\n");       assert(aa_dpi() == 125);
    /* Frame rate: 30 unless the shared file says exactly 60 (Android Auto knows only these). */
    unlink(AA_FILE_FPS);             assert(aa_fps() == 30);
    put(AA_FILE_FPS, "60\n");        assert(aa_fps() == 60);
    put(AA_FILE_FPS, "24\n");        assert(aa_fps() == 30);
    put(AA_FILE_FPS, "junk\n");      assert(aa_fps() == 30);
    unlink(AA_FILE_FPS);

    /* Insets override: 12 numbers in viewport pixels, otherwise the presets stay. They are
     * sent as written on every layout. */
    put(AA_FILE_INSETS, "10 20 30 40  11 21 31 41  12 22 32 42\n");
    load_insets();
    check_view(2, 12, 22, 32, 42);
    geo_dx = -460; geo_y = 18;
    check_view(2, 12, 22, 32, 42);
    geo_dx = -476; geo_y = 26;
    put(AA_FILE_INSETS, "10 20 30\n");                   /* too few: defaults back */
    load_insets();
    check_view(0, 97, 176, 350, 370);
    put(AA_FILE_INSETS, "300 300 0 0  0 0 0 0  0 0 0 0\n");   /* leaves no area: rejected */
    load_insets();
    check_view(0, 97, 176, 350, 370);
    unlink(AA_FILE_INSETS);
    unlink(AA_FILE_DPI);

    /* Issue #19: a theme change mid-session is never sent (the phone drops Android Auto);
     * a later view change still goes out, with the theme the session connected with. */
    {
        unsigned char sink[16] = {0};
        sink[4] = 1;                          /* channel open */
        sink[5] = 64;                         /* cluster video channel */
        wr(sink, 8, 0x1234u);                 /* router (opaque to the fake) */
        conn_view = 0;
        conn_theme = UI_THEME_DARK;           /* connected at night */
        daynight = 1;
        put(AA_VIEW_PATH, "full   \n");
        put(AA_THEME_PATH, "day  \n");       /* MMI switched to day */
        queued = 0;
        aa_relayout_tick(sink);               /* reads both records */
        aa_relayout_tick(sink);
        assert(queued == 0);                  /* theme change alone: nothing sent */
        usleep(350000);                       /* past the 300 ms read gate */
        put(AA_VIEW_PATH, "classic\n");
        aa_relayout_tick(sink);               /* view change picked up */
        aa_relayout_tick(sink);               /* ... and sent */
        assert(queued == 1 && queued_channel == 64);
        assert(queued_msg[0] == 0x80 && queued_msg[1] == 0x09);
        assert(queued_msg[queued_len - 2] == 0x20 && queued_msg[queued_len - 1] == UI_THEME_DARK);
        daynight = 0;
        unlink(AA_THEME_PATH);

        /* Issue #53: the session connected on the fallback layout (B9Sport numbers) and the
         * stock Q7Sport layout arrives later. Same view, other insets: sent once. */
        usleep(350000);
        put(AA_VIEW_PATH, "sport     -476     26\n");
        aa_relayout_tick(sink);
        aa_relayout_tick(sink);
        assert(queued == 2 && queued_is(78, 176, 484, 394));
        usleep(350000);
        put(AA_VIEW_PATH, "sport     -460     18\n");
        aa_relayout_tick(sink);
        aa_relayout_tick(sink);
        assert(queued == 3 && queued_is(86, 168, 468, 410));
        usleep(350000);
        aa_relayout_tick(sink);               /* unchanged record: nothing more */
        aa_relayout_tick(sink);
        assert(queued == 3);
        /* An insets file is the same on every layout, so a layout change alone sends nothing. */
        put(AA_FILE_INSETS, "10 20 30 40  11 21 31 41  12 22 32 42\n");
        load_insets();
        usleep(350000);
        put(AA_VIEW_PATH, "sport     -476     26\n");
        aa_relayout_tick(sink);               /* the file's Sport row differs from what was sent */
        aa_relayout_tick(sink);
        assert(queued == 4);
        usleep(350000);
        put(AA_VIEW_PATH, "sport     -460     18\n");
        aa_relayout_tick(sink);
        aa_relayout_tick(sink);
        assert(queued == 4);
        unlink(AA_FILE_INSETS);
        load_insets();
        geo_dx = -476; geo_y = 26;
    }
    unlink(AA_VIEW_PATH);

    printf("aa_uiconfig_test: full/classic/sport 0x8009 bytes (margins + viewport insets, legacy encoding), connect UiConfig, view record with layout geometry (Q7, #53), density, frame rate, insets file, day/night theme, no mid-session theme change (#19) PASS\n");
    return 0;
}
