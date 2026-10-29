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

## Changed

- **The head unit no longer needs to recognise the HW301.** The v1172 image uses the normal
  two-display path like a phone. The old path for the `iPhone9,1` / `18D70` identity stays for
  v1167, older custom images and stock HW301 firmware, and for senders listed in
  `mibr-carplay111-noadvertise.senders`.
- **More robust stream-111 transport for the old dongle path.** The head unit now waits on that
  socket with `poll()` instead of `select()`, so a high descriptor number no longer stops the
  cluster stream. A stalled view or key frame message times out after 200 ms instead of
  blocking, and these messages also go out while the cluster video is idle.

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

## Known issues

- **HW301: the start of Siri's answer can be cut.** In the 2026-10-27 test the car took 1.6 to
  1.9 s to open the Siri audio stream, against 0.1 to 0.4 s for media and prompts. The phone
  starts speaking at once, so the first 1.5 to 2 s are lost. Not caused by the v1172 changes; no
  earlier HW301 log contains a Siri session to compare. Under investigation.
- **Short cluster map blinks.** The picture restarts for a moment when iOS restarts its video
  encoder (seen several times when route guidance starts, on both screens at once) and when the
  car asks for a key frame (seen while the MMI showed the Audi menu).

## Documentation

- [altscreen-mhi2q](../deploy/altscreen-mhi2q.md): the compass and the wide-view switches are
  added to the cluster URL switch table; the HW301 v1172 identity next to the legacy one.
- [hw301-dongle](../deploy/hw301-dongle.md), dongle README and FINDINGS: v1167 and its
  per-layout URLs; the v1172 design, its build and the first car test; the v1176 route
  guidance bridge.

## Updating from 1.3.1

Copy both `carplay/` **and** `mod/` to the card, because the menu has new buttons. Run
**M.I.B. -> Advanced Settings -> Run Custom Script** once, then reboot. The menu's
**Install / update CarPlay** button updates the program files but not the menu, so the new
buttons appear only after the M.I.B. run.

HW301 users: update the head unit first. Then flash `fw/hw301_v1176.tar` on the bench, or keep
v1172 or v1167, which work as before (without route guidance).
