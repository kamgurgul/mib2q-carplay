---
title: CarPlay AltScreen (cluster video) on Audi MHI2Q
tags: [altscreen, cluster, video, mhi2q, deploy]
status: verified-on-car (Audi A5 F5, MU1329)
---

# CarPlay AltScreen (cluster video) on Audi MHI2Q

AltScreen shows the **CarPlay secondary-screen map video** on the Audi Virtual Cockpit, beside
the route-guidance overlay. It ports the Škoda `mhi2_altscreen_carplay` GEN2 hook to MHI2Q and
adds an on-HU decoder that draws the video into a cluster plane the stock DisplayManager already
composites. Developed and tested on an Audi A5 (F5) with MU1329.

> [!WARNING]
> Use only on a unit you own, with M.I.B. / SSH access. The installer keeps a `.carplay-stock`
> copy of every edited config and an uninstall path, but a wrong setting can leave the cluster or
> CarPlay in a bad state until reboot.

---

## 1. Pipeline

On Škoda MU1440 the hook injects the CarPlay H.264 as MPEG-TS straight into the cluster's MOST
channel. **Audi MHI2Q does not feed the cluster that way:** the head unit renders and
H.264-encodes the cluster image itself (`videoencoderservice`) and the VC decodes it. So, like
the maneuver overlay ([compositing](../cluster/compositing.md),
[display-contexts](../cluster/display-contexts.md)), AltScreen decodes on the HU and draws into a
managed QNX Screen window that the stock encoder captures:

```
iPhone CarPlay Auxiliary/ScreenAlt
      │  stream 111 (H.264, encrypted)
      ▼
libaltscreen111_mhi2q.so  (second LD_PRELOAD in dio_manager)
      ├─ interpose AirPlay /info, SETUP, stream 111   (PLT, constructor-free)
      ├─ reuse the stock AES session key to decrypt the stream
      └─ AVCC → Annex-B on 127.0.0.1:19820  (the "tee")
      ▼
altscreen_render
      ├─ hw_decode.c  Qualcomm OMX, zero copy into the window (default)
      └─ decode.c + video_gles.c  FFmpeg software decode → GLES (fallback)
      ▼
cluster displayable 99 → DisplayManager ctx 81 {99} or ctx 82 {98,101,102,99}
      ▼
stock videoencoderservice → MOST/LVDS → Virtual Cockpit
```

Cluster contexts (added in `DisplayManagerMIB2High`):

| ctx | displayables | meaning |
| ---: | --- | --- |
| 74 | stock | resting cluster (stock map) |
| 80 | `{98,101,102,33}` | RGI maneuver over the stock native map |
| **81** | `{99}` | CarPlay video only |
| **82** | `{98,101,102,99}` | CarPlay video + RGI maneuver overlay |

`ScreenModule` is the single cluster-context writer. `AltScreenModule` watches the renderer's
heartbeat file and selects 81/82 while video is live, falling back to 80/74 otherwise.
`ClusterLayerController` makes plane 99 visible whenever video is live (not gated on
`navActive`) and positions it at the stock map origin (layout slots 108/109 plus the small-stage
offset), never at the KDK offset ([kdk-geometry](../cluster/kdk-geometry.md)).

Hardware decode details: [hw-decoder](../altscreen/hw-decoder.md).

---

## 2. Build and install

Built by `./scripts/build_all.sh` (see the README). Individually:

```sh
scripts/build_altscreen_hook.sh     # -> build/libaltscreen111_mhi2q.so  (GEN2 source, -DALT111_TARGET_MHI2Q)
scripts/build_altscreen_render.sh   # -> build/altscreen_render  (fetches and builds FFmpeg 6.1.5, H.264 only)
```

Host decode self-test:

```sh
cd altscreen_render
make hosttest FFMPEG_PREFIX=/path/to/ffmpeg
./host_decode_test some_carplay_clip.mp4
```

The two files are optional additions to a normal release in `mod/carplay/`. The installer copies
them when present, `carplay_startup.sh` preloads the hook beside `libcarplay_hook.so`, and
`carplay_monitor.sh` starts `altscreen_render`. No extra config edits. Uninstall removes them.

---

## 3. SafeArea and ViewAreas

iOS draws the map over the whole 1440×540 canvas but keeps the puck and its overlays inside the
**SafeArea**. The hook advertises three full-canvas ViewAreas that differ only in SafeArea:

