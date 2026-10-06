---
title: MHI2Q CarPlay - Knowledge Index
tags: [moc]
status: complete
---

# MHI2Q CarPlay - Knowledge Index

Map of Content for the reverse-engineering and implementation notes. Each note covers **one topic**;
every factual claim is validated against a source (code / firmware / iOS binary) and states only the
final verified fact. `reconciles:` frontmatter records which legacy docs were folded in.

> **All topics seeded [x]** - 34 notes. `(!)` items inside notes are real product TODOs, not doc gaps.

## 🗂️ [architecture](architecture.md) - process topology, threading, boot / init - build, test & deploy  [x]

## 🪝 Hook - `libcarplay_hook.so`  [x]
- [iap2-interception](hook/iap2-interception.md) - recv/read hooks, FF-5A framing, Identify patch
- [bus-protocol](hook/bus-protocol.md) - localhost TCP :19810, sticky event/command frames, fd/generation lifecycle, signal policy
- [cover-art](hook/cover-art.md) - iAP2 artwork file-transfer reassembly, bounded decode on a worker -> VC picture
- [integration-seam](hook/integration-seam.md) - what LD_PRELOAD interposes vs stays stock (5-symbol export allowlist); NME injection ABI; hardening

## 🧭 Route guidance - RGD -> BAP  [x]
- [rgd-tlv](rgd/rgd-tlv.md) - iAP2 RouteGuidanceUpdate TLV map (0x5200-0x5204), whole-message validation, route_generation
- [rgd-activation](rgd/rgd-activation.md) - route state machine, `visible_in_app`, BAP start -> ctx 80, route-end hold, which nav apps send RGI
- [maneuver-mapping](rgd/maneuver-mapping.md) - full EManeuverType 0-53 -> BAP descriptor
- [bap-fctids](rgd/bap-fctids.md) - CarPlay-owned FctID matrix + gating
- [bargraph-sync](rgd/bargraph-sync.md) - FctID 18 bargraph + renderer arrow fill, shared 600 ms blink
- [lane-guidance](rgd/lane-guidance.md) - FctID 24 + renderer lane panel, atomic LANES batch
- [vc-route-text](rgd/vc-route-text.md) - FctID 19 route text, ETA toggle, grapheme-safe scrolling, vc-text.bin
- [navsd-catalogue](rgd/navsd-catalogue.md) - complete NavSD FctID catalogue (1-56)

## 🖥️ Cluster  [x]
- [display-contexts](cluster/display-contexts.md) - dc[74]/dc[80], displayables 98/33/101/102, switch worker
- [compositing](cluster/compositing.md) - maneuver overlay over native map, HU->MOST->VC H.264
- [maneuver-renderer](cluster/maneuver-renderer.md) - :19800 protocol, C++ scene engine, visible area, watchdog
- [kdk-geometry](cluster/kdk-geometry.md) - KDK backings 101/102, VC Fct44/Fct54-driven visibility & stage, HU geometry table

## 🤖 Android Auto  [x]
- [overview](android-auto/overview.md) - what works without / with the gal hook, topology, Java bridge, cluster map, switches, logs
- [firmware-porting](android-auto/firmware-porting.md) - receiver profiles: get gal + libautoreceiver.so, compute, verify layouts, trial marker

## 🎛️ Input  [x]
- [touchpad-dpad](input/touchpad-dpad.md) - MMI touchpad -> DPAD bridge (TouchpadController)
- [steering-wheel](input/steering-wheel.md) - MFW roller: rotation = stock zoom, press = route-info toggle

## 📱 HMI - head-unit screen  [x]
- [pdc-small-stage](hmi/pdc-small-stage.md) - CarPlay stays beside the side parking (OPS) popup: small stage, message 108, APS drawer, status line

## 🚀 Deploy  [x]
- [install](deploy/install.md) - release contents, M.I.B. installer, manual install (SSH or Telnet), verify, uninstall
- [supervisor-lifecycle](deploy/supervisor-lifecycle.md) - smartphone_integrator, startup wrapper + renderer monitor, generation ownership
- [connect](deploy/connect.md) - USB / NCM / Bonjour connect flow + failure root cause
- [session-lifecycle](deploy/session-lifecycle.md) - session audit: watchdog-hang, USB pre-RTSP class, resilience risks R1-R4

## 🗺️ AltScreen - CarPlay map video on the cluster  [x]
- [altscreen-mhi2q](deploy/altscreen-mhi2q.md) - stream-111 hook + renderer, ctx 81/82, SafeArea/ViewAreas, PF port allowlist, runtime knobs, triage
- [hw-decoder](altscreen/hw-decoder.md) - zero-copy Qualcomm OMX H.264 decode into displayable 99, software fallback

## 🔍 Reverse engineering - iOS  [x]
- [accessoryd-rgd](re/ios/accessoryd-rgd.md) - ACCNav RGUpdate enum (accessoryd 23G71)
- [carkitd-bonjour](re/ios/carkitd-bonjour.md) - iOS 26 vs 27 connect divergence
- [maps-maneuvers](re/ios/maps-maneuvers.md) - Maps accNav enum + signed exit angle

## 🔍 Reverse engineering - firmware (MHI2Q MU1316 decompile, upstream)  [x]
- [display-manager](re/firmware/display-manager.md) - DisplayManager + dmdt, window binding
- [komo-widget-video](re/firmware/komo-widget-video.md) - KOMO widget video + gfxAvailable gate
- [dsi-carkombi](re/firmware/dsi-carkombi.md) - DSICarKombi + DSIKombiSync2
- [vc-aio-arrow](re/firmware/vc-aio-arrow.md) - why the VC-native AIO arrow path is blocked (InfoStates=6 rejected) -> BAP HUD instead

---

## ✅ Verification

Every note carries a `status` recording how its facts were checked.

| status | meaning | notes |
|---|---|---|
| `verified-decompile` | confirmed by reading the disassembly/decompilation of the actual binary | rgd-tlv, rgd-activation, accessoryd-rgd, maps-maneuvers, carkitd-bonjour, display-manager, compositing, kdk-geometry (VC section) |
| `verified-trace` | confirmed against the actual on-device log / config | connect |
| `verified-on-car` | confirmed working in the car (logs + driver report) | android-auto overview |
| `verified-source` | confirmed against this repo's source (ground truth for our own code) | architecture, iap2-interception, bus-protocol, cover-art, integration-seam, maneuver-mapping, bap-fctids, bargraph-sync, lane-guidance, vc-route-text, maneuver-renderer, display-contexts, kdk-geometry (HU), touchpad-dpad, steering-wheel, supervisor-lifecycle, session-lifecycle, dsi-carkombi, navsd-catalogue, android-auto firmware-porting |
| `partially-verified` | code paths confirmed; some symbols only string-level / inferred | komo-widget-video (gfx-gate chain) |
| `from-re-notes` | carried faithfully from prior RE notes; not re-verified in the binary this pass | vc-aio-arrow |

**Corrections the verification pass caught** (wrong facts inherited from legacy docs, now fixed):
- `display-manager` - invented symbol names (`display_create_window` / `screen_manage_window`) -> real `CTerminal` + `CScreenHandler::evtNewWindow` / `CASIMostEncoder::setActiveDisplayable`.
- `komo-widget-video` - fabricated `KVS_RGI2 363x260` -> real `KVS_FPK 210x153 / 328x181`.
- `connect` - denominator 256 not 257; the ~3 s lag is Bonjour resolution, not addressing (addressing ~8 ms).
