---
title: Hardware H.264 decode for the cluster stream (stream 111)
tags: [altscreen, omx, decoder, re]
status: experimental (opt-in, untested on the car)
sources:
  - firmware: /mnt/app/eso/lib/libairplay.so (copied by the logging mod to carplay_logs/hwdecNNN/)
  - logs: carplay_logs/007/hwdec, 008/hwdec (omx_libs.txt, dio_libs.txt)
---

# Hardware H.264 decode for the cluster stream

`altscreen_render` decodes stream 111 with FFmpeg in software. On the car this shows up as
`gen2 video AU rc=2 -> consumer reset` while the map starts (the hook's 16-AU queue overflows).
The unit has a hardware decoder that stock already uses for the main CarPlay screen.

## What is on the unit (log sets 007/008)

- `/mnt/app/armle/lib/libOmxCore.so`, `libOmxBase.so`, `libomxctx.so(.1)`, plus Audi wrappers
  `libAOMXDecoder.so` / `libAOMXRenderer.so` and `libmmiVdec_asic_AF.so`.
- `dio_manager` maps `libOmxCore.so` and `libomxctx.so.1` directly (`pidin mem`).

## How stock drives it (libairplay.so, exported symbols)

| piece | finding |
| --- | --- |
| component | `OMX.qcom.video.decoder.avc` via `OMX_Init` / `OMX_GetHandle` |
| extensions | `OMX.QCOM.index.param.video.SyntaxHdr` (codec config), `OMX.QCOM.index.config.video.DisplayPictureBuffer` |
| output buffers | **zero copy**: `CScreenRender::sharedBuffers()` hands the QNX screen window buffers to `CBufferPool::attach()` -> `OMX_UseBuffer`; `CScreenRender::render(ptr)` posts the buffer the decoder just filled |
| buffer count | 12 output buffers for 1024x480 (stock log: `Output Buffer: Count = 12 (minN0 11)`) |
| screen format | `convertColorFormatOMX2Screen()`: OMX 21/39/0x7F000003 -> 12 (NV12), 0x7F000005 -> 0x2000C, anything else -> **0x1000C** |
| what stock used | screen images `format=65548` (0x1000C), `usage=0x486` -> the decoder's Qualcomm tiled NV12 scanned out natively |

So the efficient path needs no GL and no CPU copy: decode straight into displayable 99's
buffers in the decoder's native format and post them.

## Probe result on the car (log sets 009/010, canvas 1440x540)

```text
input  port 0: 3 buffers x 1175040 B, align 4096
output port 1: 12 buffers x 1278000 B, align 8192, stride 1536, slice 544,
               colour 0x7F000004 -> screen format 0x1000C (tiled NV12)
extensions: SyntaxHdr = 0x7F100001, DisplayPictureBuffer = 0x7F100008
```

1536 x 544 is 1440 x 540 rounded to 128 x 32 and the size is exactly a 64x32-tiled NV12 frame
(Y 835584 B, chroma at the next 8 KB boundary), so the stock zero-copy path applies unchanged.

## Hardware decode (`altscreen_render/hw_decode.c`, opt-in)

GEM CarPlay-RGI -> *HW decoder ON / OFF* (`/mnt/app/root/altscreen_render.hwdecode`), read per
CarPlay connection, created on the first stream bytes:

1. window 99 with format `omx_screen_format_for(output colour)` (0x1000C), usage 0x486, as many
   buffers as the decoder wants (12);
2. the screen buffers must match the decoder layout (pointer, stride >= 1536, chroma offset)
   or the session falls back - the decoder never writes past a screen buffer;
3. decode-order output via `DisplayPictureBuffer` (stock `enableDecoderOrderMode`);
4. 3 allocated input buffers, `OMX_UseBuffer` on the 12 screen buffers, Idle -> Executing;
5. AUs split by the FFmpeg H.264 parser; leading SPS/PPS sent with 0x80 (CODECCONFIG),
   pictures with 0x10 (ENDOFFRAME), like `COMXVideoDecoder::job_decodeFrame`;
6. `FillBufferDone` posts that screen buffer (`SCREEN_WAIT_IDLE`) and hands the previous one
   back to the decoder.

Any setup failure, OMX error or unsupported port-settings change logs a `hw_decode:` reason,
switches the marker OFF and continues with FFmpeg + GLES (the tee is reconnected so the hook
re-primes with a fresh IDR).

## Plan

1. **Probe (done, results above).** GEM CarPlay-RGI -> *HW decoder probe (OMX, logs only)*.
   `altscreen_render` loads `libOmxCore.so`, opens the decoder for the 1440x540 canvas,
   sets the AVC input size and logs the output port (colour format, stride, slice height,
   buffer count/size, contiguity) and the extension indices to `/tmp/altscreen_render.log`
   (`omx_probe:` lines). No buffers, no decoding, handle freed. Save logs afterwards.
2. **Decode to screen buffers (implemented, see above; needs the car).** Window 99 with the stock format/usage for that colour format,
   `nbuffers` = output `nBufferCountActual`, `OMX_UseBuffer` on the screen buffer pointers,
   SPS/PPS as `OMX_BUFFERFLAG_CODECCONFIG`, `FillBufferDone` -> `screen_post_window` of that
   buffer. Behind a marker, FFmpeg stays the fallback.
3. Remaining stock details to read when needed: `CScreenRender::config` (window property
   sequence), `COMXVideoDecoder::configure/setOutputBuffers/parseHeaderFrame`.
