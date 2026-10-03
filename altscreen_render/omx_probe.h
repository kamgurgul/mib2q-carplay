/*
 * altscreen_render — on-car probe of the Qualcomm OMX H.264 decoder (see omx_probe.c).
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ALTR_OMX_PROBE_H
#define ALTR_OMX_PROBE_H

/* Load libOmxCore.so, open OMX.qcom.video.decoder.avc for width x height and log
 * the port requirements to stderr. Decodes nothing. 0 = probe ran, -1 = failed early. */
int altr_omx_probe(int width, int height);

#endif /* ALTR_OMX_PROBE_H */
