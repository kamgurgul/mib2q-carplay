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
| issue #14 | Android Auto 1080p decoder opened 64 s after boot (phone route already active, so the cluster stream starts with the session) | slog `VCD_HWTimeoutHandler` 2 s later, then `0x80001011` (Timeout), Executing never reached. The teardown (Idle->Loaded, FreeBuffer, FreeHandle) **never returned**: `videoCore` threads deadlocked on a MUTEX, HMI and the cockpit's Audi map frozen, watchdog reboot minutes later. Car logs 020/021 opened the same decoder at ~100 s without trouble. |

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

Hung video hardware (issue #14):

- **Boot settle time.** No hardware decoder before 120 s of uptime (`ALTR_HW_MIN_UPTIME_S`).
  A session that starts earlier decodes in software, then reconnects to the tee once at 120 s
  and switches to hardware. Always for **Android Auto** (#14). **CarPlay** starts on hardware at
  once, like upstream mib2q-carplay-rgi (no switch, so no blink); the marker
  `/mnt/app/root/altscreen_render.hwdecode.delayed` (GEM *HW decoder 2-min delayed start*) gives
  it the wait, for units that freeze when the decoder opens early (#10, cause not proven). `.live` is refreshed for up to 3 s until the first hardware picture,
  also while the decoder setup blocks (from the watchdog thread), so the cockpit does not flip
  back to the Audi map (issue #9: it showed for ~0.3 s while `.live` aged past Java's 1 s limit).
- **Wedged decoder.** A state change that times out, or an OMX `Timeout` / `Hardware` error,
  marks the component as wedged: no further OMX call is made (they block inside `videoCore`).
  The renderer switches hardware decode OFF and exits; the restarted renderer uses software.
- **Watchdog.** Setup (10 s), teardown (8 s) and each feed (4 s) have a deadline. An OMX call
  that is still blocked after it switches hardware decode OFF and exits the process.
- **Main-screen yield (car logs 2026-10-30, save 011).** With the cluster decoder running, a
  reverse-camera / CarPlay / Audi screen burst started and stopped stock's 1024x480 decoder four
  times in 80 s. The fifth start hit `VCD_HWTimeoutHandler` -> `MMKIVdecDriver_Start TimeOut` ->
  `OMX_EventError` in dio_manager. Result: a black main screen, a log save that hung in `mount`,
  and (save 009) the #14 watchdog reboot with no process core. So the two decoders must not change
  state at the same time. Before stock's 110 SETUP or teardown (also a whole-session teardown), the
  CarPlay hook writes the uptime (ms) into `/tmp/altscreen_render.hw_yield` and waits up to 1.5 s
  while `/tmp/altscreen_render.hw_active` names a live renderer. The renderer polls the yield
  file every 40 ms while it decodes in hardware. It then **suspends** the decoder
  (`altr_hwdec_suspend`: OMX component, hidden decode window and parser closed, window 99 with
  the last picture kept) and keeps `.live` fresh. It discards the stream until
  `ALTR_HW_YIELD_HOLD_S` (default 3 s) after the last yield, because stock's decoder is
  Executing about 0.5 s after its SETUP. It then reconnects the tee for a key frame and resumes
  (`altr_hwdec_resume`). The cockpit shows a frozen map rather than the Audi map, and there is
  no software round trip or window swap. A reconnect while waiting keeps the suspended decoder,
  so iOS's cluster re-config about 2.5 s after the main screen returns (save 010: consumer reset
  at 125.7 s after the 110 SETUP at 123.3 s) does not blink either. A new connection that meets a
  yield waits the same way before its first decoder. Ordering: the renderer writes the active
  file before it reads the yield file, and the hook does the reverse, so a hardware start cannot
  slip past a yield.
- **Bounded starts (cooldown).** The checks above only catch failures the renderer lives to see.
  A renderer that crashes inside the decoder start, or before the first picture, would be
  restarted by `carplay_monitor.sh` and try hardware again, and each failed `OMX_GetHandle` can
  hold a decoder slot that `dio_manager`'s own decoder needs. `/tmp/altscreen_render[.aa].hwdecode.off.starts`
  counts starts with no picture and no clean close: after 3 the hardware waits 30 s (software
  meanwhile, then the boot-settle reconnect), after another 3 it stays off for this boot. A
  first picture or a clean close clears the record. Idea from luka-dev/mib2q-carplay-rgi
  (`sink_omx_qnx.c`, 3 failures / 30 s).

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
