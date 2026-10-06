# mib2q-carplay 1.1

CarPlay **and Android Auto** on the Audi Virtual Cockpit for **MHI2Q** head units: the
phone's own map on the cluster, turn-by-turn guidance on the cluster and head-up display,
cover art, and a Green Engineering Menu page to manage it all from the car.

> [!WARNING]
> This modifies firmware processes and system configuration on your head unit. Use it
> only on a unit you own, never install or test while driving, and accept that you do it
> at your own risk. Everything is reversible with **Uninstall** (stock files are backed up
> on the unit).

## What's new in 1.1

### Android Auto support
- **Google Maps / Waze map on the Virtual Cockpit.** Android Auto gets a second display
  for the cluster: an independent map stream, separate from the main screen. It is shown
  1:1 on the VC (no scaling) and follows the cockpit layout (Large, Classic, Sport).
- **Turn-by-turn from Android Auto**: maneuver arrow, distance bar, lanes and route text on
  the VC and the **head-up display**, the same as CarPlay. This also works without the
  cluster map.
- **Android Auto cover art** on the VC media screen.
- **Audi navigation keeps working** while an Android phone is connected: the cluster and HUD
  are taken over only while an Android Auto route is running.
- CarPlay and Android Auto share one install; the head unit switches automatically with
  the phone that is connected.

### Other changes
- **New menu switches:** *Android Auto cluster map ON / OFF* and *Touchpad as D-pad ON /
  OFF*. With the touchpad switch off, the MMI touchpad goes to CarPlay the stock way.
- **Save logs** also copies the Android Auto receiver (`gal`, `libautoreceiver.so`) to the
  card once, so the cluster map can be enabled for other firmware.
- Installer: re-running it no longer adds blank lines to `smartphone_integrator.json`.

## What works and what does not

Tested on an Audi A5 (F5) with MU1329 (`MHI2Q_ER_AUG22_P5152`).

| | CarPlay | Android Auto |
| --- | --- | --- |
| Phone's map on the Virtual Cockpit | ✅ | ✅ |
| Map follows the cockpit layout (wide / classic / sport) | ✅ | ✅ |
| Maneuver arrow, distance, lanes, route text on the VC | ✅ | ✅ |
| Head-up display guidance | ✅ | ✅ |
| Cover art on the VC | ✅ | ✅ |
| **Steering-wheel roller zooms the cluster map** | ✅ | ❌ Android Auto has no zoom command for its cluster map. The roller only moves the VC scale bar. |
| MMI touchpad as D-pad | ✅ (switchable) | stock Android Auto behaviour |
| Wireless adapter | main screen only (see limitations) | not tested |

## Requirements

| | |
| --- | --- |
| Head unit | Audi **MHI2Q** with **Virtual Cockpit**. Developed and tested on an Audi A5 (F5) with MU1329. Update to the latest firmware first. |
| Tools | **M.I.B. (More Incredible Bash)** working on the unit, access to the Green Engineering Menu, a FAT32 SD card |
| Phone | iPhone with CarPlay (**USB cable** for the cluster map; tested with iOS 27), or an Android phone with Android Auto |

**Android Auto cluster map on other firmware:** the map needs a profile for the unit's Android
Auto receiver (`gal`). MU1329 and MU0918 are built in. On other firmware Android Auto stays
stock on the main screen, while turn-by-turn and cover art still work. Send the folder from
**Save logs** to get a profile added.

## What is on the SD card

