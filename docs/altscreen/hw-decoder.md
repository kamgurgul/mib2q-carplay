---
title: Hardware H.264 decode for the cluster stream (stream 111)
tags: [altscreen, omx, decoder, re]
status: default ON (vehicle-tested, log set 018)
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

**Default ON** since log set 018. Turned OFF by `/mnt/app/root/altscreen_render.hwdecode.off`
(GEM CarPlay-RGI -> *HW decoder ON / OFF*) or, for the rest of the boot, `/tmp/altscreen_render.hwdecode.off`.
After a real hardware failure the renderer writes both itself (the /mnt/app one only when that
partition is writable) and stays on software decode. Read per CarPlay connection; the decoder
is created on the first stream bytes.

What the car taught us:

| log | output window | result |
| --- | --- | --- |
| 011 | decoder buffers = window 99, usage 0x486 (stock, with OVERLAY) | 1289 pictures decoded and posted, **VC black**: an OVERLAY window goes to a hardware pipe the cluster capture never sees |
| 015/016 | same, usage 0x86 (no OVERLAY) | decoder **never wrote a picture**, stopped returning input, teardown hung: the decoder needs stock's 0x486 allocation |
| 017 | hidden 0x486 decode window + `screen_blit` to RGBA window 99 | first picture decoded and blitted; then `FillThisBuffer` on the same header inside its own `FillBufferDone` was rejected. Fixed: hand back the *previous* picture's buffer (as in 011). Teardown no longer mistakes a failed decoder for a stuck one. |
| 018 | same, buffer handed back one picture later | **works**: 3120 pictures in one session, renderer CPU ≈2.9 ms/picture (software: ≈24 ms). The only error, `0x80001018` (IncorrectStateOperation), was a picture in flight during teardown - now ignored. |

Current design:

1. a **hidden** decode window (format 0x1000C, usage 0x486, 12 buffers, never visible, so no
   pipe) whose buffers are checked against the decoder layout and given to `OMX_UseBuffer`;
2. a normal managed **RGBA window 99** (format 8, usage NATIVE|READ|WRITE, 2 buffers) - the same
   kind of plane the GL path uses, so it is composited into the captured cluster image;
3. per picture, `screen_blit` tiled NV12 -> RGBA (the 2D blitter) into window 99, post with
   `SCREEN_WAIT_IDLE`, then hand the *previous* picture's buffer back to the decoder (never the one
   inside its own `FillBufferDone`);
4. decode-order output, CODECCONFIG / ENDOFFRAME flags as stock.

Fail-safe: a blit error, no picture 2 s after the first input, 5 input stalls in a row, an OMX
error or a port change -> `hw_decode:` reason, HW decoder switched OFF, software path. A decoder
that will not leave Executing is never freed (its DMA could hit freed memory): the renderer exits
and `carplay_monitor.sh` restarts a clean one.

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
