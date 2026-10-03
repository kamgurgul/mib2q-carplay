/*
 * altscreen_render — hardware H.264 decode into the cluster window (see hw_decode.h).
 *
 * Mirrors stock dio_manager (libairplay.so dio::COMXVideoDecoder + CScreenRender):
 *   - OMX.qcom.video.decoder.avc from /mnt/app/armle/lib/libOmxCore.so;
 *   - output buffers = the QNX screen window buffers (OMX_UseBuffer), window format
 *     from convertColorFormatOMX2Screen (0x7F000004 -> 0x1000C, tiled NV12) and
 *     usage 0x486 (READ|WRITE|VIDEO|OVERLAY), as stock's wfd images were created;
 *   - SPS/PPS as OMX_BUFFERFLAG_CODECCONFIG (0x80), pictures as ENDOFFRAME (0x10)
 *     (COMXVideoDecoder::job_decodeFrame);
 *   - decode-order output via OMX.QCOM.index.config.video.DisplayPictureBuffer
 *     (COMXVideoDecoder::enableDecoderOrderMode), lowest latency for CarPlay's
 *     B-frame-free stream.
 * Probe values on the car for 1440x540 (log sets 009/010): output 12 buffers of
 * 1278000 bytes, stride 1536, slice height 544, colour 0x7F000004.
 *
 * Safety: before any buffer is handed to the decoder the screen buffers are
 * checked against the decoder's layout (pointer present, stride and chroma
 * offset as the decoder will write them). A mismatch means "fall back", never
 * "let the decoder write past a screen buffer".
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "hw_decode.h"

#ifdef PLATFORM_QNX

#include "cluster_surface.h"
#include "omx_il.h"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libavcodec/avcodec.h>

#define HW_MAX_OUT 32
#define HW_MAX_IN  8
#define HW_FRAME_US 33333

struct altr_hwdec {
    int width, height;
    cluster_surface_t *cs;
    screen_window_t win;
    screen_buffer_t sbuf[HW_MAX_OUT];
    uint8_t *sptr[HW_MAX_OUT];

    void *core;
    omx_fn_init omx_deinit;
    omx_fn_free_handle omx_free_handle;
    OMX_HANDLETYPE h;
    OMX_COMPONENTTYPE *c;
    OMX_PARAM_PORTDEFINITIONTYPE indef, outdef;

    OMX_BUFFERHEADERTYPE *in[HW_MAX_IN];
    int nin;
    int in_free[HW_MAX_IN];
    int nin_free;
    OMX_BUFFERHEADERTYPE *out[HW_MAX_OUT];
    int nout;

    pthread_mutex_t lock;
    pthread_cond_t cond;
    OMX_U32 state;          /* last state reached (EventCmdComplete) */
    volatile int failed;
    int executing;
    int shown;              /* output index on screen, -1 */
    volatile uint64_t frames;
    int64_t ts;

    AVCodecContext *pctx;
    AVCodecParserContext *parser;
};

/* ---- OMX callbacks (component threads) ---- */

static OMX_ERRORTYPE on_event(OMX_HANDLETYPE h, void *app, OMX_U32 ev, OMX_U32 d1, OMX_U32 d2, void *p)
{
    altr_hwdec_t *d = (altr_hwdec_t *)app;
    (void)h; (void)p;
    pthread_mutex_lock(&d->lock);
    if (ev == OMX_EventCmdComplete && d1 == OMX_CommandStateSet) {
        d->state = d2;
    } else if (ev == OMX_EventError) {
        fprintf(stderr, "hw_decode: OMX error 0x%x data2=0x%x -> software fallback\n", d1, d2);
        d->failed = 1;
    } else if (ev == OMX_EventPortSettingsChanged) {
        OMX_PARAM_PORTDEFINITIONTYPE def;
        omx_init_struct(&def, sizeof(def));
        def.nPortIndex = 1;
        if (d->c->GetParameter(d->h, OMX_IndexParamPortDefinition, &def) == 0 &&
            def.nBufferSize <= d->outdef.nBufferSize &&
            def.nBufferCountActual <= (OMX_U32)d->nout &&
            def.format.video.nStride == d->outdef.format.video.nStride &&
            def.format.video.nSliceHeight == d->outdef.format.video.nSliceHeight &&
            def.format.video.eColorFormat == d->outdef.format.video.eColorFormat) {
            fprintf(stderr, "hw_decode: port settings changed, layout unchanged (%ux%u) - continuing\n",
                    def.format.video.nFrameWidth, def.format.video.nFrameHeight);
        } else {
            fprintf(stderr, "hw_decode: port settings changed to %ux%u stride=%d slice=%u size=%u "
                    "count=%u -> software fallback\n",
                    def.format.video.nFrameWidth, def.format.video.nFrameHeight,
                    (int)def.format.video.nStride, def.format.video.nSliceHeight,
                    def.nBufferSize, def.nBufferCountActual);
            d->failed = 1;
        }
    }
    pthread_cond_broadcast(&d->cond);
    pthread_mutex_unlock(&d->lock);
    return 0;
}

