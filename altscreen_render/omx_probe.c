/*
 * altscreen_render — on-car probe of the Qualcomm OMX H.264 decoder.
 *
 * Step one towards hardware decode of the cluster stream. Stock dio_manager
 * decodes the main CarPlay screen with OMX.qcom.video.decoder.avc through
 * /mnt/app/armle/lib/libOmxCore.so (libairplay.so: dio::COMXVideoDecoder), with
 * OMX_UseBuffer on its QNX screen window buffers (zero copy) and the screen
 * format picked by convertColorFormatOMX2Screen():
 *     OMX 21/39/0x7F000003 -> 12 (NV12), 0x7F000005 -> 0x2000C,
 *     anything else        -> 0x1000C (NV12, Qualcomm tiled; seen as
 *                             "format=65548" in the stock wfd log).
 * This probe loads the same core, opens the decoder for the cluster canvas,
 * sets the AVC input size and logs what the component then asks for on its
 * output port (colour format, stride, slice height, buffer count/size) plus
 * the Qualcomm extension indices stock uses. It decodes nothing, allocates no
 * buffers and never goes past OMX_StateLoaded, then frees the handle.
 *
 * OMX IL 1.1.2 types come from omx_il.h (no SDK headers on the build image).
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "omx_probe.h"
#include "omx_il.h"

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static OMX_ERRORTYPE cb_event(OMX_HANDLETYPE h, void *a, OMX_U32 e, OMX_U32 d1, OMX_U32 d2, void *p)
{
    (void)h; (void)a; (void)p;
    fprintf(stderr, "omx_probe: event %u data1=0x%x data2=0x%x\n", e, d1, d2);
    return 0;
}
static OMX_ERRORTYPE cb_buffer(OMX_HANDLETYPE h, void *a, OMX_BUFFERHEADERTYPE *b) { (void)h; (void)a; (void)b; return 0; }

static void log_port(const char *tag, const OMX_PARAM_PORTDEFINITIONTYPE *d)
{
    fprintf(stderr,
            "omx_probe: %s port=%u dir=%u enabled=%u buffers actual=%u min=%u size=%u "
            "contiguous=%u align=%u frame=%ux%u stride=%d slice=%u compression=%u color=0x%x"
            " -> screen format 0x%x\n",
            tag, d->nPortIndex, d->eDir, d->bEnabled, d->nBufferCountActual,
            d->nBufferCountMin, d->nBufferSize, d->bBuffersContiguous, d->nBufferAlignment,
            d->format.video.nFrameWidth, d->format.video.nFrameHeight,
            (int)d->format.video.nStride, d->format.video.nSliceHeight,
            d->format.video.eCompressionFormat, d->format.video.eColorFormat,
            omx_screen_format_for(d->format.video.eColorFormat));
}

static void *open_lib(const char *const *paths)
{
    void *h = NULL;
    for (; *paths && !h; ++paths) {
        h = dlopen(*paths, RTLD_NOW | RTLD_GLOBAL);
        if (h) fprintf(stderr, "omx_probe: loaded %s\n", *paths);
    }
    return h;
}

int altr_omx_probe(int width, int height)
{
    static const char *const deps_ctx[] = { "/mnt/app/armle/lib/libomxctx.so.1", "libomxctx.so.1", NULL };
    static const char *const deps_base[] = { "/mnt/app/armle/lib/libOmxBase.so", "libOmxBase.so", NULL };
    static const char *const core_paths[] = { "/mnt/app/armle/lib/libOmxCore.so", "libOmxCore.so", NULL };
    static const char *const ext[] = {
        "OMX.QCOM.index.param.video.SyntaxHdr",
        "OMX.QCOM.index.config.video.DisplayPictureBuffer",
        NULL
    };
    OMX_CALLBACKTYPE cbs = { cb_event, cb_buffer, cb_buffer };
    OMX_PARAM_PORTDEFINITIONTYPE in, out;
    OMX_HANDLETYPE handle = NULL;
    OMX_COMPONENTTYPE *c;
    omx_fn_init omx_init, omx_deinit;
    omx_fn_get_handle omx_get_handle;
    omx_fn_free_handle omx_free_handle;
    omx_fn_name_enum omx_name_enum;
    OMX_ERRORTYPE e;
    OMX_U32 i, state = 0;
    void *core;
    char name[128];

    fprintf(stderr, "omx_probe: begin (canvas %dx%d)\n", width, height);
    (void)open_lib(deps_ctx);
    (void)open_lib(deps_base);
    core = open_lib(core_paths);
    if (!core) { fprintf(stderr, "omx_probe: libOmxCore.so not loadable: %s\n", dlerror()); return -1; }

    omx_init = (omx_fn_init)dlsym(core, "OMX_Init");
    omx_deinit = (omx_fn_init)dlsym(core, "OMX_Deinit");
    omx_get_handle = (omx_fn_get_handle)dlsym(core, "OMX_GetHandle");
    omx_free_handle = (omx_fn_free_handle)dlsym(core, "OMX_FreeHandle");
    omx_name_enum = (omx_fn_name_enum)dlsym(core, "OMX_ComponentNameEnum");
    if (!omx_init || !omx_deinit || !omx_get_handle || !omx_free_handle) {
        fprintf(stderr, "omx_probe: core lacks OMX_Init/Deinit/GetHandle/FreeHandle\n");
        return -1;
    }
    if ((e = omx_init()) != 0) { fprintf(stderr, "omx_probe: OMX_Init=0x%x\n", e); return -1; }

    if (omx_name_enum)
        for (i = 0; i < 64 && omx_name_enum(name, sizeof(name), i) == 0; ++i)
            fprintf(stderr, "omx_probe: component[%u] %s\n", i, name);

    e = omx_get_handle(&handle, "OMX.qcom.video.decoder.avc", NULL, &cbs);
    if (e != 0 || !handle) {
        fprintf(stderr, "omx_probe: OMX_GetHandle(OMX.qcom.video.decoder.avc)=0x%x\n", e);
        omx_deinit();
        return -1;
    }
    c = (OMX_COMPONENTTYPE *)handle;
    if (c->GetState && c->GetState(handle, &state) == 0)
        fprintf(stderr, "omx_probe: state=%u (1=Loaded)\n", state);

    for (i = 0; ext[i]; ++i) {
        OMX_INDEXTYPE idx = 0;
        e = c->GetExtensionIndex ? c->GetExtensionIndex(handle, ext[i], &idx) : 0xFFFFFFFFu;
        fprintf(stderr, "omx_probe: extension %s -> index 0x%x (err 0x%x)\n", ext[i], idx, e);
    }

    omx_init_struct(&in, sizeof(in));
    in.nPortIndex = 0;
    e = c->GetParameter(handle, OMX_IndexParamPortDefinition, &in);
    fprintf(stderr, "omx_probe: get input portdef err=0x%x\n", e);
    if (e == 0) {
        log_port("input (default)", &in);
        in.format.video.nFrameWidth = (OMX_U32)width;
        in.format.video.nFrameHeight = (OMX_U32)height;
        in.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
        e = c->SetParameter(handle, OMX_IndexParamPortDefinition, &in);
        fprintf(stderr, "omx_probe: set input %dx%d AVC err=0x%x\n", width, height, e);
        if (c->GetParameter(handle, OMX_IndexParamPortDefinition, &in) == 0)
            log_port("input (after set)", &in);
    }

    omx_init_struct(&out, sizeof(out));
    out.nPortIndex = 1;
    e = c->GetParameter(handle, OMX_IndexParamPortDefinition, &out);
    fprintf(stderr, "omx_probe: get output portdef err=0x%x\n", e);
    if (e == 0) log_port("output", &out);

    for (i = 0; i < 16; ++i) {
        OMX_VIDEO_PARAM_PORTFORMATTYPE f;
        omx_init_struct(&f, sizeof(f));
        f.nPortIndex = 1;
        f.nIndex = i;
        e = c->GetParameter(handle, OMX_IndexParamVideoPortFormat, &f);
        if (e != 0) {
            if (e != OMX_ErrorNoMore) fprintf(stderr, "omx_probe: output format[%u] err=0x%x\n", i, e);
            break;
        }
        fprintf(stderr, "omx_probe: output format[%u] color=0x%x compression=%u -> screen 0x%x\n",
                i, f.eColorFormat, f.eCompressionFormat, omx_screen_format_for(f.eColorFormat));
    }

    e = omx_free_handle(handle);
    fprintf(stderr, "omx_probe: OMX_FreeHandle=0x%x\n", e);
    omx_deinit();
    fprintf(stderr, "omx_probe: done\n");
    return 0;
}
