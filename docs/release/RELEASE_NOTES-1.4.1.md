# mib2q-carplay 1.4.1

Bug-fix release for Android Auto: **no more drop about 2 minutes after an MMI start**, and the
Android Auto cluster map uses the hardware decoder from the start.

> [!WARNING]
> This modifies firmware processes and system configuration on your head unit. Use it
> only on a unit you own, never install or test while driving, and accept that you do it
> at your own risk. Everything is reversible with **Uninstall** (stock files are backed up
> on the unit).

> [!IMPORTANT]
> **Run the M.I.B. setup again when updating.** The *HW decoder 2-min delayed start* button has
> a new label and now covers Android Auto too. See [Update](#update-from-14-or-older).

## What's new in 1.4.1

### Android Auto
- **Android Auto no longer drops once about 2 minutes after an MMI start**
  ([#48](https://github.com/kamgurgul/mib2q-carplay/issues/48)). With the phone connected right
  after a start, the cluster map switched from software to hardware decoding at 120 s, and that
  switch ended Android Auto's receiver; the phone reconnected about 15 s later.
- **The cluster map starts on the hardware decoder at once**, also right after an MMI start.
  The 2-minute software start is gone: before Android Auto's main screen sets up or changes its
  own decoder, the cluster map now steps aside for a moment (as CarPlay does since 1.4), so the
  two decoders never start together
  ([#14](https://github.com/kamgurgul/mib2q-carplay/issues/14)).
- **HW decoder 2-min delayed start ON / OFF** now applies to CarPlay **and** Android Auto
  (default OFF). ON brings back the old Android Auto behaviour: software decoding for the first
  2 minutes after an MMI start, then hardware. Turn it on if the MMI freezes when the phone
  connects right after a start.

CarPlay is unchanged.

## Not yet tested in a car

These passed every bench test, but have not been driven yet. Please report how they behave
(with **Save logs**):

- Android Auto connected right after an MMI start: no reconnect at about 2 minutes, cluster map on
  the hardware decoder from the first picture (`aa_cluster_hook.log`: `hw.yield why=setup ...`)
- Android Auto with a route running at connect, and screen switches on the MMI: no MMI freeze

## Update from 1.4 or older

1. Copy the new `SD-CARD/` contents to the root of the M.I.B. SD card: `carplay/` **and**
   `mod/`, replacing the old ones.
2. Disconnect the phone. Insert the card.
3. **Green Engineering Menu -> M.I.B. -> Advanced Settings -> Run Custom Script**
   (older M.I.B. release zips: **Run individual script**). The menu's **Install / update
   CarPlay** button updates the program files but not the menu, so the changed button label
   appears only after this M.I.B. run.
4. Wait for `DONE. mib2q-carplay 1.4.1 installed.` A `WARN` line names a step that was skipped; a
   `FAILED` line means nothing was changed.
5. Keep the phone disconnected, wait a few seconds, then **reboot the head unit normally** (not
   the forced button combination right after copying files).
6. A unit that had *HW decoder 2-min delayed start* ON keeps it, and it now delays Android Auto
   too. Turn it OFF to test the new behaviour.

Coming from 1.3.1 or older, also read the
[1.4 release notes](https://github.com/kamgurgul/mib2q-carplay/blob/master/docs/release/RELEASE_NOTES-1.4.md)
(new menu buttons, HW301 image v1176).

Full list of changes: [CHANGELOG-1.4.1](https://github.com/kamgurgul/mib2q-carplay/blob/master/docs/release/CHANGELOG-1.4.1.md).
First install, uninstall, the menu buttons, credits and licenses:
[README](https://github.com/kamgurgul/mib2q-carplay#readme), `LICENSE` and
`THIRD_PARTY_NOTICES.md` in this download.
