/*
 * altscreen_render — minimal OpenMAX IL 1.1.2 declarations for the Qualcomm
 * decoder on MHI2Q (libOmxCore.so is dlopen()ed at runtime; the build image has
 * no OMX SDK). Only what omx_probe.c / hw_decode.c use. Layouts are checked
 * below against the 32-bit ARM offsets stock libairplay.so uses
 * (COMXVideoDecoder::job_decodeFrame writes nTimeStamp at +56, nFlags at +64).
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ALTR_OMX_IL_H
#define ALTR_OMX_IL_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t OMX_U32;
typedef int32_t OMX_S32;
typedef uint32_t OMX_ERRORTYPE;
typedef uint32_t OMX_INDEXTYPE;
typedef void *OMX_HANDLETYPE;

typedef union {
    struct { uint8_t nVersionMajor, nVersionMinor, nRevision, nStep; } s;
    OMX_U32 nVersion;
} OMX_VERSIONTYPE;

typedef struct {
    OMX_U32 nSize;
    OMX_VERSIONTYPE nVersion;
    uint8_t *pBuffer;
    OMX_U32 nAllocLen;
    OMX_U32 nFilledLen;
    OMX_U32 nOffset;
    void *pAppPrivate;
    void *pPlatformPrivate;
    void *pInputPortPrivate;
    void *pOutputPortPrivate;
    void *hMarkTargetComponent;
    void *pMarkData;
    OMX_U32 nTickCount;
    int64_t nTimeStamp;
    OMX_U32 nFlags;
    OMX_U32 nOutputPortIndex;
    OMX_U32 nInputPortIndex;
} OMX_BUFFERHEADERTYPE;

typedef struct {
    OMX_ERRORTYPE (*EventHandler)(OMX_HANDLETYPE, void *, OMX_U32, OMX_U32, OMX_U32, void *);
    OMX_ERRORTYPE (*EmptyBufferDone)(OMX_HANDLETYPE, void *, OMX_BUFFERHEADERTYPE *);
    OMX_ERRORTYPE (*FillBufferDone)(OMX_HANDLETYPE, void *, OMX_BUFFERHEADERTYPE *);
} OMX_CALLBACKTYPE;

typedef struct {
    OMX_U32 nSize;
    OMX_VERSIONTYPE nVersion;
    void *pComponentPrivate;
    void *pApplicationPrivate;
    OMX_ERRORTYPE (*GetComponentVersion)(OMX_HANDLETYPE, char *, OMX_VERSIONTYPE *,
                                         OMX_VERSIONTYPE *, void *);
    OMX_ERRORTYPE (*SendCommand)(OMX_HANDLETYPE, OMX_U32, OMX_U32, void *);
    OMX_ERRORTYPE (*GetParameter)(OMX_HANDLETYPE, OMX_INDEXTYPE, void *);
    OMX_ERRORTYPE (*SetParameter)(OMX_HANDLETYPE, OMX_INDEXTYPE, void *);
    OMX_ERRORTYPE (*GetConfig)(OMX_HANDLETYPE, OMX_INDEXTYPE, void *);
    OMX_ERRORTYPE (*SetConfig)(OMX_HANDLETYPE, OMX_INDEXTYPE, void *);
    OMX_ERRORTYPE (*GetExtensionIndex)(OMX_HANDLETYPE, const char *, OMX_INDEXTYPE *);
    OMX_ERRORTYPE (*GetState)(OMX_HANDLETYPE, OMX_U32 *);
    OMX_ERRORTYPE (*ComponentTunnelRequest)(OMX_HANDLETYPE, OMX_U32, OMX_HANDLETYPE, OMX_U32, void *);
    OMX_ERRORTYPE (*UseBuffer)(OMX_HANDLETYPE, OMX_BUFFERHEADERTYPE **, OMX_U32, void *,
                               OMX_U32, uint8_t *);
    OMX_ERRORTYPE (*AllocateBuffer)(OMX_HANDLETYPE, OMX_BUFFERHEADERTYPE **, OMX_U32, void *,
                                    OMX_U32);
    OMX_ERRORTYPE (*FreeBuffer)(OMX_HANDLETYPE, OMX_U32, OMX_BUFFERHEADERTYPE *);
    OMX_ERRORTYPE (*EmptyThisBuffer)(OMX_HANDLETYPE, OMX_BUFFERHEADERTYPE *);
    OMX_ERRORTYPE (*FillThisBuffer)(OMX_HANDLETYPE, OMX_BUFFERHEADERTYPE *);
} OMX_COMPONENTTYPE;

typedef struct {
    char *cMIMEType;
    void *pNativeRender;
    OMX_U32 nFrameWidth;
    OMX_U32 nFrameHeight;
    OMX_S32 nStride;
    OMX_U32 nSliceHeight;
    OMX_U32 nBitrate;
    OMX_U32 xFramerate;
    OMX_U32 bFlagErrorConcealment;
    OMX_U32 eCompressionFormat;
    OMX_U32 eColorFormat;
    void *pNativeWindow;
} OMX_VIDEO_PORTDEFINITIONTYPE;

typedef struct {
    OMX_U32 nSize;
    OMX_VERSIONTYPE nVersion;
    OMX_U32 nPortIndex;
    OMX_U32 eDir;
    OMX_U32 nBufferCountActual;
    OMX_U32 nBufferCountMin;
    OMX_U32 nBufferSize;
    OMX_U32 bEnabled;
    OMX_U32 bPopulated;
    OMX_U32 eDomain;
    union { OMX_VIDEO_PORTDEFINITIONTYPE video; } format;   /* video is the largest member */
    OMX_U32 bBuffersContiguous;
    OMX_U32 nBufferAlignment;
} OMX_PARAM_PORTDEFINITIONTYPE;