static OMX_ERRORTYPE on_empty_done(OMX_HANDLETYPE h, void *app, OMX_BUFFERHEADERTYPE *b)
{
    altr_hwdec_t *d = (altr_hwdec_t *)app;
    (void)h;
    pthread_mutex_lock(&d->lock);
    if (d->nin_free < HW_MAX_IN) d->in_free[d->nin_free++] = (int)(intptr_t)b->pAppPrivate;
    pthread_cond_broadcast(&d->cond);
    pthread_mutex_unlock(&d->lock);
    return 0;
}

static OMX_ERRORTYPE on_fill_done(OMX_HANDLETYPE h, void *app, OMX_BUFFERHEADERTYPE *b)
{
    altr_hwdec_t *d = (altr_hwdec_t *)app;
    int idx = (int)(intptr_t)b->pAppPrivate;
    OMX_BUFFERHEADERTYPE *give_back = NULL;
    (void)h;

    if (!d->executing || d->failed || idx < 0 || idx >= d->nout)
        return 0;   /* draining: the component returns it on the Idle transition */
    if (b->nFilledLen > 0) {
        int rect[4] = { 0, 0, d->width, d->height };
        /* Only this callback thread posts or hands output back. WAIT_IDLE: when it
         * returns, the previous picture is no longer scanned out, so giving it back
         * to the decoder cannot tear the visible frame. */
        if (screen_post_window(d->win, d->sbuf[idx], 1, rect, SCREEN_WAIT_IDLE) == 0) {
            pthread_mutex_lock(&d->lock);
            if (d->shown >= 0) give_back = d->out[d->shown];
            d->shown = idx;
            d->frames++;
            pthread_mutex_unlock(&d->lock);
        } else {
            fprintf(stderr, "hw_decode: screen_post_window failed errno=%d\n", errno);
            give_back = b;
        }
    } else {
        give_back = b;
    }

    if (give_back && d->c->FillThisBuffer(d->h, give_back) != 0) {
        fprintf(stderr, "hw_decode: FillThisBuffer failed -> software fallback\n");
        d->failed = 1;
    }
    return 0;
}

/* ---- helpers ---- */

static int wait_state(altr_hwdec_t *d, OMX_U32 want, int ms)
{
    struct timespec t;
    int ok;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_sec += ms / 1000;
    t.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (t.tv_nsec >= 1000000000L) { t.tv_sec++; t.tv_nsec -= 1000000000L; }
    pthread_mutex_lock(&d->lock);
    while (d->state != want && !d->failed)
        if (pthread_cond_timedwait(&d->cond, &d->lock, &t) == ETIMEDOUT) break;
    ok = d->state == want;
    pthread_mutex_unlock(&d->lock);
    if (!ok) fprintf(stderr, "hw_decode: state %u not reached (at %u)\n", want, d->state);
    return ok ? 0 : -1;
}

static void *load_core(void)
{
    static const char *const libs[] = {
        "/mnt/app/armle/lib/libomxctx.so.1", "/mnt/app/armle/lib/libOmxBase.so",
        "/mnt/app/armle/lib/libOmxCore.so", NULL
    };
    void *h = NULL;
    int i;
    for (i = 0; libs[i]; ++i) {
        h = dlopen(libs[i], RTLD_NOW | RTLD_GLOBAL);
        if (!h) { fprintf(stderr, "hw_decode: dlopen %s: %s\n", libs[i], dlerror()); return NULL; }
    }
    return h;   /* libOmxCore.so */
}

/* Stock enableDecoderOrderMode(true): 20-byte struct, enable=1, count=1. */
static void enable_decode_order(altr_hwdec_t *d)
{
    OMX_INDEXTYPE idx = 0;
    OMX_U32 s[5];
    OMX_ERRORTYPE e;
    if (!d->c->GetExtensionIndex ||
        d->c->GetExtensionIndex(d->h, "OMX.QCOM.index.config.video.DisplayPictureBuffer", &idx) != 0) {
        fprintf(stderr, "hw_decode: no DisplayPictureBuffer extension (display order kept)\n");
        return;
    }
    omx_init_struct(s, sizeof(s));
    e = d->c->GetParameter(d->h, idx, s);
    if (e == 0) {
        s[3] = 1;
        s[4] = 1;
        e = d->c->SetParameter(d->h, idx, s);
    }
    fprintf(stderr, "hw_decode: decode-order output %s (0x%x)\n", e == 0 ? "on" : "unchanged", e);
}

