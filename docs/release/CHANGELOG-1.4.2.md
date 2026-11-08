# mib2q-carplay 1.4.2 (unreleased) - changelog

Changes since [1.4.1](CHANGELOG-1.4.1.md). Ported from
[luka-dev/mib2q-carplay-rgi](https://github.com/luka-dev/mib2q-carplay-rgi) `main` as of
2026-11-06 (`21f5016`, after the `MHI2Q-2026-11-04` tag), on top of the `MHI2Q-2026-10-29` port in 1.4.

## Fixed

- **Route guidance: HUD and cockpit arrow could disagree or show the previous turn's progress.**
  One resolved maneuver (`ManeuverPresentation`) now drives both the HUD (FctID 23/18/22) and
  `maneuver_render`; the distance and progress caches are cleared when the current maneuver
  changes, so a new turn never inherits the old bar. While the current maneuver is unknown or has
  no icon, both show *follow road* instead of borrowing the next one. From luka-dev `e253b98`,
  `31984a7`. Our *Guidance Maneuver Preview* setting (follow road beyond the prepare zone,
  default) is kept and now feeds the same decision.
- **Route guidance: route end racing an update.** The native hook serializes the route-end
  timer's publication with a concurrent iAP2 update, so an active route is not ended by a stale
  timeout. luka-dev `e9e1291`; `tests/rgd_timeout_race_test.c` added to `run_tests.sh`.
- **Cluster phone tab: recent calls shown in UTC** (2 h off on a UTC+2 unit). Recents (and the
  ETA, already) use the head unit's local time through the shared `HuTime` helper. luka-dev
  `8a7b362`, `e444de9`.
- **Cluster now playing: cleared title or cover art stayed on the cockpit.** Metadata and
  artwork clears are forwarded; an empty update is held 800 ms so a track change does not blank
  the cockpit, and YouTube Music's track skip no longer tears down media audio (1.5 s guard).
  luka-dev `8c73881`, `b26d267`, `25e2bae`.
- **Entertainment audio focus released in the middle of a call or Siri.** The release is
  deferred while a CarPlay call or speech is active (`ReleaseAudioFocus` override). luka-dev
  `25e2bae`, `2aedb95`.
- **Parking display (OPS) over CarPlay:** the standalone OPS popup uses a trimmed background
  (`2199999.png`, by chefranov) while CarPlay owns the screen, and the stock one otherwise.
  Installed to `/mnt/app/eso/hmi/lsd/images/HMIEarlyAppsEvoHigh/` as an optional asset and
  removed by Uninstall; without the file the stock background stays. luka-dev `1698cc2`.
- **maneuver_render:** roundabout layout, U-turn elevation, contact-shadow corners and
  transition caching. luka-dev `036cc75`.
- **Cockpit went black for up to about a second when a phone session ended.** With
  hardware decoding, closing the decoder destroyed window 99 at once, while Java switches
  the cockpit back to the stock map only after `.live` has been missing for three 250 ms
  polls. The renderer now suspends the decoder (the video core is free for stock at once,
  as before), drops `.live` and keeps the last picture for 1.5 s, then closes the window; a
  new session ends the hold at once. The software path already kept its window.
  `run_tests.sh` checks that the hold stays longer than Java's switch-back time. Idea from
  luka-dev `d8944f5` (freeze at disconnect), own implementation.

## Tests

- `SANITIZE=1 scripts/run_tests.sh` builds every C test (and the HW301 bridge tests) with
  ASan + UBSan; the whole suite passes clean.
- `tests/altscreen_nal_fuzz.c`: fuzz test of the two Annex-B scanners that see every byte
  from the phone, the hardware decoder's SPS/PPS split (`altscreen_render/nal.h`, moved out of
  `hw_decode.c` so the host can run it) and the Android Auto tee's key-frame gate. Structured
  AUs with an exact expected split plus random buffers, exact-size heap blocks;
  `ALTR_FUZZ_ITERS` sets the size (5 million per generator ran clean).

## Kept as in this fork

- Junction type still does not gate the arrow (MIBSI parity, issue #9). Upstream's `8bc44f0`
  fixes the same missing-junction case but keeps the MHI3 gate for supplied junction types;
  the upstream tests were adapted to this fork's mapping.
- Cluster phone tab dialling (`dialable()`, number-less favorites through InitiateCall) is
  unchanged; upstream's `resolveDialNumber` was not taken. The wider formatted-number check
  (NBSP, dots, direction marks) was.

## Not ported

- Upstream's AltScreen renderer work (PRISM loader and transition, lifecycle epoch fences,
  reconnect gate, loader/NAL speed-ups, `d5f7685`, `d8944f5`, `0373ff2`, `1d62e15`): this fork
  uses a different cluster video renderer.
- Upstream's cluster SafeArea recalibration (`7490ffd`): this fork has its own calibrated
  layouts and the SafeArea files.
- Audio processing, tone and upmix (`585ffbe`, `52c3002`, `4847fbd`, `b4ebb44`): not in this
  fork.
- NCM cold-boot mount fast path (`33eaaa1`): experimental USB replug recovery, not in this fork.

*Car test pending for all of the above.*
