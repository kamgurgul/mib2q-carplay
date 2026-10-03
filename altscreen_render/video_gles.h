/*
 * altscreen_render — GLES2 YUV420P -> RGB presenter.
 *
 * Uploads a decoded 4:2:0 frame as three luma/chroma textures and draws a
 * full-surface quad with a BT.601 (video-range) YUV->RGB fragment shader.
 * The GL calls are guarded for PLATFORM_QNX; on the host this compiles to stubs
 * so the decode path stays testable.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ALTR_VIDEO_GLES_H
#define ALTR_VIDEO_GLES_H

#include "decode.h"

typedef struct altr_gles altr_gles_t;

/* Create the GL program + textures. Requires a current EGL context.
 * surface_w/h are the drawable (cluster window) pixel size. NULL on failure. */
altr_gles_t *altr_gles_create(int surface_w, int surface_h);

/* Upload + draw one frame. Does not swap buffers (caller owns eglSwapBuffers).
 * Returns 0 on success. */
int altr_gles_draw(altr_gles_t *g, const altr_frame_t *f);

/* Update the drawable size after a window recreate. */
void altr_gles_resize(altr_gles_t *g, int surface_w, int surface_h);

void altr_gles_destroy(altr_gles_t *g);

#endif /* ALTR_VIDEO_GLES_H */
