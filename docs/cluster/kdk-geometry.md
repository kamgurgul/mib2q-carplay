---
title: KDK plane geometry (backings 101/102, stages)
tags: [cluster, kdk, geometry, verified]
status: verified-source
sources:
  - code: java_patch/com/luka/carplay/cluster/ClusterLayerController.java
  - code: java_patch/de/audi/tghu/navi/app/cluster/ScreenCombiBAPListener.java
  - code: java_patch/de/esolutions/hmi/widgets/audi/evo/high/widgets/CombiMapController.java
  - firmware: LayoutMIB2HighB9 / B9Sport / Q7 (Layout.getIntegerConstant)
  - firmware: VC AU491 gtf2 (SV_LVDS_KDK)
reconciles:
  - docs/reference/CLUSTER_KDK_GEOMETRY.md
  - docs/reference/VC_LAYOUT_STYLE.md
---

# KDK plane geometry (backings 101/102, stages)

Where the maneuver overlay (98) and its KDK backing sit on the cluster, and who decides.

## 📋 Context

> [display-contexts](display-contexts.md) - ctx 80 planes -> **kdk-geometry** - position/crop 98 + 101/102.
> Applied by `ClusterLayerController.reapply()` on every stock KDK model update, VC FctID 44/54 input
> and CarPlay context change.

## 🔍 The VC obeys; the HU dictates

> `status: verified-decompile` - decompiled against AU491 `gtf2` (ELF ARM, image base 0x100000); the
> `FUN_*` addresses below are from this build.

VC-side (AU491 `gtf2`): the KDK surface is placed purely from data-pool variables
`LVDS_KDK_position_x/y/opacity/visible` (resolved by name at model-init - **no code xref** on any of
the four, nor on the `_DM_` twins / `LVDS_KDK_follow`). `openKDKHole` (`FUN_00177be8`) /
`closeKDKHole` (`FUN_00177dac`) carry no geometry - each only appends animation event 15 (open) /
14 (close) as a `uint32` to two message targets (`appendUInt8 1,1` and `1,2`, via CHMISendMessage)
and flips `m_KDKHoleOpen`. `resizeHole` (`FUN_00711490`) is a pass-through:
`INode2D::setTranslation` from its two args, `setScale` from model width/height - nothing
design-dependent. The only hard-coded hole rectangles in this area are Night Vision's
(`makeNvHole` `FUN_001e63b4`: big stage 640x360 centred; S-design(full) x=0, y=+48, 640x400) -
**not KDK**. So the exact KDK rectangle comes over from the HU via
`IDisplayManagerKombiControl.setCropping/setPosition`.

## 📊 HU-side authoritative table (MU1316)

`Layout.getIntegerConstant(id)`, chain `LayoutMIB2HighB9Sport -> LayoutMIB2HighB9 -> LayoutMIB2HighQ7`.
Classic (B9) overrides 58/59/60/61 and the map origin 108/109; everything else falls through to Q7.

| id | meaning | Classic (B9) | Sport (B9Sport) |
|---:|---|---:|---:|
| 58 / 59 | in-tube anchor -> backing **101** | 1055, 207 | 984, 139 |
| 60 / 61 | popup anchor -> backing **102** | 1091, 110 | 1091, 110 |
| 122-125 | in-tube crop src (x,y,w,h) | 59, 27, 210x153 (Q7) | 0, 0, 328x180 |
| 118-121 | popup crop src (x,y,w,h) | 59, 27, 210x153 | 59, 27, 210x153 |
| 108 / 109 | map origin (displayables 33, 58) | 0, 26 | 0, 26 |
| 80 / 81 | small-stage map offset | 0, 0 (Q7) | -476, 0 |

A Q7 runs `LayoutMIB2HighQ7` / `LayoutMIB2HighQ7Sport` (Sport extends Q7 directly, not B9). Same
ids, read from the MU1329 stock jar; the crops and 81 are as above:

| id | meaning | Q7 | Q7Sport |
|---:|---|---:|---:|
| 58 / 59 | in-tube anchor | 1045, 199 | 979, 133 |
| 60 / 61 | popup anchor | 1083, 112 | 1083, 112 |
| 108 / 109 | map origin | 0, 18 | 0, 18 |
| 80 | small-stage map offset | 0 | -460 |

