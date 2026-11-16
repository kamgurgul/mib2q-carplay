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
