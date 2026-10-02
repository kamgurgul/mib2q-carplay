---
title: CarPlay AltScreen (cluster video) on Audi MHI2Q — build, install, bring-up
tags: [altscreen, cluster, video, mhi2q, deploy]
status: partial — host-validated pipeline, on-car steps marked
---

# CarPlay AltScreen (cluster video) on Audi MHI2Q

This wires the **CarPlay secondary-screen map VIDEO** into the Audi Virtual
Cockpit, on top of the existing `mib2q-carplay-rgi` route-guidance patch. It ports
the vehicle-proven Škoda `mhi2_altscreen_carplay` GEN2 hook to Audi MHI2Q and adds
an on-HU decoder that draws the CarPlay video into a cluster plane the stock
DisplayManager already composites.

> Read the sibling background first:
> [Audi B9/MU1438 compatibility lead](../altscreen/AUDI_B9_MU1438_COMPATIBILITY_LEAD_2026-09-28.md)
> and [MU1438↔MU1440 offline comparison](../altscreen/MU1438_MU1440_OFFLINE_COMPARISON_2026-09-28.md).

> [!WARNING]
> Research / experimental / use at your own risk. This is for a unit **you own**
> with green-menu / SSH developer access. Keep backups; the M.I.B. installer keeps
> a `.carplay-stock` of every edited config and an uninstall path, but a wrong
> experiment can still leave the cluster or CarPlay in a bad state until reboot.

---

## 1. Why the Audi path is different from Škoda MU1440

On Škoda MU1440 the hook converts CarPlay H.264 to MPEG-TS and injects it
straight into the cluster's MOST video channel (`/dev/mlb/isoTX2`) — no decode.

On **Audi MHI2Q the cluster map is not fed that way.** The head unit *renders and
H.264-encodes* the cluster image itself (`videoencoderservice`) and ships the
result over MOST/LVDS; the VC decodes it. That is exactly how this project's
maneuver overlay already reaches the cluster: it draws into a **managed QNX
Screen window (displayable)** and the stock encoder captures the composite. See
[cluster/compositing](../cluster/compositing.md) and [cluster/display-contexts](../cluster/display-contexts.md).

So the Audi AltScreen fits the stock pipeline by **decoding the CarPlay video on
the HU and drawing it into a cluster plane**, which the stock encoder then
re-encodes to the VC. Concretely:

```
iPhone CarPlay Auxiliary/ScreenAlt
      │  stream 111 (H.264, encrypted)
      ▼
libaltscreen111_mhi2q.so  (LD_PRELOAD in dio_manager, beside the RGI hook)
      ├─ interpose AirPlay session/SETUP/stream 111   (PLT, constructor-free)
      ├─ reuse stock AES to decrypt the screen stream
      └─ AVCC → Annex-B, expose on 127.0.0.1:19820  (the "tee")
      ▼
altscreen_render  (new; QNX/EGL/GLES2)
      ├─ decode.c   H.264 → YUV        (libavcodec)
      ├─ video_gles.c  YUV → RGB       (GLES2 shader)
      └─ draw into cluster displayable 99  (cluster_surface)
      ▼
DisplayManager composites ctx 81 {99} or ctx 82 {98,101,102,99}
      ▼
stock videoencoderservice → MOST/LVDS → Virtual Cockpit
```

Cluster contexts (added in `DisplayManagerMIB2High`):

| ctx | displayables | meaning |
| ---: | --- | --- |
| 74 | stock | resting cluster (stock map) |
| 80 | `{98,101,102,33}` | RGI maneuver over **stock native map** (existing) |
| **81** | `{99}` | **CarPlay video only** |
| **82** | `{98,101,102,99}` | **CarPlay video + RGI maneuver overlay** |

`ScreenModule` is the single cluster-context writer. `AltScreenModule` polls the
renderer's liveness file and tells `ScreenModule` to select 81/82 while CarPlay
video is live, falling back to 80/74 otherwise.

---

## 2. What is done vs. what needs your car

**Done and host-validated** (built and run on a Linux host in this repo's dev flow):

- `libaltscreen111_gen2.c` compiles for both `mu1440` and `mhi2q` targets (the
  Škoda build is byte-unchanged; MHI2Q is a compile-time variant).
- `altscreen_render` decode path: all three public PoC clips decode cleanly
  (0 errors) through `decode.c`; the full `main.c` tee-client → decode →
  liveness-file path was proven against a local socket serving a real Annex-B
  stream (746 frames, resolution tracked, `.live` lifecycle correct).
- Java `AltScreenModule` compiles; `DisplayManagerMIB2High`/`ScreenModule` context
  additions are minimal and additive.

**Needs your car / your firmware (marked ⚠ VERIFY throughout):**

1. **⚠ KEY CAPTURE on MU13xx** — the one part not (re)written here. See §7.
2. **⚠ PLT vs inline hooks** — whether PLT interposition wins on your libairplay.
3. ~~stream-111 reachability~~ — **SOLVED**: the unit filters the CarPlay link with PF; the data port must be in its allowlist **and** must not be a stock port. Default is now 7100 (6030 is the stock main-screen port). See §15.
4. **⚠ displayable 99 creatable** — whether the DM binds id 99 on your cluster.
5. **⚠ plane-99 geometry** — position/crop to the cluster map region.
6. **⚠ decode performance** — SW H.264 decode cost on the APQ8064.
7. **⚠ cluster canvas size** — the 1440×540 default.

---

## 3. Prerequisites

- Working `mib2q-carplay-rgi` install (route guidance already runs on the car).
- Green-menu M.I.B. on an SD card (you already have this).
- The `qnx65-armv7-toolchain` Docker image (already needed for the RGI native
  builds): build once per its README.
- Docker + `curl` on your build host (the renderer build downloads FFmpeg 6.1.5).
- Your unit's **stock HMI jar** to build the Java patch (as the RGI README
  describes — `stock/base.jar`, see [`stock/README.md`](../../stock/README.md)). This is required for the Java changes;
  the native parts do not need it.

