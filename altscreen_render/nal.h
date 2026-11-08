/*
 * Annex-B helpers shared by the hardware decoder (hw_decode.c, QNX) and the host
 * fuzz test (tests/altscreen_nal_fuzz.c). Header-only so the QNX build and the
 * host test run the same code.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ALTR_NAL_H
#define ALTR_NAL_H

#include <stddef.h>
#include <stdint.h>

/* Leading SPS/PPS (and AUD/SEI before them) of an Annex-B AU: bytes to send as
 * CODECCONFIG. 0 when the AU starts with a picture. The result is 0, n, or the
 * offset of the start code of the first picture NAL. */
static inline size_t altr_config_prefix(const uint8_t *p, size_t n)
{
    size_t i = 0;
    int have_cfg = 0;
    while (i + 3 < n) {
        size_t sc = 0;
        if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1) sc = 3;
        else if (i + 4 < n && p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 0 && p[i + 3] == 1) sc = 4;
        if (!sc) { ++i; continue; }
        {
            unsigned type = p[i + sc] & 31u;
            if (type == 7 || type == 8) { have_cfg = 1; }
            else if (type == 9 || type == 6) { /* AUD / SEI ahead of the parameter sets */ }
            else return have_cfg ? i : 0;   /* first picture NAL */
        }
        i += sc + 1;
    }
    return have_cfg ? n : 0;
}

#endif
