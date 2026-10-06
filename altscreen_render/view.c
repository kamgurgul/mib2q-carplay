/*
 * altscreen_render — view geometry (see altr_view_t in decode.h).
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
