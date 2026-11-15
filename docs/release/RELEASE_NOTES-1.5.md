# mib2q-carplay 1.5

New in 1.5: **route guidance for Baidu, AMap and Yandex roundabouts**, a cluster map that **no
longer goes black when a phone session ends**, Android Auto safe areas that fit Yandex and the
Q7, a faster software decoder, and new opt-in switches: hide the phone's turn card with Android
Auto, pause the cluster map while the cockpit hides it.

> [!WARNING]
> This modifies firmware processes and system configuration on your head unit. Use it
> only on a unit you own, never install or test while driving, and accept that you do it
> at your own risk. Everything is reversible with **Uninstall** (stock files are backed up
> on the unit).

> [!IMPORTANT]
> **Run the M.I.B. setup again when updating.** One run installs the release and updates the
> Green Engineering Menu, which has five new buttons in 1.5. See
> [Update from 1.4.1 or older](#update-from-141-or-older).

> [!NOTE]
> **Most of 1.5 has passed the bench tests only and has not been driven yet.** See
> [Not yet tested in a car](#not-yet-tested-in-a-car), and please report with **Save logs**.

## What's new in 1.5

### Android Auto
- **Hide the phone's turn card on the cluster map** (opt-in, experimental;
  [#56](https://github.com/kamgurgul/mib2q-carplay/issues/56)). With route guidance on, the
  cockpit showed two turn cards: the car's own arrow and the one Google Maps or Yandex draws on
  the map. New button **Android Auto hide phone turn card ON / OFF**, MMI *Hide Android Auto Turn
  Card*: the phone is told that the car draws the turn card, and the app leaves its own out. It works only while **Route guidance**
  is ON, and applies at the next phone connection. Known limits: if the cockpit hides its arrow
  during a route, or the app sends no turn data, no turn card is shown at all; with the card
  hidden, Google Maps may place its arrival-time line differently. Idea and car test by NuiQuant.
- **Yandex Maps is centred on the cockpit**
  ([#53](https://github.com/kamgurgul/mib2q-carplay/issues/53)). The safe area now reaches the
  phone the way Android Auto defines it; Google Maps looks as before. If a navigation app is
  placed worse than in 1.4, the file `/mnt/app/root/aa_cluster.uiconfig_legacy` brings the 1.4
  form back (next phone connection).
- **The phone's arrival-time pill is no longer hidden behind the street-name bar in the Sport
  layout**, and the **safe areas fit the Q7**
  ([#41](https://github.com/kamgurgul/mib2q-carplay/issues/41),
  [#53](https://github.com/kamgurgul/mib2q-carplay/issues/53)). The map area now ends just above
  that bar in every view, and it follows the car's own cockpit layout instead of the A4/A5
  numbers.
- **Cluster key frame on demand** (opt-in, experimental): new button **Android Auto cluster key
  frame on demand ON / OFF**. When the cluster map needs a fresh full picture, the phone is asked
  for one instead of the map waiting up to a minute or two.
- **Android Auto Map on Cluster**, **Android Auto Day/Night Map** and **Hide Android Auto Turn
  Card** are now also in the MMI CarPlay settings.

### Cluster map, CarPlay and Android Auto
- **No black cockpit when a phone session ends.** The last picture stays until the car's own map
  is back, instead of a black area for up to about a second. A stream that restarts continues on
  the same picture.
- **Pause the cluster map while the cockpit hides it** (opt-in, experimental): new button
  **Pause cluster map while hidden ON / OFF**, MMI *Pause Hidden Cluster Map*. While the cockpit
  shows another tab or the Audi map, the phone stops drawing and sending a map nobody sees, and
  resumes when the map is back.
- **Faster software decoder.** The fallback used when the hardware decoder is off now uses the
  processor's NEON instructions (aimed at the 1080p freeze of
  [#44](https://github.com/kamgurgul/mib2q-carplay/issues/44)). New button **Software decoder
  plain C (no NEON) ON / OFF** selects the 1.4 decoder again (after a reboot).
- **Hardware Video Decoder** and **Cluster Map Frame Rate** (30 or 60) in the MMI CarPlay
  settings. Keep 30 unless you test: the cockpit shows 30 pictures a second either way, 60
  doubles the decoder's work, and some units were reported to crash with CarPlay at 60.
- **Cluster video status**: new read-only button that shows which decoder runs, why hardware
  decoding is off if it is, the stream counters and the opt-in switches that are set.
- **A damaged picture is repaired at once**: the map asks the iPhone for a fresh full picture
  instead of waiting for the next periodic one.
- **CarPlay: a slow start no longer restarts the cluster map.** When the map cannot keep up for
  a moment, pictures are skipped until the next full one instead of the connection being reset.
- **One hardware decoder failure no longer keeps software decoding for good.** Only a hung video
  core is remembered after a reboot; any other failure is off until the next start.
- **Calibration grid works with the hardware decoder** (it reported ON but showed nothing since
  1.3).
- A change of the unit's clock can no longer switch hardware decoding off or make the cluster
  map look stale.
- Less background work in the head unit while the cluster map is still.

### Route guidance
- **Baidu, AMap and Yandex roundabouts with CarPlay.** The navigation app's name now selects how
  its roundabout data is read. Yandex sends only the exit number, so the HUD and the cockpit
  show a schematic roundabout with the right exit count (the real exit direction is not known).
  Apple Maps, Google Maps and other apps are unchanged.
- **HUD and cockpit arrow always show the same maneuver**, and a new turn never starts with the
  previous turn's distance bar.
- **Audi navigation texts are back after a CarPlay route**: street name, next turn, distance and
  time to destination no longer stay blank or keep CarPlay's last values.
- A route is no longer ended by a late timeout while the phone is still sending updates.
- The map-only cluster view (cards hidden during guidance) no longer keeps an empty side margin.

### Phone, media and parking
- **Recent calls on the cockpit phone tab show local time** (they were in UTC).
- **Now playing:** a cleared title or cover art no longer stays on the cockpit, and skipping a
  track in YouTube Music no longer interrupts media audio.
- Media audio is no longer released in the middle of a call or Siri.
- **Parking display over CarPlay** uses a trimmed background while CarPlay owns the screen
  (image by chefranov).

### Fixed
- **The cluster video program crashed when an Android Auto session ended**
  ([#43](https://github.com/kamgurgul/mib2q-carplay/issues/43), 1.4.1). Nothing showed in the
  car, and it restarted at the next connection.

The roundabout, Audi navigation text, HUD/arrow, timeout, recent calls, now playing, audio focus
and parking display changes come from
[luka-dev/mib2q-carplay-rgi](https://github.com/luka-dev/mib2q-carplay-rgi) (`main`, 2026-11-06
and 2026-11-11).

## New switches

| Green Engineering Menu button | Default | Applies |
| --- | --- | --- |
| Android Auto hide phone turn card ON / OFF | OFF | next phone connection; needs Route guidance ON |
| Android Auto cluster key frame on demand ON / OFF | OFF | at once |
| Pause cluster map while hidden ON / OFF | OFF | next change of the cockpit view |
| Software decoder plain C (no NEON) ON / OFF | OFF (NEON) | next reboot |
| Cluster video status | read-only | |

New in the MMI (*Connection manager → Options → CarPlay settings*): Android Auto Map on Cluster,
Android Auto Day/Night Map, Hide Android Auto Turn Card, Pause Hidden Cluster Map, Hardware Video Decoder, Cluster Map Frame
Rate. They change the same settings as the menu buttons.

## Not yet tested in a car

These passed every bench test, but have not been driven yet. Please report how they behave
(with **Save logs**):

- **Android Auto hide phone turn card**: this version sets the flag once, when the phone
  connects. A test build that changed it during the session hid the card with Google Maps and
  Yandex on an A4 B9. To check: both apps in the large, Classic and Sport views, where the
  arrival-time line ends up, and that Android Auto stays connected
  (`aa_cluster_hook.log`: `uiconfig.set ... turncard=car`)
- Android Auto safe areas: Yandex and Google Maps in all three views, on a B9 and on a Q7
  (`uiconfig.set ... encoding=viewport`)
- Session end and stream restart with the hardware decoder: no black frame, no crash
- The NEON software decoder (HW decoder OFF), and the plain C switch
- Pause cluster map while hidden and Android Auto key frame on demand, both opt-in
- Cluster Map Frame Rate 60, with CarPlay and with Android Auto
- Baidu, AMap and Yandex roundabouts; HUD and cockpit arrow; Audi navigation texts after a
  CarPlay route
- Recent calls time, now playing clears, the parking display background

## Update from 1.4.1 or older

1. Copy the new `SD-CARD/` contents to the root of the M.I.B. SD card: `carplay/` **and**
   `mod/`, replacing the old ones.
2. Disconnect the phone. Insert the card.
3. **Green Engineering Menu -> M.I.B. -> Advanced Settings -> Run Custom Script**
   (older M.I.B. release zips: **Run individual script**). The menu's **Install / update
   CarPlay** button updates the program files but not the menu, so the new buttons appear only
   after this M.I.B. run.
4. Wait for `DONE. mib2q-carplay 1.5 installed.` A `WARN` line names a step that was skipped; a
   `FAILED` line means nothing was changed.
5. Keep the phone disconnected, wait a few seconds, then **reboot the head unit normally** (not
   the forced button combination right after copying files).
6. Every new switch is OFF after the update. Android Auto safe areas change without a switch;
   a unit with its own `/mnt/app/root/aa_cluster.insets` file keeps its values.
7. HW301: the dongle image is unchanged (v1176 from 1.4).

Coming from 1.3.1 or older, also read the
[1.4 release notes](https://github.com/kamgurgul/mib2q-carplay/blob/master/docs/release/RELEASE_NOTES-1.4.md)
(MMI CarPlay settings, HW301 image v1176).

Full list of changes: [CHANGELOG-1.5](https://github.com/kamgurgul/mib2q-carplay/blob/master/docs/release/CHANGELOG-1.5.md).
First install, uninstall, the menu buttons, credits and licenses:
[README](https://github.com/kamgurgul/mib2q-carplay#readme), `LICENSE` and
`THIRD_PARTY_NOTICES.md` in this download.
