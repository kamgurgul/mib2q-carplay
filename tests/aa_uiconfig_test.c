/*
 * Host test for aa_hook/aa_uiconfig.c: the per-view 0x8009 UpdateUiConfigRequest bytes (the
 * fork's car-validated viewport presets, sent in full 1920x1080 frame coordinates), the view
 * record Java writes, the density file and the insets override file.
 * Build with -DAA_VIEW_PATH / -DAA_FILE_INSETS / -DAA_FILE_DPI pointing at a temp dir.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../aa_hook/aa_uiconfig.c"

#undef NDEBUG
#include <assert.h>
#include <stdio.h>

void aa_log(const char *fmt, ...) { (void)fmt; }
int aa_marker(const char *path) { (void)path; return 0; }

#define V2(v) (unsigned char)(0x80 | ((v) & 0x7f)), (unsigned char)((v) >> 7)

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    assert(f);
    fputs(text, f);
    fclose(f);
}

/* want = the insets message (wn bytes) followed by the theme field (0x20 2). */
static void check_view(int view, const unsigned char *want, unsigned wn)
{
    unsigned char p[64];
    unsigned n = aa_relayout_message(p, sizeof(p), view);
    assert(n == 4 + 2 + wn + 2);
    assert(p[0] == 0x80 && p[1] == 0x09 && p[2] == 0x0a && p[3] == 2 + wn + 2);   /* 0x8009, field 1 */
    assert(p[4] == 0x12 && p[5] == wn);                                          /* UiConfig.insets */
    assert(!memcmp(p + 6, want, wn + 2));
}

int main(void)
{
    /* insets top/bottom/left/right = preset + viewport origin (270 y, 240 x), then dark theme */
    static const unsigned char full[] = { 0x08, V2(97 + 270), 0x10, V2(167 + 270), 0x18, V2(350 + 240),
                                          0x20, V2(370 + 240), 0x20, 2 };
    static const unsigned char classic[] = { 0x08, V2(97 + 270), 0x10, V2(167 + 270), 0x18, V2(510 + 240),
                                             0x20, V2(510 + 240), 0x20, 2 };
    static const unsigned char sport[] = { 0x08, V2(78 + 270), 0x10, V2(146 + 270), 0x18, V2(484 + 240),
                                           0x20, V2(394 + 240), 0x20, 2 };
    unsigned char p[64];

    /* Presets are valid before any connect-time UiConfig ran. */
    check_view(0, full, sizeof(full) - 2);
    check_view(1, classic, sizeof(classic) - 2);
    check_view(2, sport, sizeof(sport) - 2);
    assert(aa_relayout_message(p, sizeof(p), 3) == 0 && aa_relayout_message(p, sizeof(p), -1) == 0);
    assert(aa_relayout_message(p, 10, 0) == 0);                     /* no room: nothing */

    /* View record from Java (fixed 8 bytes); missing or garbled = unknown. */
    unlink(AA_VIEW_PATH);
    assert(aa_view_state() == -1);
    put(AA_VIEW_PATH, "full   \n");  assert(aa_view_state() == 0);
    put(AA_VIEW_PATH, "classic\n");  assert(aa_view_state() == 1);
    put(AA_VIEW_PATH, "sport  \n");  assert(aa_view_state() == 2);
    put(AA_VIEW_PATH, "spo");        assert(aa_view_state() == -1);

    /* Density: default 125 (1:1 physical size on the 125 PPI panel); file 80..400. */
    unlink(AA_FILE_DPI);
    assert(aa_dpi() == 125);
    put(AA_FILE_DPI, "140\n");       assert(aa_dpi() == 140);
    put(AA_FILE_DPI, "999\n");       assert(aa_dpi() == 125);

    /* Insets override: 12 numbers in viewport pixels, otherwise the presets stay. */
    put(AA_FILE_INSETS, "10 20 30 40  11 21 31 41  12 22 32 42\n");
    load_insets();
    {
        static const unsigned char want[] = { 0x08, V2(12 + 270), 0x10, V2(22 + 270), 0x18, V2(32 + 240),
                                              0x20, V2(42 + 240), 0x20, 2 };
        check_view(2, want, sizeof(want) - 2);
    }
    put(AA_FILE_INSETS, "10 20 30\n");                   /* too few: defaults back */
    load_insets();
    check_view(0, full, sizeof(full) - 2);
    put(AA_FILE_INSETS, "300 300 0 0  0 0 0 0  0 0 0 0\n");   /* leaves no area: rejected */
    load_insets();
    check_view(0, full, sizeof(full) - 2);
    unlink(AA_FILE_INSETS);
    unlink(AA_FILE_DPI);
    unlink(AA_VIEW_PATH);

    printf("aa_uiconfig_test: full/classic/sport 0x8009 bytes, view record, density, insets file PASS\n");
    return 0;
}
