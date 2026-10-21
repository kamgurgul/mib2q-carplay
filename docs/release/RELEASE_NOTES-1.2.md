# mib2q-carplay 1.2

CarPlay and Android Auto on the Audi Virtual Cockpit for **MHI2Q** head units: the phone's own
map on the cluster, turn-by-turn guidance on the cluster and head-up display, cover art, and now
**phone calls on the cluster phone tab** and **car data for the iPhone**.

> [!WARNING]
> This modifies firmware processes and system configuration on your head unit. Use it
> only on a unit you own, never install or test while driving, and accept that you do it
> at your own risk. Everything is reversible with **Uninstall** (stock files are backed up
> on the unit).

> [!IMPORTANT]
> **Run the installer again when updating from 1.1.** 1.2 registers new message IDs in
> `dio_manager.json`; without them the phone tab and the car data stay inactive. See
> [Update from 1.1](#update-from-11).

## What's new in 1.2

### Phone calls on the Virtual Cockpit
- **CarPlay calls on the cluster phone tab.** Instead of "Your iPhone is in Apple CarPlay mode",
  the phone tab works as with a Bluetooth phone: caller name and number, call state and timer,
  accept / hang up, hold, mute and dialling from the tab or the steering wheel, recents and
  favorites, missed calls, signal and carrier. The MMI phone app skips the CarPlay popup and goes
  straight to CarPlay. There is no caller photo: the iPhone does not send one to the car.
- **Android Auto calls on the cluster phone tab.** The tab showed an "Android Auto" note although
  the car already had the call over Bluetooth. It now shows the normal call view.

### CarPlay
- **Car data to the iPhone:** wheel speed, remaining range, outside temperature and the low-fuel
  warning, for apps that use them. Set your engine type with one word (`gasoline`, `diesel`,
  `electric` or `cng`) in `/mnt/app/root/carplay_vehicle_engine`; the default is gasoline.
- **The cluster map follows the guidance source.** During Apple Maps or Google Maps guidance the
  phone streams the bare map, so its own turn card and arrival time no longer appear next to the
  cluster arrow. Without CarPlay route guidance (Waze, or no route) the phone draws its own turn
  card and arrival time into the cluster map, so Waze guidance now reaches the cockpit too.
- **Siri on a short press (opt-in).** New menu button: with it on, a short press of the steering-
  wheel voice key opens Siri while CarPlay is connected. Off by default: the short press keeps
  starting the car's own voice control, the long press opens Siri as before.
- **No more blink 2 minutes after start.** The cluster video starts on the hardware decoder at
  once. If your MMI froze when CarPlay connected right after a start
  ([#10](https://github.com/kamgurgul/mib2q-carplay/issues/10)), turn on the new menu button
  *HW decoder 2-min delayed start*.
- **Smoother cluster video**: the video renderer runs at a higher priority, so the system can
  no longer stall it for up to half a second.
- **"Follow road" far from the next turn**, like Audi navigation: the turn shows from 1.5 km in
  town and 3 km on the highway.
- **Frame rate and size overrides** for experiments: `/mnt/app/root/mibr-carplay111-fps` (5 to 60,
  default 30) and `/mnt/app/root/mibr-carplay111-physical` (`width height` in mm, default
  290 x 108). The cockpit captures its picture 30 times a second, so more than 30 fps is not
  smoother there; some MHI2Q builds crash at 60.

### Android Auto
- **Cluster map follows MMI day/night (opt-in, experimental).** New menu button: light map by
  day, dark by night. Off by default because the light theme crashed Android Auto on the phone
  in earlier tests; press the button again if that happens.

### Fixed
- **MMI froze and rebooted** when Android Auto started with a route already running right after
  an MMI start ([#14](https://github.com/kamgurgul/mib2q-carplay/issues/14)). The Android Auto
  cluster video now waits 2 minutes after a start before using the hardware decoder. A decoder
  that stops answering is switched off, and a watchdog stops the renderer before it can freeze
  the MMI.
- **No endless decoder retries** after a crash of the cluster video renderer: after 3 failed
  starts it decodes in software for 30 s, after 3 more the hardware decoder stays off until the
  next MMI start.
- **"Navigation is initializing"** no longer covers the CarPlay cluster map while the Audi
  navigation is still loading after a start.
- **No turn arrow with some third-party CarPlay apps** (e.g. Sygic).
- **Google Maps logo cut by the cockpit's bottom bar**: the built-in layout is 27 px shorter.
- **Stale arrow or text** could stay on the cockpit after a route ended, and a late route update
  could wipe the current route.
- **Removed:** the *Native Audi VC arrows* switch. It never worked on Q7 and A4/A5 cockpits: they
  reserve the arrow box and draw nothing in it. The 3D arrow is the only mode.

## Not yet tested in a car

These are new in 1.2 and passed every bench test, but have not been driven yet. Please report
how they behave (with **Save logs**):

- CarPlay and Android Auto calls on the cluster phone tab
- Car data to the iPhone, Siri on a short press
- Cluster map following the guidance source (Waze card, no duplicate Apple Maps card)
- Hardware decoder at once for CarPlay, the 2-minute delay for Android Auto, the start cooldown
- Android Auto cluster day/night, the shorter built-in layout

## What works and what does not

Developed on an Audi A5 (F5) with MU1329 (`MHI2Q_ER_AUG22_P5152`); also reported on
`MHI2Q_ER_AUG22_P5092` (MU1329).

| | CarPlay | Android Auto |
| --- | --- | --- |
| Phone's map on the Virtual Cockpit | ✅ | ✅ |
| Map follows the cockpit layout (wide / classic / sport) | ✅ | ✅ |
| Maneuver arrow, distance, lanes, route text on the VC | ✅ (apps with CarPlay route guidance) | ✅ |
| Waze guidance on the VC | ✅ new: Waze's own card in the cluster map | ✅ |
| Head-up display guidance | ✅ | ✅ |
| Cover art on the VC | ✅ | ✅ |
| Calls on the VC phone tab | ✅ new | ✅ new (over Bluetooth) |
| Car data to the phone | ✅ new | ❌ |
| Steering-wheel roller zooms the cluster map | ✅ | ❌ Android Auto has no zoom command for its cluster map |
| MMI touchpad as D-pad | ✅ (switchable) | stock Android Auto behaviour |
| Wireless adapter | ✅ with tested adapters (see below) | not tested |

**Wireless adapters** reported to work with the cluster map: Baseus Safe Journey (fast) and
Carlinkit 5.0 / 2Air (slow to start). Generic `smartBox-xxxx` dongles do not work with 1.2.
An HW301 `smartBox` dongle with custom firmware is supported from **1.3** ([HW301 guide](../deploy/hw301-dongle.md)).

## Requirements

| | |
| --- | --- |
| Head unit | Audi **MHI2Q** with **Virtual Cockpit**. Update to the latest firmware first. |
| Tools | **M.I.B. (More Incredible Bash)** working on the unit, access to the Green Engineering Menu, a FAT32 SD card |
| Phone | iPhone with CarPlay (USB cable or a tested wireless adapter), or an Android phone with Android Auto. For calls on the phone tab with Android Auto the phone must also be connected over Bluetooth (the normal Android Auto setup). |

**Android Auto cluster map on other firmware:** the map needs a profile for the unit's Android
Auto receiver (`gal`). MU1329 and MU0918 are built in. On other firmware Android Auto stays stock
on the main screen, while turn-by-turn and cover art still work. Send the folder from
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
5. Wait for `DONE. mib2q-carplay 1.2 installed.` A `WARN` line names a step that was
   skipped; a `FAILED` line means nothing was changed.
6. Keep the phone disconnected, wait a few seconds, then **reboot the head unit normally**
   (not the forced button combination right after copying files).
7. Connect the phone. The cluster shows the phone's map; the Green Engineering Menu now has
   **CarPlay-RGI**.

## Update from 1.1

1. Copy the new `SD-CARD/` contents to the card: `carplay/` **and** `mod/`, because the menu has
   three new buttons.
2. Run step 4 of *Install* once. This is required for 1.2: besides updating the release and the
   menu, it adds the new phone and car-data message IDs to `dio_manager.json`. It only adds what
   is missing; **Uninstall** restores the original file.
3. Reboot the head unit.

If you had turned on *Native Audi VC arrows* in 1.1, nothing is needed: 1.2 ignores that
setting.

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
| **Calibration grid ON / OFF (toggle)** | Draws a coloured ruler over the cluster map (lines every 60 px) to measure the visible area. |
| **Apply SafeArea from SD (carplay_safearea*.conf)** | CarPlay: applies your own layout values from the card root: `carplay_safearea.conf` (wide map) and/or `carplay_safearea_small.conf` (classic view). Format: four lines `x=`, `y=`, `w=`, `h=`. *Next connection.* |
| **Reset SafeArea to built-in** | Back to the built-in layout values. *Next connection.* |
| **Classic-view SafeArea ON / OFF (2 ViewAreas)** | CarPlay: toggles the separate classic-view layout (on by default). *Next connection.* |
| **Re-request cluster map (test app switching)** | CarPlay diagnostic: asks the phone for the cluster map again in the running session. |
| **HW decoder ON / OFF (toggle, default ON)** | Hardware or software decoding of the cluster video. *Next connection.* |
| **HW decoder 2-min delayed start ON / OFF** | **New.** CarPlay: software decoding for the first 2 minutes after an MMI start, then hardware (default OFF = hardware at once). Turn on if the MMI freezes when CarPlay connects right after a start. Android Auto always waits. *Next connection.* |
| **Android Auto cluster map ON / OFF** | Offers the second display to Android Auto (default ON). OFF = stock Android Auto; the arrow, HUD and cover art still work. *Next connection.* |
| **Android Auto cluster day/night ON / OFF** | **New, experimental.** Light Android Auto cluster map by day, dark by night (default OFF = always dark). Switches live. |
| **Touchpad as D-pad ON / OFF** | CarPlay: finger drags on the MMI touchpad move the selection (default ON). OFF = the touchpad goes to CarPlay as on the stock unit. *Next connection.* |
| **Siri on short voice-key press ON / OFF** | **New.** CarPlay: a short press of the steering-wheel voice key opens Siri (default OFF = the car's voice control). Applies at once. |
| **HW decoder probe (OMX, logs only)** | Diagnostic: logs the hardware decoder's capabilities; then **Save logs**. |
| **Copy stock lsd.jxe to SD** | Copies the stock HMI file to the card (read-only on the unit; needed only to build from source). |
| **Uninstall CarPlay (restore stock)** | Removes everything (CarPlay and Android Auto parts) and restores the stock configuration. Reboot afterwards. |
| **Remove this menu** | Deletes the CarPlay-RGI page (gone after the next reboot). |

**Settings without a button** (create the file over SSH, delete it to undo):

| File | Effect |
| --- | --- |
| `/mnt/app/root/carplay_vehicle_engine` | engine type sent to the iPhone: `gasoline` (default), `diesel`, `electric` or `cng` |
| `/mnt/app/carplay_aa_phone_tab.off` | Android Auto: keep the stock "Android Auto" note on the phone tab |
| `/mnt/app/root/mibr-carplay111-rgi-url.disabled` | CarPlay cluster map: always the plain map request of 1.1 |
| `/mnt/app/root/mibr-carplay111-fps` | CarPlay cluster frame rate, 5 to 60 (default 30) |
| `/mnt/app/root/mibr-carplay111-physical` | CarPlay cluster size in mm, `width height` (default `290 108`) |

## Known limitations

- **No zoom of the Android Auto cluster map.** Android Auto offers no zoom command for its
  cluster display. The roller zooms only the hidden Audi map, so the VC scale bar still moves.
- **Android Auto size and density** are fixed when the phone connects; layouts switch live.
- The CarPlay turn-by-turn **arrow** needs an app that sends CarPlay route guidance (Apple Maps,
  Google Maps, recent AMap). Waze shows its own card inside the cluster map instead.
- The phone decides which navigation app is drawn on the cluster; the car cannot pick one.
- **No caller photo** on the phone tab, and a mute changed on the iPhone during a call shows on the
  cockpit only after the call.
- Layout values are the built-in defaults for the B9 cluster. Other clusters may need their
  own values (see *Calibration grid* and *Apply SafeArea from SD*).

## Troubleshooting

| Symptom | Try |
| --- | --- |
| No CarPlay map on the cluster | **Cluster video ON**; reconnect the phone; with a wireless adapter, check the tested list above. |
| Cluster map flickers between the Audi map and black | **HW decoder ON / OFF** (software decoding); **Save logs** and report ([#18](https://github.com/kamgurgul/mib2q-carplay/issues/18)). |
| MMI freezes when CarPlay connects right after a start | **HW decoder 2-min delayed start ON**. |
| Phone tab still shows "Apple CarPlay mode" | Run the 1.2 installer again (it registers the phone message IDs), reboot. |
| No Android Auto map on the cluster | **Android Auto cluster map ON**; reconnect; **Save logs** and look for `init enabled` in `aa_cluster_hook.log` (no profile for your firmware yet if it says `unknown_receiver`). |
| No arrow / HUD | **Route guidance ON**; for Android Auto, start navigation in Android Auto itself. |
| Anything else | **Save logs to SD** (twice, as described above) and report with the `carplay_logs/NNN` folder. |

## Credits and license

The biggest thanks go to
[Mr-MIBonk/M.I.B._More-Incredible-Bash](https://github.com/Mr-MIBonk/M.I.B._More-Incredible-Bash),
the base this mod is built on: every install, update and uninstall runs through M.I.B.

Also built on [luka-dev/mib2q-carplay-rgi](https://github.com/luka-dev/mib2q-carplay-rgi) (base
hook, Java patch, maneuver renderer, installer; in 1.2 also the phone tab, car data, Siri short
press, the guidance-source cluster map and several fixes),
[harman-f/mhi2_altscreen_carplay](https://github.com/harman-f/mhi2_altscreen_carplay) (CarPlay
cluster video hook) and
[wasimlhr/mib2q-android-auto-cluster](https://github.com/wasimlhr/mib2q-android-auto-cluster)
(Android Auto receiver hook, navigation translation, layouts, cover-art converter), whose hook
adapts [chopinwong01/mhi2-android-auto-video-vc](https://github.com/chopinwong01/mhi2-android-auto-video-vc).
Findings from [joeyQuery/MHI2-altScreen](https://github.com/joeyQuery/MHI2-altScreen) and
[yuedizhibo/MHI2Q-CarPlay-AltScreen](https://github.com/yuedizhibo/MHI2Q-CarPlay-AltScreen).
See `LICENSE` and `THIRD_PARTY_NOTICES.md` in this download. `carplay_hook.jar` contains
modified classes of the Audi/e.solutions HMI and is not covered by the GPL (see the notices).
