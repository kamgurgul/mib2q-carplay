# mib2q-carplay 1.4

New in 1.4: **CarPlay settings in the MMI**, the Android Auto trip details, route guidance
through the HW301 and a fix for the freezing Android Auto cluster map.

> [!WARNING]
> This modifies firmware processes and system configuration on your head unit. Use it
> only on a unit you own, never install or test while driving, and accept that you do it
> at your own risk. Everything is reversible with **Uninstall** (stock files are backed up
> on the unit).

> [!IMPORTANT]
> **Run the M.I.B. setup again when updating.** One run installs the release and updates the
> Green Engineering Menu, which has new buttons and a changed *HW decoder* switch in 1.4. See
> [Update from 1.3.1 or older](#update-from-131-or-older).

## What's new in 1.4

### CarPlay settings in the MMI
- **Connection manager → Options → CarPlay settings** puts the CarPlay options on the MMI, with a
  short description of each item at the bottom of the screen, in 14 languages (other MMI
  languages show English). Adapted from luka-dev's 2026-10-29 release
  ([#42](https://github.com/kamgurgul/mib2q-carplay/issues/42)).

  | Item | Choices | Applies |
  | --- | --- | --- |
  | Maps on Cluster | On / Off | next connection |
  | Maneuver Cards Position | App default / Left / Center / Right | at once |
  | Route Guidance | On / Off (cluster and HUD, CarPlay and Android Auto) | at once |
  | Map Cards during Guidance | Hidden / Shown: hide the phone's turn card and ETA while the cockpit shows the car's own arrow | at once |
  | Guidance Maneuver Preview | Always next maneuver / Follow road when far (straight arrow until the turn is within 1.5 km, 3 km on motorways) | at once |
  | Turn card, Arrival time | On / Off | at once |
  | Speed limit, Compass | Off / On / Phone setting | at once |
  | All features in wide view | On / Off | at once |

- The MMI items and the Green Engineering Menu buttons change the same settings, so both always
  show the same state. If the new screen cannot load, the Connection Manager stays stock.

### Cluster map buttons
- **Cluster map compass ON / OFF** (default OFF).
- **Cluster map all features on wide view ON / OFF** (default OFF): the wide cockpit view shows
  the turn card, ETA, speed limit and compass whatever the four switches say; the small maps keep
  using the switches. Keep it OFF with Yanosik: every wide ↔ small switch then asks the phone to
  restart the cluster map, and Yanosik restarts its CarPlay screens, so the cluster map stops and
  the app on the MMI may close (Apple Maps and Google Maps just redraw).

### CarPlay cluster map at 30 FPS
- The car now asks the iPhone for the 30 FPS Maps profile, so Apple Maps draws the cluster map
  more smoothly (about 30 instead of 20 pictures a second; the real rate depends on the phone and
  app). Off switch: `/mnt/app/root/carplay_maps30.off`.

### Android Auto
- **Destination, remaining distance and arrival time** from Google Maps reach the cockpit and the
  HUD, as with CarPlay.
- **The cluster map no longer freezes for 1-2 minutes at a time**
  ([#44](https://github.com/kamgurgul/mib2q-carplay/issues/44)). After one hardware decoder
  failure the map stayed on software decoding for good, and software decoding cannot keep up
  with Android Auto's stream. Now a failure turns hardware decoding off only until the next reboot,
  and **HW decoder ON / OFF** switches Android Auto as well as CarPlay. **If your Android Auto
  map freezes: press HW decoder ON once** (if it says OFF, press it again).
- **Cluster map on more firmware.** The map needs a known Android Auto receiver
  (`libautoreceiver.so`); the `gal` program next to it no longer has to be a known build
  ([#28](https://github.com/kamgurgul/mib2q-carplay/issues/28)). An
  untested `gal` runs with a crash guard: if it crashes before the first cluster picture, the next
  Android Auto start stays stock until a reboot.
- Korean firmware `MHI2Q_KR_AUG22_P1201` is built in
  ([#33](https://github.com/kamgurgul/mib2q-carplay/issues/33)).

### HW301 wireless dongle
- **Image v1176**: the car's cluster settings reach the dongle (the cluster map switches, the
  SafeAreas and the wide / classic / Sport switch), and **route guidance works through the
  dongle**: maneuver arrow, lanes and HUD as with a cable. Update the head unit first, then flash
  the dongle ([HW301 guide](https://github.com/kamgurgul/mib2q-carplay/blob/master/docs/deploy/hw301-dongle.md)).

### Fixed
- **Audi navigation resumed an old route after standby.** Phone guidance no longer marks Audi
  navigation's own guidance as active, and the first connect clears a stale entry left by older
  versions. The car's guidance is also stopped only when it is actually running.
- **Waze ETA missing in the cluster map until the switch was toggled**
  ([#35](https://github.com/kamgurgul/mib2q-carplay/issues/35)).
- **CarPlay cluster map fell back to the Audi map with Apple Maps and no route**
  ([#39](https://github.com/kamgurgul/mib2q-carplay/issues/39)).
- **Black main screen, then a reboot, after switching screens while driving.** The cluster map's
  decoder now gives way while the main screen's video starts or stops; the cockpit keeps the last
  picture meanwhile.
- **Route guidance or Now Playing messages could be corrupted** when the phone and the car sent at
  the same moment.
- **A single damaged picture no longer stops the cluster map** with the hardware decoder.
- **Cover art of the previous track** stayed on the cockpit when the next track has none.
- **Phone tab stuck on "Ending call"**, or a finished call that stayed on the cockpit.
- **Play / pause bouncing** when the volume is turned down to 0.
- **Dialling a favourite or recent from the cockpit** did nothing while CarPlay was in the
  background on the MMI; it now opens CarPlay and starts the call.
- **Touchpad:** a vertical swipe no longer jumps sideways at the end of a list.
- **Android Auto: the left roller press** now toggles arrival time / time left on the
  cockpit's route line, and a long press switches between the phone map and the Audi map, as
  with CarPlay. The press no longer also acts as Select on the phone.
- **Android Auto without the hardware decoder** decodes on two threads, so the map keeps up.

The stale-route, message, damaged-picture and cover art fixes come from
[luka-dev/mib2q-carplay-rgi](https://github.com/luka-dev/mib2q-carplay-rgi)'s 2026-10-29 release.

## Not yet tested in a car

These passed every bench test, but have not been driven yet. Please report how they behave
(with **Save logs**):

- The MMI CarPlay settings screen, on every firmware
- Android Auto: destination / distance / ETA, the #44 fix, the crash guard on an untested `gal`
- The 30 FPS Maps profile, the phone tab and volume-0 fixes, dialling from the cockpit with
  CarPlay in the background
- The stale-route fix: start a phone route, let the unit go to standby, start again; Audi
  navigation must not resume a route
- All features in wide view, Maneuver Cards Position, Map Cards during Guidance
- HW301 v1176: layout switching, the menu switches, unplug/replug

## Update from 1.3.1 or older

1. Copy the new `SD-CARD/` contents to the root of the M.I.B. SD card: `carplay/` **and**
   `mod/`, replacing the old ones.
2. Disconnect the phone. Insert the card.
3. **Green Engineering Menu -> M.I.B. -> Advanced Settings -> Run Custom Script**
   (older M.I.B. release zips: **Run individual script**). The menu's **Install / update
   CarPlay** button updates the program files but not the menu, so the new and changed buttons
   appear only after this M.I.B. run. From 1.2 or older it also completes the message IDs in
   `dio_manager.json` (it only adds what is missing; **Uninstall** restores the original file).
4. Wait for `DONE. mib2q-carplay 1.4 installed.` A `WARN` line names a step that was skipped; a
   `FAILED` line means nothing was changed.
5. Keep the phone disconnected, wait a few seconds, then **reboot the head unit normally** (not
   the forced button combination right after copying files).
6. Connect the phone. **Connection manager → Options** now has **CarPlay settings**; if it is
   missing, reboot once more (the new screen loads at start).
7. Android Auto users whose cluster map froze in 1.3 / 1.3.1: press **HW decoder ON** once.
8. HW301 only: after the head unit, flash `hw301_v1176.tar` as described in the
   [HW301 guide](https://github.com/kamgurgul/mib2q-carplay/blob/master/docs/deploy/hw301-dongle.md)
   (needs a computer on the dongle's Wi-Fi and a bench USB supply). The image contains the
   dongle's stock firmware with an added library and is provided for owners of that dongle only.

Full list of changes: [CHANGELOG-1.4](https://github.com/kamgurgul/mib2q-carplay/blob/master/docs/release/CHANGELOG-1.4.md).
First install, uninstall, the menu buttons, credits and licenses:
[README](https://github.com/kamgurgul/mib2q-carplay#readme), `LICENSE` and
`THIRD_PARTY_NOTICES.md` in this download.
