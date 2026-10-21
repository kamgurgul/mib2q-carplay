# mib2q-carplay 1.2 - changelog

Changes since [1.1](RELEASE_NOTES-1.1.md); user-facing summary in
[RELEASE_NOTES-1.2.md](RELEASE_NOTES-1.2.md). Entries marked *car test pending* passed the bench
tests but have not been driven yet.

## New

- **CarPlay calls on the cluster phone tab.** The Virtual Cockpit telephone tab now works as
  with a Bluetooth phone: caller name and number, call state and timer, accept / hang up, hold,
  mute and dialling from the tab or the steering wheel, recents and favorites, missed calls,
  signal and carrier. The HU phone app skips the "Your iPhone is in Apple CarPlay mode" popup.
  Ported from mib2q-carplay-rgi. *Car test pending.*
- **Android Auto calls on the cluster phone tab.** With Android Auto the phone tab showed the
  "Android Auto" note although the car's Bluetooth phone already had the call. It now shows the
  normal call view with the stock Bluetooth data (caller, state, accept / hang up, lists).
  `/mnt/app/carplay_aa_phone_tab.off` switches it off. *Car test pending.*
- **Car data to the iPhone.** Wheel speed, remaining range, outside temperature and the
  low-fuel warning go to the phone, for apps that use them. Engine type: one word in
  `/mnt/app/root/carplay_vehicle_engine` (default gasoline). Ported from mib2q-carplay-rgi.
  *Car test pending.*
- **Siri on a short press (opt-in).** With *Siri on short voice-key press ON / OFF* in the menu, a
  short press of the steering-wheel voice key opens Siri while CarPlay is connected (a long press
  always did). Off by default: the short press keeps starting the car's own voice control.
  Ported from mib2q-carplay-rgi. *Car test pending.*
- **AltScreen: the cluster map follows the guidance source.** During Apple Maps / Google Maps
  guidance the phone streams the bare map, so its turn card and ETA no longer appear next to our
  arrow. Without route guidance (Waze, or no route) the phone draws its own turn card and ETA into
  the cluster map. `/mnt/app/root/mibr-carplay111-rgi-url.disabled` restores the old behaviour.
  Ported from mib2q-carplay-rgi. *Car test pending.*