---

## 4. Build

> **Building on Windows:** do the Docker builds from **WSL**, not Git Bash. The
> `qnx65-armv7-toolchain` image contains a QNX sysroot full of **symlinks**, and a
> Windows `git` clone (with `core.autocrlf=true` / no symlink support) turns those
> into text stubs and mangles line endings — the image build then fails in `ar`
> at libgcc. Clone and build inside WSL's native filesystem (`~/…`, not `/mnt/c`)
> so git makes real symlinks with LF endings:
> `git clone https://github.com/luka-dev/qnx65-armv7-toolchain && cd … && docker build --platform=linux/amd64 --target base-env --build-arg BASE=base-8.5 -t qnx65-armv7-toolchain:latest .`
> (the C/C++ `base-env` target is all these builds need — it skips the Go/Rust
> stages). The two project builds below can then run from WSL against the repos on
> `/mnt/c`; `build_altscreen_render.sh` already builds its FFmpeg in a
> container-local dir so the 32-bit QNX `ar` never touches the `/mnt/c` mount.

### 4a. The MHI2Q AltScreen hook

The source is in `altscreen_hook/` (imported from `mhi2_altscreen_carplay`):

```sh
scripts/build_altscreen_hook.sh              # -> build/libaltscreen111_mhi2q.so
```

This is the GEN2 source compiled with `-DALT111_TARGET_MHI2Q`: constructor-free
(no thread/dlsym/patch from the ELF constructor — the K1004 loader-lock lesson),
PLT interposition by default, 1440×540 canvas, stream 111 on port 7100 (§15).

### 4b. The renderer

From this repo:

```sh
scripts/build_altscreen_render.sh            # -> build/altscreen_render
```

It builds a minimal static FFmpeg 6.1.5 for QNX (H.264 decoder + parser only) and
links `altscreen_render` against it plus the unit's Screen/EGL/GLES (import stubs,
like the maneuver renderer).

Host decode self-test (no car), to sanity-check the decode path against any clip:

```sh
cd altscreen_render
make hosttest FFMPEG_PREFIX=/path/to/ffmpeg      # needs host FFmpeg dev libs
./host_decode_test some_carplay_clip.mp4         # prints frames/errors/size
```

### 4c. The Java patch (adds AltScreenModule + ctx 81/82)

```sh
scripts/build_java.sh                        # -> build/carplay_hook.jar
```

Same as the base RGI build; it now also compiles `AltScreenModule` and the
context additions.

**Getting the stock jar (required for this step).** The patch compiles against
your unit's OEM HMI classes, which cannot be downloaded — they come off your car:

1. Copy the HMI JXE off the unit over SSH (IBM J9 image the HMI runs from):
   ```sh
   scp root@<unit>:/mnt/app/eso/hmi/lsd/lsd.jxe .
   # if not there:  ssh root@<unit> "find /mnt/app -name '*.jxe' | grep -i lsd"
   ```
2. Convert JXE → JAR with the community `jxe2jar` tool (the same step the upstream
   author used to make `MU1316-final.jar`).
3. Save the jar as `stock/base.jar` (gitignored); override with
   `STOCK_JAR=<path inside stock/>`.

The OSGi libs `build_java.sh` also needs (`org.osgi.framework-1.10.0.jar`,
`org.osgi.util.tracker-1.5.4.jar`) are public and go in `stock/libs/`.
With the jar in place, `scripts/build_java.sh` produces `build/carplay_hook.jar`
in a pinned JDK-8 container (no host JVM needed).

---

## 5. Install (green-menu M.I.B.)

The AltScreen files are **optional additions** to a normal RGI release. Drop a
full RGI release into `mod/carplay/` on the M.I.B. SD card, and **add these two**:

```
mod/carplay/libaltscreen111_mhi2q.so     (from build/)
mod/carplay/altscreen_render             (from build/)
```

plus the usual RGI assets (`libcarplay_hook.so`, `maneuver_render`,
`flag_atlas.rgba`, the four `carplay_*.sh`, `carplay_hook.jar`, `carplay_child.json`).

Then run **GEM → M.I.B. → Advanced Settings → Run Custom Script** with CarPlay
disconnected, exactly as for the base RGI install
([deploy/install](install.md)). The installer now:

