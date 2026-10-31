/*
 * altscreen_render — hardware H.264 decode straight into the cluster window.
 *
 * OMX.qcom.video.decoder.avc writes its native (Qualcomm tiled NV12) output into
 * the QNX screen buffers of displayable 99 (OMX_UseBuffer, zero copy), and each
 * decoded picture is posted as-is, exactly like stock dio_manager shows the main
 * CarPlay screen. No GL and no CPU copies. See docs/altscreen/hw-decoder.md.
 *
 * Opt-in per session; on any setup mismatch or component error the caller falls
 * back to the FFmpeg + GLES path.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ALTR_HW_DECODE_H
#define ALTR_HW_DECODE_H

#include <stddef.h>
#include <stdint.h>

#include "decode.h"   /* altr_view_t */

typedef struct altr_hwdec altr_hwdec_t;

/* Open the decoder at view->stream_w x stream_h and its managed cluster window
 * (displayable id, view->view_w x view_h). Each picture's crop rectangle is
 * blitted 1:1 into the window. NULL = use the software path (the reason is
 * logged to stderr). *stuck = 1 with NULL: the video hardware stopped answering
 * and the half-open decoder was left allocated; the caller must switch hardware
 * decode off and restart the process. */
altr_hwdec_t *altr_hwdec_create(const altr_view_t *view, int displayable_id, int *stuck);

/* Feed Annex-B bytes in any chunking. <0 = the decoder failed; destroy it and
 * fall back. */
int altr_hwdec_feed(altr_hwdec_t *d, const uint8_t *data, size_t len);

/* Pictures posted to the screen so far. */
uint64_t altr_hwdec_frames(altr_hwdec_t *d);

/* 1 once the component reported an error or an unsupported port change. */
int altr_hwdec_failed(altr_hwdec_t *d);

/* Close only the decoder (the video core is free for stock's main-screen
 * decoder); window 99 keeps the last picture. 0 = done, -1 = stuck as in
 * altr_hwdec_destroy. Feeding a suspended decoder fails. */
int altr_hwdec_suspend(altr_hwdec_t *d);

/* Reopen a suspended decoder on the same window; the next input must start with
 * SPS/PPS + IDR. 0 = running, -1 = failed (*stuck as in altr_hwdec_create;
 * without stuck the caller destroys d). */
int altr_hwdec_resume(altr_hwdec_t *d, int *stuck);

int altr_hwdec_suspended(altr_hwdec_t *d);

/* 0 = released. -1 = the decoder never left Executing or the video hardware
 * stopped answering; its buffers were left allocated on purpose and the caller
 * must restart the process. */
int altr_hwdec_destroy(altr_hwdec_t *d);

#endif /* ALTR_HW_DECODE_H */
