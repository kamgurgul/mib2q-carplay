# Phase D - hardening (after Phase C shows the map once)

Status 2026-10-19. Items 6 and 7 are implemented; items 1-5 are specified
below. Everything is in `work/src/cluster_dual.c`; build targets in
`work/tools/build_dual.sh`. Do not start Phase D before v1150 (or v1151)
has put the map on the Virtual Cockpit at least once.

## DONE

### 6. Diagnostics out of the release build - IMPLEMENTED
- `CLUSTER_DIAG` (default 1 = test builds, unchanged). `CLUSTER_DIAG=0` drops:
  the stock /info dump, the stock 110 response-entry dump, per-SETUP and
  per-TEARDOWN stream-type lines, the "enabledFeatures +=" line, the setup
  `rc=0` line (non-zero rc still logs), and the c111.h264 AU dump.
- Kept in release: LOADED, bind, key captured, 111 receiver ready/connected,
  chacha key derived, video config, 10 s frame stats, relay connect/close,
  every error line.
- `build_dual.sh release` -> v1160 (Output key), `release-input` -> v1161.
  Verified: v1160 has 0 diagnostic strings, keeps relay + session-end close,
  import check 42/42, dry-run OK.

### 7. Close the relay socket at the end of a phone session - IMPLEMENTED
- In the TearDown hook, a whole-session teardown (request == NULL) now closes
  the shared head-unit socket with `dead=1` and logs
  `session end: head-unit relay closed (relayed=N err=N)`.
- Why it is safe: when the phone leaves, CPAAProxyEx tears the head-unit
  screen down; when it returns, the head unit re-SETUPs the screen
  (`requestUI` -> SetupScreen with the injected 111, seen in the logs), which
  advances `g_hu_gen`, and relay_frame reconnects with the new key.
- Check in the test matrix ("phone disconnect/reconnect"): after reconnect,
  `head-unit screen key captured hu_gen=N+1` then `head-unit 111 connected`.

## TO DO

### 5. Forward the head unit's cluster commands to the iPhone
Problem: the head-unit hook sends `showUI`, `stopUI`, `updateViewArea` and
`changeMapZoomLevel` to its sender (= CPAAProxyEx). CPAAProxyEx's
`_ProcessProxyRequestCommand` (around 0x373e8, CPAAProxyEx v129) only handles
`disableBluetooth`, `requestUI`, `changeModes`, `forceKeyFrame`,
`setLimitedUI`; everything else is logged as
`not support request command type = %s` (string at 0x151778, used near 0x37480)
and dropped. Effect: cockpit layout changes (wide/classic) and zoom never reach
iOS; the cluster stays on `initialViewArea=0`.
What to build:
1. Find the CF call `_ProcessProxyRequestCommand` uses to read the request
   `type` (Ghidra on CPAAProxyEx; it is a libCoreUtils import such as
   `CFDictionaryGetCString`/`CFDictionaryGetValue`, so the shim can interpose
   it, filtered with `caller_is_cpaa()` the same way the 111 injection is).
2. Keep a retained pointer to the current PHONE session: set it in
   `start_receiver` (already retains `session`), release it in the whole-session
   TearDown branch. Guard with `g_lock`.
3. When that call returns one of the four unsupported cluster types, copy the
   request dict and send it to the iPhone with
   `AirPlayReceiverSessionSendCommand(phoneSession, copy, kf_done, 0)`
   (already resolved as `r_send_cmd`; `kf_done` is a no-op completion).
   Send from a short-lived thread or the existing receiver thread, not inline
   in CPAAProxyEx's handler.
4. The params need no rewriting: the head-unit hook uses the same cluster UUID
   (`b7e6c5a0-2222-4000-8000-000000000002`) that the dongle advertises to the
   phone, and the same command format it sends to a real iPhone.
5. Log `forwarded <type> rc=<n>` once per type; CPAAProxyEx will still answer
   the head unit "not supported" (harmless; the head-unit hook continues).
Test: switch cockpit layouts; the iPhone log should show the view-area change
on the AltScreen endpoint and the cluster map should re-layout.

### 1. Automatic fallback when CarPlay keeps failing (most important)
Problem: no dongle shell, so the kill-switch files cannot be created; a bad
interaction means a bench reflash.
What to build:
- A small counter file on `/mnt/UDISK` (writable by CPAAProxyEx; its logs
  live there), e.g. `/mnt/UDISK/cluster_dual.fails`.
- Phone session start (first SETUP with streams=<none>): remember the time.
- Whole-session TearDown within ~5 s of that start (the signature of iOS
  rejecting us): increment the counter. A session that lasts > 60 s: reset it.
- `disabled()` additionally returns true when the counter is >= 3, so the
  next sessions get stock /info and plain CarPlay. Reset it automatically
  after one good stock session (or after N boots) so the feature retries.
- Log `auto-fallback ON (fails=N)` / `OFF`.

### 2. Recover from a ChaCha nonce desync
Problem: a failed verify does not advance the nonce, so every later frame
fails; stock tears the stream down instead. CarPlay stays up but the
cluster freezes for the rest of the session.
What to build (in rx_thread, STEP_DECRYPT path):
- Count consecutive `chacha_open` failures; on 5 in a row, log
  `111 nonce desync, closing stream` and break out of the read loop (closing
  the 111 socket). iOS then re-SETUPs 111 with a fresh nonce; the shared
  relay socket stays open, so the head unit keeps its connection.
- Reset the counter on every successful open.

### 3. Keep head-unit writes off the phone read path
Problem: `send_full` can block up to 2 s per call on the thread that reads
the iPhone stream; a slow head unit stalls the phone side.
What to build:
- A bounded queue (e.g. 8 AUs) + one writer thread that owns the relay
  socket writes. rx_thread enqueues plaintext AUs (copy) and returns.
- When the queue is full, drop non-IDR AUs until the next IDR, then call
  `ask_keyframe` once.
- The AES-CTR re-encryption must move to the writer thread (it is stateful
  per socket) and keep the exact send order.

### 4. Pin the firmware assumptions
Problem: v129 internals are hardcoded (SetupProxyScreenSecurityInfo span
0x2b8, session+0x3ec, the ChaCha layout, CPAAProxyEx at base 0x10000).
What to build (in bind_all):
- Check the CPAAProxyEx build string (`20260122129` in app.log's start line;
  find it in the binary) and the `dlsym`'d sizes/addresses that the shim
  relies on (e.g. SetupProxyScreenSecurityInfo address == 0x5b7c4).
- On mismatch: log `firmware mismatch, cluster_dual pass-through` and make
  `disabled()` return true.

## Test matrix for the hardened build (each must end with CarPlay on the MMI)
| Case | Expect |
|---|---|
| iPhone disconnect + reconnect | relay closed at session end, reconnects on the new HU SETUP |
| ignition off / on | clean start, cluster back |
| Wi-Fi drop ~10 s | session or 111 recovers; no permanent freeze |
| head-unit re-SETUP mid-session (~190 s) | `head-unit re-keyed` then relay continues |
| phone call, Siri | cluster survives or returns |
| cockpit layout switch | (after item 5) cluster re-layouts |
| 30+ min route | relayed climbs, send_err 0, no freeze |
| forced failure (e.g. flash a bad advert build) | (after item 1) falls back to plain CarPlay within 3 tries |

## Order
1 and 2 first (they turn failures into hiccups), then 3, then 4, then 5.
Release build last: `build_dual.sh release` (v1160) with all of them in.
