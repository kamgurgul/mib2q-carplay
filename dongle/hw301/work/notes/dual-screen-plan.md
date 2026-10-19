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

## REVIEW (2026-10-18): all items below APPLIED

Fixed in `work/src/cluster_dual.c` and rebuilt (clean -Wall -Wextra for steps
1/2/3; per-image step gating re-verified; v1132 dry-run = 127 chunks). All
three images (v1132/v1133/v1134) are now ready to flash in order. The items
are kept below as the record of what each change addresses.

1. **Split the generation counter** (blocks step 2).
   `g_gen` serves two jobs: `AES_CTR_Init` (head-unit key capture) bumps it,
   and `rx_thread`/`read_full` exit when it changes. A head-unit screen re-setup
   (the logs show one ~190 s into a session) therefore kills the phone's 111
   stream. Use two counters: `g_rx_gen`, bumped only by `start_receiver` and
   the teardown hook, retires the receiver; `g_hu_gen`, bumped only by the key
   capture, makes the relay close its head-unit socket and reconnect with the
   new key. Nothing else may bump `g_rx_gen`.

2. **Listener fd ownership** (blocks step 2).
   `start_receiver` and the teardown hook close `g_listen_fd` while the old
   thread is still polling it. If the new listener gets the same fd number, the
   old thread's exit path (`if (g_listen_fd == listen_fd) close(...)`) closes
   the *new* listener. Fix: only the thread closes its own listen fd (right
   after `accept`, since one connection per SETUP is enough, and on exit);
   `start_receiver` and teardown only bump `g_rx_gen`. Drop `g_listen_fd`,
   `g_thread` and `g_thread_live`. While the old thread winds down (≤1 s), port
   7100 is still bound and the new bind falls back to an ephemeral port; that
   is fine because the response carries the actual port.

3. **Add the decrypt check `nal_ok`** (step 2 validation).
   Without a shell, a wrong key is only visible this way. For each decrypted
   VIDEO_FRAME, walk the AVCC NAL lengths: each length is `nal_len` bytes
   (from the avcC config, byte 4 `& 3` + 1, normally 4), each NAL's forbidden
   bit must be 0, and the lengths must add up exactly to `bodySize`. Count bad
   frames, log `nal_ok=1/0` on the first frame and `bad=` in the periodic line,
   and count IDR as "any NAL type 5 in the AU", not only the first NAL.
   Also log the width×height from the SPS in the VIDEO_CONFIG avcC: 1440×540
   proves this is the cluster stream, not a second copy of the main 1024×480.
   If the SPS parse is too much, log the avcC length and profile/level bytes
   as a minimum.

4. **Head-unit reconnect policy** (blocks step 3).
   Once the head-unit socket is lost, the relay calls `hu_connect()` (up to 3 s
   blocking) again on every frame, which stalls the phone read loop and can
   make iOS drop 111. The head-unit hook accepts one connection per SETUP, so:
   - before the first success, try at most once every 5 s for the current
     `g_hu_gen`;
   - after a connected socket fails, stop until `g_hu_gen` changes;
   - when `g_hu_gen` changes while connected, close, re-init the head-unit
     AES-CTR context with the new key, reconnect, resend config and request a
     key frame.
   Keep this state in a per-thread relay struct (fd, gen, last-try time, dead
   flag, ctx, sent/err counters).

5. **Session lifetime and SendCommand** (blocks step 3).
   `rx_thread` uses the `session` pointer from the SETUP hook without a
   retain; a session that ends first makes `ask_keyframe` a use-after-free in
   `CPAAProxyEx`. Bind `CFRetain`, retain in `start_receiver`, release when the
   thread exits. `AirPlayReceiverSessionSendCommand` dispatches onto the
   session queue (0x14564); the head-unit hook always passes a completion, so
   pass a no-op completion `(OSStatus, CFRef, void *)` instead of NULL.

6. **VIDEO_CONFIG handling** (blocks step 3).
   Only the first config is cached (`config_seen`) and later ones are never
   forwarded; the cached one is resent with a zeroed header, which loses the
   params (source size floats at params+8/+12, flags in smallParam). Fix:
   cache the full header and body on every VIDEO_CONFIG (replace the previous
   copy); if the head-unit socket is open, forward it right away, unchanged
   (it is not encrypted); on (re)connect send the cached header and body as is.