/* The screen buffers must hold exactly what the decoder writes. */
static int map_screen_buffers(altr_hwdec_t *d)
{
    int i, stride = 0, planar[3] = { 0, 0, 0 };
    long y_bytes = (long)d->outdef.format.video.nStride * (long)d->outdef.format.video.nSliceHeight;
    long uv_expected = (y_bytes + 8191L) & ~8191L;

    if (screen_get_window_property_pv(d->win, SCREEN_PROPERTY_RENDER_BUFFERS, (void **)d->sbuf) != 0) {
        fprintf(stderr, "hw_decode: RENDER_BUFFERS failed errno=%d\n", errno);
        return -1;
    }
    for (i = 0; i < d->nout; ++i) {
        void *p = NULL;
        if (!d->sbuf[i] ||
            screen_get_buffer_property_pv(d->sbuf[i], SCREEN_PROPERTY_POINTER, &p) != 0 || !p) {
            fprintf(stderr, "hw_decode: screen buffer %d has no CPU pointer\n", i);
            return -1;
        }
        d->sptr[i] = (uint8_t *)p;
        stride = 0;
        screen_get_buffer_property_iv(d->sbuf[i], SCREEN_PROPERTY_STRIDE, &stride);
        planar[0] = planar[1] = planar[2] = 0;
        screen_get_buffer_property_iv(d->sbuf[i], SCREEN_PROPERTY_PLANAR_OFFSETS, planar);
        if (i == 0)
            fprintf(stderr, "hw_decode: screen buffer stride=%d planar=%d/%d/%d (decoder stride=%d "
                    "slice=%u uv@%ld size=%u)\n", stride, planar[0], planar[1], planar[2],
                    (int)d->outdef.format.video.nStride, d->outdef.format.video.nSliceHeight,
                    uv_expected, d->outdef.nBufferSize);
        if (stride < (int)d->outdef.format.video.nStride ||
            (planar[1] != 0 && planar[1] != uv_expected)) {
            fprintf(stderr, "hw_decode: screen buffer layout does not match the decoder "
                    "-> software fallback\n");
            return -1;
        }
    }
    return 0;
}

/* Leading SPS/PPS (and AUD/SEI before them) of an Annex-B AU: bytes to send as
 * CODECCONFIG. 0 when the AU starts with a picture. */
static size_t config_prefix(const uint8_t *p, size_t n)
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

static int send_input(altr_hwdec_t *d, const uint8_t *p, size_t n, OMX_U32 flags)
{
    struct timespec t;
    OMX_BUFFERHEADERTYPE *b;
    int idx = -1;

    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_nsec += 200000000L;
    if (t.tv_nsec >= 1000000000L) { t.tv_sec++; t.tv_nsec -= 1000000000L; }
    pthread_mutex_lock(&d->lock);
    while (!d->nin_free && !d->failed)
        if (pthread_cond_timedwait(&d->cond, &d->lock, &t) == ETIMEDOUT) break;
    if (d->nin_free && !d->failed) idx = d->in_free[--d->nin_free];
    pthread_mutex_unlock(&d->lock);
    if (d->failed) return -1;
    if (idx < 0) { fprintf(stderr, "hw_decode: input buffers busy, AU dropped\n"); return 0; }

    b = d->in[idx];
    if (n > b->nAllocLen) {
        fprintf(stderr, "hw_decode: AU %zu bytes > input buffer %u, dropped\n", n, b->nAllocLen);
        pthread_mutex_lock(&d->lock);
        d->in_free[d->nin_free++] = idx;
        pthread_mutex_unlock(&d->lock);
        return 0;
    }
    memcpy(b->pBuffer, p, n);
    b->nFilledLen = (OMX_U32)n;
    b->nOffset = 0;
    b->nFlags = flags;
    b->nTimeStamp = d->ts;
    if (flags & OMX_BUFFERFLAG_ENDOFFRAME) d->ts += HW_FRAME_US;
    if (d->c->EmptyThisBuffer(d->h, b) != 0) {
        fprintf(stderr, "hw_decode: EmptyThisBuffer failed -> software fallback\n");
        d->failed = 1;
        return -1;
    }
    return 0;
}