typedef struct {
    OMX_U32 nSize;
    OMX_VERSIONTYPE nVersion;
    OMX_U32 nPortIndex;
    OMX_U32 nIndex;
    OMX_U32 eCompressionFormat;
    OMX_U32 eColorFormat;
    OMX_U32 xFramerate;
} OMX_VIDEO_PARAM_PORTFORMATTYPE;

/* Checked on the 32-bit target only (the host build just compiles the code). */
#define OMX_IL_ARM32 (sizeof(void *) == 4)
typedef char omx_portdef_is_96_bytes[(!OMX_IL_ARM32 || sizeof(OMX_PARAM_PORTDEFINITIONTYPE) == 96) ? 1 : -1];
typedef char omx_portformat_is_28_bytes[(sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE) == 28) ? 1 : -1];
typedef char omx_hdr_timestamp_at_56[(!OMX_IL_ARM32 || offsetof(OMX_BUFFERHEADERTYPE, nTimeStamp) == 56) ? 1 : -1];
typedef char omx_hdr_flags_at_64[(!OMX_IL_ARM32 || offsetof(OMX_BUFFERHEADERTYPE, nFlags) == 64) ? 1 : -1];

#define OMX_IndexParamPortDefinition   0x02000001u
#define OMX_IndexParamVideoPortFormat  0x06000001u
#define OMX_VIDEO_CodingAVC            7u
#define OMX_ErrorNoMore                0x8000100Eu

#define OMX_CommandStateSet    0u
#define OMX_CommandFlush       1u
#define OMX_StateLoaded        1u
#define OMX_StateIdle          2u
#define OMX_StateExecuting     3u

#define OMX_EventCmdComplete          0u
#define OMX_EventError                1u
#define OMX_EventPortSettingsChanged  3u

#define OMX_BUFFERFLAG_ENDOFFRAME   0x10u
#define OMX_BUFFERFLAG_CODECCONFIG  0x80u

#define OMX_ALL 0xFFFFFFFFu

typedef OMX_ERRORTYPE (*omx_fn_init)(void);
typedef OMX_ERRORTYPE (*omx_fn_get_handle)(OMX_HANDLETYPE *, const char *, void *, OMX_CALLBACKTYPE *);
typedef OMX_ERRORTYPE (*omx_fn_free_handle)(OMX_HANDLETYPE);
typedef OMX_ERRORTYPE (*omx_fn_name_enum)(char *, OMX_U32, OMX_U32);

/* Set nSize and OMX 1.1.2 version on a parameter struct. */
static inline void omx_init_struct(void *p, size_t size)
{
    uint8_t *b = (uint8_t *)p;
    size_t i;
    for (i = 0; i < size; ++i) b[i] = 0;
    ((OMX_U32 *)p)[0] = (OMX_U32)size;
    b[4] = 1; b[5] = 1; b[6] = 2; b[7] = 0;
}

/* Screen format stock would use for an OMX output colour format
 * (libairplay.so convertColorFormatOMX2Screen). */
static inline int omx_screen_format_for(OMX_U32 omx)
{
    switch (omx) {
    case 21: case 39: case 0x7F000003u: return 12;
    case 0x7F000005u: return 0x2000C;
    case 25: return 16;
    case 27: return 14;
    case 11: return 7;
    case 16: return 8;
    case 19: return 11;
    case 6: return 6;
    default: return 0x1000C;
    }
}

#endif /* ALTR_OMX_IL_H */
