# mib2q-carplay 1.6-dev-bf9f487-dirty (development build of 1.6) - changelog

Changes since [1.5](CHANGELOG-1.5.md). Entries marked *car test pending* passed the bench
tests but have not been driven yet.

## Changed

- **Android Auto: only the car's turn card during route guidance, always** (issue
  [#56](https://github.com/kamgurgul/mib2q-carplay/issues/56)). In 1.5 this was the opt-in
  switch *Android Auto hide phone turn card ON / OFF* (GEM) / *Hide Android Auto Turn Card*
  (MMI), default OFF. NuiQuant's test of 1.5 (A4 B9, Google Maps and Yandex, large, Classic and
  Sport) showed it working: with *Route guidance* ON the app's card is gone and the car's
  arrow remains, with *Route guidance* OFF the app keeps its card, and the ETA line is no
  longer hidden with the 1.5 insets. No drop was traced to it (that car also loses Android Auto
  now and then on stock software). With the switch off, route guidance shows two turn cards.
  So the switch is removed from both menus and the hook sends the flag (UiConfig field 5 =
  `[5]`) at every phone connect while *Route guidance* is ON. It is still decided at connect
  and kept for the session (#19), and the 1.5 limits stay: the flag does not follow the
  cockpit hiding the arrow tile during a route, and an app that sends no turn events then shows
  no turn card. The way back is a file with no menu entry,
  `/mnt/app/root/aa_cluster.turncard_off` (listed on the GEM status page); the 1.5 marker
  `aa_cluster.turncard` is no longer read, and a leftover file is harmless. Reinstalling the
  GEM menu removes the old button. `aa_cluster_hook.log`: `uiconfig.set ... turncard=car|phone`.
  The flag itself is in `libaa_cluster_hook.so`; the jar only loses the MMI switch. *Car test
  pending* for the always-on default.
- **The cluster map picked with a long roller press is remembered.** *Car test pending* (bench
  tests and a simulated HMI restart pass). Until 1.5, choosing the Audi map with a long press of
  the left steering-wheel roller lasted only for that phone session: every reconnect or car
  start brought back the phone map. The choice is now saved in `/mnt/app/root/cluster_map_source`
  (`phone` or `audi`). It survives phone reconnects and car restarts and is shared by CarPlay and
  Android Auto. A missing or invalid file means the phone map, so existing installs start as
  before. Only a long press writes the file; disconnects, route changes and the phone video
  dropping out do not. The press switches the map at once, and the file is written on a
  background thread (temporary file, rename, `sync`), so a slow flash never delays the switch,
  and a write that fails keeps the previous choice. As before, the Audi map is shown until phone
  video is live. With the Audi map saved and *Pause Hidden Cluster Map* on, the phone stream is
  paused from the start of the session, and a long press brings it back even before video is
  live. The GEM status page shows the saved choice under *Options*. In `carplay_java.log`, a saved
  Audi choice is a warning when the MMI starts (`ClusterMap: loaded Audi map ...`), so logs at the
  default level show why the phone map stays off the cluster; a failed save is a warning too.
  Verbose logging adds `saved phone map` / `saved Audi map` and `ready (... saved map: Audi)` at
  session start. To go back to the phone map, long-press again. Deleting the file also works, but only from the next car
  start, because the choice is read once when the MMI starts.

## Fixed

- **Cluster map off-centre at connect until VIEW was pressed twice** (issue
  [#68](https://github.com/kamgurgul/mib2q-carplay/issues/68), Yandex Maps, wired CarPlay, Q7
  2017). *Car test pending.* After a connect the puck sat low and left, under the scale bar;
  switching the cockpit to the small map and back put it in place. The phone now gets the
  same view data at connect that a VIEW press gives it:
  - `/info` declares `adjacentViewAreas` next to `initialViewArea` on the cluster display, the
    same list (every other view) `updateViewArea` carries. Until 1.5 `/info` had only
    `initialViewArea`, and the `updateViewArea` after the first `showUI` repeats that view, so
    nothing completed the starting view state until a real switch. The MHI3 native helper
    declares both as well.
  - Java sends the cockpit layout when a new session's hook connects to the bus, about 1.5 s
    before `/info`, so `initialViewArea` is the visible map's view. Until 1.5 it went out only
    once video was live, so `/info` always said wide and a classic or Sport cockpit started on
    the wide SafeArea until then. With a small map and no stock layout yet the skin is unknown
    and the hook keeps wide.

  One ViewArea (`mibr-carplay111-viewareas.single`) keeps the old `/info`. Logs:
  `GEN2 ViewAreas=3 initialViewArea=0 (wide) adjacentViewAreas=2` in `altscreen111.log`,
  `AltScreen view area for the new session (initialViewArea) -> ...` in `carplay_java.log`.
  The HW301 dongle (v1176 and later) passes the car's display and its `updateViewArea` to the
  phone field for field, so both changes reach the phone through it with the head unit alone.
  The dongle source adds the dongle's own case. It keeps the car's display for the whole car
  session, so a phone that reconnected while the dongle stayed connected to the car started
  in the view from the start of that car session. The phone's `/info` now takes
  `initialViewArea` and `adjacentViewAreas` from the car's latest `updateViewArea`, which the
  dongle already keeps for replay. An index outside the display keeps the car's own view, and
  an adjacency that is not a list of other views is dropped. Dongle log: `/info
  initialViewArea=N from the car's last updateViewArea`. This part is in the new dongle image
  `dongle/hw301/fw/hw301_v1178.tar` (marker `v1178-20261124092549`), now the newest HW301
  image; `dongle/hw301/fw/hw301_v1179.tar` (marker `v1179-20261124092559`) is the same version
  with logging. Otherwise identical to v1176, which stays available. Car-tested (reported
  2026-11-25). The archived v1167 image builds its own `/info` and is unchanged.
  The other half of #68, the roller not zooming Yandex, is the app: iOS accepts our `changeMapZoomLevel`
  (`status=0`) and hands it to the app, which ignores it, as Waze does.

- **HW301 dongle: CarPlay would not open on the MMI after verbose logging was switched on**
  (car logs 2026-11-17, saves 004/005; audio kept playing). *Car test pending.* The dongle's
  stock proxy gives the car about 0.5 s to answer its main-screen SETUP after the phone's first
  picture, then hands the screen back to the car (`sendMainScreenModes(1,500,500,500)` in
  `recvCarPlayVideoFrame`). With `carplay_verbose` set, the AltScreen hook wrote its per-command
  FLIGHT traces inside the AirPlay control calls, about 25 lines per `modesChanged` at 3 to 30 ms
  each, so the three `modesChanged` the dongle sends first held the SETUP back by 0.8 to 1.1 s.
  Every attempt to open CarPlay bounced back about 1 s later. `altscreen111.log` is now written by
  its own thread: the control path only formats a line and queues it (256 lines, the oldest are
  dropped and counted when full); lines from before the deferred runtime start are written in
  place as before. Wired CarPlay has no such deadline, which is why it never showed this.
- **AltScreen hook: the log rotation never worked on the unit.** `/tmp` is `/dev/shmem`, which
  refuses `rename()`, so `altscreen111.log` grew past its 512 KB cap (866 KB in save 005) and
  every line past it cost a failed rename and an extra open. Rotation now copies the file to
  `.1` and truncates it when rename is refused.
- **CarPlay main-screen SETUP no longer waits for the cluster decoder.** *Car test pending.* The
  hook asked the cluster renderer to free the shared video core only when stock's SETUP 110
  arrived, and waited 100 to 170 ms for it there. Through the HW301 dongle that wait took a third of
  the 0.5 s answer window (the first attempt after save 003 bounced once). The `modesChanged` that
  gives the main screen to the phone now stamps the yield file at once (`hw yield (screen-to-phone):
  stamped ahead of the main-screen SETUP`), so the decoder is free about 0.3 s later when the
  SETUP comes; the SETUP still checks and waits if it is not.
- **"Your iPhone is in Apple CarPlay mode" still came up on some phone entries** (issue
  [#69](https://github.com/kamgurgul/mib2q-carplay/issues/69), P5092, wired iPhone). *Car test
  pending.* After a skip the phone app was left on the popup while the screen went to CarPlay.
  The phone app keeps deep history, so the next MENU -> Telephone, or TEL while the popup
  showed, brought the popup back straight from history without checking anything. The skip
  only hooked those checks: five phone transitions and system 833. That is why the popup
  showed with no `Telephony` log line, and why the result depended on which phone screen was
  last open. The skip now also fires whenever the popup's phone state is entered by any route
  (`CarPlay popup skipped (phone state 300553 entered)`). It is found by its stock label, not
  a firmware-specific number, and it fires only while the stock choice says CarPlay holds the
  phone. The old hooks stay. A second press from the same entry within a second is dropped
  (it would flip the toggle back), and verbose logging now shows that drop. System transition
  833 is not the TEL key, as the code comment said: TEL reaches the phone app through its slot
  and history.
- **"!" stuck next to the phone signal bars on the cockpit** (CarPlay through the HW301 dongle,
  2026-11-25). *Car test pending.* The cockpit's register state comes from the iPhone's
  CommunicationsUpdate. `hook/telephony` clears its copy on every new Identify and disconnect,
  and iOS re-sends only the params that change, so after a reconnect the RegistrationStatus can
  stay unreported for the rest of the drive while the signal keeps updating. Java sent that
  as 0 (not registered), which the cockpit shows as "!". An unreported status is now sent as
  registered home. Every status the iPhone does report, and airplane mode, reaches the
  cockpit as before.

## Improved

- **Route-start diagnostics without verbose logging.** `carplay_hook.log` (WARN level) now records
  each `route_state` change with the number of cached maneuvers and lane events, a route reset
  frame that drops cached data, and a current maneuver with no `0x5202` data yet (once per index
  and route). Reason: on the first long Apple Maps route through the HW301 dongle (2026-11-17)
  the cockpit arrow stayed on *follow road* and the lane events came without lanes, and the
  WARN-only log could not tell where the maneuvers were lost.

- **Save logs collects the HMI's crash evidence** (issue
  [#62](https://github.com/kamgurgul/mib2q-carplay/issues/62)). An MMI restart leaves a `j9`
  core (the HMI, which our jar runs in) and the stock HMI's error dumps in
  `/mnt/ota/system/logs/` (`-DErrorDumpDir`). Both are on persistent flash and survive the restart,
  but the logging mod never copied them: #62's upload listed four `j9` cores, all at 5 min of
  uptime, and contained none. They are now saved to `core/` and `errdump/` (`errdump_ls.txt` lists
  the folder; at most 1 MB per file and 16 MB in all). The size check also handled a `wc` that
  pads its output, so a vidc dump could be skipped. The bug report form and the mod now describe
  how to catch a problem that only happens at power-on: persistent *Verbose logging ON*, start
  with the phone connected, save after the problem and again after the restart.

## Tests

- `tests/GatedPhoneCommTest.java` (in `scripts/test_java_transports.sh`): EVT_TEL_COMM frames
  through the real `GatedPhoneService` with CarPlay holding the phone tab. Each iAP2
  RegistrationStatus reaches the cockpit as stock maps DSI, airplane mode is not registered, and
  a status not reported (registration -1, with or without a signal) is registered home.
- `tests/CarPlayDisclaimerSkipTest.java` (in `scripts/test_java_transports.sh`): the real stock
  PhoneSMM tables under `CarPlayPhoneSMM`. The popup state is found by label (300553 on P5152)
  and keeps its enter action. Entering it from history presses *Switch to Apple CarPlay* once,
  and the stock drawers still run. A repeat within a second, choice 301049 = 0, or any other
  state presses nothing. Guard 300002 followed by the state entry is one flip.
- `tests/alt111_info_viewarea_test.c` (in `run_tests.sh`): the shipping `/info` builder on a stub
  CF graph declares `adjacentViewAreas` with `initialViewArea`, the same list `updateViewArea`
  sends for every view; a layout received before the session becomes the next
  `initialViewArea`; one ViewArea, or no `CFNumberCreate`, keeps the old `/info`.
- `tests/hw301_dongle_test.c` (in `scripts/test_hw301.sh`): the first phone of a car session
  gets the car's own view; after a car `updateViewArea` a reconnecting phone gets its index and
  adjacency while the car snapshot stays untouched; an adjacency listing the view itself or a
  view the display lacks is dropped; an index the display lacks keeps the car's view; a command
  without adjacency removes the car's; a car reset brings back the car's view.
- `tests/AltZoneHelloTest.java` (in `scripts/test_java_transports.sh`): at bus HELLO the real
  `ClusterLayerController` sends wide, classic or Sport over the real `CarplayBus` (the stock
  stage without a FctID 54), nothing for a small map with no stock layout, and leaves the
  per-video-session resend in place.
- `tests/alt111_log_test.c` (in `run_tests.sh`): `altscreen111.log` is written in place before the
  writer thread starts; with the writer running, a log target whose `open()` blocks (a FIFO with no
  reader) does not block 300 callers, the overflow is counted and the survivors arrive in order;
  rotation works when `rename()` is refused, as on `/dev/shmem`.
- `tests/ClusterMapPreferenceTest.java` (in `scripts/test_java_transports.sh`): both choices
  survive a reload; a missing, invalid or unreadable file means the phone map and writes nothing;
  an interrupted save leaves the previous choice; a failed save does not block the switch, and
  the next differing choice retries; presses during a slow save are coalesced and the latest one
  wins; an unchanged choice does not write flash.
- `tests/ScreenLongPressTest.java` now runs the real `ScreenModule` start/stop: the Audi choice
  stays through an immediate reconnect and a simulated HMI restart, hides the phone stream from
  session start, and a long press brings back a paused stream; video fallback, an unsupported
  cluster and presses without a session do not change the saved choice.
- `scripts/test_gem_menu.sh`: the status page shows the saved cluster map (*Audi*, and *phone*
  when there is no file).