| index | VC layout | built-in SafeArea | override file |
| --- | --- | --- | --- |
| 0 | wide map (small dials) | x=360 y=87 w=720 h=270 | `/mnt/app/root/mibr-carplay111-safearea.conf` |
| 1 | classic small map (big dials) | x=520 y=64 w=400 h=300 | `/mnt/app/root/mibr-carplay111-safearea-small.conf` |
| 2 | Sport skin small map (left of the rev counter) | x=566 y=64 w=360 h=300 | `/mnt/app/root/mibr-carplay111-safearea-sport.conf` |

VC FctID 54 (`largeMapView`) selects wide vs small; for the small map the stock layout's small-stage
offset picks classic (0) or sport (`LayoutMIB2HighB9Sport`, -476): that offset shifts plane 99, so the
classic rect would land at panel x 44..444 on Sport while its map runs to the rev counter at ~600
(issue #20). The sport rect is mib2q-carplay-rgi's grid-calibrated panel (90,90) 360x300 moved by
(+476, -26) into our canvas, *car test pending*. Both small maps are 300 tall: iOS treats a SafeArea
narrower than ~498 px as narrow and then hides third-party apps' turn card and ETA unless
0.3 x height exceeds ~87 px (270 did not).
Java sends `CMD_ALT_ZONE` (0=full, 1=sport, 2=classic), `hook/altzoom` calls
`altscreen111_view_area()`, and the control worker sends `updateViewArea` (which **must** carry
`adjacentViewAreas`, or iOS acknowledges but keeps the old layout) until the phone acknowledges.
The current view is also the `initialViewArea` of the next `/info`.

Calibration from the GEM **CarPlay-RGI** page:

1. *Calibration grid ON / OFF* — ruler over the video, lines every 60 px.
2. Put `x=`/`y=`/`w=`/`h=` in `carplay_safearea.conf`, `carplay_safearea_small.conf` and/or
   `carplay_safearea_sport.conf` in the card root, press *Apply SafeArea from SD*, reconnect.
3. *Reset SafeArea to built-in* returns to the table above. *Small-map SafeAreas ON / OFF*
   falls back to a single ViewArea.

The hook logs what it advertised: `GEN2 ViewArea[n] wide|classic|sport area=... safe=...` and
`gen2 view area -> n`.

> [!WARNING]
> Keep both advertised dimensions even (ideally multiples of 16). An odd height such as
> 1440×455 makes the phone tear the session down instead of requesting stream 111.

---

## 4. Stream-111 port and the PF allowlist

The unit filters inbound traffic on `carplay0` with a PF allowlist (`pfctl -sr`):
**5000:5001, 5010, 6000:6001, 6030, 6100, 6200, 7000:7001, 7100**. Stock uses 5000 (RTSP),
5001 (events) and **6030 (main screen, stream 110)**. Binding stream 111 on 6030 starves the main
screen until `smartphone_integrator`'s 10 s `decoderRestartTimeout` tears the session down.

The default is **7100**. The hook refuses 5000/5001/6030 even if configured, falls back through
6200, 6100, 5010, 7001, 7000, and never uses an ephemeral port (it would be outside the
allowlist). Runtime override, re-read at every SETUP:

```sh
echo 6200 > /mnt/app/mibr-carplay111.port    # then reconnect the phone
```

A blocked port looks like `MHI2Q stream111: no iPhone connection on port N after 10s` in the
hook log while the renderer sits at `frames=0`.

---

## 5. Runtime knobs

Hook (`libaltscreen111_mhi2q.so`), env in the carplay child:

| var | default | meaning |
| --- | --- | --- |
| `ALTSCREEN111_ENABLED` | 1 | 0 = fully inert (stock CarPlay only) |
| `ALTSCREEN111_ADVERTISE` | 1 | 0 = `/info` byte-for-byte stock (same as `/mnt/app/mibr-carplay111.noadvertise`, the GEM *Cluster video OFF*) |
| `ALTSCREEN111_PORT` | 7100 | stream-111 port (§4); `/mnt/app/mibr-carplay111.port` wins |
| `ALTSCREEN111_WIDTH` / `_HEIGHT` | 1440 / 540 | advertised secondary display |
| `ALTSCREEN111_FPS` | 30 | advertised max frame rate (`/mnt/app/root/mibr-carplay111-fps` overrides, 5-60; above 30 at own risk, some MHI2Q builds crash at 60. The VC captures plane 99 on a fixed 33 ms timer in `videoencoderservice`, so more than 30 fps does not reach the cockpit; it only adds decode load) |
| `ALTSCREEN111_WIDTH_MM` / `_HEIGHT_MM` | 290 / 108 | advertised physical size; iOS scales the cluster UI from it (`/mnt/app/root/mibr-carplay111-physical` with `w h` in mm overrides, e.g. `echo 320 120 > …` for the larger MIBSI geometry) |
| `ALTSCREEN111_TEE_PORT` | 19820 | loopback Annex-B tee |
| `ALTSCREEN111_INLINE_HOOKS` | 0 | 1 = also install inline prologue hooks if PLT interposition is not on the call path |

Marker files (`/mnt/app/root/`): `mibr-carplay111-zoom.inverted` flips the steering-wheel zoom
direction ([steering-wheel](../input/steering-wheel.md)); `mibr-carplay111-viewareas.single`
uses one ViewArea; `mibr-carplay111-noadvertise.senders` lists extra senders (one
`model osBuildVersion` line, `*` = any build) that get the stock `/info`. The known wireless
legacy dongle persona (`model=iPhone9,1`, `osBuildVersion=18D70`) is built in:
that firmware never requests stream 111 and drops the session when it sees the
advertisement. These listed senders retain the legacy AES listener. The native
HW301 proxy candidate reports `model=HW301` and uses the ordinary two-display
SETUP/command path. `viewareas.single` gives it one car-provided view; it does
not disable the cluster.

**Cluster URL content is set by four GEM switches** (*Cluster map turn card / ETA / speed limit /
compass ON / OFF*), independent of route guidance, plus a fifth one with priority (*Cluster map all
features on wide view ON / OFF*). Each is a value file in `/mnt/app/root/`; absent = default:

| file `mibr-carplay111-nav.…` | default | values |
| --- | --- | --- |
| `showManeuver` | `yes` | `yes` = root URL `maps:/car/instrumentcluster` with the app's turn card; `no` = bare `…/map` surface (the root URL is "anyContent" and keeps the card even with `showETA=no`) |
| `showETA` | `no` | `yes` / `no` |
| `showSpeedLimit` | `no` | `yes` / `no` / `user` (follows the iPhone's Maps setting; file only) |
| `showCompass` | `no` | `yes` / `no` / `user` (follows the iPhone's Maps setting; file only) |
| `wideAll` | `no` | `yes` = on the wide view (ViewArea 0) turn card, ETA, speed limit and compass are all ON, whatever the four files above say; the classic and Sport small maps keep using them. `no` = the four switches apply to every view |

Defaults give `maps:/car/instrumentcluster?showETA=no&showSpeedLimit=no&showCompass=no`; turn card off gives
`maps:/car/instrumentcluster/map?showETA=…&maneuverLayout=no&showSpeedLimit=…&showCompass=…`. With our BAP/KDK arrow
active the phone's turn card is therefore shown as well unless the turn-card switch is OFF. A GEM press
with a session up (`command_ready=1` in `/tmp/mibr-alt111-gen2.status`) re-sends `showUI` through
`/tmp/mibr-alt111-show-only`; otherwise it applies at the next connection. With `wideAll=yes` the URL
depends on the cockpit layout: a switch between the wide view and a small map sends `showUI` with the
new URL first, then `updateViewArea`. When an app starts
guidance and iOS hands it the cluster, iOS sends `suggestUI`; 1 s later the hook answers with
`showUI` (then `updateViewArea` and `forceKeyFrame`) so that app gets the current URL too. A `showUI`
that repeats the URL the phone already has does not reach that app, so with ETA ON the answer starts
with one `showUI` carrying `showETA=no` and then sends the saved URL (issue #35: without it Waze drew
no ETA after a reconnect or after another app had guided, until the ETA switch went OFF and ON).
Java still sends
`CMD_ALT_RGI 0x0116` on route-guidance edges; `altscreen111_set_rgi` only logs it.

The turn-card URL carries no `maneuverLayout`: 1.2's `rightAligned` pinned the app's card to the SafeArea's
right edge and the app moved the puck left of it (issue #20; on the Sport skin the card also sat
behind the rev counter). `mibr-carplay111-nav.maneuverLayout` (`left`, `right`, `top`) adds one back.

Every existing URL override wins (`/tmp/mibr-alt111-url-mode`, `mibr-carplay111-nav.surface`,
`mibr-carplay111-url-map.enabled`, `mibr-carplay111-nav-query.enabled`, `ALTSCREEN111_PARITY_URL`).
`mibr-carplay111-rgi-url.disabled` restores the historic bare `maps:/car/instrumentcluster`.

Renderer (`altscreen_render`):

| var / file | default | meaning |
| --- | --- | --- |
| `ALTR_TEE_HOST` / `ALTR_TEE_PORT` | 127.0.0.1 / 19820 | must match the hook's tee |
| `ALTR_WIDTH` / `ALTR_HEIGHT` | 1440 / 540 | cluster surface size |
| `ALTR_DISPLAYABLE_ID` | 99 | cluster plane id (also in `DisplayManagerMIB2High.dc[81]/dc[82]`) |
| `ALTR_LIVE_FILE` | `/tmp/altscreen_render.live` | heartbeat (`pid=`), refreshed while frames are presented |
| `ALTR_LIVE_TIMEOUT_MS` | 0 | heartbeat removed if no frame within this; 0 = kept while the tee is connected (the hook closes it when stream 111 ends) |
| `ALTR_STATUS_FILE` | `/tmp/altscreen_render.status` | `state=`, size, `frames=` |
| `/mnt/app/root/altscreen_render.hwdecode.off` | absent | software decode (GEM *HW decoder ON / OFF*); `/tmp/…` for this boot only |
| `/mnt/app/root/altscreen_render.hwdecode.delayed` | absent | CarPlay waits `ALTR_HW_MIN_UPTIME_S` (120 s) before hardware decode (GEM *HW decoder 2-min delayed start*); Android Auto always waits |

The renderer runs at QNX priority 15 (like `maneuver_render`): at the default 10 the compositor's
workers can stall every renderer thread for 300-650 ms at once (measured upstream, 2026-10-09).

Stock NAVSD *INITIALIZING* ("Initialising navigation system", stock navigation still loading after a
cold boot) also covers plane 99 until stock navigation reports ready. It is not deferred: upstream
removed both workarounds (luka-dev/mib2q-carplay-rgi 2969ed8) because neither cleared it on the car.
The NAVSD BAP module stays off until Java navigation registers its cluster listener, and
OP_State NORMAL needs the native readiness report, so there is nothing to override in that window
([STOCK_NAV_READINESS.md](https://github.com/luka-dev/mib2q-carplay-rgi/blob/main/docs/cluster-and-rgi/STOCK_NAV_READINESS.md)).

Java: `-Dcarplay.altscreen.liveFile=…` if `ALTR_LIVE_FILE` changes.

Route guidance can be turned off independently (`/mnt/app/carplay_rgd.disabled`, or the
`rgd_disable` mod / GEM button); with AltScreen live the cluster then shows pure video (ctx 81).

---

## 6. Triage

Turn on verbose logging (GEM *Save logs to SD* or `touch /mnt/app/carplay_verbose`), reconnect,
start navigation, collect logs again. Walk the chain and stop at the first missing line:

| # | Where | Expect | If missing |
| --- | --- | --- | --- |
| 1 | `/tmp/altscreen111.log` | `GEN2 lazy runtime ready target=mhi2q …` | hook not preloaded: check `carplay_wrapper.log` |
| 2 | same | `GEN2 SETUP contains stream111 cid=…` | phone did not request 111: wireless dongle, advertisement off, or a sender on the no-advertise list |
| 3 | same | `captured stock CarPlay master AES key at SetSecurityInfo caller` | `libairplay.so` differs from MU1329: the caller window in the key observer needs adapting; send `sha256sum /mnt/app/eso/lib/libairplay.so` |
| 4 | same | `AltScreen stream 111 connected` | port not reachable (§4) |
| 5 | `/tmp/altscreen_render.status` | `state=decoding`, rising `frames=` | decoder: try *HW decoder ON / OFF* |
| 6 | `/tmp/altscreen_render.log` | `connected to tee`, `GL up on displayable 99` | displayable not bound: `dmdt gd` |
| 7 | `/tmp/carplay_java.log` | `altScreen video live -> ctx 81/82`, `cluster -> ctx 81` (or 82) | heartbeat stale or `AltScreenModule` disabled |
| 8 | same | `ClusterLayers apply … video=on videoDst=(x,y)` | plane 99 position/opacity |

`sloginfo` is a ~90 s ring dominated by normal cluster-video traffic (`devp-iso-mmx`,
`videoCore`, `vpeCore`, `MMPM` lines are baseline noise); use the logs above for anything
time-ordered.
