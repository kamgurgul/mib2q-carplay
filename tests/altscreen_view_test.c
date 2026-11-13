/*
 * altscreen_render view geometry: the Android Auto 1920x1080 picture shows its
 * centred 1440x540 viewport 1:1; CarPlay's window-sized picture passes through.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "decode.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

static uint8_t Y[1920 * 1080], U[960 * 540], V[960 * 540];

static altr_frame_t frame(int w, int h)
{
    altr_frame_t f;
    f.width = w; f.height = h;
    f.y = Y; f.u = U; f.v = V;
    f.ystride = w + 32; f.ustride = w / 2 + 16; f.vstride = w / 2 + 16;  /* padded linesizes */
    f.chroma_shift_w = f.chroma_shift_h = 1;
    return f;
}

int main(void)
{
    altr_view_t aa = { 1920, 1080, 240, 270, 1440, 540 };
    altr_view_t cp = { 1440, 540, 0, 0, 1440, 540 };
    altr_frame_t in, out;

    /* Android Auto: centred viewport, 1:1, planes offset by the crop. */
    in = frame(1920, 1080);
    CHECK(altr_frame_crop(&in, &aa, &out) == 1, "AA picture not cropped");
    CHECK(out.width == 1440 && out.height == 540, "AA view size");
    CHECK(out.y == Y + 270L * in.ystride + 240, "AA luma origin");
    CHECK(out.u == U + 135L * in.ustride + 120, "AA chroma U origin");
    CHECK(out.v == V + 135L * in.vstride + 120, "AA chroma V origin");
    CHECK(out.ystride == in.ystride && out.ustride == in.ustride, "strides kept");

    /* CarPlay: picture == window, passed through unchanged. */
    in = frame(1440, 540);
    CHECK(altr_frame_crop(&in, &cp, &out) == 0, "CarPlay picture cropped");
    CHECK(out.y == Y && out.width == 1440 && out.height == 540, "CarPlay passthrough");

    /* Odd crop origin snaps down to the chroma grid. */
    {
        altr_view_t odd = { 1920, 1080, 241, 271, 1440, 540 };
        in = frame(1920, 1080);
        altr_frame_crop(&in, &odd, &out);
        CHECK(out.y == Y + 270L * in.ystride + 240, "odd origin not snapped to even");
    }

    /* Crop past the picture edge is clamped inside it. */
    {
        altr_view_t far = { 1920, 1080, 900, 900, 1440, 540 };
        in = frame(1920, 1080);
        altr_frame_crop(&in, &far, &out);
        CHECK(out.y == Y + 540L * in.ystride + 480, "crop not clamped into the picture");
    }

    /* A smaller picture than the view (phone fell back to 720p) is shown whole. */
    in = frame(1280, 720);
    CHECK(altr_frame_crop(&in, &aa, &out) == 0 && out.width == 1280 && out.y == Y,
          "small picture not passed through");

    /* YUV matrix: unsignalled = the BT.601 video-range numbers the shader always used. */
    {
        altr_yuv_matrix_t m;
        altr_yuv_matrix(ALTR_CS_UNSPECIFIED, 0, &m);
        CHECK(fabsf(m.ys - 1.1643f) < 1e-3f && fabsf(m.rv - 1.5958f) < 1e-3f &&
              fabsf(m.gu - 0.39173f) < 1e-3f && fabsf(m.gv - 0.81290f) < 1e-3f &&
              fabsf(m.bu - 2.017f) < 1e-3f, "BT.601 video range");
        altr_yuv_matrix(ALTR_CS_BT709, 0, &m);
        CHECK(fabsf(m.rv - 1.7927f) < 1e-3f && fabsf(m.gu - 0.2132f) < 1e-3f &&
              fabsf(m.gv - 0.5329f) < 1e-3f && fabsf(m.bu - 2.1124f) < 1e-3f, "BT.709 video range");
        altr_yuv_matrix(ALTR_CS_BT601, 1, &m);
        CHECK(m.ys == 1.0f && m.yo == 0.0f && fabsf(m.rv - 1.402f) < 1e-3f &&
              fabsf(m.bu - 1.772f) < 1e-3f, "BT.601 full range");
        /* Grey stays grey: chroma 0 gives r = g = b. */
        altr_yuv_matrix(ALTR_CS_BT709, 1, &m);
        CHECK(fabsf(m.rv - 1.5748f) < 1e-3f && fabsf(m.gu - 0.18732f) < 1e-3f &&
              fabsf(m.gv - 0.46812f) < 1e-3f && fabsf(m.bu - 1.8556f) < 1e-3f, "BT.709 full range");
    }

    /* Calibration ruler: 7 verticals, 8 coloured rows, 4 border strips on 1440x540, all
     * inside the window (the hardware blitter rejects a fill outside the buffer). */
    {
        altr_rect_t r[ALTR_GRID_MAX_RECTS];
        int k, n = altr_grid_rects(1440, 540, r, ALTR_GRID_MAX_RECTS), reds = 0, whites = 0;
        CHECK(n == 7 + 8 + 4, "grid rectangle count");
        for (k = 0; k < n; k++) {
            CHECK(r[k].x >= 0 && r[k].y >= 0 && r[k].w > 0 && r[k].h > 0 &&
                  r[k].x + r[k].w <= 1440 && r[k].y + r[k].h <= 540, "grid rect inside the window");
            if (r[k].argb == 0xFFFF0000u) { reds++; CHECK(r[k].y == 57 && r[k].h == 6, "red row at 60"); }
            if (r[k].argb == 0xFFFFFFFFu) { whites++; CHECK(r[k].y == 477, "white row at 480"); }
        }
        CHECK(reds == 1 && whites == 1, "one red and one white row");
        CHECK(altr_grid_rects(1440, 540, r, 5) == 5, "grid respects max");
        CHECK(altr_grid_rects(4, 4, r, ALTR_GRID_MAX_RECTS) == 0, "no grid on a tiny window");
    }

    if (failures) return 1;
    printf("altscreen_view_test: AA centred 1:1 viewport, CarPlay passthrough, chroma snap, clamp, "
           "YUV matrices, calibration ruler PASS\n");
    return 0;
}