static int submit_au(altr_hwdec_t *d, const uint8_t *p, size_t n)
{
    size_t cfg = config_prefix(p, n);
    if (cfg && send_input(d, p, cfg, OMX_BUFFERFLAG_CODECCONFIG) < 0) return -1;
    if (n > cfg && send_input(d, p + cfg, n - cfg, OMX_BUFFERFLAG_ENDOFFRAME) < 0) return -1;
    return 0;
}

/* ---- public ---- */

altr_hwdec_t *altr_hwdec_create(int width, int height, int displayable_id)
{
    OMX_CALLBACKTYPE cbs = { on_event, on_empty_done, on_fill_done };
    omx_fn_init omx_init;
    omx_fn_get_handle omx_get_handle;
    cluster_surface_cfg cfg;
    altr_hwdec_t *d;
    int i;

    d = (altr_hwdec_t *)calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->width = width; d->height = height; d->shown = -1;
    pthread_mutex_init(&d->lock, NULL);
    pthread_cond_init(&d->cond, NULL);

    d->core = load_core();
    if (!d->core) goto fail;
    omx_init = (omx_fn_init)dlsym(d->core, "OMX_Init");
    d->omx_deinit = (omx_fn_init)dlsym(d->core, "OMX_Deinit");
    omx_get_handle = (omx_fn_get_handle)dlsym(d->core, "OMX_GetHandle");
    d->omx_free_handle = (omx_fn_free_handle)dlsym(d->core, "OMX_FreeHandle");
    if (!omx_init || !d->omx_deinit || !omx_get_handle || !d->omx_free_handle) goto fail;
    if (omx_init() != 0) { d->omx_deinit = NULL; goto fail; }
    if (omx_get_handle(&d->h, "OMX.qcom.video.decoder.avc", d, &cbs) != 0 || !d->h) {
        fprintf(stderr, "hw_decode: OMX_GetHandle failed\n");
        d->h = NULL;
        goto fail;
    }
    d->c = (OMX_COMPONENTTYPE *)d->h;
    d->state = OMX_StateLoaded;

    omx_init_struct(&d->indef, sizeof(d->indef));
    d->indef.nPortIndex = 0;
    if (d->c->GetParameter(d->h, OMX_IndexParamPortDefinition, &d->indef) != 0) goto fail;
    d->indef.format.video.nFrameWidth = (OMX_U32)width;
    d->indef.format.video.nFrameHeight = (OMX_U32)height;
    d->indef.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
    if (d->c->SetParameter(d->h, OMX_IndexParamPortDefinition, &d->indef) != 0 ||
        d->c->GetParameter(d->h, OMX_IndexParamPortDefinition, &d->indef) != 0) goto fail;

    omx_init_struct(&d->outdef, sizeof(d->outdef));
    d->outdef.nPortIndex = 1;
    if (d->c->GetParameter(d->h, OMX_IndexParamPortDefinition, &d->outdef) != 0) goto fail;
    d->nin = (int)d->indef.nBufferCountActual;
    d->nout = (int)d->outdef.nBufferCountActual;
    if (d->nin < 1 || d->nin > HW_MAX_IN || d->nout < 1 || d->nout > HW_MAX_OUT) {
        fprintf(stderr, "hw_decode: unexpected buffer counts in=%d out=%d\n", d->nin, d->nout);
        goto fail;
    }
    fprintf(stderr, "hw_decode: decoder %dx%d out color=0x%x stride=%d slice=%u %dx%u bytes, in %dx%u bytes\n",
            width, height, d->outdef.format.video.eColorFormat, (int)d->outdef.format.video.nStride,
            d->outdef.format.video.nSliceHeight, d->nout, d->outdef.nBufferSize, d->nin,
            d->indef.nBufferSize);
    enable_decode_order(d);

    memset(&cfg, 0, sizeof(cfg));
    cfg.id = displayable_id;
    cfg.width = width;
    cfg.height = height;
    cfg.format = omx_screen_format_for(d->outdef.format.video.eColorFormat);
    cfg.usage = 0x486;   /* stock: READ | WRITE | VIDEO | OVERLAY */
    cfg.nbuffers = d->nout;
    cfg.transparent = 0;
    d->cs = cluster_surface_create(&cfg);
    if (!d->cs) goto fail;
    d->win = cluster_surface_window(d->cs);
    if (map_screen_buffers(d) != 0) goto fail;

    if (d->c->SendCommand(d->h, OMX_CommandStateSet, OMX_StateIdle, NULL) != 0) goto fail;
    for (i = 0; i < d->nin; ++i) {
        if (d->c->AllocateBuffer(d->h, &d->in[i], 0, (void *)(intptr_t)i, d->indef.nBufferSize) != 0) {
            fprintf(stderr, "hw_decode: AllocateBuffer(in %d) failed\n", i);
            goto fail;
        }
        d->in_free[d->nin_free++] = i;
    }
    for (i = 0; i < d->nout; ++i) {
        if (d->c->UseBuffer(d->h, &d->out[i], 1, (void *)(intptr_t)i, d->outdef.nBufferSize,
                            d->sptr[i]) != 0) {
            fprintf(stderr, "hw_decode: UseBuffer(out %d) failed\n", i);
            goto fail;
        }
    }
    if (wait_state(d, OMX_StateIdle, 2000) != 0) goto fail;
    if (d->c->SendCommand(d->h, OMX_CommandStateSet, OMX_StateExecuting, NULL) != 0 ||
        wait_state(d, OMX_StateExecuting, 2000) != 0) goto fail;

    pthread_mutex_lock(&d->lock);
    d->executing = 1;
    pthread_mutex_unlock(&d->lock);
    for (i = 0; i < d->nout; ++i)
        if (d->c->FillThisBuffer(d->h, d->out[i]) != 0) {
            fprintf(stderr, "hw_decode: initial FillThisBuffer(%d) failed\n", i);
            goto fail;
        }

    d->pctx = avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_H264));
    d->parser = av_parser_init(AV_CODEC_ID_H264);
    if (!d->pctx || !d->parser) goto fail;

    fprintf(stderr, "hw_decode: running (zero-copy into displayable %d, screen format 0x%x)\n",
            displayable_id, cfg.format);
    return d;

