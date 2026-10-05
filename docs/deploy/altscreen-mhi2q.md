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
**SafeArea**. The hook advertises two full-canvas ViewAreas that differ only in SafeArea:

| index | VC layout | built-in SafeArea | override file |
| --- | --- | --- | --- |
| 0 | wide map (small dials) | x=360 y=87 w=720 h=297 | `/mnt/app/root/mibr-carplay111-safearea.conf` |
| 1 | classic small map (big dials) | x=520 y=87 w=400 h=297 | `/mnt/app/root/mibr-carplay111-safearea-small.conf` |

VC FctID 54 (`largeMapView`) selects the view: Java sends `CMD_ALT_ZONE`, `hook/altzoom` calls
`altscreen111_view_area()`, and the control worker sends `updateViewArea` (which **must** carry
`adjacentViewAreas`, or iOS acknowledges but keeps the old layout) until the phone acknowledges.
The current view is also the `initialViewArea` of the next `/info`.

Calibration from the GEM **CarPlay-RGI** page:

1. *Calibration grid ON / OFF* — ruler over the video, lines every 60 px.
2. Put `x=`/`y=`/`w=`/`h=` in `carplay_safearea.conf` and/or `carplay_safearea_small.conf` in the
   card root, press *Apply SafeArea from SD*, reconnect.
3. *Reset SafeArea to built-in* returns to the table above. *Classic-view SafeArea ON / OFF*
   falls back to a single ViewArea.

The hook logs what it advertised: `GEN2 ViewArea[n] wide|classic area=... safe=...` and
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
| `ALTSCREEN111_FPS` | 30 | advertised max frame rate (`/mnt/app/root/mibr-carplay111-fps` overrides) |
| `ALTSCREEN111_TEE_PORT` | 19820 | loopback Annex-B tee |
| `ALTSCREEN111_INLINE_HOOKS` | 0 | 1 = also install inline prologue hooks if PLT interposition is not on the call path |

Marker files (`/mnt/app/root/`): `mibr-carplay111-zoom.inverted` flips the steering-wheel zoom
direction ([steering-wheel](../input/steering-wheel.md)); `mibr-carplay111-viewareas.single`
uses one ViewArea; `mibr-carplay111-noadvertise.senders` lists extra senders (one
`model osBuildVersion` line, `*` = any build) that get the stock `/info`. The known wireless
dongle (`model=iPhone9,1`, `osBuildVersion=18D70`) is built in: it never requests stream 111 and
drops the session when it sees the advertisement.

Renderer (`altscreen_render`):

| var / file | default | meaning |
| --- | --- | --- |
| `ALTR_TEE_HOST` / `ALTR_TEE_PORT` | 127.0.0.1 / 19820 | must match the hook's tee |
| `ALTR_WIDTH` / `ALTR_HEIGHT` | 1440 / 540 | cluster surface size |
| `ALTR_DISPLAYABLE_ID` | 99 | cluster plane id (also in `DisplayManagerMIB2High.dc[81]/dc[82]`) |
| `ALTR_LIVE_FILE` | `/tmp/altscreen_render.live` | heartbeat (`pid=`), refreshed while frames are presented |
| `ALTR_LIVE_TIMEOUT_MS` | 500 | heartbeat removed if no frame within this |
| `ALTR_STATUS_FILE` | `/tmp/altscreen_render.status` | `state=`, size, `frames=` |
| `/mnt/app/root/altscreen_render.hwdecode.off` | absent | software decode (GEM *HW decoder ON / OFF*); `/tmp/…` for this boot only |

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
