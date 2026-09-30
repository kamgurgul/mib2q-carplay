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
