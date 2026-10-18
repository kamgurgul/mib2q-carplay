# HW301 dual screen: implementation plan (2026-10-18)

Goal: normal CarPlay on the MMI (stream 110, unchanged) **and** a dedicated iOS
cluster view (stream 111) on the Virtual Cockpit, through the HW301 dongle.
Not a mirror. No CPAAProxyEx byte patch.

```
iPhone ==Wi-Fi AirPlay==> HW301 (receiver: libAirPlay) ==USB CarPlay==> MHI2Q
  110 ------------------> stock path ---------------------------------> 110 (stock)
  111 --> [new] listener, decrypt --> [new] re-encrypt, connect -----> 111 (our altscreen hook, :7100)
```

## 0. Facts this plan rests on (verified 2026-10-18)

| # | Fact | Evidence |
|---|---|---|
| F1 | Shim log lines appended to `app.log` after start are lost; kmsg keeps them | `dmesg.log`: 12x `appended cluster stream (type=111)`; `app.log`: 0 |
| F2 | The libCoreUtils wrapper **does** interpose CPAAProxyEx imports | F1 + `[ProxyVideo] arrray size = 2` in every cluster_inject session |
| F3 | `[ProxyVideo] arrray size/type/data_port` = dongle as **client of the head unit** | `_AirPlayClientSendSetupScreenCommandCompletion`, host `fe80::…%usb0:5000` |
| F4 | Head unit accepts a 2nd stream from the dongle with the guard ON (count:1) | `data_port=7100 type=111 err=0`, `init video socket:35` (Oct 11-12) |
| F5 | Guard OFF (count:2) kills the session in 0.18 s even with a clean single stream | 2026-10-12 21:58 log |
| F6 | gate-NOP makes 111 overwrite the single socket slot `0x17e418` -> `SendProxyScreenFrame failed, err:32` | Oct 11 17:35 log, relay_decomp |
| F7 | Stock libAirPlay SETUP: `switch(type-100)` 0..10; 111 -> "Unsupported stream type", skipped, rc=0, no dataPort | `AirPlayReceiverSessionSetup` 0x1650c..0x16548, default 0x16d2c |
| F8 | One screen object per session (`session+0x17e0`, flag `+0x17dc`, 2nd -> -6719) | 0x1654c..0x16580 |
| F9 | libAirPlay calls its exports via PLT (no -Bsymbolic) | JUMP_SLOT for CopyServerInfo, SessionSetup, SetSecurityInfo, TearDown, SendCommand, DeriveAESKeySHA512ForScreen, Screen_* |
| F10 | CPAAProxyEx NEEDED order: …, libCoreUtils.so (#12), libAirPlay.so (#13), … | `readelf -d` |
| F11 | `AirPlayReceiverSessionSetSecurityInfo(session, key[16], iv[16])` | 0x144e0: AES_CBCFrame_Init(session+0xf4, r1, r2), copies 16+16 B |
| F12 | `AirPlayReceiverSessionScreen_SetSecurityInfo(screen, key, iv)` -> `AES_CTR_Init(screen+0xb8, key, iv)` | 0x1b3a0 |
| F13 | CPAAProxyEx derives its head-unit screen key in `SetupProxyScreenSecurityInfo` (0x5b7c4, 0x2b8 B, exported) and calls imported `AES_CTR_Init` at 0x5ba28 | objdump |
| F14 | The injected 111 clone keeps 110's `streamConnectionID` -> head unit derives the **same** key for 111 as for 110 | why the mirror decrypted on both screens |
| F15 | Head-unit hook 111 receiver: 128 B header `{u32 bodySize; u8 opcode; u8 small[3]; u8 params[120]}`, op 0 = VIDEO_FRAME (body AES-CTR), 1 = VIDEO_CONFIG (avcC, plain), 2/3/4/5 = keepalive/forceKeyFrame/ignore/keepalive+body; one accept per SETUP | libaltscreen111_gen2.c:171-206, 2027-2179 |
| F16 | All needed primitives are exported: `ServerSocketOpen`, `SocketAccept`, `AES_CTR_Init/Update/Final`, `AirPlay_DeriveAESKeySHA512ForScreen(master,16,cid,key,iv)`, `AirPlayReceiverSessionSendCommand(session,dict,cb,ctx)` | dyn-syms |

Unknowns (each answered by a step below): U1 does iOS render a cluster for this
receiver; U2 is the phone screen link AES-CTR (like wired) or ChaCha; U3 does
the session survive a phone-side 111; U4 does the head-unit hook take 111 frames
from the dongle (key + framing).

## STATUS (2026-10-18): implemented, not flashed

NO DONGLE SHELL on this unit: step selection is baked in at COMPILE time
(`-DCLUSTER_STEP=N`), one flashable image per step. The /mnt/UDISK marker files
remain only as optional overrides for anyone who does have a shell; the normal
flow never needs them. "Off" without a shell = flash stock fw/hw301_v129.tar.

`work/src/cluster_dual.c` = all three steps in one source; `build_dual.sh`
emits the images (clean cross-compile -Wall -Wextra; wrapper topology verified
in each packed image; each image confirmed to contain ONLY its step's code;
flasher dry-run = 127 well-formed compact-JSON chunks):

  ./work/tools/build_dual.sh all      # or 1 / 2 / 3

  STEP_RECEIVE = CLUSTER_STEP>=2, STEP_RELAY = CLUSTER_STEP>=3.
  step 1 -> hw301_v1132.tar  advertise cluster display; log SETUP + key capture
  step 2 -> hw301_v1133.tar  + 111 listener, dataPort in response, decrypt, count
  step 3 -> hw301_v1134.tar  + inject 111 to the head unit and relay (full)

Validation is by kmsg lines via getlogs -> dmesg.log (section 3, per step); the
c111.h264 dump needs a shell to retrieve, so the no-shell flow relies on the
frames/idr counters instead. Next action = flash v1132, car-test, getlogs.
Nothing flashed.

## 1. Ground rules

- Head-unit guard stays **ON** (`sender_listed()` returns 1 for iPhone9,1/18D70). Never count:2 (F5).
- No `PATCH_CPAA`. Stock CPAAProxyEx in every build.
- Flash only on bench power; recovery = `flash_local.py fw/hw301_v129.tar --version 129`.
- Every behaviour behind a kill switch file on `/mnt/UDISK` (survives flashing).
- All shim logging to `/dev/kmsg` (F1); keep it terse (dmesg ring is small).
  Pull with `getlogs.py`, read `dmesg.log`, grep `cluster_dual:`.
- One build per step; never combine an unverified step with the next.

## 2. Code layout

One new source, `work/src/cluster_dual.c`, built as the **libCoreUtils** wrapper
(F10: it precedes libAirPlay, so it can interpose libAirPlay's PLT calls too,
and `dlsym(RTLD_NEXT)` reaches real libAirPlay and real libCoreUtils).

```
WRAP_LIB=libCoreUtils.so CHECK_SYM=AirPlayReceiverSessionSetup \
CLUSTER_SRC=cluster_dual.c ./work/tools/build.sh 129 <code>
```

Rules for the source:
- Every real function via `dlsym(RTLD_NEXT, …)`, resolved lazily under
  `pthread_once` (hooks can run before our constructor; constructor only logs).
- A `__thread` re-entrancy flag in every hook.
- Any hook on a hot path (`CFDictionaryGetInt64`, `AES_CTR_Init`,
  `CFArrayEnsureCreatedAndAppend`) does the cheap value test first and only then
  `dladdr(__builtin_return_address(0))`.
- "Called from CPAAProxyEx" = `dladdr(ra).dli_fbase == dladdr(&SetupProxyScreenSecurityInfo).dli_fbase`
  (no hard-coded offsets; the old `VIDEO_CONNECT_OFF` check is why the mirror hook never matched).
- Sockets: `send(…, MSG_NOSIGNAL)` only (SIGPIPE would kill CPAAProxyEx),
  `SO_SNDTIMEO`/`poll` timeouts so a stalled peer never blocks a stock thread.
- Copy reusable code: CF helpers + `augment()` from `cluster_screen.c`,
  stream clone from `cluster_inject.c`, receiver loop shape from
  `libaltscreen111_gen2.c` `alt_receiver_thread` / `start_alt_receiver` /
  `append_alt_setup_response`.

Kill switches / knobs (all files under `/mnt/UDISK`):

| File | Effect |
|---|---|
| `cluster_dual.off` | everything stock (pure pass-through) |
| `cluster_dual.noinject` | no 111 in the SETUP to the head unit |
| `cluster_dual.norelay` | phone side receives + decrypts, never connects to the head unit |
| `cluster_viewareas.on` | add `viewAreas` feature + areas (head-unit default is off) |
| `cluster_hu.port` | head-unit 111 port if discovery fails (default 7100) |
| `cluster_dump.on` | write the first ~2 MB of decrypted 111 AUs to `/mnt/UDISK/c111.h264` |

## 3. Steps

### Step A - phone advertisement + observation (build 1132)

Implement:
1. `AirPlayCopyServerInfo` hook = `cluster_screen.c augment()` with the head-unit
   contract (libaltscreen111_gen2.c:2739-2849):
   - root `enabledFeatures` += `altScreen` (+ `viewAreas` only with the opt-in file);
   - clone `displays[0]`; remove `primaryInputDevice edid platformLayer avcc xOffset yOffset windowWidth windowHeight`;
   - set `type=111 maxFPS=30 features=0 widthPixels=1440 heightPixels=540 widthPhysical=290 heightPhysical=108 uuid=CLUSTER_UUID initialURL=maps:/car/instrumentcluster`, `altScreenSuggestUIURLs` = the three URLs.
2. `AirPlayReceiverSessionSetSecurityInfo` hook: forward, then log `master key captured` (never the key).
3. `AirPlayReceiverSessionSetup` hook: log request stream types + `streamConnectionID` of 111;
   call stock; log rc and response stream types. No response edits yet.
4. Log-only `AES_CTR_Init` hook: count calls by caller module (libAirPlay vs CPAAProxyEx).

Expected kmsg:
```
cluster_dual: LOADED …
cluster_dual: /info cluster display added displays=2
cluster_dual: master key captured            <- U2: AES path present
cluster_dual: setup request types=110 …
cluster_dual: setup request types=111 cid=…  <- U1: iOS wants to render the cluster
cluster_dual: setup rc=0 response types=110  <- F7 confirmed live (111 dropped)
```
Gate: proceed if a 111 SETUP arrives and the main screen keeps working.
- No 111 SETUP at all -> iOS does not treat the dongle as cluster-capable: try
  `features`/`enabledFeatures` variants (bit26 A/B as in the head-unit hook) before going on.
- No `master key captured` but screen works -> ChaCha path (U2): add Step A2 =
  log-only hooks on `Screen_SetChaChaSecurityInfo` and `PairingSessionDeriveKey`
  to learn salt/info strings, and derive the 111 key from the pairing session instead.
- Session drops right after the 111 SETUP -> iOS needs the stream to exist; go to Step B directly.

### Step B - phone-side 111 receiver (build 1133)

Implement, inside the `AirPlayReceiverSessionSetup` hook, after stock rc==0 and
only when the request contains type 111:
1. `cid` = 111 `streamConnectionID`; `key,iv = AirPlay_DeriveAESKeySHA512ForScreen(master,16,cid)`;
   `AES_CTR_Init(phone_ctx, key, iv)`; wipe key/iv.
2. Listener: IPv6 dual-stack TCP, prefer port 7100, fall back to ephemeral (the
   dongle has no PF). `listen(fd,1)`.
3. Append to the stock response `streams`: clone of the requested 111 desc +
   `dataPort=<port>` + `streamID=111` (same as `append_alt_setup_response`).
   Also add `altScreen` to the response `enabledFeatures` (head-unit hook does this too).
4. Receiver thread (one at a time, generation counter; a new 111 SETUP or
   whole-session teardown bumps the generation, the old thread exits within 1 s):
   - `poll` 1 s slices for accept/read; read 128 B header, reject `bodySize > 8 MB`, read body;
   - op 1 VIDEO_CONFIG: log size; parse avcC SPS for width/height; keep a copy;
   - op 0 VIDEO_FRAME: `AES_CTR_Update` in place (always, even when not relaying, to keep the counter);
     check the first bytes form a valid length-prefixed NAL; count IDR/non-IDR;
   - others: count.
   - optional dump (`cluster_dump.on`): avcC + Annex-B AUs to `/mnt/UDISK/c111.h264`.
5. `AirPlayReceiverSessionTearDown` hook: forward unchanged; if request is NULL or
   contains 111, bump the receiver generation.

Expected kmsg:
```
cluster_dual: 111 receiver ready cid=… port=7100
cluster_dual: 111 connected from <phone>
cluster_dual: 111 config avcC=… 1440x540
cluster_dual: 111 frames=1 idr=1 nal_ok=1
cluster_dual: 111 frames=900 idr=… bytes=…
```
Gate: `nal_ok` stays 1 and the SPS says 1440x540 -> key + framing correct (U2/U3 closed).
Bench check: pull `c111.h264` (or play it with `ffplay` after copying off
`/mnt/UDISK`) - it must show the cluster map, not the main screen.
Failure modes: `nal_ok=0` -> wrong key (check cid byte order / ChaCha);
phone never connects -> port/response format; main screen breaks -> revert with
`cluster_dual.off` and compare response dicts.

### Step C - head-unit relay (build 1134)

Implement:
1. `CFArrayEnsureCreatedAndAppend` hook (from cluster_inject.c) with two extra filters:
   caller is CPAAProxyEx, value has `streamConnectionID` **and** `type==110`.
   Append the type-111 clone. Do not patch the gate: stock skips 111 (F4/F6 safe).
2. `AES_CTR_Init` hook: if `ra` is inside `[SetupProxyScreenSecurityInfo, +0x2b8)`
   store key+iv as the head-unit 111 key (F13/F14), bump `hu_gen`, close any open head-unit 111 socket.
3. `CFDictionaryGetInt64` hook: when the result is 111 and the caller is CPAAProxyEx,
   read `dataPort` from the same dict with the real getter; if > 0 that is the
   head-unit 111 port (the head unit's SETUP response). Fallback: `cluster_hu.port`, then 7100.
4. Head-unit address: scan fds 3..1023 for a TCP socket whose peer port is 6030
   (CPAAProxyEx's stream-110 data socket); copy its peer `sockaddr_in6` (keeps the
   `%usb0` scope id) and set the port from 3.
5. Relay (in the Step B receiver thread):
   - connect lazily on the first phone 111 header; non-blocking connect with 3 s timeout,
     `TCP_NODELAY`, `SO_SNDTIMEO` 2 s; `AES_CTR_Init(hu_ctx, hu_key, hu_iv)` on connect;
   - on connect: send the cached VIDEO_CONFIG first, then ask the phone for a key frame:
     `AirPlayReceiverSessionSendCommand(session, {type:"forceKeyFrame", params:{uuid:CLUSTER_UUID}}, NULL, NULL)`;
   - op 0: phone-decrypt (Step B) -> `AES_CTR_Update(hu_ctx)` in place -> send header + body;
   - op 1 and others: send unchanged;
   - the head-unit hook accepts once per head-unit SETUP (F15): keep the socket across
     phone reconnects; after a send error do not reconnect until `hu_gen` changes.
6. Head unit: nothing to change. Read `/tmp/altscreen111.log` for
   `GEN2 SETUP contains stream111`, `AltScreen stream 111 connected`,
   `valid transactional video config`, `first complete Screen VIDEO_FRAME accepted`.

Expected kmsg:
```
cluster_dual: hu 111 key captured gen=1
cluster_dual: hu 111 port 7100 (from response)
cluster_dual: hu peer fe80::…%3 port 7100
cluster_dual: hu 111 connected, config sent, keyframe requested
cluster_dual: hu 111 frames=900 send_err=0
```
Gate: map on the VC, CarPlay UI on the MMI, both live for 10+ minutes.
Fallback if the head unit rejects the frames (decrypt errors in altscreen111.log):
switch the head-unit hook to accept unencrypted 111 frames when the sender is
iPhone9,1/18D70 (small change in `alt_receiver_thread`, op 0 branch) and send
plain frames from the dongle (`cluster_dual.plainhu` knob).

### Step D - hardening (build 1135+)

- Phone reconnect, head-unit re-SETUP mid-session (logs show a 2nd `arrray size` ~190 s in),
  Wi-Fi drop, ignition cycle: each must recover or fall back to "main only", never kill 110.
- Rate-limit logs; drop the dump code path by default.
- Head-unit -> phone cluster commands (`showUI`, `updateViewArea`, zoom) currently die in
  CPAAProxyEx. If needed: log what CPAAProxyEx receives from the head unit and forward
  matching commands with `AirPlayReceiverSessionSendCommand` from the shim.
- Update `relay-plan.md`: correct F1-F3 conclusions; record the final build line.
- Only then consider README/release wording (dongle docs stay under `dongle/`).

## 4. Test protocol per build

1. Build: `build.sh 129 <code>`; check step 5 output shows the shim exports
   `AirPlayReceiverSessionSetup`, `CFArrayEnsureCreatedAndAppend`, `AES_CTR_Init`,
   and `libCoreUtils.so` NEEDs `libCoreUtilsReal.so`.
2. `flash_local.py … --dry-run`, then flash on bench power; `curl 192.168.5.1/clbuild.txt`.
3. Car: connect iPhone, open Maps, start a route, wait 5 min, switch cockpit layouts, take a call.
4. Bench: `getlogs.py`; `grep cluster_dual work/logs/dmesg.log`; head unit `/tmp/altscreen111.log`.
5. If anything regressed: `touch /mnt/UDISK/cluster_dual.off` (no reflash) or flash stock v129.

## 5. Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| iOS never requests 111 from this receiver (U1) | medium | Step A gates it before any relay code; try feature-bit variants |
| ChaCha on the wireless screen link (U2) | medium | Step A detects; Step A2 derives from the pairing session |
| `AES_CTR_Init` capture also catches other callers | low | strict `ra` range of `SetupProxyScreenSecurityInfo` |
| Head unit expects 111 within a timeout after SETUP | low | logs show it waits indefinitely; connect as soon as phone 111 starts |
| Wi-Fi bandwidth / dongle CPU | low | no transcoding; two H.264 streams + AES-CTR |
| Shim crash takes CPAAProxyEx down | medium | every hook fail-soft to stock; kill-switch files; stock recovery tar |