fail:
    fprintf(stderr, "hw_decode: setup failed -> software path\n");
    altr_hwdec_destroy(d);
    return NULL;
}

int altr_hwdec_feed(altr_hwdec_t *d, const uint8_t *data, size_t len)
{
    if (!d || d->failed) return -1;
    while (len > 0) {
        uint8_t *au = NULL;
        int au_size = 0;
        int used = av_parser_parse2(d->parser, d->pctx, &au, &au_size, data, (int)len,
                                    AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (used < 0) return -1;
        data += used;
        len -= (size_t)used;
        if (au_size > 0 && submit_au(d, au, (size_t)au_size) < 0) return -1;
    }
    return d->failed ? -1 : 0;
}

uint64_t altr_hwdec_frames(altr_hwdec_t *d) { return d ? d->frames : 0; }
int altr_hwdec_failed(altr_hwdec_t *d) { return d ? d->failed : 1; }

void altr_hwdec_destroy(altr_hwdec_t *d)
{
    int i;
    if (!d) return;
    if (d->h) {
        pthread_mutex_lock(&d->lock);
        d->executing = 0;
        pthread_mutex_unlock(&d->lock);
        if (d->state == OMX_StateExecuting) {
            d->c->SendCommand(d->h, OMX_CommandStateSet, OMX_StateIdle, NULL);
            wait_state(d, OMX_StateIdle, 2000);
        }
        if (d->state == OMX_StateIdle)
            d->c->SendCommand(d->h, OMX_CommandStateSet, OMX_StateLoaded, NULL);
        for (i = 0; i < HW_MAX_IN; ++i) if (d->in[i]) d->c->FreeBuffer(d->h, 0, d->in[i]);
        for (i = 0; i < HW_MAX_OUT; ++i) if (d->out[i]) d->c->FreeBuffer(d->h, 1, d->out[i]);
        if (d->state == OMX_StateIdle) wait_state(d, OMX_StateLoaded, 2000);
        d->omx_free_handle(d->h);
        d->h = NULL;
    }
    if (d->omx_deinit) d->omx_deinit();
    if (d->parser) av_parser_close(d->parser);
    if (d->pctx) avcodec_free_context(&d->pctx);
    if (d->cs) cluster_surface_destroy(d->cs);   /* after the decoder released its buffers */
    pthread_cond_destroy(&d->cond);
    pthread_mutex_destroy(&d->lock);
    fprintf(stderr, "hw_decode: closed after %llu pictures\n", (unsigned long long)d->frames);
    free(d);
}

#else /* !PLATFORM_QNX: host build has no OMX/screen */

altr_hwdec_t *altr_hwdec_create(int w, int h, int id) { (void)w; (void)h; (void)id; return NULL; }
int altr_hwdec_feed(altr_hwdec_t *d, const uint8_t *p, size_t n) { (void)d; (void)p; (void)n; return -1; }
uint64_t altr_hwdec_frames(altr_hwdec_t *d) { (void)d; return 0; }
int altr_hwdec_failed(altr_hwdec_t *d) { (void)d; return 1; }
void altr_hwdec_destroy(altr_hwdec_t *d) { (void)d; }

#endif