- installs the two AltScreen files if present (skips them silently if absent — a
  base RGI card still installs cleanly);
- `carplay_startup.sh` preloads `libaltscreen111_mhi2q.so` **beside** the RGI hook
  when it is present;
- `carplay_monitor.sh` starts `altscreen_render` when the binary is present.

No extra config edits beyond what the RGI installer already does (the
`dio_manager.json` route-guidance IDs and the `smartphone_integrator.json` child
are RGI's; AltScreen rides the same `carplay_startup.sh`).

**Reboot** (disconnect CarPlay, `sync`, wait, reboot normally). The jar loads on a
full restart; on boot `smartphone_integrator` launches the stack.

Uninstall: run `mods/uninstall_MoreIncredibleBash/` the same way — it removes the two
AltScreen files too.

---

## 6. Bring-up order and what to check (logs)

Turn on verbose logging first (M.I.B. **logging** script, or `touch
/mnt/app/carplay_verbose`), connect the phone, start CarPlay navigation, then
collect logs (the logging script now also grabs `altscreen_render.*`,
`altscreen111.log`, and the `mibr-alt111*` status files).

Check, in order:

1. **Hook up** — `/tmp/altscreen111.log`:
   `GEN2 lazy runtime ready target=mhi2q …`  (constructor-free start fired).
2. **iOS offered stream 111** — `GEN2 SETUP contains stream111 cid=…`. If you see
   `MHI2Q stock SETUP rejected a stream111 request … retrying without 111`, that
   is expected (210.81 has no 111 branch) and the hook adds it back.
3. **⚠ KEY CAPTURE** — `captured stock CarPlay master AES key at SetSecurityInfo
   caller`. **If this line never appears**, decryption cannot run — go to §7.
4. **stream 111 connected** — `AltScreen stream 111 connected`. If instead you see
   repeated `MHI2Q stream111: no iPhone connection on port 7111 after …s`, the
   iPhone cannot reach the port — §15 (PF allowlist).
5. **Video decoding** — `/tmp/altscreen_render.status` shows `state=decoding` and a
   rising `frames=`; `/tmp/altscreen_render.log` shows `connected to tee`,
   `GL up on displayable 99`.
6. **Cluster switched** — `/tmp/carplay_java.log`: `ScreenModule … altScreen video
   live -> ctx 81/82` and `cluster -> ctx 81` (or 82).
7. **⚠ Picture on the cluster** — if ctx 81 is active but nothing shows, it is
   almost always displayable 99 not binding (§9) or plane-99 geometry (§10).

---

## 7. ⚠ MISSING PART: session key capture on MU13xx

This is the one piece intentionally **not written/modified here.** The GEN2 hook
already contains the upstream Škoda key observer (`AES_CBCFrame_Init` interposer
that records the master key when the stock `SetSecurityInfo` caller invokes it).
It is compiled into the MHI2Q build **unchanged**.

- **If §6 step 3 logs the "captured … master AES key" line**, it already works on
  your firmware — nothing to do.
- **If it does not**, the caller-window heuristic (`ra` within
  `AirPlayReceiverSessionSetSecurityInfo + 0x100`) does not match your exact
  `libairplay`. The offline comparison found the `SetSecurityInfo` structure and
  the `AES_CBCFrame_Init` call site shift on MU1438 vs MU1440
  ([offline report §Lifecycle and security](../altscreen/MU1438_MU1440_OFFLINE_COMPARISON_2026-09-28.md)),
  so MU13xx may differ again. Adapting that observer to your `libairplay`
  (the caller window, or the structure offsets) is the work left to you or another
  tool. Everything downstream of a captured key is done and host-tested. The hook
  fails safe: without a key it logs `no captured session AES key; cannot start
  stream111` and leaves stock CarPlay untouched.

Capture your stock `libairplay.so` identity for that work:
`sha256sum /mnt/app/eso/lib/libairplay.so`.

---

## 8. ⚠ VERIFY: PLT vs inline hooks

MHI2Q defaults to PLT interposition (the exported `AirPlayReceiverSession*`
symbols are the hooks). The offline compare found genuine JUMP_SLOT routes, but
that is not proof the loader picks our copy at runtime. If §6 step 2 never shows
SETUP being seen by the hook (stock CarPlay works but no `GEN2 SETUP …` lines),
the interposers are not on the call path. Enable inline hooks:

```sh
# add ALTSCREEN111_INLINE_HOOKS=1 to the carplay child env, or export before test
```

The inline installer logs the actual prologue bytes if they differ from the
MU1438-observed values and leaves the mismatching symbol on PLT. It will not
`MAP_FIXED`-replace a libairplay text page: by the time the hook runs, other
`dio_manager` threads may be executing that page. If the page cannot be made
writable in place, the log says so and the process stays on PLT. Read the bytes
from `/tmp/altscreen111.log` and set the expectations for your firmware in
`gen2_install_inline_hooks()` if you need the inline path.

---

## 9. ⚠ VERIFY: displayable 99 is creatable

`altscreen_render` creates a managed Screen window with `ID_STRING="99"` via
`cluster_surface`, exactly like `maneuver_render` does for 98. Displayable 98 is
proven creatable; **99 is the id the pre-fork AltScreen used but is not yet
confirmed on your firmware** (the RGI notes explicitly warn that some ids, e.g.
103, are *not* creatable). Check `dmdt gd` / the DM logs for a bound source on 99.
If 99 does not bind, pick another free displayable id and set it in three places:
`ALTR_DISPLAYABLE_ID` (renderer env), and the `99` entries in
`DisplayManagerMIB2High.dc[81]/dc[82]`.

---

## 10. plane-99 opacity and geometry (implemented — verify the rect on-car)

`ClusterLayerController` now drives displayable 99 alongside 98/101/102:

- **Opacity follows the VIDEO, not `navActive`.** ctx 81 is video *without* a
  maneuver, so gating plane 99 on `navActive` (as the maneuver planes are) would
  leave it invisible there. It is applied on every path, including the early
  returns, so no branch can leave 99 at a stale opacity.
- **Position = the stock map origin** (layout slots 108/109 — what stock uses for
  planes 33/58 in `CombiMapController.positionMap()`) **plus the small-stage
  offset** (80/81) stock applies to those map planes. Plane 99 stands in for the
  map, so it takes the map's placement — *not* the KDK offset, which
  [cluster/kdk-geometry](../cluster/kdk-geometry.md) warns must never be applied
  to the panel.

> This was the direct cause of a "**only the maneuver arrow shows on the
> cluster**" failure: nothing set plane 99's opacity, so the video plane was
> composited in ctx 81/82 but never made visible.

Each decision is logged once — look for `video=on|off videoDst=(x,y)` in the
`ClusterLayers apply` line in `/tmp/carplay_java.log`. ⚠ The map rect itself is
still unmeasured on your cluster; if the video is visible but offset, that line
tells you exactly which origin was used (Classic vs Sport layouts differ).

For ctx 82 the maneuver plane 98 and its backings follow VC Fct44/Fct54 as before.

---

## 11. ⚠ VERIFY: decode performance and canvas size

- **Performance**: `altscreen_render` uses FFmpeg **software** H.264 decode. The
  CarPlay cluster stream is small (the MU1440 reference was 1010×376; the public
  clips were 500×224), so SW decode on the APQ8064 Krait is expected to keep up,
  but confirm frame cadence on-car via `frames=` in the status file. If it cannot
  keep up, the follow-up is HW decode through the platform OMX/Adreno decoder — a
  larger change, out of scope here.
- **Canvas**: the hook advertises a 1440×540 secondary display and the renderer
  window defaults to 1440×540 (`ALTR_WIDTH`/`ALTR_HEIGHT`). This is the strongly-
  supported early-B9 VC class but was **not vehicle-measured**. If your cluster is
  a different raster, set both the hook (`ALTSCREEN111_WIDTH/HEIGHT`) and the
  renderer (`ALTR_WIDTH/HEIGHT`) to match. The actual decoded video size is
  whatever iOS sends and is handled dynamically by the renderer regardless.

---

## 11b. Calibrating the SafeArea (puck position)

iOS draws the map over the whole advertised 1440x540 ViewArea but keeps the puck and its
overlays centred in the nested **SafeArea**. The hook defaults the SafeArea to the full canvas,
so if the VC shows only part of the canvas the puck can land outside it (seen on the car:
below the visible bottom in both normal and full-width view).

1. Run the **altscreen_grid** mod. `altscreen_render` overlays a ruler on the video: horizontal
   lines every 60 px (red 60, orange 120, yellow 180, green 240, cyan 300, blue 360, magenta 420,
   white 480), grey verticals every 180 px, grey border on the canvas edges.
2. Photograph the VC in each view. The visible lines give the visible canvas rectangle.
3. Put that rectangle in `safearea.conf` of the **altscreen_safearea** mod, run it, reconnect.
   It writes `/mnt/app/root/mibr-carplay111-safearea.conf`, which the hook reads at every connect
   (`GEN2 ViewArea full=... safe=...` in `/tmp/altscreen111.log`). `reset` in the file restores the
   full canvas.
4. Run altscreen_grid again to remove the ruler.

Different VC views can need different rectangles. The hook already has `updateViewArea`; once both
views are measured they can be advertised as separate ViewAreas and switched from the VC view state.

---

## 12. Runtime knobs

Hook (`libaltscreen111_mhi2q.so`) env, set in the carplay child if you need to
override the MHI2Q defaults:

| var | default | meaning |
| --- | --- | --- |
| — | — | `/mnt/app/root/mibr-carplay111-zoom.inverted`: a positive roller MapScale step zooms in instead of out |
| `ALTSCREEN111_PORT` | 7100 | stream-111 listen port; must be in the PF allowlist and not a stock port (5000, 5001, 6030 are refused) (§15). Runtime override: `/mnt/app/mibr-carplay111.port` |
| `ALTSCREEN111_WIDTH`/`_HEIGHT` | 1440/540 | advertised secondary display size |
| `ALTSCREEN111_TEE_PORT` | 19820 | loopback Annex-B tee |
| `ALTSCREEN111_INLINE_HOOKS` | 0 | 1 = also install inline prologue hooks (§8) |
| `ALTSCREEN111_ADVERTISE` | 1 | 0 = leave `/info` byte-for-byte stock (no AltScreen advertisement). Diagnostic lever for sender-negotiation failures — see §14 |
| `ALTSCREEN111_ENABLED` | 1 | 0 = fully inert (stock CarPlay only) |

Renderer (`altscreen_render`) env:

| var | default | meaning |
| --- | --- | --- |
| `ALTR_TEE_PORT` | 19820 | must match `ALTSCREEN111_TEE_PORT` |
| `ALTR_WIDTH`/`ALTR_HEIGHT` | 1440/540 | cluster surface size (§11) |
| `ALTR_DISPLAYABLE_ID` | 99 | cluster plane id (§9) |
| `ALTR_LIVE_FILE` | /tmp/altscreen_render.live | heartbeat (`pid=<pid>`, mtime refreshed while frames are swapped). `AltScreenModule` ignores a stale file or a dead pid |

Java: `-Dcarplay.altscreen.liveFile=…` overrides the liveness path if you change
`ALTR_LIVE_FILE`.

To disable AltScreen without uninstalling: remove `/mnt/app/root/hooks/altscreen_render`
(cluster falls back to stock-map ctx 80/74) or set `ALTSCREEN111_ENABLED=0`.

### Turning route guidance on/off in the car (no reboot, no rebuild)

Route guidance (the cluster maneuver arrow, HUD and BAP takeover) has a runtime
kill-switch, so you can run **CarPlay video only** without removing anything:

```sh
touch /mnt/app/carplay_rgd.disabled     # persistent (survives reboot)
#   or
touch /tmp/carplay_rgd.disabled         # this boot only
```

The marker is read at every CarPlay session start (same idiom as
`carplay_verbose`), so it takes effect on the **next phone reconnect** — no reboot.
Remove the file to re-enable route guidance (again, effective next reconnect).

**No shell? Use the green menu.** Two M.I.B. custom-script folders do exactly the
same touch/rm from **GEM → M.I.B. → Advanced Settings → Run Custom Script**:

- `mods/rgd_disable_MoreIncredibleBash/` — route guidance OFF (CarPlay video only)
- `mods/rgd_enable_MoreIncredibleBash/` — route guidance ON

Copy the one you want to the M.I.B. SD card and run it (like the install/logging
scripts), then reconnect the phone. They forward to MMX and mount `/mnt/app`
themselves, so nothing else is needed.

While disabled, `RgdModule` stays inert (no BAP takeover), so with AltScreen live
the cluster shows pure CarPlay video (ctx 81) and the maneuver overlay never
appears. Cover art, PDC and touchpad are unaffected. Confirm in
`/tmp/carplay_java.log`: `route guidance disabled by marker; skipping BAP takeover`.

---

## 13. Getting logs off the car

Use the M.I.B. **logging** script (`mods/logging_MoreIncredibleBash/`) exactly like the
base project: run it once (saves current state, turns verbose on), reconnect the
phone and drive with CarPlay, run it again (saves the verbose session). Each run
now also captures `altscreen_render.log`/`.status`/`.live`, `altscreen111.log`,
and the `mibr-alt111*` status files. Attach the numbered folder to a report.

Over SSH you can also tail directly:
`/tmp/altscreen111.log` (hook), `/tmp/altscreen_render.log` + `.status`
(renderer), `/tmp/carplay_java.log` (context switching).

The headless host renderer is handy for isolating the tee: forward the port and
run it on your host —
`ssh -L 19820:127.0.0.1:19820 unit` then `altscreen_render/altscreen_render_host`
prints decoded frame counts without touching the cluster.

---

## 14. Troubleshooting observed failures

> **D is the currently open failure** on the MHI2Q unit under test. A-C are
> resolved or ruled out; read D first.

### A. Wireless / dongle CarPlay stops connecting after install

`/info` is the receiver's capability advertisement, and the hook used to **replace**
`enabledFeatures` wholesale with `["altScreen","viewAreas"]`, destroying everything
stock advertised. A sender that needs one of those features then fails to
negotiate. A wireless dongle proxies the session and is stricter than a wired
iPhone, so it can fail where wired still works.

**Ruled out on this unit by the logs**, though the merge is still correct and
stays. `GEN2 stock /info enabledFeatures=<absent>` and
`enabledFeatures merge: stockEntries=0` mean the stock dictionary carried no
`enabledFeatures` at all, so the old overwrite destroyed nothing here. The same
log also shows `uiContextURLs`, `altScreenURLs` and `altScreenSuggestUIURLs` all
`<absent>` stock - we are *adding* capabilities to an `/info` that advertises
none of them, which is still the most likely thing a proxying dongle rejects.
That is what the marker in D.3 tests.

Fixed: the hook now **merges** — stock entries are preserved, `altScreen` /
`viewAreas` are appended only when absent, never duplicated, and the key is only
rewritten when something actually changed. Both the `/info` and the SETUP-response
paths go through the same merge.

Three other constructor-time hazards were removed at the same time, any of which
can disturb the stock CarPlay process:

- `signal(SIGPIPE, SIG_IGN)` no longer runs in the ELF constructor on MHI2Q —
  `dio_manager` snapshots SIGPIPE on its first Cinemo call, and the RGI hook
  deliberately saves/restores those dispositions via `signal_guard`.
- no `dlsym` from `.init` (the QNX loader-lock class of failure); `init_api()`
  runs from the deferred runtime start and can retry.
- the inline-hook installer refuses the `MAP_FIXED` text-page clone on MHI2Q,
  because that replaces a libairplay text page while AirPlay threads are
  executing it. A page that cannot be made writable in place stays on PLT.

**To confirm the cause on your unit**, A/B it:

```sh
# /info stays pure stock; everything else unchanged
ALTSCREEN111_ADVERTISE=0   # add to the carplay child env, reconnect the phone
```

If the dongle works with `ADVERTISE=0` and fails with `=1`, the remaining
difference is the synthetic AltScreen display appended to `displays` (the merge
itself is non-destructive). `/tmp/altscreen111.log` prints what stock advertised:

```
GEN2 stock /info enabledFeatures count=N
GEN2 stock /info enabledFeatures[i]=<name>
GEN2 enabledFeatures merge: stockEntries=N altScreen=appended viewAreas=... total=M
```

### B. Only the maneuver arrow shows on the cluster (no CarPlay video)

Two independent causes, both fixed:

1. **Plane 99 was never made visible.** `ClusterLayerController` set opacity for
   98/101/102 on every path but never touched 99, so the video plane was
   composited in ctx 81/82 and stayed invisible. See §10.
2. **The ctx-72 bounce disowned plane 99.** `applySwitch` bounced through the
   stock kombi-map context before *every* non-74 target; the DisplayManager
   disowns managed windows across context transitions, so each 80↔81↔82 move tore
   down the renderer's window. The bounce is now only taken when coming from
   stock 74 (or an unknown state), where the MOST encoder really is off.

Supporting fixes in the renderer: `/tmp/altscreen_render.live` is a **heartbeat**
(holds `pid=`, rewritten while frames are swapped, unlinked at startup so a
predecessor's crash cannot pin ctx 81/82), it is only written when the draw **and**
`eglSwapBuffers` succeed, and a disowned window is recovered via
`cluster_surface_lost()` → `cluster_surface_recreate()` + EGL rebind.

**Triage order** — walk the chain and stop at the first line that is missing:

| # | Where | Expect |
| --- | --- | --- |
| 1 | `/tmp/altscreen111.log` | `GEN2 lazy runtime ready target=mhi2q` |
| 2 | same | `GEN2 enabledFeatures merge: stockEntries=…` |
| 3 | same | `GEN2 SETUP contains stream111 cid=…` (sender offered 111) |
| 4 | same | `captured stock CarPlay master AES key …` — **if absent, see §7** |
| 5 | same | `AltScreen stream 111 connected` (else: PF allowlist, §15) |
| 6 | `/tmp/altscreen_render.status` | `state=decoding`, rising `frames=` |
| 7 | `/tmp/altscreen_render.log` | `GL up on displayable 99` (else §9) |
| 8 | `/tmp/carplay_java.log` | `altScreen video live -> ctx 81/82`, `cluster -> ctx 81` |
| 9 | same | `ClusterLayers apply … video=on videoDst=(x,y)` |

Steps 1–3 failing points at the advertisement (A). Step 4 is the documented
session-key gap. Steps 8–9 present but no picture means geometry (§10) or the
displayable id (§9).

### C. Video runs for a few seconds, then the main display blacks out

Symptom: the CarPlay map appears on the cluster, then stops after a few seconds;
the head-unit display goes black; a dongle never leaves its splash screen; the
unit feels like it is "crashing" even though nothing has actually died
(`pidin ar` still shows `j9`, `dio_manager`, `displaymanager` and both renderers
alive).

> **Superseded by D.** The fix below is real and stays, but it did **not** resolve
> this symptom, and the measurements in D show it was not the cause. Keep reading
> at D. What follows is retained because the hygiene argument is still valid and
> the fix is in the shipped binary.

Suspected cause: `cluster_surface_lost()` was being called **once per decoded
frame**. `common/cluster_surface.h` documents it as a *"cheap health probe
(~every 5 s)"* - it is two synchronous Screen IPC round-trips. At video frame rate
that is ~60 blocking IPC calls per second into the DisplayManager that also serves
the main display.

Fixed: the probe now runs on its documented ~5 s cadence, plus immediately when
the surface is already gone or the **previous present failed** - a failed
draw/`eglSwapBuffers` is the real signal of a disowned window, so recovery is
still prompt without polling.

Why it was not the cause: 60 IPC/s is unpleasant but an order of magnitude below
what would saturate Screen, and log set 004's renderer log contains **no**
`surface lost` or `rebind` lines at all - the probe was silent every time it ran.
The symptom also predates any frame reaching the screen: in 003 `frames=0` and the
dongle was *already* stuck on its splash.

The giveaway in `sloginfo` is the Adreno renderbuffer pool cleanup firing
repeatedly inside the HMI:

```text
(name:j9) E/Adreno-ES20: <rb_mem_pool_cleanup_thread:998>: got the pulse
```

If you see `rc=2 -> consumer reset` with a healthy `frames=` count in
`altscreen_render.status`, the renderer is producing but cannot keep pace with the
hook's queue - look for per-frame work in the present path before suspecting the
decoder.


### D. Session restarts every 10 s; main screen black; dongle stuck on splash

This is **not** our video path failing on its own - it is
`smartphone_integrator` deliberately restarting the session. From the unit's own
`smartphone_integrator.json`:

```json
"decoderRestartTimeout":10000
```

SI restarts the CarPlay session when the **main-screen** video decoder makes no
progress for 10 s. Measured in log set 004, every generation fits that to the
millisecond - here from `SETUP entry streams=1 types=110` to the whole-session
teardown:

```text
110 SETUP   66.682
teardown    76.691     -> 10.009 s
```

and from `AltScreen stream 111 connected` to teardown, over ten consecutive
sessions: 10.28 10.20 10.12 10.10 10.10 10.06 10.10 10.06 10.10 10.12 s.
Eleven `dio_manager` generations in ~4.5 minutes, each ending in an orderly
`TEARDOWN ... reason=0` (not a crash), SI respawning ~6 s later. The user-visible
result is a black main screen and a cluster image that lasts a couple of seconds.

The same log contains its own control case: the one generation where stream 111
**never set up** lived **48 s**, ~5x longer. Port 6030 was already configured
there (`altPort=6030` in its constructor line); the phone simply sent no
type-111 SETUP that session, so it ran as plain stock CarPlay. Across
log sets the correlation is exact:

| log | 111 connected | dio generations |
| --- | --- | --- |
| 001 | 0 | 3 |
| 002 | 0 | 5 |
| 003 | 0 | 2 |
| 004 | 10 | 11 |

So an active stream 111 is what stalls the **main** decoder.

> [!IMPORTANT]
> **Solved (log sets 005/006): a port collision on 6030, not any of D.1-D.3.**
> 6030 is the **stock main-screen (stream 110) data port**. In 005 (advertisement
> off, hook never listens) the phone is still connected to `:6030`, and in 003
> (hook on 7111) too. With our 111 listener bound to 6030 just before stock sets
> up 110, the stock screen receiver never gets its connection: `sloginfo` shows
> `AirPlayReceiverSessionScreen ... NONE -> INITIALIZING` and **no**
> `ScreenStream Creating...` / `Decoder started`, until SI's 10 s
> `decoderRestartTimeout` tears the session down. The working 005 run reaches
> `INITIALIZING -> READY` within ~1 s. Stream 111 now defaults to **7100**, the
> hook refuses 5000/5001/6030 even from the override file, and it falls back
> through the other permitted ports if the chosen one won't bind. See §15.
>
> The candidates below are kept for reference.

Three candidates were considered, in the order worth testing:

**D.1 - CPU starvation.** `altscreen_render` software-decodes 1440x540 H.264
(FFmpeg, no hardware path available to us) and uploads YUV to GLES every frame.
If that saturates a core, `dio_manager`'s main decode misses its deadline and SI's
timer fires. The logging card did not capture CPU at all, so this was untestable;
it now runs `hogs -i 3`, `pidin times` and `pidin mem`. **Read `hogs.txt` first in
the next capture** - if `altscreen_render` is near 100 % of a core, this is it, and
the fix is to drop the advertised FPS or the cluster resolution rather than to
chase the protocol.

**D.2 - the phone stops sending.** Our stream receives ~60 access units with
`source_idrs=1` and then freezes:

```text
seq=2  source_aus=30     seq=5  source_aus=60
seq=3  source_aus=35     seq=6  source_aus=60   <- frozen
seq=4  source_aus=39     seq=7  source_aus=60
```

60 AUs is about one socket buffer, so this looks like backpressure, but the tee
send path is nonblocking and drops on `EAGAIN`, so it is not the hook blocking on
the socket. Note also that **35 tee reconnects and 8 `consumer reset`s** occurred
across 10 sessions: once a consumer resets, the renderer needs a fresh IDR, the
hook asks (`gen2 consumer keyframe intent`, 19x), and `source_idrs` never leaves
1 - so the cluster stays frozen for the rest of that session even if the session
survives. Worth fixing on its own merits, but it does not explain the main screen.

**D.3 - our `/info` advertisement.** The decisive A/B, and the one to run first
because it needs no rebuild:

```sh
touch /mnt/app/mibr-carplay111.noadvertise   # /info returns stock byte-for-byte
# reconnect the phone / dongle
rm /mnt/app/mibr-carplay111.noadvertise      # back to AltScreen
```

The marker is re-read per session. With it in place the hook advertises nothing,
accepts no 111 SETUP, and the unit negotiates CarPlay exactly as it does without
us. If the main screen and the dongle then behave, the cause is on the
advertisement/SETUP side, not the video side - see A for why that is plausible on
a unit whose stock `/info` carries no `enabledFeatures` and no altScreen URLs.

**Note on the dongle specifically:** in log set 003 the dongle was already stuck on
its splash screen while `111connected=0` - no AltScreen stream existed at all.
Whatever breaks the dongle therefore happens **before** any video, which rules out
D.1 and D.2 for the dongle and makes D.3 the only candidate of the three.

### E. What the system log cannot tell you

`sloginfo` on the MMX is a ~13 000-line ring covering only ~90 s, and ~95 % of it
is normal cluster-video traffic:

```text
1329  devp-iso-mmx[isoTX2]: io_open / io_close     (iso TX to the cluster)
1321  videoCore: Resource_BlockWait /dev/mmpm      (Venus encoder)
1321  vpeCore:   Waiting for events from Server    (scaler)
1321  qcore MMPM [MmpmClkMgrRelease]: Clock is not requested by the client
```

Those counts are **the same in 001/002/003, where `frames=0`** and our video never
reached the screen, so they are baseline noise, not a symptom - and `Dropped N
messages in the last N ms` shows real errors being evicted. Use
`/tmp/altscreen111.log` and `carplay_wrapper.log` for anything time-ordered.
---

## 15. The PF allowlist on the CarPlay link (solved)

The head unit filters inbound traffic on the CarPlay link with **PF** — `ifconfig`
shows `pflog0: flags=41<UP,RUNNING>`. This is the firewall profile the base
install deliberately does not touch, and it is an **explicit allowlist**, so an
AltScreen data port that is not in it simply never receives the phone's
connection.

Verbatim from `pfctl -sr` on an AUG22/MU13xx unit, interface `carplay0`:

```text
pass in quick on carplay0 proto tcp from any to any port 5000:5001
pass in quick on carplay0 proto tcp from any to any port = 5010
pass in quick on carplay0 proto tcp from any to any port 6000:6001
pass in quick on carplay0 proto tcp from any to any port = 6030
pass in quick on carplay0 proto tcp from any to any port = 6100
pass in quick on carplay0 proto tcp from any to any port = 6200
pass in quick on carplay0 proto tcp from any to any port 7000:7001
pass in quick on carplay0 proto tcp from any to any port = 7100
```

So the permitted inbound TCP set is **5000:5001, 5010, 6000:6001, 6030, 6100,
6200, 7000:7001, 7100**. Stock itself uses three of them:

| port | stock use |
| --- | --- |
| 5000 | RTSP control (`Registering Bonjour _airplay._tcp. port 5000`) |
| 5001 | event channel |
| 6030 | **main screen, stream 110** |

6030 was the default here for a while because `netstat -an` on a live unit only
showed 5000 busy. That was misleading: stock opens its 6030 listener only between
the 110 SETUP and the phone's connect, so it never shows as `LISTEN`. Every log set
has an established phone connection to `:6030` (even with our advertisement off),
and putting stream 111 there starves the main screen. See §14 D for how that
looks.

The MHI2Q default is now **7100** (the legacy AirPlay mirroring port, which a
CarPlay session does not use). The hook refuses the stock ports even if they are
configured, and if 7100 does not bind it tries 6200, 6100, 5010, 7001, 7000 in
that order. MU1440's 6031 would be blocked here.

### How this failure looks

The phone agrees to the stream and then nothing happens — the SYN is dropped, so
the receiver simply never sees a connection:

```text
GEN2 SETUP contains stream111 cid=…
stream111 receiver ready: conn=… dataPort=7111 transport=IPv6-dualstack
MHI2Q stream111: no iPhone connection on port 7111 after 10s …
MHI2Q stream111: no iPhone connection on port 7111 after 60s …
```

Meanwhile the renderer is healthy and simply starved — `tee closed; frames so
far=0`, repeatedly. It is easy to misread this as a renderer or cluster problem;
it is neither.

A **wireless dongle can stall completely** on this, where a wired iPhone carries
on and just shows the stock map: the dongle proxies the session and waits on the
stream it set up, so an unreachable data port can leave it stuck on its own splash
screen.

### Changing the port

Runtime, no rebuild and no config edit — re-read at every SETUP:

```sh
echo 6200 > /mnt/app/mibr-carplay111.port    # then reconnect the phone
```

An invalid or out-of-range value is logged and ignored, and so is a stock port
(5000, 5001, 6030); the hook falls back to 7100. If an older install left
`/mnt/app/mibr-carplay111.port` containing `6030`, it is now ignored, but you can
also delete the file. `ALTSCREEN111_PORT` in the
carplay child env does the same thing at a lower precedence.

> [!IMPORTANT]
> **Ephemeral fallback is disabled on MHI2Q.** A kernel-assigned port is
> guaranteed to be outside the allowlist, so falling back to one would bind a port
> the phone can never reach and turn a clean failure into a silent 120-second
> stall. If the configured port cannot be bound the receiver tries the other
> non-stock permitted ports, then fails closed and says why. (MU1440 keeps the fallback — it has no such filter.)

If you ever need a port outside the list, the alternative is a PF rule, which
means modifying the firewall profile the install otherwise leaves alone — prefer a
permitted port.
