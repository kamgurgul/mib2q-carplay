# mib2q-carplay 1.4.1 - changelog

Changes since [1.4](CHANGELOG-1.4.md).

## Fixed

- **Android Auto dropped once about 2 minutes after an MMI start** (issue
  [#48](https://github.com/kamgurgul/mib2q-carplay/issues/48); since 1.2). With the phone connected
  before 120 s of uptime, the cluster map decoded in software and reconnected to the cluster
  stream at 120 s to switch to the hardware decoder. The tee in `gal` then sent the next picture
  to the closed connection. On QNX that raises `SIGPIPE`, and `gal` keeps the default action, so
  `gal` was killed: no log line, no core, `smartphone_integrator` restarted it at once on the
  still-connected phone, the new `gal` got no answer to its version request, and Android Auto
  reconnected about 15 s later (car log: `hardware wait over` at 120.0 s, new `gal` at 121.1 s
  without a session start, `device INVALID` at 126 s, new session at 134 s). The tee's writer
  thread now blocks `SIGPIPE`, so a closed or reset renderer connection costs only that
  connection. This also covers the renderer's other reconnects (main-screen yield, restart,
  crash). `tests/aa_tee_test.c` delivers `SIGPIPE` to the writer thread the way QNX does.
  *Car test pending.*

## Changed

- **Android Auto starts the cluster map on the hardware decoder at once, also right after an MMI
  start.** The 2-minute software start (1.2-1.4) protected against issue
  [#14](https://github.com/kamgurgul/mib2q-carplay/issues/14): with a route running, the cluster
  stream starts with the session, so the cluster decoder opened together with `gal`'s
  main-screen decoder and hung the shared video core. The gal hook now does what the CarPlay hook
  does since 1.4 (main-screen yield): before `gal`'s main-screen video sink is set up, configured
  (codec), started or stopped, and before the session shuts down
  (`GalReceiver::prepareShutdown`), it writes `/tmp/altscreen_render.hw_yield` and waits up to
  1.5 s for the renderer to release its decoder. A session's first cluster picture therefore
  meets the yield, and the cluster decoder opens 3 s after `gal`'s last main-screen change.
  `aa_cluster_hook.log`: `hw.yield why=setup renderer=no_hw_decoder` and so on.
  *Car test pending.*
- **GEM *HW decoder 2-min delayed start* now applies to Android Auto too** (default **OFF**,
  label *… (toggle, CarPlay + Android Auto, default OFF)*). ON brings back the old Android Auto
  behaviour: software decode for the first 2 minutes after an MMI start, then one switch to
  hardware. Turn it on if the MMI freezes when the phone connects right after a start.

## Not changed

- CarPlay: still hardware at once, with the same main-screen yield and the same delayed-start
  switch.
- Only the first cluster picture of a session waits for the yield. Video focus changes (MMI
  screen switches) announce nothing: `gal`'s decoder runs for the whole session, and a
  reconnect in the middle of a session would wait for Android Auto's next key frame, which can be
  a minute or two away.

## Documentation

- [hw-decoder](../altscreen/hw-decoder.md): the boot settle time is opt-in for both phone types;
  new *Main-screen yield, Android Auto* section.
- [Android Auto overview](../android-auto/overview.md), [altscreen-mhi2q](../deploy/altscreen-mhi2q.md)
  and the README settings table: the delayed-start marker for Android Auto.

## Updating from 1.4

Install as usual. A unit that had the delayed-start switch ON keeps it, and it now delays
Android Auto too.
