# mib2q-carplay 1.5 (in development) - changelog

Changes since [1.4.1](CHANGELOG-1.4.1.md). Entries marked *car test pending* passed the bench
tests but have not been driven yet.

First change for 1.5: ported from
[luka-dev/mib2q-carplay-rgi](https://github.com/luka-dev/mib2q-carplay-rgi) `main` as of
2026-11-06 (`21f5016`, after the `MHI2Q-2026-11-04` tag), on top of the `MHI2Q-2026-10-29` port in 1.4.
Second port: `main` as of 2026-11-11 (`9861081`, untagged); what was and was not taken is listed
under each section.

## Fixed

- **Route guidance per navigation app: Baidu, AMap and Yandex roundabouts** (luka-dev #92).
  *Car test pending.* The app's name (`0x5200` SourceName, so far dropped by Java) now selects a
  compensation stack. Baidu's roundabout types are directions, not exit numbers, and get coarse
  arrows; AMap sends junction 0 for its roundabouts (left circle, departure, right circle); Yandex
  sends the exit number with no angle and now gets an illustrative roundabout (exits 1 to 14) on
  the HUD and the cockpit arrow: entry, the exits before the chosen one and one after it, evenly
  spaced. The real exit direction is still unknown. These apps do not send a driving side, so the
  side of circulation comes from the car's RHD coding unless the phone sends one. Every other app
  (Apple Maps, Google Maps, unknown) keeps the existing mapping. Switching apps mid-route clears
  the old app's maneuvers instead of relabelling them, and the name survives soft reroutes.
  luka-dev `52fbc2b`, `5508460`, `a334f1b`. Only CarPlay: Android Auto roundabouts (Waze, Google
  Maps) still use `AaManeuverMap`'s estimate from the exit number.
- **Stock navigation text on the cockpit after a CarPlay route.** The stock setters skip values
  they already sent, so after CarPlay released the cluster the stock street name, next-turn text,
  distance/time to destination and destination could stay blank or show CarPlay's last values until
  the stock navigator changed them. The native wrapper now keeps the latest stock value of every
  FctID CarPlay owns and replays it on NavigationJobs when ownership ends (route end: position
  group; disconnect: also the route group, in RGStatus sync order); a failed or lost replay is
  retried by a short-lived worker, and a replaced Navigation service never gets a stale snapshot.
  luka-dev `8a6b220`. Map scale and altitude are not owned on this fork (see below).
- **Map-only cluster view (cards hidden during guidance) could keep a side margin.** The URL now
  asks for `maneuverLayout=topAligned` instead of `no`: `no` selects automatic layout, which can
  keep the card column reserved as a SafeArea inset with the cards hidden. *Car test pending*
  (from luka-dev `1299619`'s iOS 26.6/27.0 static analysis). The visible-card default still names
  no layout (issue #20).

- **Route guidance: HUD and cockpit arrow could disagree or show the previous turn's progress.**
  One resolved maneuver (`ManeuverPresentation`) now drives both the HUD (FctID 23/18/22) and
  `maneuver_render`; the distance and progress caches are cleared when the current maneuver
  changes, so a new turn never inherits the old bar. While the current maneuver is unknown or has
  no icon, both show *follow road* instead of borrowing the next one. From luka-dev `e253b98`,
  `31984a7`. Our *Guidance Maneuver Preview* setting (follow road beyond the prepare zone,
  default) is kept and now feeds the same decision.
- **Android Auto: the phone's ETA pill was hidden behind the cockpit's street-name bar in the
  Sport layout, and the safe areas were off on a Q7** (issues #41, #53). *Car test pending.*
  Two causes. The built-in Sport bottom inset (146) ended the safe area on panel row 420, 30
  rows below the top of that bar; all three views now end on row 390 (bottom inset 176: Sport
  30 rows higher, large map and Classic 9 rows higher than in 1.4). And the built-in values
  are B9 numbers, while a Q7 shows the phone's picture from panel row 18 (B9 26) and shifts it
  by 460 in the Sport small stage (B9Sport 476): Java now writes both numbers of the stock
  layout into `/tmp/aa_cluster_view` and the hook moves its presets by the difference, so a
  Q7 gets `105 168 350 370`, `105 168 510 510`, `86 168 468 410` where a B9 gets
  `97 176 350 370`, `97 176 510 510`, `78 176 484 394`. An `aa_cluster.insets` file is still
  sent exactly as written. Install the jar and `libaa_cluster_hook.so` together; with only
  one of them new nothing breaks, but a Q7 keeps the B9 numbers. Not changed: the Sport safe area is still
  562 px wide (the fork's choice), about 100 px wider on each side than the visible map window,
  and where a navigation app puts the car inside the safe area is the app's decision (Yandex
  keeps it right under its turn banner in every view).
- **Android Auto: Yandex placed its map off-centre on the cockpit, in every view and on every
  car** (issue #53). *Car test pending* (NuiQuant tested the same encoding on an S4 B9 with
  Yandex and Google Maps). The safe area reached the phone as content insets measured from the
  whole 1920x1080 stream; Google Maps placed itself from that, Yandex did not. The UiConfig now
  says it the way Android Auto defines it: margins 270/270/240/240 around the 1440x540 viewport
  (field 1; the phone paints that frame black, and it is cropped away anyway), and the view's
  safe area relative to the viewport as content and stable insets (fields 2 and 3). Same numbers,
  same views; connect and view changes alike. `/mnt/app/root/aa_cluster.uiconfig_legacy` sends
  the 1.4 form again (read at phone connect); `aa_cluster_hook.log` shows
  `uiconfig.set ... encoding=viewport|legacy`.
- **`altscreen_render` crashed when an Android Auto session ended** (1.4.1, issue #43: Q7
  MU1316). After the main-screen yield had suspended the hardware decoder, unplugging the phone
  closed that decoder with the connection already gone, and `hw_close` cleared the start counter
  of a NULL source (`hw_starts_clear`, SIGSEGV reading +0x28; the core matches the 1.4.1 binary).
  Nothing showed in the car, and the renderer restarted at the next connection. The decoder now
  keeps its own source (`hw_src`), the same fix as for the frozen picture below.
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
  as before), drops `.live` and keeps the last picture for 1.5 s, then closes the window.
  The software path already kept its window. `run_tests.sh` checks that the hold stays longer
  than Java's switch-back time. Idea from luka-dev `d8944f5` (freeze at disconnect), own
  implementation. The first version of this (earlier in 1.5) had two faults, both fixed before
  release: closing the frozen decoder after its connection was gone dereferenced a cleared
  source pointer, so the renderer crashed at every hardware-decoded session end and
  `carplay_monitor.sh` restarted it 5 s later; and the renderer's immediate reconnect to the
  hook's tee, which listens all the time, ended the hold at once, so the black frame stayed.
- **A new stream no longer needs a new cluster window.** A tee connection alone is not a new
  stream any more: the frozen picture stays until that phone type sends bytes, and then its
  decoder reopens on the same window (`new carplay stream; decoder resumes on the frozen
  window`), so a stream-111 restart or the next Android Auto playback goes from the old picture
  straight to the new one. Another phone type, or 1.5 s without a stream, releases the window.
- **CarPlay: a slow renderer cost a reconnect and a new decoder.** The hook reset the renderer's
  connection whenever its 16-AU queue (about 0.5 s) filled, typically while the map starts
  (`gen2 video AU rc=2 -> consumer reset`). The queue now holds 32 AUs; when it is full the
  unsent backlog is dropped, pictures are skipped until the next IDR on the same connection and
  the phone is asked for one (`forceKeyFrame`). A renderer still priming is reset as before,
  and so is one that delivered nothing between two overflows (it stopped reading: no key-frame
  request every second for a renderer that is gone).
- **Calibration grid did nothing with hardware decode** (the default since 1.3), although the
  GEM button reported it ON. The hardware path now fills the same ruler into each picture with
  the 2D blitter before posting it; GL and hardware share one rectangle list (`altr_grid_rects`).
- **A wall-clock change could switch CarPlay hardware decode off.** The decoder's state waits
  used `CLOCK_REALTIME`: a clock set forward during decoder setup timed every wait out at once,
  the decoder looked hung and hardware was switched off. They use a monotonic condition
  variable now; so does the Android Auto tee's writer.
- **One CarPlay hardware failure kept software decode for good.** Any failure wrote the
  persistent off marker, with nothing on screen to say so. Now only a hung video core (a call
  past the watchdog deadline, a decoder that never left Executing) is remembered after a
  reboot, because it can freeze the HMI; any other failure is off for this boot only, as for
  Android Auto since 1.4. GEM *Cluster video status* shows which marker keeps hardware off.
- **Cluster video looked stale after a wall-clock step.** Java judged the renderer's `.live`
  heartbeat by its mtime against the wall clock. The file now carries `seq=`, and Java counts its
  own polls since it last changed (stale after about 1 s); a renderer without `seq=` is still
  judged by mtime.

## Improved

- **Software fallback with NEON:** FFmpeg is built with its ARM/NEON assembly (284 NEON
  functions; `FFMPEG_NEON=0` builds the plain C decoder of 1.4). FFmpeg picks NEON from its build
  flags on QNX, so the renderer reads the CPU flags from the syspage at start and turns NEON off
  on a CPU that does not report it (`CPU NEON yes; software decoder uses NEON`). Aimed at the
  1080p freeze of issue #44 with hardware decode off; the speed-up has not been measured on the
  car yet, and the assembly has not run there either. GEM *Software decoder plain C (no NEON)*
  (`/mnt/app/root/altscreen_render.noasm`) selects FFmpeg's C code again without a rebuild, and
  after an illegal instruction the renderer sets the same switch for the boot and restarts on C
  code. `ALTR_AA_SW_FAST=1` additionally lets the Android Auto fallback skip the deblocking
  filter (about a third less CPU); off by default, because without the in-loop filter the
  picture drifts until the next key frame, which Android Auto sends only every minute or two.
- **Less work in `dio_manager` while the cluster map is still.** The AltScreen hook's output
  worker woke every 5 ms; it now sleeps on its condition variable until there is something to
  send. The control worker read about nine marker files every 10 ms (around 1000 file-system
  round trips a second); operator markers are now read at most every 250 ms.
- **Phone tab signal bars, diagnostics** (issue #52: one bar with CarPlay whatever the
  signal). `carplay_java.log` now logs the iPhone's signal (`iPhone signal N/5 -> VC quality P`)
  and the value stock sends for a Bluetooth phone (`stock signal quality Q`). The 0..5 -> percent
  scale CarPlay uses was never checked against stock; one log with each phone type settles it.
- **Colours of the software path follow the stream:** the YUV->RGB matrix uses the stream's
  VUI colour description (BT.709, full range). Streams that signal nothing, as all seen so far,
  keep the BT.601 video-range matrix.

## Added

- **Key-frame requests from the renderer.** When a decoder reports a damaged picture (hardware
  stream-corrupt / macroblock errors, or a software decode error) the renderer writes one byte
  `K` on the tee socket, at most every 2 s (`ALTR_KEYFRAME_GAP_MS`). The CarPlay hook answers with
  `forceKeyFrame` through its serialized command queue instead of waiting for the next periodic
  key frame. A hook that predates this never reads the byte. The Android Auto tee records the
  request, and also when a renderer needs a key frame the tee dropped for its 8 MB budget.
- **Android Auto cluster key frame on demand** (opt-in, experimental; GEM button, marker
  `/mnt/app/root/aa_cluster.keyframe_refocus`): for such a request the cluster sink reports
  NATIVE and 200 ms later PROJECTED video focus, at most every 10 s, so the phone should restart
  the stream with a key frame instead of the cockpit waiting up to a minute or two. Untested on
  the cluster display, hence opt-in (a mid-session theme change dropped the session, #19). A
  request inside the 10 s gap is served when the gap is over; nothing is sent before the sink's
  own setup.
- **Pause the cluster map while the cockpit hides it** (opt-in, experimental, both phone
  types; GEM button, MMI *Pause Hidden Cluster Map*, marker
  `/mnt/app/root/cluster_video.pause_hidden`): while the VC shows another tab or the Audi map
  picked with a long roller press, CarPlay gets `stopUI` and Android Auto NATIVE video focus, so
  the phone stops drawing and encoding a map nobody sees; the map resumes when it is back. Java
  reports the state over the new bus command `CMD_ALT_HIDDEN` (0x0119) and, for Android Auto,
  `/tmp/aa_cluster_hidden`. The long roller press back to the phone map now also works while the
  phone video is not live (the paused stream is exactly that case); picking the Audi map still
  needs live video.
- **Android Auto switches in the MMI:** *Android Auto Map on Cluster* and *Android Auto Day/Night
  Map* join the CarPlay settings (*Connection manager → Options → CarPlay settings*), with the
  pause switch above; 14 languages. They write the same marker files as the GEM buttons.
- **Decoder and frame rate in the MMI:** *Hardware Video Decoder* (the GEM *HW decoder ON / OFF*
  files, both phone types; it also shows off when the renderer switched hardware off for this
  boot, and ticking it clears those markers for a fresh try) and *Cluster Map Frame Rate*, 30 or
  60. The frame rate file is the CarPlay hook's `mibr-carplay111-fps`; the Android Auto hook now
  reads it too and registers its cluster display at 60 fps when it says 60. The cockpit captures
  the map at 30 fps, so 60 does not show more pictures there; it doubles the decoder's work, some
  MHI2Q builds were reported to crash with CarPlay at 60, and Android Auto at 60 has not run on
  the car. Both apply at the next phone connection.
- **GEM *Cluster video status*:** a read-only page with the renderer's state, which decoder runs
  (`decoder=` hardware / software / suspended / frozen) and which marker keeps hardware off
  (`hw_off=`), frame, connection, key-frame request and damaged-picture counters, the stream-111
  counters of the CarPlay hook, the last Android Auto hook events and the opt-in switches set.
- **View change animation knob:** `updateViewArea` takes its `animationDurationMillis` from
  `/mnt/app/root/mibr-carplay111-view.animation` (0..1000, default 0 as before). Animation looked
  inert in an earlier trace, before `adjacentViewAreas`; this allows a retest without a build.

## Tests

- Second upstream port: `NavigationStackTest` (app names, default parity over every type /
  junction / angle, Baidu/AMap/Yandex semantics, schematic geometry, RHD fallback),
  `NativeBapReplayTest`, `NativeBapHandoffTest` (the real `ScreenNavStatusGate` on a controlled
  NavigationJobs queue) and `NativeBapStockListenerTest` (stock `CombiBAPListener` bytecode) run in
  `test_route_info.sh`; `rgd_timeout_race_test` gained source-change and late-name cases.
- `SANITIZE=1 scripts/run_tests.sh` builds every C test (and the HW301 bridge tests) with
  ASan + UBSan; the whole suite passes clean.
- New host tests in `run_tests.sh`: `tests/alt111_video_test.c` (stream-111 overflow skips to the
  next IDR, partly written AU kept, stalled and priming consumers reset), `tests/altscreen_policy_test.c`
  (bounded hardware starts, yield stamp, hang-only persistence, key-frame pacing, frozen picture
  on a new connection; the renderer's session decisions moved to `altscreen_render/policy.c` for
  it), `tests/aa_focus_test.c` (opt-in pause and refocus, nothing sent without the markers);
  `altscreen_view_test` covers the YUV matrices and the ruler, `aa_tee_test` the back-channel
  and the evicted-key-frame flag, a contract check that both hooks and Java agree on the pause
  marker and the hidden record. Java: `AltScreenLivenessTest` (seq heartbeat, wall-clock
  independent), `ScreenLongPressTest` (back to the phone map while paused);
  `CarPlaySettingsTest` / `SettingsModelTest` cover the three new settings.
- Review fixes before the first car test: the Android Auto tee reads its back-channel only when
  `select()` reports data (its socket is blocking and that thread sends every picture; no
  reliance on `MSG_DONTWAIT`); the monotonic condition variables fall back to the wall clock,
  for the variable and its deadlines alike, when the unit refuses `CLOCK_MONOTONIC`; the hook
  drops a closed tee client inside the lock hold that saw it, so a renderer reconnecting at that
  moment keeps its new connection; the hardware path looks the blitter's fill call up at run
  time, so a `libscreen` without it costs only the ruler.
- `tests/altscreen_nal_fuzz.c`: fuzz test of the two Annex-B scanners that see every byte
  from the phone, the hardware decoder's SPS/PPS split (`altscreen_render/nal.h`, moved out of
  `hw_decode.c` so the host can run it) and the Android Auto tee's key-frame gate. Structured
  AUs with an exact expected split plus random buffers, exact-size heap blocks;
  `ALTR_FUZZ_ITERS` sets the size (5 million per generator ran clean).

## Kept as in this fork

- Junction type still does not gate the arrow (MIBSI parity, issue #9). Upstream's `8bc44f0`
  fixes the same missing-junction case but keeps the MHI3 gate for supplied junction types;
  the upstream tests were adapted to this fork's mapping. The same goes for the new
  `NavigationStackTest` and `ManeuverFieldArrivalTest` cases: where upstream expects *follow
  road* for a roundabout type without junction 1, this fork shows the type's own arrow.
- Native map scale (FctID 45) and altitude (47) stay live during CarPlay guidance (route guidance
  is an overlay on the stock cluster map since `ab6962e`). Upstream's `8a6b220` hides and later
  restores them; here they pass through, and `NativeBapReplayTest` was adapted.
- Cluster phone tab dialling (`dialable()`, number-less favorites through InitiateCall) is
  unchanged; upstream's `resolveDialNumber` was not taken. The wider formatted-number check
  (NBSP, dots, direction marks) was.

## Not ported from upstream (second port)

- Audio, UpMix and Sound effects (`87b7f3f`, `f9d8b45`, `add09f3`, `2ba3573`, `1cd61d1`) and the
  installer's audio-path lines (`b63ec5e`): this fork does not carry upstream's audio hook,
  tone menu or installer, as in the first port.
- CN P1404 firmware support (`219bf3a`): no CN Java variant here.
- `showsInstruments` on the cluster display panel (`9861081`, upstream #78): it fixes iOS's
  `displayPanels` path, where `showsInstruments` defaults to NO. This fork advertises the cluster
  only in the legacy `displays[]` array, where iOS defaults it to YES (the HW301 iPhone syslog
  shows `using default showsInstruments: YES`), so there is nothing to fix.

## Not done

- `CMD_ALT_APPEARANCE` / `CMD_ALT_UICTX` (reserved MHI3 bus ids) stay without a handler: the
  CarPlay command format for a per-display appearance is not known. The phone's night mode is
  session-wide and already follows the MMI through stock `requestNightMode`.
- The renderer's main loop is still one function; only its file-based and pure decisions moved
  to `policy.c` with a host test. The OMX/GL paths are exercised on the car only.

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
