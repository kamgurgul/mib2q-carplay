# mib2q-carplay 1.6 (in development) - changelog

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

## Fixed

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

## Improved

- **Route-start diagnostics without verbose logging.** `carplay_hook.log` (WARN level) now records
  each `route_state` change with the number of cached maneuvers and lane events, a route reset
  frame that drops cached data, and a current maneuver with no `0x5202` data yet (once per index
  and route). Reason: on the first long Apple Maps route through the HW301 dongle (2026-11-17)
  the cockpit arrow stayed on *follow road* and the lane events came without lanes, and the
  WARN-only log could not tell where the maneuvers were lost.

## Tests

- `tests/alt111_log_test.c` (in `run_tests.sh`): `altscreen111.log` is written in place before the
  writer thread starts; with the writer running, a log target whose `open()` blocks (a FIFO with no
  reader) does not block 300 callers, the overflow is counted and the survivors arrive in order;
  rotation works when `rename()` is refused, as on `/dev/shmem`.
