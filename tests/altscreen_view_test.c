/*
 * altscreen_render view geometry: the Android Auto 1920x1080 picture shows its
 * centred 1440x540 viewport 1:1; CarPlay's window-sized picture passes through.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "decode.h"

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

    if (failures) return 1;
    printf("altscreen_view_test: AA centred 1:1 viewport, CarPlay passthrough, chroma snap, clamp PASS\n");
    return 0;
}
