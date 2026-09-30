/*
 * altscreen_render — host decode test.
 *
 * Validates decode.c on the host without a vehicle: reads an .mp4 or raw
 * Annex-B .264 file, feeds it to the decoder the same way the on-car tee client
 * will, and prints frame/size/error stats.  For .mp4 it demuxes the video track
 * and applies the h264_mp4toannexb bitstream filter, mirroring the AltScreen
 * hook's AVCC->Annex-B conversion.
 *
 * Build/run: see altscreen_render/Makefile target `hosttest`.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>

struct stats { int w, h; uint64_t n; };
static FILE *g_dump; /* optional Annex-B dump (ALTR_DUMP_ANNEXB) */

static void on_frame(void *user, const altr_frame_t *f)
{
    struct stats *s = (struct stats *)user;
    if (s->n == 0) { s->w = f->width; s->h = f->height; }
    s->n++;
    if (s->n <= 3 || (s->n % 100) == 0)
        printf("  frame %llu: %dx%d y/u/v stride=%d/%d/%d\n",
               (unsigned long long)s->n, f->width, f->height,
               f->ystride, f->ustride, f->vstride);
}

static int is_mp4(const char *path)
{
    const char *dot = strrchr(path, '.');
    return dot && (strcmp(dot, ".mp4") == 0 || strcmp(dot, ".mov") == 0 ||
                   strcmp(dot, ".m4v") == 0);
}

static int run_raw(const char *path, struct stats *st, altr_decoder_t *dec)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) { perror("open"); return 1; }
    uint8_t buf[32768];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        { if(g_dump) fwrite(buf,1,n,g_dump); altr_decode_feed(dec, buf, n); }
    fclose(fp);
    (void)st;
    return 0;
}

static int run_mp4(const char *path, struct stats *st, altr_decoder_t *dec)
{
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, path, NULL, NULL) < 0) {
        fprintf(stderr, "cannot open %s\n", path); return 1;
    }
    if (avformat_find_stream_info(fmt, NULL) < 0) {
        fprintf(stderr, "no stream info\n"); avformat_close_input(&fmt); return 1;
    }
    int vs = -1;
    for (unsigned i = 0; i < fmt->nb_streams; i++)
        if (fmt->streams[i]->codecpar->codec_id == AV_CODEC_ID_H264) { vs = (int)i; break; }
    if (vs < 0) { fprintf(stderr, "no H264 track\n"); avformat_close_input(&fmt); return 1; }

    const AVBitStreamFilter *bsf = av_bsf_get_by_name("h264_mp4toannexb");
    AVBSFContext *bsfc = NULL;
    if (!bsf || av_bsf_alloc(bsf, &bsfc) < 0 ||
        avcodec_parameters_copy(bsfc->par_in, fmt->streams[vs]->codecpar) < 0 ||
        av_bsf_init(bsfc) < 0) {
        fprintf(stderr, "bsf init failed\n");
        if (bsfc) av_bsf_free(&bsfc);
        avformat_close_input(&fmt); return 1;
    }

    AVPacket *pkt = av_packet_alloc();
    while (av_read_frame(fmt, pkt) == 0) {
        if (pkt->stream_index == vs) {
            if (av_bsf_send_packet(bsfc, pkt) == 0) {
                while (av_bsf_receive_packet(bsfc, pkt) == 0)
                    { if(g_dump) fwrite(pkt->data,1,(size_t)pkt->size,g_dump); altr_decode_feed(dec, pkt->data, (size_t)pkt->size); }
            }
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    av_bsf_free(&bsfc);
    avformat_close_input(&fmt);
    (void)st;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.mp4|file.264>\n", argv[0]);
        return 2;
    }
    struct stats st; memset(&st, 0, sizeof(st));
    { const char *dp=getenv("ALTR_DUMP_ANNEXB"); if(dp&&*dp) g_dump=fopen(dp,"wb"); }
    altr_decoder_t *dec = altr_decode_create(on_frame, &st);
    if (!dec) { fprintf(stderr, "decoder create failed\n"); return 1; }

    int rc = is_mp4(argv[1]) ? run_mp4(argv[1], &st, dec)
                             : run_raw(argv[1], &st, dec);
    altr_decode_flush(dec);

    printf("decoded frames=%llu errors=%llu first=%dx%d\n",
           (unsigned long long)altr_decode_frame_count(dec),
           (unsigned long long)altr_decode_error_count(dec),
           st.w, st.h);
    uint64_t frames = altr_decode_frame_count(dec);
    if(g_dump) fclose(g_dump);
    altr_decode_destroy(dec);
    if (rc != 0) return rc;
    return frames > 0 ? 0 : 3;
}