```text
SD card root/
├─ mod/                     first install: M.I.B. "Run Custom Script"
│  ├─ custom.sh, command.sh   setup (installs everything + the menu in one run)
│  ├─ install/                installer
│  └─ menu/                   Green Engineering Menu installer
├─ carplay/                 the release (also used by the menu's "Install / update")
│  └─ 14 program files + VERSION
└─ (optional) carplay_safearea.conf, carplay_safearea_small.conf   your own layout values
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
5. Wait for `DONE. mib2q-carplay 1.1 installed.` A `WARN` line names a step that was
   skipped; a `FAILED` line means nothing was changed.
6. Keep the phone disconnected, wait a few seconds, then **reboot the head unit normally**
   (not the forced button combination right after copying files).
7. Connect the phone. The cluster shows the phone's map; the Green Engineering Menu now has
   **CarPlay-RGI**.

## Update from 1.0

1. Copy the new `SD-CARD/` contents to the card: `carplay/` **and** `mod/`, because the menu
   has new buttons.
2. Run step 4 of *Install* once (this updates the release and the menu together).
3. Reboot the head unit.

The installer now also switches the Android Auto launcher in `smartphone_integrator.json`.
Uninstall restores the original file.

## Uninstall

Green Engineering Menu -> CarPlay-RGI -> **Uninstall CarPlay (restore stock)**, then
**Remove this menu**, then reboot. Without the menu: copy `extras/MIB-mods/uninstall/mod/`
to the card as `mod/` and run it from M.I.B. Stock configuration files are restored from
the backups the installer made.

## Green Engineering Menu: CarPlay-RGI

Settings marked *next connection* apply when the phone is reconnected; no reboot needed.

| Button | What it does |
| --- | --- |
| **Install / update CarPlay (release on SD)** | Installs the release from `carplay/` (or `mod/carplay/`) on the card. Reboot afterwards. |
| **Save logs to SD (+ verbose on)** | Saves all logs to `carplay_logs/NNN/` on the card and switches detailed logging on. For a bug report: press it, reconnect, reproduce, press it again, send the newest folder. |
| **Cluster video (AltScreen) ON / OFF** | CarPlay: offer the cluster screen to the iPhone (default ON). OFF = no CarPlay map on the cluster; the arrow still works. *Next connection.* |
| **Route guidance ON / OFF** | Turn-by-turn arrow, route text and HUD guidance, for CarPlay and Android Auto (default ON). |
| **Android Auto cluster map ON / OFF** | **New.** Offers the second display to Android Auto (default ON). OFF = stock Android Auto; the arrow, HUD and cover art still work. *Next connection.* |
| **Touchpad as D-pad ON / OFF** | **New.** CarPlay: finger drags on the MMI touchpad move the selection (default ON). OFF = the touchpad goes to CarPlay as on the stock unit. *Next connection.* |
| **Calibration grid ON / OFF (toggle)** | Draws a coloured ruler over the cluster map (lines every 60 px) to measure the visible area. |
| **Apply SafeArea from SD (carplay_safearea*.conf)** | CarPlay: applies your own layout values from the card root: `carplay_safearea.conf` (wide map) and/or `carplay_safearea_small.conf` (classic view). Format: four lines `x=`, `y=`, `w=`, `h=`. *Next connection.* |
| **Reset SafeArea to built-in** | Back to the built-in layout values. *Next connection.* |
| **Classic-view SafeArea ON / OFF (2 ViewAreas)** | CarPlay: toggles the separate classic-view layout (on by default). *Next connection.* |
| **Re-request cluster map (test app switching)** | CarPlay diagnostic: asks the phone for the cluster map again in the running session. |
| **HW decoder ON / OFF (toggle, default ON)** | CarPlay: hardware or software decoding of the cluster video. *Next connection.* |
| **HW decoder probe (OMX, logs only)** | Diagnostic: logs the hardware decoder's capabilities; then **Save logs**. |
| **Copy stock lsd.jxe to SD** | Copies the stock HMI file to the card (read-only on the unit; needed only to build from source). |
| **Uninstall CarPlay (restore stock)** | Removes everything (CarPlay and Android Auto parts) and restores the stock configuration. Reboot afterwards. |
| **Remove this menu** | Deletes the CarPlay-RGI page (gone after the next reboot). |

## Known limitations

- **No zoom of the Android Auto cluster map.** Android Auto offers no zoom command for its
  cluster display and ignores the roller there. The roller zooms only the hidden Audi map,
  so the VC scale bar still moves.
- **Android Auto cluster map is always dark.** The light theme on the cluster display
  crashed Android Auto on the phone in upstream testing.
- **Android Auto size and density** are fixed when the phone connects; layouts switch live.
- **Wireless CarPlay dongles do not support the cluster map.** The tested dongle always
  identifies itself as an iPhone 7 on iOS 14.4, never requests the cluster stream and does
  not forward CarPlay route guidance, so it gets standard CarPlay on the main screen only.
- The CarPlay turn-by-turn **arrow** needs an app that sends CarPlay route guidance (Apple
  Maps, Google Maps; Waze does not). The cluster **map** works with any CarPlay navigation app.
- The phone decides which navigation app is drawn on the cluster; the car cannot pick one.
- Layout values are the built-in defaults for the B9 cluster. Other clusters may need their
  own values (see *Calibration grid* and *Apply SafeArea from SD*).

## Troubleshooting

| Symptom | Try |
| --- | --- |
| No CarPlay map on the cluster | USB cable (not a dongle); **Cluster video ON**; reconnect the phone. |
| No Android Auto map on the cluster | **Android Auto cluster map ON**; reconnect; **Save logs** and look for `init enabled` in `aa_cluster_hook.log` (no profile for your firmware yet if it says `unknown_receiver`). |
| Cluster map black | CarPlay: **HW decoder ON / OFF**; **Save logs**. |
| Android Auto drops after connecting | **Android Auto cluster map OFF** (stock Android Auto), then **Save logs** and report. |
| No arrow / HUD | **Route guidance ON**; for Android Auto, start navigation in Android Auto itself. |
| Anything else | **Save logs to SD** (twice, as described above) and report with the `carplay_logs/NNN` folder. |

## Credits and license

The biggest thanks go to
[Mr-MIBonk/M.I.B._More-Incredible-Bash](https://github.com/Mr-MIBonk/M.I.B._More-Incredible-Bash),
the base this mod is built on: every install, update and uninstall runs through M.I.B.

Also built on [luka-dev/mib2q-carplay-rgi](https://github.com/luka-dev/mib2q-carplay-rgi) (base
hook, Java patch, maneuver renderer, installer),
[harman-f/mhi2_altscreen_carplay](https://github.com/harman-f/mhi2_altscreen_carplay) (CarPlay
cluster video hook) and
[wasimlhr/mib2q-android-auto-cluster](https://github.com/wasimlhr/mib2q-android-auto-cluster)
(Android Auto receiver hook, navigation translation, layouts, cover-art converter), whose hook
adapts [chopinwong01/mhi2-android-auto-video-vc](https://github.com/chopinwong01/mhi2-android-auto-video-vc).
Findings from [joeyQuery/MHI2-altScreen](https://github.com/joeyQuery/MHI2-altScreen) and
[yuedizhibo/MHI2Q-CarPlay-AltScreen](https://github.com/yuedizhibo/MHI2Q-CarPlay-AltScreen).
See `LICENSE` and `THIRD_PARTY_NOTICES.md` in this download. `carplay_hook.jar` contains
modified classes of the Audi/e.solutions HMI and is not covered by the GPL (see the notices).