The code reads all of these from the live `Layout`, so the planes are right on both cars. What
is measured per car is anything laid out *inside* plane 99 by the phone: the Android Auto safe
areas are B9 numbers and are moved by the difference in 80 and 109
([Android Auto overview](../android-auto/overview.md), issue #53).

## 🔄 Stages & visibility (VC-driven)

- **Backing 101** = sport/in-tube 328x180 stage; **backing 102** = popup 210x153 stage.
- **Stage** - while CarPlay owns the cluster it follows **VC FctID 54** (`setMapPresentation(
  largeMapView, ...)` -> `onVcPresentation`, emitted at the VC's stage-animation midpoint): popup =
  `largeMapView`. Before the first FctID 54 (and outside CarPlay) the stock KDK hint is used
  (`popup = !inTube`). The Audi View button (`NAV_VIEW_SIZE_CHOICE`) is the map's full/small axis, not
  the KDK stage.
- **Visibility** - opacity follows **VC FctID 44** (`updateMapVisibility` -> `onVcVisibility`):
  100 when the VC reports the KDK visible, 0 otherwise; the stock KDK visible/opacity hint is used only
  until the first FctID 44. The requested View mode never reveals a layer.
- `ScreenCombiBAPListener` forwards both **before** stock sends its Status acknowledgement, so the HU
  plane and the VC animation start together.
- **Eligibility** - 98 and its backing are opaque only while `ScreenModule.isConnected() && isNavActive()`;
  the other backing is always 0. Outside CarPlay the cached stock values are restored on 101/102 and 98
  is 0, so Audi navigation's KDK never stays transparent after a disconnect.
- Plane 98 gets the stage's crop (`setCropping`, src = crop, dst = anchor), the backing gets the anchor
  (`setPosition`). `maneuverViewport()` returns the same crop to the renderer ([maneuver-renderer](maneuver-renderer.md)).
- Geometry comes from the terminal's live stock `Layout` (`updateLayout`), cached as primitives; a
  fallback Sport table is used only before the first stock model update. Every distinct decision is
  logged once (`ClusterLayers apply ...`).

## ⚠️ `NAV_VIEW_SIZE_CHOICE` is not raised for every stage change

> `status: verified-on-car` - Audi MHI2Q `MHI2Q_US_AUG22_P5087` (NAR G22), `LayoutMIB2HighB9`
> (measured by complex256, PR #15).

On **Classic (B9)** the Audi View button frequently raises **no** `NAV_VIEW_SIZE_CHOICE` update while
CarPlay owns the cluster - the stock map restages, but the model event never arrives. A stage derived
from that flag alone then goes stale and the panel keeps the *other* stage's anchor: the popup anchor
`(1091,110)` in an in-tube view sits **36 px right and 97 px up** of the in-tube anchor `(1055,207)`,
i.e. "too far up and right".

This branch covers it twice:

- `ClusterLayerController` takes the stage from the VC's own **FctID 54** (`onVcPresentation`) while
  CarPlay owns the cluster; the view-size flag is only the fallback before the first Fct54.
- `CombiMapController.syncViewSize()` re-reads the choice model on every KDK update that *does* arrive
  (A5 / KDK via displayables); it compares against the cache, so a skin whose dedicated update is
  delivered reliably (Sport) does no extra work.

Both stage anchors in the table above are correct as they stand - a stale stage is not a geometry
problem, and tuning the anchors to compensate for one will mis-place the other. In the log, the
`ClusterLayers apply` line prints `view=` and `stage=` together: a `view=single stage=popup` (or the
reverse) pairing is a stale stage, a matched pairing with a visibly wrong position is genuinely
geometry.

## ⚠️ Do NOT apply the small-stage offset (80/81) to the KDK panel

Stock adds the `-476,0` Sport singlescreen offset to the **map planes 33/58 only**. The KDK panel and
its backing have no view-size dependency (`positionKDKBackgrounds` / `handleKdkDualTerminal` read no
view size). Moving the panel by -476 in Sport singlescreen was measured on the car to break a view
the stock keeps correct, so `ClusterLayerController` **logs the offset but never applies it** to the
panel.
