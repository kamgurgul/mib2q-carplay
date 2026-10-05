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

typedef struct altr_hwdec altr_hwdec_t;

/* Open the decoder and its managed cluster window (displayable id). NULL = use
 * the software path (the reason is logged to stderr). */
altr_hwdec_t *altr_hwdec_create(int width, int height, int displayable_id);

/* Feed Annex-B bytes in any chunking. <0 = the decoder failed; destroy it and
 * fall back. */
int altr_hwdec_feed(altr_hwdec_t *d, const uint8_t *data, size_t len);

/* Pictures posted to the screen so far. */
uint64_t altr_hwdec_frames(altr_hwdec_t *d);

/* 1 once the component reported an error or an unsupported port change. */
int altr_hwdec_failed(altr_hwdec_t *d);

/* 0 = released. -1 = the decoder never left Executing; its buffers were left
 * allocated on purpose and the caller must restart the process. */
int altr_hwdec_destroy(altr_hwdec_t *d);

#endif /* ALTR_HW_DECODE_H */
