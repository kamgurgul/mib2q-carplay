---
title: Android Auto - receiver profile for a firmware
tags: [android-auto, porting, firmware]
status: verified-source
sources:
  - code: aa_hook/aa_hook.c (profiles[], match_profile, log_fingerprint)
  - code: scripts/aa_receiver_profile.py, mods/logging_MoreIncredibleBash/
---

# Android Auto - receiver profile for a firmware

The cluster-display hook writes into objects inside the stock `libautoreceiver.so` (video
sink, discovery response, protobuf strings, router tables), so it runs only on a receiver it
has a **profile** for. A profile is the set of symbol offsets of that exact build. On any other
receiver the hook keeps Android Auto stock and logs an `abi.fingerprint` line. Route guidance in
the cockpit and HUD does not depend on this; it works on every firmware the jar builds for.

A profile also records two addresses of the `gal` build it was validated with. The hook touches
no `gal` object, so a known receiver under a different `gal` runs as well, **unpinned**
(`init enabled profile=<name> gal=unpinned` in the log). A safety marker,
`/tmp/aa_cluster.unpinned`, exists from start-up until the first cluster frame or a normal
`gal` exit. If an unpinned `gal` crashes before either, the next start finds the marker and
keeps Android Auto stock (`init disabled reason=unpinned_failed_before`) until a reboot clears
`/tmp`. At worst, one session is lost instead of a crash loop. Deleting the marker allows
another try without a reboot.

| Firmware | Receiver | Profile |
|---|---|---|
| `MHI2Q_US_AUG22_P3639` MU0918 | sha256 `6ef4abea…` (gal `6fffb360…`) | built in, validated on the car upstream |
| `MHI2Q_ER_AUG22_P5152` MU1329 | identical to MU0918 (gal `a659a955…` differs) | built in from the unit's files (`P5152`), validated on the car (Audi A5 F5) |
| `MHI2Q_US_AUG22_P5087` MU1316 | identical to MU0918 (gal `cd00d89d…` differs, at P5152's two addresses) | runs as `P5152`, pinned by those addresses; validated on the car ([#34](https://github.com/kamgurgul/mib2q-carplay/issues/34) RS5, [#41](https://github.com/kamgurgul/mib2q-carplay/issues/41) S5, [#43](https://github.com/kamgurgul/mib2q-carplay/issues/43) Q7) |
| `MHI2Q_KR_AUG22_P1201` MU1201 | identical to MU0918 (gal `e35627a0…` differs) | built in from the unit's files (`P1201`, [issue #33](https://github.com/kamgurgul/mib2q-carplay/issues/33)); car test pending |

## 1. Get the files and the fingerprint

1. Install a release built with `ANDROID_AUTO=1` (the default of `build_all.sh`), reboot, and
   connect the Android phone once.
2. Run *Save logs to SD* (GEM) or the logging M.I.B. mod. The card then holds
   `carplay_logs/aa/gal`, `carplay_logs/aa/libautoreceiver.so` (copied once per card) and
   `carplay_logs/NNN/tmp/aa_cluster_hook.log`.
3. On the build machine:

   ```sh
   python3 scripts/aa_receiver_profile.py <card>/carplay_logs/aa/gal <card>/carplay_logs/aa/libautoreceiver.so
   ```

## 2. Read the result

- **`Identical to the receiver validated on the car (MU0918)`** or **`this gal is built in
  (profile …)`**: nothing to port. The log already says `init enabled profile=<that name>`.
- **`This gal is another build, but its addresses equal profile …'s`**: nothing to port either.
  The hook checks the two `gal` addresses, not the file, so it pins this build under that
  profile (`init enabled profile=<that name> gal=pinned`). MU1316's `gal` runs as `P5152` this way.
- **`The receiver is identical to MU0918 … only gal differs`**: the hook already runs unpinned.
  Once the cluster map worked on the car, add the printed entry to pin that `gal` build.
- **Missing symbols**: this receiver cannot run the hook. Use the cluster guidance without the
  map (`aa_cluster.off` keeps the launcher quiet).
- **A candidate profile entry**: the symbols exist at other offsets. The offsets must equal the
  `abi.fingerprint` line in the log, which checks that the files you read are the ones that ran.

## 3. Check a new receiver before trusting it

The offsets prove only the symbol layout. The hook also relies on these object layouts. Check
each against the disassembly of the new `libautoreceiver.so`, for example in Ghidra next to the
MU0918 receiver:

| Object | Offsets the hook uses |
|---|---|
| `ProtocolEndpointBase` | +0 vtable, +4 open flag, +5 channel, +8 router, +12 service id |
| `VideoSink` (as built by `addSupportedConfiguration`) | size 0x50, +0x14, +0x18 session, +0x1c, +0x30, +0x40/+0x44 config vector, +0x4c; vtable slots 0-2, 7-14 |
| `VideoConfiguration` | +0x08/+0x18/+0x1c unknown-fields string, +0x28 codec resolution |
| `ServiceDiscoveryResponse` / service | +0x28/+0x2c services, +0x58 id, +0x2c media (+0x40/+0x44 video configurations), +0x20/+0x30 input |
| `MessageRouter` | endpoint table at (id+64)*4 |
| `shared_ptr<IoBuffer>` / `IoBuffer` | +4 / base +0, offset +8, end +12 |
| `ChannelOpenRequest`, `VideoFocusRequest` | +0x2c |

Then add the entry to `profiles[]` in `aa_hook/aa_hook.c`, rebuild
(`./scripts/build_aa_hook.sh`), and add the firmware to the table above.

## Trying without a profile

Creating `/mnt/app/root/aa_cluster.abi_trial` makes the hook run on a receiver without a
profile, as long as all symbols resolve and the load-time sanity checks pass (primary sink
vtable and router, configuration vector size). If a layout differs, `gal` can crash and
Android Auto restarts. Delete the marker, or create `aa_cluster.off`, to return to stock. Use it
only for a supervised test drive. A clean `cluster.register ok=1` followed by
`cluster.frame count=…` in `aa_cluster_hook.log` is the first sign it works.
