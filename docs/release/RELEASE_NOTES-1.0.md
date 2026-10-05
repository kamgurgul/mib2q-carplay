# mib2q-carplay 1.0

CarPlay on the Audi Virtual Cockpit for **MHI2Q** head units: the CarPlay map on the
cluster, turn-by-turn guidance on the cluster and head-up display, cover art, and a
Green Engineering Menu page to manage it all from the car.

> [!WARNING]
> This modifies firmware processes and system configuration on your head unit. Use it
> only on a unit you own, never install or test while driving, and accept that you do it
> at your own risk. Everything is reversible with **Uninstall** (stock files are backed up
> on the unit).

## Features

### CarPlay map on the Virtual Cockpit
- The navigation map of your CarPlay app (Apple Maps, Google Maps, Waze, ...) is shown on
  the Virtual Cockpit, next to CarPlay on the main screen.
- **Follows the cluster layout.** Switching the VC between the wide map and the classic
  view (big dials, small map window) moves the map's arrow and turn card into the visible
  area automatically.
- **Steering-wheel zoom.** The scroll wheel zooms the CarPlay map; the scale readout on
  the VC follows the steps.
- **The navigating app stays on the cluster** while you use another app on the main screen
  (iOS behaviour, kept intact).
- **Hardware video decoding** (Qualcomm decoder, about 8x less CPU than software). If it
  ever fails it switches itself off and the software decoder takes over.
- Leaving CarPlay returns the cluster to the native Audi map.

### Turn-by-turn and more (also without the cluster map)
- 3D maneuver arrow with lanes, distance, arrival time and remaining distance on the cluster.
- Route text on the VC (next road or exit sign); **OK** on the steering wheel toggles
  arrival time / time left.
- Maneuver icons, lanes and distance on the **head-up display**.
- **Cover art** on the cluster media screen.
- Parking (PDC) popups no longer hide CarPlay; the MMI touchpad works as a D-pad in CarPlay.

### Green Engineering Menu: "CarPlay-RGI"
Installed together with CarPlay. Every function is a button; no SD-card script swapping.

### Wireless CarPlay dongles
Dongles are detected and get standard CarPlay on the main screen. See *Known limitations*.

## Requirements

| | |
| --- | --- |
| Head unit | Audi **MHI2Q** with **Virtual Cockpit**. Developed and tested on an Audi A5 (B9) with MU1329. Update to the latest firmware first. |
| Tools | **M.I.B. (More Incredible Bash)** working on the unit, access to the Green Engineering Menu, a FAT32 SD card |
| Phone | iPhone with CarPlay. **The cluster map needs a USB cable** (not a wireless dongle). Tested with iOS 27. |

## What is on the SD card

```text
SD card root/
├─ mod/                     first install: M.I.B. "Run Custom Script"
│  ├─ custom.sh, command.sh   setup (installs CarPlay + the menu in one run)
│  ├─ install/                CarPlay installer
│  └─ menu/                   Green Engineering Menu installer
├─ carplay/                 the release (also used by the menu's "Install / update")
│  └─ 11 program files + VERSION
└─ (optional) carplay_safearea.conf, carplay_safearea_small.conf   your own layout values
```

`extras/MIB-mods/` in the download holds the individual M.I.B. scripts for use without the
menu (copy one `mod/` folder to the card at a time).

## Install (first time)

1. Back up the SD card's current `mod/` folder if it has one.
2. Copy the **contents of `SD-CARD/`** to the root of the M.I.B. SD card: `mod/` and
   `carplay/` must sit directly in the card root.
3. Disconnect CarPlay. Insert the card.
4. **Green Engineering Menu -> M.I.B. -> Advanced Settings -> Run Custom Script**
   (older M.I.B. release zips: **Run individual script**).
5. Wait for `DONE. mib2q-carplay 1.0 installed.` A `WARN` line names a step that was
   skipped; a `FAILED` line means nothing was changed.
6. Keep CarPlay disconnected, wait a few seconds, then **reboot the head unit normally**
   (not the forced button combination right after copying files).
7. Connect the iPhone by USB. The cluster shows the CarPlay map when navigation (or the map)
   is active; the Green Engineering Menu now has **CarPlay-RGI**.

## Update to a later release

1. Replace `carplay/` on the card with the new one (keep `mod/` only if the new release notes
   say the menu is unchanged; otherwise copy the new `mod/` too and run step 4 of *Install*).
