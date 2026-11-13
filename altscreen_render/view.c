/*
 * altscreen_render — view geometry, YUV matrix and calibration ruler (see decode.h).
 * No FFmpeg/QNX/GL dependency; host-tested by tests/altscreen_view_test.c.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "decode.h"

int altr_frame_crop(const altr_frame_t *in, const altr_view_t *v, altr_frame_t *out)
{
    int x, y;
    *out = *in;
    if (!v || v->view_w <= 0 || v->view_h <= 0 ||
        in->width < v->view_w || in->height < v->view_h)
        return 0;
    if (in->width == v->view_w && in->height == v->view_h)
        return 0;
    x = v->crop_x;
    y = v->crop_y;
    if (x > in->width - v->view_w) x = in->width - v->view_w;
    if (y > in->height - v->view_h) y = in->height - v->view_h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    /* 4:2:0 chroma covers 2x2 luma: start on an even sample so luma and chroma agree. */
    x &= ~((1 << in->chroma_shift_w) - 1);
    y &= ~((1 << in->chroma_shift_h) - 1);
    out->width = v->view_w;
    out->height = v->view_h;
    out->y = in->y + (long)y * in->ystride + x;
    out->u = in->u + (long)(y >> in->chroma_shift_h) * in->ustride + (x >> in->chroma_shift_w);
    out->v = in->v + (long)(y >> in->chroma_shift_h) * in->vstride + (x >> in->chroma_shift_w);
    return 1;
}

void altr_yuv_matrix(int colorspace, int full_range, altr_yuv_matrix_t *m)
{
    /* Kr/Kb of the two matrices; limited range scales luma by 255/219 and chroma by
     * 255/224 (the renderer's historical BT.601 numbers are exactly these). */
    double kr = colorspace == ALTR_CS_BT709 ? 0.2126 : 0.299;
    double kb = colorspace == ALTR_CS_BT709 ? 0.0722 : 0.114;
    double kg = 1.0 - kr - kb;
    double cs = full_range ? 1.0 : 255.0 / 224.0;
    m->ys = full_range ? 1.0f : (float)(255.0 / 219.0);
    m->yo = full_range ? 0.0f : (float)(16.0 / 255.0);
    m->rv = (float)(2.0 * (1.0 - kr) * cs);
    m->bu = (float)(2.0 * (1.0 - kb) * cs);
    m->gu = (float)(2.0 * (1.0 - kb) * kb / kg * cs);
    m->gv = (float)(2.0 * (1.0 - kr) * kr / kg * cs);
}

static int grid_add(altr_rect_t *out, int n, int max, int x, int y, int w, int h, uint32_t argb)
{
    if (n >= max || w <= 0 || h <= 0) return n;
    out[n].x = x; out[n].y = y; out[n].w = w; out[n].h = h; out[n].argb = argb;
    return n + 1;
}

int altr_grid_rects(int width, int height, altr_rect_t *out, int max)
{
    static const uint32_t rows[8] = {
        0xFFFF0000u, 0xFFFF8000u, 0xFFFFFF00u, 0xFF00FF00u,   /*  60 120 180 240 */
        0xFF00FFFFu, 0xFF0000FFu, 0xFFFF00FFu, 0xFFFFFFFFu,   /* 300 360 420 480 */
    };
    const uint32_t grey = 0xFF808080u, edge = 0xFF999999u;
    int i, x, n = 0;
    if (!out || max <= 0 || width < 8 || height < 8) return 0;
    for (x = 180; x < width; x += 180)
        n = grid_add(out, n, max, x - 1, 0, 2, height, grey);
    for (i = 0; i < 8 && (i + 1) * 60 < height; i++)
        n = grid_add(out, n, max, 0, (i + 1) * 60 - 3, width, 6, rows[i]);
    /* Canvas edges: if the bottom one is missing, the VC crops below it. */
    n = grid_add(out, n, max, 0, 0, width, 4, edge);
    n = grid_add(out, n, max, 0, height - 4, width, 4, edge);
    n = grid_add(out, n, max, 0, 0, 4, height, edge);
    n = grid_add(out, n, max, width - 4, 0, 4, height, edge);
    return n;
}