7. **Smaller items.**
   - Relay only when the phone key is valid (`phone_valid`); otherwise frames
     are still ciphertext and would reach the head unit as garbage.
   - Re-encrypt for the head unit in place (the body is not needed after the
     dump) instead of a second `malloc` per frame.
   - Add `_Static_assert(sizeof(ScreenHeader) == 128)`.
   - Periodic relay line: `hu sent=N err=N gen=N`, so step 3 shows progress in
     `dmesg.log`.
   - Rename the key-capture log to `head-unit screen key captured hu_gen=N`
     (it fires in every step, also in v1132, which is a useful step-1 signal).

### Second review pass (2026-10-18): applied

Re-read of the first-pass fixes against the plan found these; all applied:

- R1 (item 4 was wrong): a *failed connect* set the relay dead for the whole
  head-unit session. A refused/timed-out connect never consumed the head-unit
  hook's single accept, so it now retries every 5 s, up to 12 tries per
  head-unit key generation (~1 min), then gives up until the next HU SETUP.
  Only a lost *connected* socket waits for the next HU SETUP immediately.
- R2: `hu_connect` waited up to 3 s on the phone read path (stalls iOS's 111
  send). Now 1 s.
- R3 (was "known limitation"): the head-unit socket is now one shared relay
  (`g_relay` + `g_relay_lock`), not per receiver thread. An iPhone 111
  reconnect reuses it (the head-unit hook accepts once per HU SETUP and would
  not accept again); the new receiver replays its config once and requests a
  key frame. Only the current receiver (`rx_gen == g_rx_gen`) writes.
- R4: one-time log `relay: no head-unit screen key captured yet` so step 3
  does not fail silently.
- R5: head-unit address lookup falls back to the AirPlay control peer (:5000)
  if no :6030 peer is found.
- R6: log-only hooks on `Screen_SetSecurityInfo` (AES) and
  `Screen_SetChaChaSecurityInfo` (screen, key, len==32; 0x1b318) report which
  cipher stock uses for the phone's 110 screen, answering U2 in step 1.
- R7: the 111 injection accepted only `type==110` at append time; the proven
  cluster_inject.c matched on `streamConnectionID` alone. Now accepts 110 or
  "type not set yet", rejects any other explicit type.
- R8: `bind ok=… spssi=… cpaa_base=…` at bind time; both addresses must be
  non-zero or step 3 key capture + injection are inactive.
- Verified: the SETUP response stock returns is the `CFDictionaryCreateMutable`
  dict from 0x16344, stored to `*outResponse` at 0x17590, so appending the 111
  entry to it is safe.

### Still open: needs evidence first (step-gated), then code

1. **ChaCha on the phone screen (U2).** If step 1 logs
   `phone screen security: ChaCha20-Poly1305` for 110, step 2's AES-CTR
   decrypt of 111 is wrong (`nal_ok=0`). Then:
   - RE the ChaCha branch of `AirPlayReceiverSessionSetup`'s screen case
     (around 0x165b8: `session+0x3ec` test, call 0x12dcc) to get the 32-byte
     key derivation (expected: pairing-session HKDF with a
     `DataStream-Salt<cid>` salt and an output-key info string) and the
     per-frame nonce/AAD layout used by `_AirPlayReceiverSessionScreen_ProcessFrame`.
   - Capture the pairing session the same way (hook the derive call through
     the PLT), derive for the 111 cid, decrypt with `chacha20_poly1305_*`
     (exported by libCoreUtils).
   - The head-unit side stays AES-CTR (the dongle↔HU log says
     `use aes ctr security`), so only the phone side changes.
