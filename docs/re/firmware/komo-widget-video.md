---
title: KOMO widget video & gfxAvailable gate
tags: [re, firmware, most, video, verified]
status: partially-verified
sources:
  - firmware: libPresentationController.so, videoencoderservice, KOMOService (diag.jar/DSITracer.jar)
  - firmware: DSIKOMOGfxStreamSink (org.dsi.ifc.komogfxstreamsink), EB GUIDE ViewModeSM
verification:
  - KVS enum + komoviewstyle sizes confirmed by decompile (libPresentationController.so)
  - encoder chain confirmed as strings in videoencoderservice
  - gfx chain: DSIKOMOGfxStreamSink + updateGfxState + KOMOService found only as strings in lsd jars;
    ATTR_GFXSTATE / setGFXAvailable / ClusterViewMode not found in this extract (inferred names)
reconciles:
  - docs/reference/widget_video_architecture.md
  - docs/reference/gfx_available_root_cause.md
  - docs/reference/lvds_video_pipeline.md
---

# KOMO widget video & gfxAvailable gate

The stock cluster video pipeline (which [compositing](../../cluster/compositing.md) hooks into) and the flag that lets the VC
actually show it.

## 🧭 Pipeline

```mermaid
flowchart LR
    accTitle: KOMO widget video pipeline
    accDescr: PresentationController renders to a framebuffer that videoencoderservice captures, encodes to H.264 and MPEG-TS, and sends over MLB ISO and MOST to the VC LVDS input.
    pc["PresentationController<br/>renders -> framebuffer (displayable)"] --> ipte["videoencoderservice<br/>IPTE capture"]
    ipte --> h264["QC OMX H.264"] --> ts["MPEG-TS"] --> iso["MLB ISO"] --> most(["MOST"]) --> vc["VC LVDS"]
```

## 📊 Widget size - komoviewstyle

Widget render size comes from `komoviewstyle.conf`. The current MHI2Q FPK cluster widget is
**`KVS_FPK`** - DSI 2 = **210x153**, DSI 3 = **328x181** (agrees with [compositing](../../cluster/compositing.md)). Other styles:
`KVS_Most` **800x252** (MOST display, DSI 1), `KVS_RGI` **263x366** (old MIB1 style, DSI 255);
DSI 4-7 = `KVS_Invalid`.

`KVS_RGI2` exists only as **EB style enum ordinal 2** in the binary (switch FUN_00638d44:
`0 Invalid / 1 RGI / 2 RGI2 / 3 FPK / 4 Most / 5 Debug_MoKoInMainDisplay`) and has **no DSI
assignment** in this build's `komoviewstyle.conf`. There is no `363x260` size anywhere in the
firmware - the earlier "KVS_RGI2 363x260" claim was wrong (a scramble of RGI's 263x366).

## 🔍 The gfxAvailable gate

The VC only transitions to LVDS map view (`SV_LVDS_NavMap_FPK`) when **`gfxAvailable=true`** - else the
video never shows even though it is encoded and sent. The flag is driven by the KOMO GFX-stream-sink
DSI interface, and combined with `LVDS_Available=1` (from MOST video sync) it walks the EB GUIDE state
machine to the map view:

```mermaid
flowchart LR
    accTitle: KOMO gfxAvailable display gate
    accDescr: The GFX-state DSI attribute sets gfxAvailable in KOMOService; only with gfxAvailable and LVDS_Available both true does the VC enter the NavMap FPK view, otherwise the video is sent but not shown.
    ves["videoencoderservice"] --> sink["DSIKOMOGfxStreamSink<br/>(org.dsi.ifc.komogfxstreamsink)"]
    sink -->|"GFX-state DSI attr"| ks["KOMOService.updateGfxState(i,j)<br/>if j==1 -> gfxAvailable = (i==1)"]
    ks --> gate{"gfxAvailable=true<br/>AND LVDS_Available=1?"}
    lvds["MOST video sync"] --> gate
    gate -->|yes| sm["EB GUIDE ViewModeSM -> MAP<br/>BAP rgType=4 -> INTERN_Active_NavFPK_Content=Map(1)"]
    sm --> view["SV_LVDS_NavMap_FPK"]
    gate -->|no| hidden["video encoded + sent,<br/>but VC shows nothing"]
```

Verified as strings in this extract: `DSIKOMOGfxStreamSink` (traceConfig.properties + DSITracer.jar),
`updateGfxState` and `KOMOService` (diag.jar / DSITracer.jar). **Not found literally** here:
`ATTR_GFXSTATE`, `setGFXAvailable`, `ClusterViewMode` - likely EB GUIDE HMI-model symbols not shipped
in this firmware dump, so treat those exact names as inferred. None of this chain lives in
`libPresentationController.so`.

## ⚙️ Relevance to the patch

On this branch the cluster keeps showing the **stock native map** (its own MOST video path), and our
maneuver plane is composited into that stream ([compositing](../../cluster/compositing.md)), so the patch does not need to drive
the gfx gate itself. `BAPBridge.forceGfxAvailable` writes data rate + `gfxAvailable` only when
`Util.isClusterMapMOST()` is true and is a no-op on the FPK cluster; forcing the data rate there parked
the stock kombi map in its hidden context. Context 80 is selected on the
RGI BAP start, not on a renderer first-frame handshake ([rgd-activation](../../rgd/rgd-activation.md), [display-contexts](../../cluster/display-contexts.md)).