- **Android Auto cluster map follows MMI day/night (opt-in, experimental).** With *Android
  Auto cluster day/night ON / OFF* in the menu, the Android Auto map on the VC is light by day
  and dark by night, following the MMI, instead of always dark. It switches live while the
  phone is connected. Off by default: in earlier tests by others, the light theme crashed
  Android Auto on the phone. If that happens, press the button again.
  ([#16](https://github.com/kamgurgul/mib2q-carplay/issues/16)) *Car test pending.*
- **CarPlay cluster frame rate up to 60 fps (opt-in).** `/mnt/app/root/mibr-carplay111-fps`
  now accepts 5 to 60 (before: 20, 25 or 30). The default stays 30. Above 30 is at your own
  risk: some MHI2Q builds crash at 60. The Virtual Cockpit itself samples the picture 30 times
  a second, so above 30 the phone and the decoder work harder without a smoother cockpit. ([#9](https://github.com/kamgurgul/mib2q-carplay/issues/9))
- **CarPlay cluster physical size can be overridden.** `/mnt/app/root/mibr-carplay111-physical`
  takes `width height` in mm (or only the width). iOS scales the cluster UI from this size,
  so e.g. `320 120` (the size other packages report) moves the car marker up in apps such as
  Sygic. The default stays 290 x 108 mm, the real 12.3" panel.
  ([#9](https://github.com/kamgurgul/mib2q-carplay/issues/9))

## Fixed

- **MMI froze and rebooted when Android Auto started with a route already running**
  (Android Auto cluster map and hardware decoder ON). If Google Maps navigation was already
  running when the phone connected right after an MMI start, the cluster map's video decoder
  was opened about a minute after boot. That hung the head unit's video hardware. Our cleanup
  then got stuck inside it. The MMI screen and the Audi map on the VC froze, music stopped
  some minutes later, and the unit rebooted. Now:
  - for the first 2 minutes after boot the Android Auto cluster video is decoded in software,
    then it switches to the hardware decoder by itself (one short reconnect of the cluster
    stream). CarPlay starts on the hardware decoder at once, like mib2q-carplay-rgi, so it has
    no switch; *HW decoder 2-min delayed start* in the menu gives CarPlay the same wait, for
    units that freeze when the decoder opens early
    ([#10](https://github.com/kamgurgul/mib2q-carplay/issues/10));
  - a decoder that stops answering is never touched again: hardware decoding is switched off
    and the cluster renderer restarts on software decoding;
  - a watchdog ends the renderer if a call to the decoder ever blocks, so it can no longer
    freeze the MMI.

  ([#14](https://github.com/kamgurgul/mib2q-carplay/issues/14)) *Car test pending.*
- **No turn arrow with some third-party CarPlay apps (e.g. Sygic).** Apps that leave out the
  junction type got no arrow for ordinary turns. The arrow is now chosen from the maneuver
  type, exit angle and driving side only, as in MIBSI. Apple Maps and Google Maps are
  unchanged. ([#9](https://github.com/kamgurgul/mib2q-carplay/issues/9))
- **"Follow road" far from the next turn**, like Audi navigation: the maneuver box shows the
  real turn only from 1.5 km in town, 3 km on the highway.
  ([#9](https://github.com/kamgurgul/mib2q-carplay/issues/9))
- **Google Maps logo cut by the VC bottom bar** on the CarPlay cluster map. The built-in
  SafeArea is now 27 px shorter in both views (wide 360,87 720x270, classic 520,87 400x270),
  so the logo sits above the bar and the car marker moves up slightly. A custom SafeArea
  from the card still overrides this. ([#9](https://github.com/kamgurgul/mib2q-carplay/issues/9))
  *Car test pending.*
- **No short blink** about 2 minutes after start: CarPlay no longer switches from software to
  hardware decoding mid-drive (it starts on hardware). With the delayed start turned on, or with
  Android Auto, the switch keeps the phone's map selected instead of flashing the Audi map.
  ([#9](https://github.com/kamgurgul/mib2q-carplay/issues/9))
- **A late, older route update could wipe the current route** (stale `route_generation`
  frames are now dropped), and **one failed cluster write at route end no longer skips the
  remaining clears**, which could leave a stale arrow or text on the VC. Ported from
  mib2q-carplay-rgi.
- **AltScreen: stock "Navigation is initializing" no longer covers the CarPlay cluster map**
  while the stock navigation is still loading after a start; the stock state is replayed when the
  video stops. Ported from mib2q-carplay-rgi. *Car test pending.*
- **AltScreen: fewer stutters.** The cluster video renderer now runs at the same raised priority
  as the maneuver renderer; at the default priority the compositor could stall it for
  300-650 ms. Ported from mib2q-carplay-rgi. *Car test pending.*
- **AltScreen: no hardware-decoder retry loop after a renderer crash.** If the renderer dies
  while starting the hardware decoder, it no longer retries endlessly on every restart (each try
  can take a decoder slot that CarPlay itself needs): after 3 starts without a picture it decodes
  in software for 30 s, after 3 more hardware decode stays off until the next start of the MMI.
  Idea from mib2q-carplay-rgi. *Car test pending.*

- **Removed: the *Native Audi VC arrows* switch** (menu buttons and M.I.B. mods). Hiding the
  KDK box does not make these cockpits draw their own arrows: Q7 and A4/A5 VCs kept an empty
  box. The 3D arrow is the only mode ([#9](https://github.com/kamgurgul/mib2q-carplay/issues/9)).

## Documentation and project

- README: **Supported units** (P5152 A5 F5 MU1329, P5092 MU1329) and **Tested wireless
  adapters** (Baseus Safe Journey: works, fast; Carlinkit 5.0 / 2Air: works, slow to start;
  generic `smartBox-xxxx`: does not work; the HW301 is supported from [1.3](CHANGELOG-1.3.md)).
  ([#5](https://github.com/kamgurgul/mib2q-carplay/issues/5))
- README: a section on the CarPlay-RGI Green Engineering Menu page and all its buttons.
- GitHub bug report template. It asks for firmware, settings and both log saves.

## Updating from 1.1

Copy both `carplay/` **and** `mod/` to the card: the menu has three new buttons. Run
**M.I.B. -> Advanced Settings -> Run Custom Script** once, then reboot. The installer must run
again in any case: it registers the new telephony and vehicle message IDs in
`dio_manager.json` (it only adds what is missing).
