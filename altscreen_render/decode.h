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
} altr_frame_t;

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

/* Feed Annex-B H.264 bytes (any chunking). Emits frames via the callback.
 * Returns 0 on success, <0 on a fatal decoder error. */
int altr_decode_feed(altr_decoder_t *d, const uint8_t *data, size_t len);

/* Drain buffered frames (end of stream). */
void altr_decode_flush(altr_decoder_t *d);

void altr_decode_destroy(altr_decoder_t *d);

uint64_t altr_decode_frame_count(const altr_decoder_t *d);
uint64_t altr_decode_error_count(const altr_decoder_t *d);

#endif /* ALTR_DECODE_H */
