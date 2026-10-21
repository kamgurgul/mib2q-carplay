# mib2q-carplay 1.3 - changelog

Changes since [1.2](CHANGELOG-1.2.md). Entries marked *car test pending* passed the bench
tests but have not been driven yet.

## New

- **HW301 wireless dongle: the CarPlay map on the Virtual Cockpit.** The HW301 is the
  `smartBox-xxxx` dongle with the setup page at `192.168.5.1`. With its stock firmware
  (v1.26 / v1.29) it shows CarPlay on the MMI only. The custom app image
  `dongle/hw301/fw/hw301_v1162.tar` (marker `v1162-20261020152434`) keeps that main screen and
  adds a dedicated iPhone cluster view on the Virtual Cockpit. It is not a copy of the MMI screen.
  The roller zooms the map, and the phone draws the turn card and ETA.
  - Requires this release on the head unit, with **Cluster video ON**. The head-unit hook now
    opens the cluster stream for this dongle.
  - Flash on a bench USB supply, never in the car. Roll back with `hw301_v129.tar --version 129`.
    If a flash leaves the dongle dead, it can be recovered over USB (FEL) with the committed
    restore image.
  - Steps: [HW301 guide](../deploy/hw301-dongle.md).

  Map, MMI and zoom were tested in the car with v1154. Turn card and ETA, the corrected safe
  area and layout switching came in later builds and are *car test pending*.
- **Cockpit layout switch resizes the dongle's cluster map.** Switching wide and classic sends
  the new view to the iPhone through the dongle, which the stock dongle cannot forward.
  *Car test pending.*
- **Long press of the left roller toggles the phone map and the Audi map** on the Virtual
  Cockpit (from PR #15). The short press now acts on release and still toggles the route-info
  line. The roller zooms whichever map is visible.
- **Dial favorites that have no number** from the cluster phone tab. They are dialled by their
  address-book entry. *Car test pending.*
- **Verbose logging ON / OFF** in the Green Engineering Menu. It stays on after a reboot
  (`/mnt/app/carplay_verbose`). *Save logs* still turns it on only until the next reboot. The
  boot-time diagnostics below are written only while it is on.

## Fixed

- **Native navigation late or dead after power-on with a CarPlay dongle** when CarPlay was
  the last audio source ([#10](https://github.com/kamgurgul/mib2q-carplay/issues/10),
  [#22](https://github.com/kamgurgul/mib2q-carplay/issues/22)). Navigation took 2-4 minutes
  to come up, or never came back. Two causes:
  - the HMI's early Media start request was dropped, and every later start waited out a 20 s timeout;
  - navigation's start-up threads then queued behind the projection session.

  Now:
  - the dropped request is re-sent: Media only, within the first 60 s, at most twice. Turn it off
    with `carplay_domain_rescue.off` on the SD-card root or in `/mnt/app/root`;
  - on the first session after power-on, navigation's start-up threads run at a raised priority
    until navigation is up or for at most 300 s. Only threads that were raised are restored.
    Turn it off with `/mnt/app/root/carplay_nav_boost.off`;
  - the renderers wait up to 300 s for navigation (was 90 s).

  *Car test pending.*
- **Lane guidance only at the start of a route.** Lane events that iOS sends ahead of time
  were deleted before they became active. The cache now behaves like iOS: entries last until
  the route resets, and passed events are dropped when the current lane moves. The HUD and the
  renderer use one shared lookup of the active lane.
- **CarPlay did not start, or a phone feature was rejected, after another tool trimmed
  `dio_manager.json`.** The installer now registers every message ID the hook relies on,
  including the stock groups (now playing, power, location, call state, communications, list
  updates). It moves an ID found in the wrong list: `0x5203` among the received IDs kept CarPlay
  from starting ([#23](https://github.com/kamgurgul/mib2q-carplay/issues/23)).
- **Vehicle data re-sent on every connect after iOS rejected it.** Vehicle status is now
  advertised only when `dio_manager.json` declares its three messages. Ported from mib2q-carplay-rgi.
- **The first cluster picture could be missed.** The cluster video renderer now starts with the
  phone session instead of waiting for native navigation. The maneuver overlay still waits for navigation.
- **Android Auto dropped when the cluster day/night theme changed during a session**
  ([#19](https://github.com/kamgurgul/mib2q-carplay/issues/19)). The theme is now chosen when the
  phone connects. A day/night change while connected waits for the next connection.
  *Car test pending.*
- **The touchpad D-pad switch applies from the next touch** instead of the next CarPlay connection.
  It is never applied in the middle of a drag.
- **Removed: the "Navigation is initializing" takeover from 1.2.** It did not clear the message
  in the car: the stock navigation module stays off until navigation itself is ready, so there was
  nothing to override. Ported from mib2q-carplay-rgi.

## Documentation and project

- README: `MHI2Q_US_AUG22_P5087` (Audi A5 Sportback B9 US, MU1316) added to the supported
  units ([#21](https://github.com/kamgurgul/mib2q-carplay/issues/21)). The HW301 is added to the
  tested wireless adapters (from 1.3, with the custom image).
- New [HW301 guide](../deploy/hw301-dongle.md): flash, back to stock, FEL recovery, troubleshooting.
- `dongle/hw301/`: the investigation notes are reduced to [FINDINGS.md](../../dongle/hw301/FINDINGS.md).
  The official stock images (v1.26, v1.29), the custom image v1162 (and the same v1162 with logging enabled, `hw301_v1160.tar`)
  and the FEL restore image are committed.
- Logging card also collects video-core fatal dumps, the tail of native navigation's console log,
  navigation and display-manager dumps; `sloginfo` runs first
  ([#22](https://github.com/kamgurgul/mib2q-carplay/issues/22)).
- [domain-start-rescue](../hmi/domain-start-rescue.md): why a late navigation start ends dead,
  with the healthy boot timeline.

## Updating from 1.2

Copy both `carplay/` **and** `mod/` to the card, because the menu has a new button. Run
**M.I.B. -> Advanced Settings -> Run Custom Script** once, then reboot. The installer must run
again: it completes the message IDs in `dio_manager.json` (it only adds what is missing).
For the HW301 cluster map, flash the dongle afterwards as described in the
[HW301 guide](../deploy/hw301-dongle.md).