2. Green Engineering Menu -> **CarPlay-RGI -> Install / update CarPlay**.
3. Reboot the head unit.

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
| **Cluster video (AltScreen) ON** | Offers the cluster screen to the phone (default). *Next connection.* |
| **Cluster video (AltScreen) OFF** | Stock CarPlay negotiation: no CarPlay map on the cluster (turn-by-turn arrow still works). *Next connection.* |
| **Route guidance ON** | Turn-by-turn arrow, route text and HUD guidance from CarPlay (default). |
| **Route guidance OFF** | Cluster/HUD guidance takeover off; the cluster map video is unaffected. |
| **Calibration grid ON / OFF (toggle)** | Draws a coloured ruler over the cluster map (lines every 60 px) to measure the visible area. |
| **Apply SafeArea from SD (carplay_safearea*.conf)** | Applies your own layout values from the card root: `carplay_safearea.conf` (wide map) and/or `carplay_safearea_small.conf` (classic view). Format: four lines `x=`, `y=`, `w=`, `h=`. *Next connection.* |
| **Reset SafeArea to built-in** | Back to the built-in layout values. *Next connection.* |
| **Classic-view SafeArea ON / OFF (2 ViewAreas)** | Toggles the separate classic-view layout (on by default). Off = one layout for both views. *Next connection.* |
| **Re-request cluster map (test app switching)** | Diagnostic: asks the phone for the cluster map again in the running session. |
| **HW decoder ON / OFF (toggle, default ON)** | Hardware or software decoding of the cluster video. *Next connection.* |
| **HW decoder probe (OMX, logs only)** | Diagnostic: logs the hardware decoder's capabilities; then **Save logs**. |
| **Copy stock lsd.jxe to SD** | Copies the stock HMI file to the card (read-only on the unit; needed only to build from source). |
| **Uninstall CarPlay (restore stock)** | Removes everything and restores the stock configuration. Reboot afterwards. |
| **Remove this menu** | Deletes the CarPlay-RGI page (gone after the next reboot). |

## Known limitations

- **Wireless dongles do not support the cluster map.** The tested dongle always identifies
  itself as an iPhone 7 on iOS 14.4, never requests the cluster stream and does not forward
  CarPlay route guidance, so it gets standard CarPlay on the main screen only. Use a USB
  cable for the cluster map.
- The turn-by-turn **arrow** needs an app that sends CarPlay route guidance (Apple Maps,
  Google Maps; Waze does not). The cluster **map** works with any CarPlay navigation app.
- The phone decides which navigation app is drawn on the cluster (the one navigating, else
  the last one used); the car cannot pick an app.
- The cluster map runs at the frame rate the phone sends (about 16-22 fps observed).
- Layout values are the built-in defaults for the B9 cluster (wide: 360,87 720x297;
  classic: 520,87 400x297). Other clusters may need their own values (see *Calibration grid*
  and *Apply SafeArea from SD*).

## Troubleshooting

| Symptom | Try |
| --- | --- |
| No CarPlay map on the cluster | USB cable (not a dongle); **Cluster video ON**; reconnect the phone. |
| Cluster map black | **HW decoder ON / OFF** to switch to software decoding; **Save logs**. |
| Dongle does not connect | Make sure this release is installed (dongles are detected automatically). |
| Anything else | **Save logs to SD** (twice, as described above) and report with the `carplay_logs/NNN` folder. |

## Credits and license

The biggest thanks go to
[Mr-MIBonk/M.I.B._More-Incredible-Bash](https://github.com/Mr-MIBonk/M.I.B._More-Incredible-Bash),
the base this mod is built on: every install, update and uninstall runs through M.I.B.

Also built on [luka-dev/mib2q-carplay-rgi](https://github.com/luka-dev/mib2q-carplay-rgi) (base
hook, Java patch, maneuver renderer, installer) and
[harman-f/mhi2_altscreen_carplay](https://github.com/harman-f/mhi2_altscreen_carplay) (cluster
video hook), with findings from
[joeyQuery/MHI2-altScreen](https://github.com/joeyQuery/MHI2-altScreen) and
[yuedizhibo/MHI2Q-CarPlay-AltScreen](https://github.com/yuedizhibo/MHI2Q-CarPlay-AltScreen).
See `LICENSE` and `THIRD_PARTY_NOTICES.md` in this download. `carplay_hook.jar` contains
modified classes of the Audi/e.solutions HMI and is not covered by the GPL (see the notices).
