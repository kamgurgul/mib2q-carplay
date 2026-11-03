# mib2q-carplay 1.4 - changelog

Changes since [1.3.1](CHANGELOG-1.3.1.md).

## New

- **Compass on the CarPlay cluster map switches ON / OFF in the menu.** New Green Engineering
  Menu button *Cluster map compass ON / OFF* (default **OFF**). It writes
  `/mnt/app/root/mibr-carplay111-nav.showCompass` (absent = default) and applies like the turn
  card, ETA and speed limit buttons: at once with a CarPlay cluster session up, otherwise at the
  next connection. The file also accepts `user` (follow the iPhone's Maps setting) when written by
  hand. Every cluster URL now carries `showCompass`:
  - turn card ON: `maps:/car/instrumentcluster?showETA=…&showSpeedLimit=…&showCompass=…`
  - turn card OFF: `maps:/car/instrumentcluster/map?showETA=…&maneuverLayout=no&showSpeedLimit=…&showCompass=…`

- **All cluster map features on the wide cockpit view only, one menu button.** New Green
  Engineering Menu button *Cluster map all features on wide view ON / OFF* (default **OFF**). ON
  writes `/mnt/app/root/mibr-carplay111-nav.wideAll` with `yes`: the wide view then gets the turn
  card, ETA, speed limit and compass, whatever the four individual switches say. The classic and
  Sport small maps keep using the four switches, which work as before. When the cockpit switches
  between the wide view and a small map, the hook sends `showUI` with the new URL before
  `updateViewArea`. Explicit URL overrides (`mibr-carplay111-nav.surface` and the others listed in
  [altscreen-mhi2q](../deploy/altscreen-mhi2q.md)) still win. *Car test pending.*

- **HW301 image v1167: ETA, speed limit and compass on the wide cockpit layout only.** The
  dongle now picks the cluster URL by cockpit layout:
  - wide: `maps:/car/instrumentcluster?showETA=yes&showSpeedLimit=yes&showCompass=yes`
  - classic and Sport small maps: `maps:/car/instrumentcluster?showETA=no&showSpeedLimit=no&showCompass=no`
    (turn card only, as in v1166)

  When the cockpit switches between wide and a small map, the dongle sends the phone `showUI`
  with the new URL before `updateViewArea`. The URLs are built into the image; the head unit's
  GEM switches still do not reach the dongle. Image `dongle/hw301/fw/hw301_v1167.tar`
  (marker `v1167-20261025192423`). *Car test pending.*

- **HW301 image v1172: the dongle passes the car's cluster settings to the phone.** The
  dongle now behaves like a phone that supports two screens: it takes the cockpit display (size,
  SafeAreas, layouts, start URL) from the car and forwards the car's cluster commands to the
  iPhone. The head unit's settings therefore reach the HW301 too: the five cluster map switches
  (turn card, ETA, speed limit, compass, all features on the wide view), SafeArea changes and the
  wide / classic / Sport layout switch. The image no longer has built-in URLs or SafeAreas. The
  cluster stream has its own connection and encryption, separate from the main screen. To the car
  the dongle reports `model=HW301` with the stock build `18D70`.
  - Made for the **1.4** head unit. It uses the same head-unit path as an iPhone, so 1.3 and
    1.3.1 should work too (not tested, and without the wide-view switch). Update the head unit
    first, then flash the dongle.
  - Image `dongle/hw301/fw/hw301_v1172.tar` (marker `v1172-20261027093548`), now the newest
    HW301 image. `dongle/hw301/fw/hw301_v1173.tar` (marker `v1173-20261027093537`) is the same
    version with diagnostic logging, for bug reports. See [hw301-dongle](../deploy/hw301-dongle.md).
  - First car test on 2026-10-27 with v1173: cluster map on the Virtual Cockpit, CarPlay, Siri,
    calls and navigation prompts on the MMI; the cluster kept running through two main-screen
    hand-overs to the MMI (8,400 frames, none bad, dropped or failed). *Car test pending:* layout
    switching, the menu switches, unplug/replug and phone Wi-Fi loss.
  - The v1167 image keeps working with the 1.4 head unit, unchanged.

- **HW301 image v1176: route guidance works through the dongle.** The HUD maneuvers and the
  cluster maneuver arrow now work with the HW301 as with a cable-connected iPhone. The stock
  dongle firmware never passed CarPlay route guidance on: it dropped the car's request to start
  it and did not announce it to the iPhone. v1176 copies the car's route guidance announcement
  to the iPhone and relays the start/stop requests and the iPhone's maneuver, lane and route
  updates. If the iPhone ever refuses the announcement, the dongle drops it until the next
  restart, so CarPlay keeps working as before.
  - Image `dongle/hw301/fw/hw301_v1176.tar` (marker `v1176-20261028100645`), now the newest
    HW301 image; `dongle/hw301/fw/hw301_v1177.tar` (marker `v1177-20261028100705`) is the same
    version with diagnostic logging. Otherwise identical to v1172.
  - First car test on 2026-10-28: Apple Maps maneuvers, lane guidance and route updates
    reached the car; the cluster map kept running (no bad or dropped frames).

- **Android Auto cluster map on Korean firmware `MHI2Q_KR_AUG22_P1201` (MU1201, issue #33).**
  The unit's `libautoreceiver.so` is byte-identical to the MU0918 one; its `gal` is a later
  build of the same code (only the build date, host and changelist differ). The hook therefore
  gets a built-in receiver profile `P1201` from the unit's files, and
  `scripts/aa_receiver_profile.py` reports every built-in `gal` (P5152, P1201) as "nothing to
  port". `aa_cluster_hook.log`: `init enabled profile=P1201`. The Java patch needs no change
  either: the stock classes it replaces are byte-identical to P5152's (54 class files), the jar
  built against the unit's `lsd.jxe` is byte-identical to the release jar, and the stock linkage
  audit passes against it (0 errors). *Car test pending.*

- **CarPlay settings in the MMI (issue #42).** *Connection manager → Options → CarPlay settings*
  gathers the CarPlay options in one native screen, with a two-line description of the selected
  item, in 14 languages (en, de, ru, uk, es, fr, it, nl, pl, pt, cs, tr, zh, zh_HK; other MMI
  languages show English). Adapted from luka-dev's MHI2Q-2026-10-29 release.
  - **Maps on Cluster** (On / Off): the CarPlay map on the Virtual Cockpit. *Next connection.*
  - **Maneuver Cards Position** (App default / Left / Center / Right): where the iPhone puts the
    turn card and arrival time on the cluster map (`leftAligned`, `topAligned`, `rightAligned`).
  - **Route Guidance** (On / Off): turn-by-turn on the cluster and HUD, CarPlay and Android Auto.
    Applies in the running session.
  - **Map Cards during Guidance** (Hidden / Shown): hides the phone's turn card and ETA in the
    cluster map while the cockpit shows the car's own guidance arrow. New file
    `/mnt/app/root/mibr-carplay111-nav.hideDuringGuidance`; it wins over *all features on wide
    view*. Shown (default) leaves the map switches as they are.
  - **Guidance Maneuver Preview** (Always next maneuver / Follow road when far): with *Follow
    road when far* (default, as before) the cockpit shows a straight arrow until the next turn is
    within 1.5 km (3 km on motorways). New marker `/mnt/app/carplay_rgd.next_maneuver` for
    *Always next maneuver*. Applies at once, without dropping the route.
  - The map switches of the GEM menu: turn card, ETA, speed limit, compass (Off / On / Phone
    setting) and all features on wide view.

  Every item writes the same file as the matching GEM button, so the two menus always show the
  same state. The patched `de.audi.tghu.connectivity.hmi.Activator` falls back to the stock
  Connection Manager if anything in the new screen fails to load. *Car test pending.*

- **Android Auto final destination, remaining distance and ETA.** The Android Auto hook reads the
  destination (NavigationState) and the distance, time and arrival estimate (CurrentPosition)
  that Google Maps sends about once a second, and writes them to `/tmp/aa_trip`. The Java patch
  shows them in the cockpit and HUD like CarPlay's: destination, distance to destination,
  time remaining and arrival time. A trip record older than 30 s is ignored; fresh estimates also
  keep an unchanged destination current. *Car test pending.*

- **Android Auto cluster map on untested `gal` builds (crash guard).** The hook used to need an
  exact profile for both `libautoreceiver.so` and `gal`. It touches no `gal` object, so a known
  receiver with an unknown `gal` now runs **unpinned** (`init enabled profile=<name>
  gal=unpinned`). A marker, `/tmp/aa_cluster.unpinned`, exists from start-up until the first
  cluster picture or a normal `gal` exit. If an unpinned `gal` crashes before either, the next
  start finds the marker and keeps Android Auto stock (`init disabled
  reason=unpinned_failed_before`) until a reboot. This covers units with the MU0918 receiver and
  any `gal`; a different receiver still needs a profile
  ([firmware-porting](../android-auto/firmware-porting.md)). *Car test pending.*

- **CarPlay cluster map at 30 FPS.** The hook adds the accessory PPID `bf889dfdaea54096` to the
  iAP2 Identify when stock sends none (new module `hook/mapsprofile`). In Apple's
  CarExperienceAssets that profile only sets `mapsRefreshRate=30`, so Maps renders the cluster
  map for 30 FPS instead of about 20 (upstream measurement on a virtual iPhone: 21 -> 29.7
  pictures/s). `carplay_hook.log`: `Identify: +PPID bf889dfdaea54096`. Off:
  `/mnt/app/root/carplay_maps30.off`. From luka-dev MHI2Q-2026-10-29. *Car test pending.*

## Changed

- **The head unit no longer needs to recognise the HW301.** The v1172 image uses the normal
  two-display path like a phone. The old path for the `iPhone9,1` / `18D70` identity stays for
  v1167, older custom images and stock HW301 firmware, and for senders listed in
  `mibr-carplay111-noadvertise.senders`.
- **More robust stream-111 transport for the old dongle path.** The head unit now waits on that
  socket with `poll()` instead of `select()`, so a high descriptor number no longer stops the
  cluster stream. A stalled view or key frame message times out after 200 ms instead of
  blocking, and these messages also go out while the cluster video is idle.
- **GEM *HW decoder ON / OFF* covers Android Auto too.** It used to switch CarPlay's decoder
  only. ON now also clears Android Auto's markers and both start records; OFF sets both.
- **An Android Auto hardware decoder failure turns hardware decode off for this boot only**
  (`/tmp/altscreen_render.aa.hwdecode.off`), no longer for good. See issue #44 below. CarPlay keeps
  the persistent marker. `altscreen_render.log` names the marker whenever hardware decode is off:
  `hardware decode OFF (… present); software decode`.
- **Android Auto software decoding uses two frame threads** (`ALTR_AA_DECODE_THREADS`, 1-4,
  1 = as before). Only the fallback without the hardware decoder is affected: one thread could not
  keep up with 1080p30 (issue #44); two decode about 1.75x faster (host measurement). The cost
  is one picture of delay, so while the map stands still the cockpit shows the picture before the
  last one. CarPlay stays on the low-delay decoder, because Apple Maps without a route sends few
  pictures.
- **Save logs also lists `/mnt/app` and `/mnt/app/root`** (`markers_ls.txt`), so a report shows
  every setting marker.
- **Phone route guidance no longer sets the Audi navigation's "route guidance active" flag.**
  The cockpit accepts CarPlay / Android Auto guidance through a flag of its own in the patched
  `ClusterService` (`setCarPlayRgiActive`). See the stale-route fix below.
- **The car's guidance is stopped only when it is running.** At phone connect the cluster abort
  (`setRouteGuidanceAborted`) is now sent only while Audi navigation guides or calculates a route,
  so it can no longer race the start of CarPlay guidance. From luka-dev MHI2Q-2026-10-29.

## Fixed

- **Cluster map ETA missing in Waze until the switch was toggled (issue #35).** With *Cluster
  map ETA* ON, Waze drew no ETA after a reconnect, or after Google Maps had guided in the same
  session, until the switch went OFF and ON. When an app starts guidance, iOS hands it the cluster
  with a `suggestUI`, which the hook acknowledged but never answered. The hook now answers
  `suggestUI` 1 s later with `showUI`, followed by `updateViewArea` and `forceKeyFrame`, in the
  same order as at session start. Re-sending the same URL (also after a `stopUI`) was not enough
  in the car; a changed URL was. So with ETA ON the answer is first one `showUI` with
  `showETA=no`, then the saved URL once that one completed. The saved URL also follows when the
  first one fails, because the phone may have applied it anyway. Both go through the same
  command queue as the other cluster commands, in the running CarPlay session. The saved switch
  is not changed. A burst of `suggestUI` gives one answer. `altscreen111.log`: `policy=RESHOW`, `gen2 suggestUI -> showUI reshow rc=0 eta_flip=1`
  and `gen2 showUI ETA flip url=…`. *The same sequence fixed it in the reporter's car; car test of
  this build pending.*

- **CarPlay cluster map kept falling back to the Audi map with Apple Maps and no route
  (issue #39).** Without a route, Apple Maps sends a new picture only when the map changes:
  ~4-9 fps while moving, none for 5 s and more while still (North Up worst). The renderer
  dropped its liveness heartbeat 500 ms after the last picture, so the cockpit switched to
  ctx 74/80 and back every few seconds. CarPlay now keeps the last picture for as long as the
  video connection is up, as Android Auto already did (`ALTR_LIVE_TIMEOUT_MS` default 0). The
  hook closes that connection whenever stream 111 ends, so the cockpit still falls back on a
  disconnect or a session end. `ALTR_LIVE_TIMEOUT_MS=500` restores the old behaviour. *Car test
  pending.*

- **Black main screen, then a reboot of the whole unit, after switching screens while driving
  (car logs 2026-10-30, saves 009-011).** Stock's main-screen decoder and the cluster map's
  hardware decoder share one video core. In save 011 the reverse camera and CarPlay / Audi
  screen switches started and stopped stock's decoder four times in 80 s, with the cluster
  decoder running all along. The next start of stock's decoder timed out in the video core
  (`VCD_HWTimeoutHandler`, `MMKIVdecDriver_Start TimeOut!!!`). The main screen stayed black, the
  log save hung, and the issue #14 freeze led to a watchdog reboot (save 009: no process core,
  only a new boot). The CarPlay hook now tells the renderer before stock sets up or tears down
  the main screen and waits up to 1.5 s for it to free the video core. The renderer suspends
  only its decoder: the cockpit keeps the last map picture (no blink, no Audi map) until the
  main screen has been quiet for 3 s (`ALTR_HW_YIELD_HOLD_S`), then it reconnects for a key frame
  and the map moves again. The two decoders no longer change state at the same moment.
  `altscreen111.log`: `hw yield (teardown110): renderer released the decoder in N ms`;
  `altscreen_render.log`: `main screen video changing; carplay hardware decoder suspended, last
  picture kept`, `main screen quiet; reconnecting carplay for a key frame`, `hardware decoder resumed`.
  *Car test pending.*

- **Android Auto cluster map froze for about 90 s at a time (issue #44).** The phone kept sending
  30 pictures a second, but the renderer decoded only about 60% of them. An earlier hardware
  decoder failure had left the persistent Android Auto off marker, so every later session decoded
  the 1080p30 stream in software. Software decode cannot keep up (this FFmpeg build has no NEON),
  the tee fell behind, and the picture waited for Google Maps' next key frame, which comes only
  every minute or two. The GEM switch, which only cleared CarPlay's marker, could not undo it.
  Fixed by the two *Changed* items above. Units that already have the marker: press **HW decoder
  ON** once (if it reports OFF, press it again). *Car test pending.*

- **Audi navigation resumed an old route after standby.** To make the cockpit accept phone
  guidance, the Java patch forced the navigation-wide "route guidance active" flag. Stock
  `RouteManager` stores that flag at standby (`RoutePersistenceController.storeGuidanceStatus`),
  so a forced `true` made Audi navigation resume a historical route on a later start. The patch
  now uses its own `ClusterService` flag, and on the first connect with idle Audi navigation it
  stores the real (inactive) status once, which clears a stale entry left by older builds.
  From luka-dev MHI2Q-2026-10-29. *Car test pending.*

- **Route guidance or Now Playing messages could be corrupted when incoming and outgoing iAP2
  traffic overlapped** (luka-dev #79, #81). The hook kept the direction and buffer of the current
  message in shared state, so an outgoing message could change them under an incoming one. Each
  dispatch now has its own context, and route guidance handles incoming messages only.

- **The cluster map stream no longer stops on a single damaged picture.** The hardware decoder
  treats `OMX_ErrorStreamCorrupt` and `OMX_ErrorMbErrorsInFrame` as recoverable (the next key
  frame repairs the picture) instead of falling back to software decode. Hardware and timeout
  errors still fall back as before. From luka-dev MHI2Q-2026-10-29.

- **Stale cover art when the next track has none.** iOS announces "no artwork" as an empty file
  transfer; the hook now clears the picture on the cockpit instead of keeping the previous one.
  From luka-dev MHI2Q-2026-10-29.

- **The VC phone tab stuck on "Ending call", or a call that stayed after it ended.**
  `dio_manager` keys a CarPlay call by its CallUUID, or by the number while the UUID is empty, and
  removes it only on a disconnect. A call first reported without a UUID left an orphan under the
  number. The phone tab now hides those orphans, always confirms a hang-up, and clears a call
  that iOS does not report as ended within 1 s. From luka-dev MHI2Q-2026-10-29.
- **Play / pause bouncing at volume 0.** Stock's switch to "paused by mute" handed main audio
  straight back to the iPhone (`Audio_Media: Borrow -> Unborrow`, sent only by that transition),
  and every repeated playback-state update could unmute the MMI. That hand-back is dropped and
  repeated states are ignored. From luka-dev MHI2Q-2026-10-29.
- **Dialling from the VC while CarPlay was in the background did nothing.** iOS ignores the call
  request unless CarPlay owns the screen. The patch now switches the MMI to CarPlay and dials once
  the iPhone has the screen (5 s window). From luka-dev MHI2Q-2026-10-29.
- **Touchpad: a vertical swipe could jump left or right at the end of a list.** Each touch sample
  now counts only on the axis that moved more. From luka-dev MHI2Q-2026-10-29.

## Known issues

- **HW301: the start of Siri's answer can be cut.** In the 2026-10-27 test the car took 1.6 to
  1.9 s to open the Siri audio stream, against 0.1 to 0.4 s for media and prompts. The phone
  starts speaking at once, so the first 1.5 to 2 s are lost. Not caused by the v1172 changes; no
  earlier HW301 log contains a Siri session to compare. Under investigation.
- **Short cluster map blinks.** The picture restarts for a moment when iOS restarts its video
  encoder (seen several times when route guidance starts, on both screens at once) and when the
  car asks for a key frame (seen while the MMI showed the Audi menu).
- **Yanosik with *all features on wide view* ON.** That switch makes the wide and small maps use
  different cluster URLs, so every wide ↔ small switch re-sends `showUI` to the phone (car logs
  2026-11-02, saves 012/013). Apple Maps and Google Maps just redraw; Yanosik restarts its
  CarPlay screens: the cluster map stops and the first switch closed the app on the MMI (the
  iPhone gave the main screen back to the car 0.15 s after `showUI`). Workaround: switch *all
  features on wide view* OFF, so a layout switch sends only `updateViewArea`.

## Documentation

- [altscreen-mhi2q](../deploy/altscreen-mhi2q.md): the compass and the wide-view switches are
  added to the cluster URL switch table; the HW301 v1172 identity next to the legacy one.
- [hw301-dongle](../deploy/hw301-dongle.md), dongle README and FINDINGS: v1167 and its
  per-layout URLs; the v1172 design, its build and the first car test; the v1176 route
  guidance bridge.
- [hw-decoder](../altscreen/hw-decoder.md) and [Android Auto overview](../android-auto/overview.md):
  the Android Auto decoder markers and issue #44.
- [firmware-porting](../android-auto/firmware-porting.md): unpinned `gal` mode and its marker.
- README: the MMI CarPlay settings, the release layout and the 1.4 known issues.
- Tests: GitHub Actions runs `scripts/run_tests.sh` on every push and pull request (Ubuntu,
  clang); the Java suites need the proprietary stock jar and stay local
  (`test_route_info.sh`, `test_java_transports.sh`, new `test_mmi_settings.sh`).

## Updating from 1.3.1

Copy both `carplay/` **and** `mod/` to the card, because the menu has new buttons. Run
**M.I.B. -> Advanced Settings -> Run Custom Script** once, then reboot. The menu's
**Install / update CarPlay** button updates the program files but not the menu, so the new
buttons appear only after the M.I.B. run.

HW301 users: update the head unit first. Then flash `fw/hw301_v1176.tar` on the bench, or keep
v1172 or v1167, which work as before (without route guidance).
