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

1. *Calibration grid ON / OFF* — ruler over the video, lines every 60 px. From 1.5 it is drawn
   on the hardware decoder's picture too (2D blitter fills before each post); until then it
   showed only with software decode.
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
| `ALTSCREEN111_FPS` | 30 | advertised max frame rate (`/mnt/app/root/mibr-carplay111-fps` overrides, 5-60; MMI *Cluster Map Frame Rate* writes 60 or removes the file, and the Android Auto hook reads the same file from 1.5; above 30 at own risk, some MHI2Q builds crash at 60. The VC captures plane 99 on a fixed 33 ms timer in `videoencoderservice`, so more than 30 fps does not reach the cockpit; it only adds decode load) |
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
`maps:/car/instrumentcluster/map?showETA=…&maneuverLayout=topAligned&showSpeedLimit=…&showCompass=…`. The hidden-card
URL names `topAligned` on purpose: `no` or no layout selects automatic layout, and a side layout keeps reserving the
card column as a SafeArea inset even with the cards hidden; top layout with cards and ETA off adds none
(luka-dev `1299619`, from iOS 26.6/27.0 static analysis, car test pending). With our BAP/KDK arrow
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

**View change animation.** `updateViewArea` carries `animationDurationMillis` 0 (car-tested). An
earlier trace found the animation inert, but that was before `adjacentViewAreas` made the switch
work at all, so it is worth one retest: `echo 300 > /mnt/app/root/mibr-carplay111-view.animation`
(0..1000 ms, read at each switch; logged as `GEN2 updateViewArea animationDurationMillis=…`).

**Pause while hidden (opt-in, 1.5).** Java reports with `CMD_ALT_HIDDEN 0x0119` whether the cockpit
shows the phone map (hidden = another VC tab, or the Audi map picked with a long roller press).
With `/mnt/app/root/cluster_video.pause_hidden` (GEM *Pause cluster map while hidden*, MMI
*Pause Hidden Cluster Map*) the hook sends `stopUI` while hidden and `showUI` (then
`updateViewArea`, `forceKeyFrame`) when the map is back, so the phone does not draw and encode a
map nobody sees; the cockpit keeps the last picture meanwhile. Without the marker a hidden map
keeps streaming as before. Car test pending: whether iOS keeps stream 111 open across `stopUI`.

**Slow renderer (1.5).** The hook queues up to 32 access units (about 1 s) for the renderer. When
it is full, the unsent backlog is dropped, pictures are skipped until the next IDR on the same
connection and the phone is asked for one (`forceKeyFrame`), logged as `gen2 video AU queue full
… backlog dropped, key frame requested`. Until 1.4 a full 16-AU queue reset the renderer's
connection (`gen2 video AU rc=2 -> consumer reset`), which cost a reconnect and, with hardware
decode, a new decoder. A renderer still priming (its first IDR not yet sent) is reset as before,
and so is one that delivered nothing between two overflows (it stopped reading). The status file
counts the skips (`resyncs=`).

**Key-frame back-channel (1.5).** `altscreen_render` writes one byte `K` on the tee socket when its
decoder reports a damaged picture (at most every `ALTR_KEYFRAME_GAP_MS`, 2 s); the hook answers
with `forceKeyFrame` through its serialized command queue (`gen2 renderer asked for a key frame`).
A renderer that closes the tee is noticed on the next read (`gen2 renderer closed the tee`).
The hook's output worker now sleeps on its condition variable while there is nothing to send,
and the control worker reads its marker files at most every 250 ms (it read about nine every
10 ms before).

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
| `ALTR_STATUS_FILE` | `/tmp/altscreen_render.status` | `state=`, size, `frames=`; from 1.5 also `decoder=` (hardware, software, suspended, frozen, none), `hw_off=` (the marker that keeps hardware off), `connections=`, `keyframe_requests=`, `damaged=`. GEM *Cluster video status* prints it |
| `ALTR_AA_SW_FAST` | 0 | 1 = the Android Auto software fallback skips the deblocking filter (about a third less CPU). Off by default: the picture then drifts until the next key frame, a minute or two away |
| `/mnt/app/root/altscreen_render.noasm` | absent | FFmpeg's plain C code instead of its ARM/NEON assembly (GEM *Software decoder plain C (no NEON)*; read at renderer start). After an illegal instruction the renderer creates `/tmp/altscreen_render.noasm` itself and restarts on C code |
| `ALTR_KEYFRAME_GAP_MS` | 2000 | at most one key-frame request over the tee back-channel per this many ms |
| `/mnt/app/root/altscreen_render.hwdecode.off` | absent | software decode (GEM *HW decoder ON / OFF*); `/tmp/…` for this boot only. From 1.5 the renderer writes the persistent one only after a hung video core; any other hardware failure writes the `/tmp` one (this boot only) |
| `/mnt/app/root/altscreen_render.hwdecode.delayed` | absent | CarPlay and Android Auto wait `ALTR_HW_MIN_UPTIME_S` (120 s) after boot before hardware decode, software until then (GEM *HW decoder 2-min delayed start*); absent = hardware at once, kept apart from the main screen's decoder by the main-screen yield |

The renderer runs at QNX priority 15 (like `maneuver_render`): at the default 10 the compositor's
workers can stall every renderer thread for 300-650 ms at once (measured upstream, 2026-10-09).

`.live` holds `pid=` and, from 1.5, `seq=` (a counter bumped on every rewrite). Java counts its own
250 ms polls since `seq` last changed and treats more than four as stale, so a wall-clock step or
a coarse mtime cannot drop a live picture; a file without `seq=` (older renderer) is still judged
by its mtime.

**End of a stream (1.5).** With hardware decode the last picture stays on window 99 for 1.5 s while
the decoder is already closed (`ALTR_DISCONNECT_FREEZE_MS`). Both hooks keep their tee listening
for as long as their process lives, so a new tee connection alone is not a new stream: the frozen
picture stays until that source sends bytes, and then its decoder reopens on the same window
(`new carplay stream; decoder resumes on the frozen window`), with no black frame and no new
window. Another phone type, or 1.5 s without a new stream, releases the window. Before this, the
renderer's immediate reconnect to the CarPlay hook ended the freeze at once, and closing the
frozen decoder after its connection was gone dereferenced a cleared source pointer (renderer
crash at a session end).

The software fallback uses FFmpeg's ARM/NEON assembly from 1.5 (`FFMPEG_NEON=0
scripts/build_altscreen_render.sh` builds the plain C decoder of 1.4). On QNX FFmpeg picks NEON from
its build flags, not the CPU, so the renderer checks the syspage (`ARM_CPU_FLAG_NEON`) at start and
turns NEON off when the CPU does not report it: `CPU NEON yes; software decoder uses NEON` in
`altscreen_render.log`. The GL path's YUV->RGB matrix follows the stream's VUI colour description
(BT.709, full range); unsignalled streams keep the BT.601 video-range matrix used so far.

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

GEM *Cluster video status* shows steps 5-6 and the stream-111 counters without pulling logs:
the renderer's `state=`, `decoder=`, `hw_off=` and counters, whether `.live` exists, the hook's
`source_aus` / `delivered_aus` / `dropped_aus`, the last Android Auto hook events and which opt-in
switches are set.

`sloginfo` is a ~90 s ring dominated by normal cluster-video traffic (`devp-iso-mmx`,
`videoCore`, `vpeCore`, `MMPM` lines are baseline noise); use the logs above for anything
time-ordered.