2. **No 111 SETUP in step 1.** iOS does not treat the dongle as
   cluster-capable. Try, one build each: root `features` bit 26 forced on (the
   head-unit hook's A/B), `viewAreas` opt-in, and the URL variants the
   head-unit hook uses (`ALT_URL_RGI_ON/OFF`). Do not continue to step 2.
3. **Session drops right after a 111 SETUP in step 1.** Expected if iOS needs
   the port: go straight to step 2.
4. **Head-unit → phone cluster commands.** The head-unit hook sends
   `showUI`/`stopUI`/`updateViewArea`/zoom to its sender, i.e. to CPAAProxyEx,
   which does not know them; they are not forwarded to the iPhone. The cluster
   still shows `initialURL`. If step 3 works but layout switching does not,
   log the command dicts CPAAProxyEx receives and forward them with
   `AirPlayReceiverSessionSendCommand` (step D).
5. **`cluster_dump.on` needs a shell** to create and to fetch `c111.h264`; the
   no-shell flow relies on `nal_ok`/`bad`/`sps=` in `dmesg.log` instead.
6. **kmsg ring size.** `dmesg.log` is a ring snapshot. Pull logs soon after
   the car test; the shim's periodic lines are every 300 frames (~10 s).

Verified while reviewing (no change needed): `TearDown(session, request,
reason, outDone)` takes the request dict in r1 and NULL-checks it (0x145c4);
the AES-CTR context in libAirPlay is ≤0x118 bytes (`screen+0xb8`..`+0x1d0`),
so the 512-byte `AesCtx` is large enough; system `python3` 3.12 runs
`flash_local.py` and `getlogs.py` (stdlib only; no `.venv` needed).

## STEP 1 RESULT (2026-10-19, v1132-20261019093451): iOS ends the session after /info

The fixed shim loads and runs: `bind ok=1 step=1 spssi=0x5b7c4 cpaa_base=0x10000`,
`head-unit screen key captured hu_gen=1`, dongle<->head unit SETUP 110 normal.
Phone side (app.log clock is ~1.75 s behind dmesg):
- 21.727 `EVENT_CARPLAY_SESSION_CREATED`
- dmesg 23.45 first SETUP (`streams=<none>`, rc=0), dmesg 23.56 our `/info`
  (`cluster display added displays=2`)
- 21.815 `Recv CarPlay session ended event` - ~10 ms after our /info.
  No screen SETUP, no screen key, no 111. Then BT/iAP2 disconnect.
=> iOS rejects the modified /info. Not yet known which change: the root
`enabledFeatures` (if stock had none, the shim creates a list with only
`altScreen`) or the added display (type 111, cloned keys, extra `displayUUID`).
The dongle is unusable for normal CarPlay with this image: flash stock back.

## PLAN FROM HERE (2026-10-19, after step 1 passed)

Standing test protocol for every image (no exceptions):
- Bench flash; keep `work/rollback/restore-app-v129-at-0x7e0000.bin` + a full
  dump (FEL recovery is proven, ~5 min).
- In the car: iPhone cabled to the Mac with `idevicesyslog > iphone-<ver>.log`
  running; reproduce; pull dongle logs with getlogs.py BEFORE unplugging the
  dongle (dmesg is lost on reboot).
- Read the iPhone log first: `grep -E "carEndpoint|FeatureKey|AltScreen|
  screen stream|kNotFoundErr|Deactivating|Endpoint_Failed"`.
- Daily driving between tests: stock v129 or v1141.

### PHASE A PASSED (2026-10-19, v1146, car + iPhone log)
- Dongle: `setup request has 111 cid=14478086969214323842` -> `111 receiver
  ready port=7100` -> `111 connected from iPhone` -> `111 video config body=40
  sps=1440x540 nal_len=4` (config is plaintext: it IS the 1440x540 cluster
  stream) -> `111 frames=1 bytes=199`; then 110 and 101 set up normally.
- iPhone: `Registering streamType AltScreen, ID 111, connectionID <same>` ->
  `remote screen port: 7100` -> `Created transport data stream` -> `resumed`
  -> `Started MediaPlaying Transaction`; `Activated endpoint 'CarPlay'`; no
  kNotFoundErr / resume failed / deactivation. MMI CarPlay stays up.
- Only frame 1 was logged (counter logs at 1 and every 300) -> v1147 adds a
  10 s stats line, and guards a NULL `streams` array in the 110-key dump
  (CFArrayGetCount(NULL) on the 111 SETUP response; did not crash, but unsafe).
- Next: Phase B (ChaCha decryption of the 111 frames).

### Phase A - step 2a "transport" (v1146 BUILT 2026-10-19, `build_dual.sh step2a`)
Implemented: STEP_DECRYPT compile gate (default 0); the receiver counts frames
(bytes/opcode, nal_ok left 0 = "still encrypted") and never AES-decrypts; the
111 listener + dataPort/streamID response entry are unchanged; append_response_111
also logs the stock 110 response entry's keys. All step-1 fixes carry over.
Goal: iOS opens stream 111 to the dongle and keeps CarPlay up. No decryption.
- Rebuild step 2 with every step-1 fix (viewAreas, real false, features in the
  SETUP response). On the 111 SETUP: open the listener, append the 111 entry
  (clone of the request + dataPort + streamID=111) to the stock response.
- Receiver counts frames/bytes per opcode only (phone key not used: the
  stream is ChaCha, see Phase B). Never reject a frame on size alone during
  this phase; just count and log the first header fields.
- Also log the keys of the stock 110 entry in the SETUP response, so the 111
  entry can be made to match it if iOS wants more than dataPort/streamID.
Pass: iPhone log shows the 111 screen stream resumed with no `kNotFoundErr`,
no `Endpoint_Failed`; dongle log shows `111 connected` and frames climbing;
MMI CarPlay stays up for 10 min.
If it fails: the iPhone log names the missing response key or the connect
error; adjust the 111 response entry and repeat.

### Phase B RESEARCH (2026-10-19) - how stock decrypts a screen stream
Traced the screen case of `AirPlayReceiverSessionSetup` (libAirPlay v129) and
the key-derivation helper it calls. The whole scheme is reusable from the shim
because every primitive is exported by libCoreUtils.

Stock per-stream screen path (0x16560..0x16620), in order:
  1. CFDictionaryCreateMutable                     -> response stream dict
  2. AirPlayReceiverSessionScreen_Setup(screenSrv=session[+0x7e0 off sl],
       streamDesc, session+0xd0)                   -> screen object
  3. CFDictionaryGetInt64(streamDesc, <connID key>) -> streamConnectionID
  4. FUN_0x12dcc(session, streamDesc, connID, outKeyBuf@sp+0x1d4)  <- KEY DERIVE
  5. AirPlayReceiverSessionScreen_SetChaChaSecurityInfo(screenSrv,
       keyBuf, 32)                                  -> 32-byte ChaCha key set
  6. ServerSocketOpen(...)                          -> listening dataPort
  7. CFDictionarySetInt64 x2                        -> dataPort + type in response
Then Screen_StartSession / Screen_ProcessFrames reads that socket, ChaCha
decrypts, and delivers plaintext H.264 to the screen delegate (on the dongle
that delegate is CPAAProxyEx -> recvCarPlayVideoFrame for stream 110).

KEY DERIVATION (FUN_0x12dcc): asprintf a salt from "DataStream-Salt" + the
connID, then PairingSessionDeriveKey TWICE:
  - info "DataStream-Output-Encryption-Key" -> sender(phone) output key
  - info "DataStream-Input-Encryption-Key"  -> receiver(dongle) ... 
  The dongle DECRYPTS the inbound 111 stream with the key the phone ENCRYPTS
  with, i.e. the phone's output key = "DataStream-Output-Encryption-Key"
  (confirm on device by trying both; nal_ok tells which is right).
  PairingSessionDeriveKey(pairingSession, salt, saltLen, info, infoLen,
  outLen=32, outKey) = HKDF-SHA512 (libCoreUtils 0x5ec38, EXPORTED).

PAIRING SESSION handle PINNED: `*(void**)((char*)session + 0x3ec)` (FUN_0x12dcc
`ldr r0,[r7,#1004]` at 0x12e20; same offset as `ldr r5,[fp,#1004]` at 0x165b8).
The shim gets `session` as the first arg of the SETUP hook.

SALT + INFO PINNED (strings in libAirPlay .rodata, used by FUN_0x12dcc):
  salt   = sprintf("%s%llu", "DataStream-Salt", connID)   e.g. "DataStream-Salt14478086969214323842"
  info#1 = "DataStream-Output-Encryption-Key"   (phone's TX key = dongle decrypts inbound with this)
  info#2 = "DataStream-Input-Encryption-Key"    (phone's RX key = dongle would encrypt with this)
  both: PairingSessionDeriveKey(pair, salt, strlen(salt), info, strlen(info), 32, out32).
Try Output first for decrypting the inbound 111 stream; nal_ok confirms.

DECRYPT PRIMITIVES (all EXPORTED by libCoreUtils, shim-callable):
  chacha20_poly1305_init_64x64, _decrypt, _verify, _final (0x3b4f0.. etc).
  Per-frame nonce/AAD/tag layout: read from ScreenStreamProcessData /
  _AirPlayReceiverSessionScreen_ProcessFrame (next RE step). AirPlay screen
  frames are typically: 128-byte header (plaintext) then ChaCha ciphertext +
  16-byte Poly1305 tag; nonce = little-endian frame counter (per stream),
  AAD = (none or the header). Verify against the first IDR: with the right
  key+nonce, chacha20_poly1305_verify passes and au_ok -> nal_ok=1.

ScreenStream* are NOT defined in libAirPlay (imported), so decrypt in the shim
with the libCoreUtils chacha primitives + PairingSessionDeriveKey, keyed per the
111 connID. No cipher is reimplemented - only stock primitives are called.

IMPLEMENTATION STATUS: design complete; the per-frame decrypt loop is not yet
written. Remaining concrete unknowns, both from ProcessFrame: (a) pairing-session
offset in the session struct; (b) exact nonce + AAD + tag position per frame.
Pin both from a Ghidra decomp of _AirPlayReceiverSessionScreen_ProcessFrame and
ScreenStreamProcessData, then the loop is: derive key once (PairingSessionDeriveKey),
per frame chacha20_poly1305_init_64x64(nonce,key) + _decrypt(body) + _verify(tag).

### Phase B - step 2b decrypt the cluster stream (ChaCha)
Goal: plaintext H.264 of the cluster on the dongle (`nal_ok=1`,
`sps=1440x540`).
- Research first, offline (Ghidra on libAirPlay v129, no car trips): how stock
  sets up a ChaCha screen stream - which key it derives for a stream ID and
  how `_AirPlayReceiverSessionScreen_ProcessFrame` decrypts a frame (nonce
  source, what is authenticated, where the tag sits, header handling).
- Implement the same for stream 111 inside the shim; validate with the
  existing nal_ok / SPS checks (log-only, no relay).
- This is the piece whose implementation was interrupted in this session; do
  it in small, separately reviewed changes.
Pass: `111 video config ... sps=1440x540`, `nal_ok=1 bad=0`, idr climbing.

### Phase C - step 3 relay to the head unit (v1134 path, rebuilt)
Goal: map on the Virtual Cockpit, CarPlay on the MMI.
- Already implemented and reviewed (inject 111 into the dongle->HU SETUP,
  capture the HU screen AES key, shared relay socket, re-encrypt, forward,
  key-frame request). Rebuild on top of Phases A+B; feed it the plaintext
  frames from Phase B.
- Head unit side: no change; the sender guard stays ON.
Pass: `/tmp/altscreen111.log` on the head unit shows `stream 111 connected`
-> `valid transactional video config` -> `first complete Screen VIDEO_FRAME
accepted`; map visible on the VC; both screens stable 10+ min.

### Phase D - hardening
- Phone 111 reconnects, head-unit re-SETUP mid-session, Wi-Fi drops,
  ignition cycles; every failure must fall back to "main screen only".
- Forward the head-unit's cluster commands (showUI/updateViewArea/zoom) to the
  iPhone if layout switching is needed.
- Remove diagnostic dumps from the default build; update README/release notes
  only after Phase C passes.

### Decision points
- Phase A fails repeatedly with iOS insisting on something the receiver cannot
  provide -> stop and reassess before Phase B.
- Phase B research shows the per-stream key cannot be reproduced for 111 ->
  fall back to the shipped mirror (v1129) for the VC.

STEP 1 PASSED (2026-10-19, v1145, iPhone log): iOS accepts the cluster.
- `Activated endpoint 'CarPlay'`; display `CarPlay (ScreenAlt)` 1440x540,
  290x108 mm, UUID b7e6c5a0-...; two screen streams created;
  `AltScreen Raise requested` -> `resuming screen stream` -> iOS sends the
  stream-111 SETUP.
- Expected step-1 failure after that: our SETUP response has no 111 entry
  (no listener in step 1) -> `apsession_setupStreamsCreatingResponse -6727
  kNotFoundErr` -> `resume failed` -> endpoint deactivated.
- Next = step 2 (listener + dataPort in the response). Open item: the phone
  screen is ChaCha20-Poly1305, so step 2's AES derivation does not apply; the
  111 receiver needs the stock ChaCha key path for its stream ID before frames
  can be decrypted. A first step-2 build can still prove the transport (iOS
  connects to the port and streams; frames counted, not decrypted).
- Not yet implemented in this session.

ROOT CAUSE FROM THE iPHONE LOG (2026-10-19, v1144, `idevicesyslog`, iPhone17,1
iOS 27.0.1 24A446 - same phone/build the head unit accepts the cluster from):
  airplayd: carEndpoint_checkCarPlayFeatureAcceptance failed: altScreen not
            defined but display structures found.
  carEndpoint_validateEnabledFeaturesWithAccessory: -16720 InvalidParameter
  Deactivating endpoint 'CarPlay' with reason 'Activation Failed'
iOS proposes a FeatureKey list (uiContext, viewAreas, cornerMasks, focusTransfer,
h.264Level5.1, mainBuffered, altScreen, enhancedSiri, hevc, vehicleStateProtocol,
sessionManagement, videoPlayback, logTransfer, iAPChannel) and validates the
displays against the features the receiver ACCEPTS in its SETUP response. /info
alone is not enough; the head-unit hook also merges into every SETUP response.
Fix: v1145 merges altScreen+viewAreas into every SETUP response.
Capture recipe: `brew install libimobiledevice`; iPhone cabled to the Mac;
`idevicesyslog > iphone.log`; reproduce; grep carEndpoint|FeatureKey|altScreen.

DIAGNOSTIC RESULTS (2026-10-19):
- v1140 (stock /info): session stays up -> the shim itself is harmless.
- v1141 (enabledFeatures += altScreen only): session stays up.
- v1142 (cluster display only): phone sends a whole-session TEARDOWN
  (`streams=<none> reason=0`) ~100 ms after /info -> the display is rejected.
- Phone screen 110 is ChaCha20-Poly1305 (`keylen=32`), NOT AES-CTR: step 2's
  receiver must decrypt 111 with ChaCha (open item 1 below is now confirmed).
- Stock /info: no root `enabledFeatures`; features=0x6104040280 (bits 7,9,18,
  26,32,37,38); displays[0] has only widthPixels=1024 heightPixels=480
  widthPhysical=188 heightPhysical=88 uuid maxFPS=30 features=2 (no `type`,
  no primaryInputDevice).
- Reference (carplay_logs/011-021, head unit + real iPhone17,1, iOS 24A446):
  iOS accepts the cluster and opens 111 in <100 ms, ALWAYS with viewAreas:
  root enabledFeatures = altScreen+viewAreas; display has viewAreas[2]
  (wide safe 720x297@360,87; classic safe 400x297@520,87; full 1440x540
  areas; drawUIOutsideSafeArea/viewAreaTransitionControl=false;
  initialViewArea=0) and no `displayUUID`.
- Fix (v1143, `build_dual.sh hu1`): display now identical to the head-unit
  hook's: viewAreas always (root + display), `displayUUID` removed.

DIAGNOSTIC BUILD READY (2026-10-19): `build_dual.sh advert` -> three images,
all import-checked, `/info` edits dead-code-gated per variant (verified):
  v1140 (ADVERT=0) stock /info, logged only  -> CONTROL, must keep CarPlay up
  v1141 (ADVERT=1) merge enabledFeatures only
  v1142 (ADVERT=2) append the cluster display only
Each logs the stock /info (root + each display's keys/values) on the first two
/info calls, plus the teardown reason and stream types. Flash one per car trip,
IN THIS ORDER (v1140 first); grep dmesg for `stock /info`, `teardown reason`.
Recover with stock v129 between trips if a variant drops the session.

Implemented in cluster_dual.c:
1. Log the stock /info before editing it: root keys with type and scalar value,
   each `displays[]` entry's keys/values, the `enabledFeatures` strings. Use
   `CFDictionaryApplyFunction`, `CFGetInt64`, `CFNumberGetTypeID`,
   `CFBooleanGetTypeID`, `CFDataGetTypeID` (all exported by libCoreUtils;
   resolve with `next_sym`, optional, so `check_imports.sh` is unaffected).
   First two /info calls only; kmsg lines < 250 chars.
2. Build-time `CLUSTER_ADVERT` bitmask, default 3: bit 0 = merge
   `enabledFeatures`, bit 1 = add the cluster display; 0 = stock /info (log only).
3. TearDown hook: always log `reason` and the request stream types.
4. `build_dual.sh`: pass extra CFLAGS (e.g. `EXTRA_CFLAGS=-DCLUSTER_ADVERT=0`)
   and use distinct codes per variant (e.g. 1140 + ADVERT) so the marker shows it.
Test order (one car trip each): ADVERT=0 (control; must keep CarPlay up and
gives the stock /info dump) -> compare with the head-unit hook's /info, which
works with a real iPhone -> ADVERT=1 -> ADVERT=2 -> fix the failing part.

## INCIDENT + RECOVERY (2026-10-19): v1132 soft-brick, restored over FEL

What happened: the first v1132 (marker `v1132-20261019084355`) imported
`__dlsym_time64` (newer musl headers; see the comment above `dlsym_plain` in
cluster_dual.c). The dongle's musl has no time64 symbols, so CPAAProxyEx could
not load the shim and exited at startup. CPAAProxyEx also runs hostapd (the
smartBox AP), the USB gadget and the RGB LED, so the symptom was: no LED, no
Wi-Fi, no USB device. Only the `app` partition was affected. Fixed in ae405d8;
`check_imports.sh` (build.sh step 5b) now refuses such images.

Hardware: board RSY03 V1.0, Allwinner V851S, 16 MiB SPI-NOR (8-pin chip above
the SoC), Realtek RTL8733BS Wi-Fi/BT. GPT on the flash:

| partition | range | size |
|---|---|---|
| boot | 0x0060000-0x02a0000 | 2304 KiB |
| rootfs | 0x02a0000-0x0730000 | 4672 KiB |
| rootfs_data | 0x0730000-0x07b0000 | 512 KiB |
| env | 0x07b0000-0x07d0000 | 128 KiB |
| private | 0x07d0000-0x07e0000 | 64 KiB |
| app | 0x07e0000-0x0ea0000 | 6912 KiB (the OTA app.img lands here) |
| UDISK | 0x0ea0000-0x1000000 | 1408 KiB |

Recovery that worked (no programmer, no soldering):
1. FEL: dongle unplugged; short flash pin 2 (DO; pin 1 = dot, top-left; pin 2 =
   second leg from the top on the left) to GND (USB plug metal shell); plug into
   the Mac; release after ~2 s. `xfel version` -> `ID=0x00188600(V851/V853)`.
2. Backup (read-only, two reads compared): `work/tools/fel_backup.sh`
   (xfel v1.4.0 from github.com/xboot/xfel, built into the git-ignored
   `work/third_party/xfel/`; ~520 KB/s, ~32 s per 16 MiB read).
3. Restore file = stock `fw/v129/app.img` + 4 KiB of the dump's own bytes, so
   the write is exactly 0x3f0000 (64 KiB aligned; xfel erases whole blocks):
   `xfel spinor write 0x7e0000 work/rollback/restore-app-v129-at-0x7e0000.bin`
4. Verify: read back 0x7e0000+0x3f0000 and `cmp`; MD5 of the first 4124672
   bytes = appmd5sum.txt; neighbouring blocks 0x7d0000 and 0xbd0000 unchanged.
5. Unplug, power from a USB charger: LED + smartBox AP back on stock v129.

Rule from now on: keep `work/rollback/restore-app-v129-at-0x7e0000.bin` (and a
full dump); a dongle that shows no LED after a flash is recovered in ~5 min.

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
