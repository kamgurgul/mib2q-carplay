/*
 * altscreen_render — H.264 Annex-B decoder wrapper (FFmpeg libavcodec).
 * See decode.h.  No QNX/GL dependency; host-testable.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>

struct altr_decoder {
    const AVCodec   *codec;
    AVCodecContext  *ctx;
    AVCodecParserContext *parser;
    AVPacket        *pkt;
    AVFrame         *frame;
    altr_frame_cb    cb;
    void            *user;
    uint64_t         frames;
    uint64_t         errors;
};

static void emit(altr_decoder_t *d);

/* Copy the parser's AU and send it. On EAGAIN, drain output and resend once. */
static int send_au(altr_decoder_t *d)
{
    AVPacket *owned;
    int r;
    int attempt;

    if (!d->parser) { d->errors++; return -1; }
    owned = av_packet_alloc();
    if (!owned) { d->errors++; return -1; }
    if (av_new_packet(owned, d->pkt->size) < 0) {
        av_packet_free(&owned);
        d->errors++;
        return -1;
    }
    memcpy(owned->data, d->pkt->data, (size_t)d->pkt->size);
    r = 0;
    for (attempt = 0; attempt < 2; attempt++) {
        r = avcodec_send_packet(d->ctx, owned);
        if (r != AVERROR(EAGAIN)) break;
        emit(d);
    }
    av_packet_free(&owned);
    if (r < 0 && r != AVERROR_EOF) {
        d->errors++;
        return -1;
    }
    return 0;
}

static void emit(altr_decoder_t *d)
{
    while (1) {
        int r = avcodec_receive_frame(d->ctx, d->frame);
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return;
        if (r < 0) { d->errors++; return; }

        altr_frame_t f;
        memset(&f, 0, sizeof(f));
        f.width  = d->frame->width;
        f.height = d->frame->height;
        f.y = d->frame->data[0];
        f.u = d->frame->data[1];
        f.v = d->frame->data[2];
        f.ystride = d->frame->linesize[0];
        f.ustride = d->frame->linesize[1];
        f.vstride = d->frame->linesize[2];
        /* CarPlay H.264 is 4:2:0 8-bit; guard anything else off the callback. */
        f.chroma_shift_w = 1;
        f.chroma_shift_h = 1;
        if (f.width > 0 && f.height > 0 && f.y && f.u && f.v) {
            d->frames++;
            if (d->cb) d->cb(d->user, &f);
        } else {
            d->errors++;
        }
        av_frame_unref(d->frame);
    }
}

altr_decoder_t *altr_decode_create(altr_frame_cb cb, void *user)
{
    altr_decoder_t *d = (altr_decoder_t *)calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->cb = cb; d->user = user;

    d->codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!d->codec) { fprintf(stderr, "decode: no H264 decoder\n"); goto fail; }
    d->parser = av_parser_init(d->codec->id);
    if (!d->parser) { fprintf(stderr, "decode: parser init failed\n"); goto fail; }
    d->ctx = avcodec_alloc_context3(d->codec);
    if (!d->ctx) goto fail;
    /* Low-latency: emit each frame as soon as it decodes. */
    d->ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    if (avcodec_open2(d->ctx, d->codec, NULL) < 0) {
        fprintf(stderr, "decode: avcodec_open2 failed\n"); goto fail;
    }
    d->pkt = av_packet_alloc();
    d->frame = av_frame_alloc();
    if (!d->pkt || !d->frame) goto fail;
    return d;

fail:
    altr_decode_destroy(d);
    return NULL;
}

int altr_decode_feed(altr_decoder_t *d, const uint8_t *data, size_t len)
{
    if (!d || !d->parser) return -1;
    while (len > 0) {
        int used = av_parser_parse2(d->parser, d->ctx,
                                    &d->pkt->data, &d->pkt->size,
                                    data, (int)len,
                                    AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (used < 0) { d->errors++; return -1; }
        data += used;
        len  -= (size_t)used;
        if (d->pkt->size > 0) {
            /* Parser output is only valid until the next av_parser_parse2, and
             * EAGAIN means this AU was not accepted. Own a copy so the retry
             * after emit() still has the bytes. */
            if (send_au(d) != 0) {
                /* Non-fatal: skip this access unit, keep the stream going. */
            }
            emit(d);
        }
    }
    return 0;
}

void altr_decode_flush(altr_decoder_t *d)
{
    if (!d || !d->ctx) return;
    avcodec_send_packet(d->ctx, NULL); /* enter drain mode */
    emit(d);
    avcodec_flush_buffers(d->ctx);
    /* The parser keeps any partial NAL. The next tee connection is a new stream. */
    if (d->parser) {
        av_parser_close(d->parser);
        d->parser = NULL;
    }
    if (d->codec)
        d->parser = av_parser_init(d->codec->id);
    if (!d->parser)
        fprintf(stderr, "decode: parser re-init failed\n");
}

void altr_decode_destroy(altr_decoder_t *d)
{
    if (!d) return;
    if (d->frame)  av_frame_free(&d->frame);
    if (d->pkt)    av_packet_free(&d->pkt);
    if (d->parser) av_parser_close(d->parser);
    if (d->ctx)    avcodec_free_context(&d->ctx);
    free(d);
}

uint64_t altr_decode_frame_count(const altr_decoder_t *d){ return d ? d->frames : 0; }
uint64_t altr_decode_error_count(const altr_decoder_t *d){ return d ? d->errors : 0; }
