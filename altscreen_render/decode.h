/*
 * altscreen_render — H.264 Annex-B decoder wrapper (FFmpeg libavcodec).
 *
 * Turns the loopback Annex-B byte stream produced by the AltScreen hook tee
 * (127.0.0.1:19820) into decoded YUV frames.  Plane pointers handed to the
 * callback are valid only for the duration of that callback.
 *
 * This unit has NO QNX/GL/screen dependency and is exercised on the host by
 * host_decode_test.c so the decode path can be validated without a vehicle.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ALTR_DECODE_H
#define ALTR_DECODE_H

#include <stddef.h>
#include <stdint.h>

typedef struct altr_frame {
    int width, height;
    /* YUV planes (planar 8-bit). For 4:2:0 the chroma planes are half size. */
    const uint8_t *y, *u, *v;
    int ystride, ustride, vstride;
    int chroma_shift_w, chroma_shift_h; /* 1,1 for 4:2:0 */
    int colorspace;     /* ALTR_CS_*: the stream's matrix_coefficients (VUI), 0 = not signalled */
    int full_range;     /* 1 = full-range (JPEG) samples, 0 = video range 16-235 */
} altr_frame_t;

/* YUV -> RGB matrix of a picture. Unsignalled (the CarPlay and Android Auto streams seen
 * so far) keeps the BT.601 video-range matrix the renderer always used. */
enum { ALTR_CS_UNSPECIFIED = 0, ALTR_CS_BT601 = 1, ALTR_CS_BT709 = 2 };

/* r = ys*(Y - yo) + rv*V, g = ys*(Y - yo) - gu*U - gv*V, b = ys*(Y - yo) + bu*U,
 * with U, V centred on 0 (sample - 0.5) and every value in 0..1. */
typedef struct altr_yuv_matrix {
    float ys, yo, rv, gu, gv, bu;
} altr_yuv_matrix_t;

void altr_yuv_matrix(int colorspace, int full_range, altr_yuv_matrix_t *m);

/* Calibration ruler, one list for the GL path and the hardware blitter: rectangles in
 * window pixels (origin top-left, as iOS sees the canvas), colour 0xAARRGGBB. Horizontal
 * lines every 60 px (red 60, orange 120, yellow 180, green 240, cyan 300, blue 360,
 * magenta 420, white 480), grey verticals every 180 px, a grey border. Returns the count
 * (at most max). */
typedef struct altr_rect { int x, y, w, h; uint32_t argb; } altr_rect_t;
#define ALTR_GRID_MAX_RECTS 24
int altr_grid_rects(int width, int height, altr_rect_t *out, int max);

typedef void (*altr_frame_cb)(void *user, const altr_frame_t *f);

/* What the renderer shows of each decoded picture: the view_w x view_h rectangle
 * at (crop_x, crop_y), presented 1:1 in the cluster window. stream_w/h is the coded
 * size the hardware decoder is opened with. CarPlay AltScreen: stream = view, crop
 * 0,0. Android Auto: a 1920x1080 stream whose centred 1440x540 viewport is the
 * cockpit terminal. */
typedef struct altr_view {
    int stream_w, stream_h;
    int crop_x, crop_y;
    int view_w, view_h;
} altr_view_t;

/* out = the part of in that v shows, sharing in's planes (no copy). The crop is
 * clamped into the picture and kept on even (chroma) coordinates; a picture
 * smaller than the view is passed through whole. Returns 1 when out is a crop,
 * 0 when it is the whole picture. */
int altr_frame_crop(const altr_frame_t *in, const altr_view_t *v, altr_frame_t *out);

typedef struct altr_decoder altr_decoder_t;

/* Create a decoder. cb is invoked once per decoded frame. NULL on failure. */
altr_decoder_t *altr_decode_create(altr_frame_cb cb, void *user);

/* threads <= 1: the low-delay decoder above, every picture out as soon as it decodes.
 * threads > 1: FFmpeg frame threading for a stream that keeps pictures coming (Android
 * Auto's 30 FPS), roughly threads times the throughput, each picture one AU later. Not
 * for a sparse stream: the last picture of a burst waits for the next AU (or a flush). */
altr_decoder_t *altr_decode_create_threads(altr_frame_cb cb, void *user, int threads);

/* As above; fast = 1 skips the deblocking filter and allows FFmpeg's non-bit-exact
 * shortcuts (AV_CODEC_FLAG2_FAST), for a stream software decode cannot keep up with. */
altr_decoder_t *altr_decode_create_opts(altr_frame_cb cb, void *user, int threads, int fast);

/* Feed Annex-B H.264 bytes (any chunking). Emits frames via the callback.
 * Returns 0 on success, <0 on a fatal decoder error. */
int altr_decode_feed(altr_decoder_t *d, const uint8_t *data, size_t len);

/* Drain buffered frames (end of stream). */
void altr_decode_flush(altr_decoder_t *d);

void altr_decode_destroy(altr_decoder_t *d);

uint64_t altr_decode_frame_count(const altr_decoder_t *d);
uint64_t altr_decode_error_count(const altr_decoder_t *d);

#endif /* ALTR_DECODE_H */
