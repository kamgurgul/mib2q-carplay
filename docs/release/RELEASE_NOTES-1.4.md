# mib2q-carplay 1.4

CarPlay and Android Auto on the Audi Virtual Cockpit for **MHI2Q** head units: the phone's own
map on the cluster, turn-by-turn guidance on the cluster and head-up display, cover art, calls on
the cluster phone tab, and the CarPlay cluster map through the HW301 wireless dongle. New in 1.4:
**CarPlay settings in the MMI**, the Android Auto trip details, route guidance through the HW301
and a fix for the freezing Android Auto cluster map.

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
  using the switches. See [Known limitations](#known-limitations) for Yanosik.

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
  (`libautoreceiver.so`); the `gal` program next to it no longer has to be a known build. An
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

## What works and what does not

Developed on an Audi A5 (F5) with MU1329 (`MHI2Q_ER_AUG22_P5152`); also reported on
`MHI2Q_ER_AUG22_P5092` (MU1329), `MHI2Q_US_AUG22_P5087` (MU1316) and `MHI2Q_KR_AUG22_P1201`
(MU1201).

| | CarPlay | Android Auto |
| --- | --- | --- |
| Phone's map on the Virtual Cockpit | ✅ | ✅ |
| Map follows the cockpit layout (wide / classic / sport) | ✅ | ✅ |
| Maneuver arrow, distance, lanes, route text on the VC | ✅ (apps with CarPlay route guidance) | ✅ |
| Destination, distance and arrival time on the VC | ✅ | ✅ (new) |
| Waze guidance on the VC | ✅ Waze's own card in the cluster map | ✅ |
| Head-up display guidance | ✅ | ✅ |
| Cover art on the VC | ✅ | ✅ |
| Calls on the VC phone tab | ✅ | ✅ (over Bluetooth) |
| Car data to the phone | ✅ | ❌ |
| Steering-wheel roller zooms the cluster map | ✅ | ❌ Android Auto has no zoom command for its cluster map |
| MMI touchpad as D-pad | ✅ (switchable) | stock Android Auto behaviour |
| Settings in the MMI | ✅ (new) | Route Guidance and Maneuver Preview |
| Wireless adapter | ✅ with tested adapters (see below) | not tested |

**Wireless adapters** reported to work with the cluster map: Baseus Safe Journey (fast),
Carlinkit 5.0 / 2Air (slow to start), AAWireless TWO+ (manufacturer test build) and the
**HW301** with the custom image v1167 or newer (v1176 adds route guidance). Other generic
`smartBox-xxxx` dongles do not work.

## Requirements

| | |
| --- | --- |
| Head unit | Audi **MHI2Q** with **Virtual Cockpit**. Update to the latest firmware first. |
| Tools | **M.I.B. (More Incredible Bash)** working on the unit, access to the Green Engineering Menu, a FAT32 SD card |
| Phone | iPhone with CarPlay (USB cable or a tested wireless adapter), or an Android phone with Android Auto. For calls on the phone tab with Android Auto the phone must also be connected over Bluetooth (the normal Android Auto setup). |
| HW301 only | a computer on the dongle's Wi-Fi and a bench USB supply to flash `hw301_v1176.tar` |

**Android Auto cluster map on other firmware:** the map needs a known Android Auto receiver
(`libautoreceiver.so`). The MU0918 receiver, shared by MU1329, MU1201 and MU1316, is built in.
On other receivers Android Auto stays stock on the main screen, while turn-by-turn and cover art
still work. Send the folder from **Save logs** to get a profile added.

## What is on the SD card

```text
SD card root/
├─ mod/                     M.I.B. "Run Custom Script"
│  ├─ custom.sh, command.sh   setup: installs the release and the menu in one run
│  ├─ install/                installer
│  └─ menu/                   Green Engineering Menu installer
├─ carplay/                 the release (also used by the menu's "Install / update")
│  └─ 15 program files + VERSION
└─ (optional) carplay_safearea.conf, carplay_safearea_small.conf, carplay_safearea_sport.conf
```

`extras/MIB-mods/` in the download holds the individual M.I.B. scripts for use without the
menu (copy one `mod/` folder to the card at a time).

## Install (first time)

1. Back up the SD card's current `mod/` folder if it has one.
2. Copy the **contents of `SD-CARD/`** to the root of the M.I.B. SD card: `mod/` and
   `carplay/` must sit directly in the card root.
3. Disconnect the phone. Insert the card.
4. **Green Engineering Menu -> M.I.B. -> Advanced Settings -> Run Custom Script**
   (older M.I.B. release zips: **Run individual script**).
5. Wait for `DONE. mib2q-carplay 1.4 installed.` A `WARN` line names a step that was
   skipped; a `FAILED` line means nothing was changed.
6. Keep the phone disconnected, wait a few seconds, then **reboot the head unit normally**
   (not the forced button combination right after copying files).
7. Connect the phone. The cluster shows the phone's map; the Green Engineering Menu has
   **CarPlay-RGI**, and **Connection manager → Options** has **CarPlay settings**.

For the HW301, flash the dongle afterwards as described in the
[HW301 guide](https://github.com/kamgurgul/mib2q-carplay/blob/master/docs/deploy/hw301-dongle.md).

## Update from 1.3.1 or older

1. Copy the new `SD-CARD/` contents to the card: `carplay/` **and** `mod/`, replacing the old
   ones.
2. Run step 4 of *Install* once. The menu's **Install / update CarPlay** button updates the
   program files but not the menu, so the new and changed buttons appear only after this M.I.B.
   run. From 1.2 or older it also completes the message IDs in `dio_manager.json` (it only adds
   what is missing; **Uninstall** restores the original file).
3. Reboot the head unit.
4. Android Auto users whose cluster map froze in 1.3 / 1.3.1: press **HW decoder ON** once.

## Uninstall

Green Engineering Menu -> CarPlay-RGI -> **Uninstall CarPlay (restore stock)**, then
**Remove this menu**, then reboot. Without the menu: copy `extras/MIB-mods/uninstall/mod/`
to the card as `mod/` and run it from M.I.B. Stock configuration files are restored from
the backups the installer made. The HW301 goes back to stock with `hw301_v129.tar`
(see the HW301 guide).

## Green Engineering Menu: CarPlay-RGI

Settings marked *next connection* apply when the phone is reconnected; no reboot needed. Most of
the CarPlay settings are also in **Connection manager → Options → CarPlay settings**.

| Button | What it does |
| --- | --- |
| **Install / update CarPlay (release on SD)** | Installs the release from `carplay/` (or `mod/carplay/`) on the card. Reboot afterwards. |
| **Save logs to SD (+ verbose on)** | Saves all logs to `carplay_logs/NNN/` on the card and switches detailed logging on until the next reboot. For a bug report: press it, reconnect, reproduce, press it again, send the newest folder. |
| **Verbose logging ON / OFF** | Detailed logging that stays on after a reboot (default OFF). Needed to capture start-up problems. |
| **Cluster video (AltScreen) ON / OFF** | CarPlay: offer the cluster screen to the iPhone (default ON). OFF = no CarPlay map on the cluster; the arrow still works. Also needed for the HW301. *Next connection.* |
| **Route guidance ON / OFF** | Turn-by-turn arrow, route text and HUD guidance, for CarPlay and Android Auto (default ON). |
| **Calibration grid ON / OFF (toggle)** | Draws a coloured ruler over the cluster map (lines every 60 px) to measure the visible area. |
| **Apply SafeArea from SD (carplay_safearea*.conf)** | CarPlay: applies your own layout values from the card root: `carplay_safearea.conf` (wide map), `carplay_safearea_small.conf` (classic small map) and/or `carplay_safearea_sport.conf` (Sport skin small map). Format: four lines `x=`, `y=`, `w=`, `h=`. *Next connection.* |
| **Reset SafeArea to built-in** | Back to the built-in layout values. *Next connection.* |
| **Small-map SafeAreas ON / OFF (3 ViewAreas)** | CarPlay: toggles the separate classic and Sport small-map layouts (on by default). *Next connection.* |
| **Cluster map turn card ON / OFF** | CarPlay: the app's turn card in the cluster map (default ON). OFF = bare map, e.g. while the car draws its own arrow. Applies at once with CarPlay connected, else *next connection*. |
| **Cluster map ETA ON / OFF** | CarPlay: the app's arrival time in the cluster map (default OFF). Applies like the turn card. |
| **Cluster map speed limit ON / OFF** | CarPlay: the app's speed limit sign in the cluster map (default OFF). Applies like the turn card. |
| **Cluster map compass ON / OFF** | **New.** CarPlay: the app's compass in the cluster map (default OFF). Applies like the turn card. |
| **Cluster map all features on wide view ON / OFF** | **New.** CarPlay: the wide view shows turn card, ETA, speed limit and compass; the small maps follow the four switches (default OFF). |
| **Re-request cluster map (test app switching)** | CarPlay diagnostic: asks the phone for the cluster map again in the running session. |
| **HW decoder ON / OFF (toggle, default ON)** | **Changed.** Hardware or software decoding of the cluster video, now for CarPlay **and Android Auto**. ON also gives a decoder that failed earlier a fresh try. *Next connection.* |
| **HW decoder 2-min delayed start ON / OFF** | CarPlay: software decoding for the first 2 minutes after an MMI start, then hardware (default OFF = hardware at once). Turn on if the MMI freezes when CarPlay connects right after a start. Android Auto always waits. *Next connection.* |
| **Android Auto cluster map ON / OFF** | Offers the second display to Android Auto (default ON). OFF = stock Android Auto; the arrow, HUD and cover art still work. *Next connection.* |
| **Android Auto cluster day/night ON / OFF** | Experimental. Light Android Auto cluster map by day, dark by night (default OFF = always dark). Picked when the phone connects. |
| **Touchpad as D-pad ON / OFF** | CarPlay: finger drags on the MMI touchpad move the selection (default ON). OFF = the touchpad goes to CarPlay as on the stock unit. Applies from the next touch. |
| **Siri on short voice-key press ON / OFF** | CarPlay: a short press of the steering-wheel voice key opens Siri (default OFF = the car's voice control). Applies at once. |
| **HW decoder probe (OMX, logs only)** | Diagnostic: logs the hardware decoder's capabilities; then **Save logs**. |
| **Copy stock lsd.jxe to SD** | Copies the stock HMI file to the card (read-only on the unit; needed only to build from source). |
| **Uninstall CarPlay (restore stock)** | Removes everything (CarPlay and Android Auto parts) and restores the stock configuration. Reboot afterwards. |
| **Remove this menu** | Deletes the CarPlay-RGI page (gone after the next reboot). |

**Settings without a button** (create the file over SSH, delete it to undo):

| File | Effect |
| --- | --- |
| `/mnt/app/root/carplay_vehicle_engine` | engine type sent to the iPhone: `gasoline` (default), `diesel`, `electric` or `cng` |
| `/mnt/app/carplay_aa_phone_tab.off` | Android Auto: keep the stock "Android Auto" note on the phone tab |
| `/mnt/app/root/mibr-carplay111-rgi-url.disabled` | CarPlay cluster map: always the plain map request of 1.1 (the cluster map buttons then have no effect) |
| `/mnt/app/root/mibr-carplay111-nav.showSpeedLimit` or `.showCompass` with `user` | CarPlay cluster speed limit / compass follows the iPhone's Maps setting (also *Phone setting* in the MMI) |
| `/mnt/app/root/mibr-carplay111-fps` | CarPlay cluster frame rate, 5 to 60 (default 30) |
| `/mnt/app/root/mibr-carplay111-physical` | CarPlay cluster size in mm, `width height` (default `290 108`) |
| `/mnt/app/root/carplay_domain_rescue.off` (or `carplay_domain_rescue.off` on the card root) | No re-send of the dropped Media start request after power-on |
| `/mnt/app/root/carplay_nav_boost.off` | No priority boost for navigation's start-up after power-on |
| `/mnt/app/root/carplay_maps30.off` | **New.** Do not ask the iPhone for the 30 FPS Maps profile |

## Known limitations

- **Yanosik with *all features on wide view* ON.** That setting gives the wide and the small maps
  different cluster settings, so every wide ↔ small switch asks the phone to restart the cluster
  map. Apple Maps and Google Maps just redraw; Yanosik restarts its CarPlay screens, the cluster
  map stops and the app on the MMI may close. Keep *all features on wide view* OFF with Yanosik.
- **No zoom of the Android Auto cluster map.** Android Auto offers no zoom command for its
  cluster display. The roller zooms only the hidden Audi map, so the VC scale bar still moves.
- **Android Auto size and density** are fixed when the phone connects; layouts switch live.
- The CarPlay turn-by-turn **arrow** needs an app that sends CarPlay route guidance (Apple Maps,
  Google Maps, recent AMap). Waze shows its own card inside the cluster map instead.
- The phone decides which navigation app is drawn on the cluster; the car cannot pick one.
- **No caller photo** on the phone tab, and a mute changed on the iPhone during a call shows on the
  cockpit only after the call.
- **HW301:** the start of Siri's answer can be cut (the car opens Siri's audio late); under
  investigation. Images v1167 and older have the cluster settings built in and ignore the head
  unit's switches.
- **Short cluster map blinks** when iOS restarts its video encoder, e.g. when route guidance
  starts.
- Layout values are the built-in defaults for the B9 cluster. Other clusters may need their
  own values (see *Calibration grid* and *Apply SafeArea from SD*).

## Troubleshooting

| Symptom | Try |
| --- | --- |
| No CarPlay map on the cluster | **Cluster video ON** (MMI: *Maps on Cluster* On); reconnect the phone; with a wireless adapter, check the tested list above. |
| Turn card shown twice (phone card and car arrow) | **Cluster map turn card OFF**, or MMI *Map Cards during Guidance*: Hidden. |
| A cluster map switch has no effect | Reconnect the phone. Remove `mibr-carplay111-rgi-url.disabled` and other cluster URL test files in `/mnt/app/root`. |
| Cluster map stops and the app on the MMI closes when switching wide ↔ small view (Yanosik) | **Cluster map all features on wide view OFF**. |
| Android Auto cluster map freezes for a minute or more while the MMI map moves | **HW decoder ON** (press again if it reports OFF); reconnect. `altscreen_render.log` names the marker if hardware decoding is still off. |
| HW301: CarPlay on the MMI but no cockpit map | Head unit on 1.4 with **Cluster video ON**? `curl http://192.168.5.1/clbuild.txt` shows `v1176-…`? Reconnect the phone. |
| Audi navigation starts an old route by itself | Should be fixed in 1.4; if not, **Save logs** and report. |
| Native navigation slow after power-on with a dongle | Turn on **Verbose logging**, reboot, reproduce, **Save logs** and report. |
| Cluster map flickers between the Audi map and black | **HW decoder ON / OFF** (software decoding); **Save logs** and report ([#18](https://github.com/kamgurgul/mib2q-carplay/issues/18)). |
| MMI freezes when CarPlay connects right after a start | **HW decoder 2-min delayed start ON**. |
| No *CarPlay settings* in Connection manager → Options | Reboot after installing (the jar loads at start). Still missing: **Save logs** and report with your firmware version; the GEM buttons still work. |
| No Android Auto map on the cluster | **Android Auto cluster map ON**; reconnect; **Save logs** and look for `init enabled` in `aa_cluster_hook.log` (`unknown_receiver` = no profile for your firmware yet; `unpinned_failed_before` = the crash guard stopped it, reboot to try again). |
| No arrow / HUD | **Route guidance ON**; for Android Auto, start navigation in Android Auto itself. |
| Anything else | **Save logs to SD** (twice, as described above) and report with the `carplay_logs/NNN` folder. |

## Credits and license

The biggest thanks go to
[Mr-MIBonk/M.I.B._More-Incredible-Bash](https://github.com/Mr-MIBonk/M.I.B._More-Incredible-Bash),
the base this mod is built on: every install, update and uninstall runs through M.I.B.

Also built on [luka-dev/mib2q-carplay-rgi](https://github.com/luka-dev/mib2q-carplay-rgi) (base
hook, Java patch, maneuver renderer, installer; in 1.4 also the CarPlay settings screen and its
translations, the stale-route fix, the iAP2 message fix, damaged-picture tolerance and the cover
art clear from its 2026-10-29 release),
[harman-f/mhi2_altscreen_carplay](https://github.com/harman-f/mhi2_altscreen_carplay) (CarPlay
cluster video hook) and
[wasimlhr/mib2q-android-auto-cluster](https://github.com/wasimlhr/mib2q-android-auto-cluster)
(Android Auto receiver hook, navigation translation, layouts, cover-art converter), whose hook
adapts [chopinwong01/mhi2-android-auto-video-vc](https://github.com/chopinwong01/mhi2-android-auto-video-vc).
Findings from [joeyQuery/MHI2-altScreen](https://github.com/joeyQuery/MHI2-altScreen) and
[yuedizhibo/MHI2Q-CarPlay-AltScreen](https://github.com/yuedizhibo/MHI2Q-CarPlay-AltScreen).
See `LICENSE` and `THIRD_PARTY_NOTICES.md` in this download. `carplay_hook.jar` contains
modified classes of the Audi/e.solutions HMI and is not covered by the GPL (see the notices).
`hw301_v1176.tar` contains the dongle's stock firmware with an added library; it is provided for
owners of that dongle only.

Full list of changes: [CHANGELOG-1.4](https://github.com/kamgurgul/mib2q-carplay/blob/master/docs/release/CHANGELOG-1.4.md).
