# HW301 findings

What is known about the HW301 dongle and how the Virtual Cockpit cluster map is
delivered through it. This is the condensed record. The day-by-day investigation
notes were removed in the cleanup; read them in git history (`git log -- dongle/hw301/work/notes`).
Addresses are stock **v1.29** (`CPAAProxyEx` build `20260122129`, link base 0x10000)
unless stated otherwise.

## Result

Shipped in mib2q-carplay **1.3** (needs the 1.3 head-unit hook). Car-verified with v1154:
normal CarPlay on the MMI (stream 110, stock path) **and** a dedicated iOS cluster view
(stream 111, 1440x540) on the Virtual Cockpit. It is not a mirror. CPAAProxyEx is not
byte-patched. Roller zoom works.

Built on top of that and still *car test pending*: the phone drawing maneuver and ETA
(v1156), SafeArea 270 (v1158), and re-rendering the map on a wide/classic cockpit switch
(v1160 back-channel). The release image v1162 includes all of these.

```
iPhone ==Wi-Fi AirPlay==> HW301 (receiver: libAirPlay) ==USB CarPlay==> MHI2Q
  110 ------------------> stock CPAAProxyEx --------------------------> 110 (stock)
  111 --> cluster_dual.c: listen, ChaCha open --> AES-CTR seal, TCP --> :7100 altscreen hook
                                   <------------- "VIEW"+u32 back-channel (layout switch)
```

## Platform

- Board RSY03 V1.0, Allwinner **V851S**, 16 MiB SPI-NOR, Realtek RTL8733BS Wi-Fi/BT.
  Linux 4.9, **musl**, ARM EABI5 hard-float.
- All CarPlay logic lives in `bin/CPAAProxyEx` plus `lib/libAirPlay.so` (Apple AirPlay SDK)
  and `lib/libCoreUtils.so`. `.dynsym` is not stripped, which is what made the work possible.
- CPAAProxyEx also starts hostapd (the `smartBox-xxxx` AP), the USB gadget and the LED.
  If it fails to start, the dongle has no LED, no Wi-Fi and no USB. That is the soft-brick symptom.
- To the head unit the dongle presents itself as an iPhone (`iPhone9,1`, build `18D70`) over
  iAP2. The head-unit hook recognises that sender by this identity (`sender_listed()`).
- There is no shell on the dongle. Every option is chosen at compile time.
- `/mnt/UDISK` is writable and survives flashing (logs live there).

### Flash layout (GPT on SPI-NOR)

| partition | range | size |
|---|---|---|
| boot | 0x0060000-0x02a0000 | 2304 KiB |
| rootfs | 0x02a0000-0x0730000 | 4672 KiB |
| rootfs_data | 0x0730000-0x07b0000 | 512 KiB |
| env | 0x07b0000-0x07d0000 | 128 KiB |
| private | 0x07d0000-0x07e0000 | 64 KiB |
| **app** | 0x07e0000-0x0ea0000 | 6912 KiB (the OTA `app.img` lands here) |
| UDISK | 0x0ea0000-0x1000000 | 1408 KiB |

## OTA / flash path

- An OTA tar contains `app.img` (xz SquashFS, block 262144, all-root) and `appmd5sum.txt`.
  It is **unsigned**: UpdateServer only checks `md5(app.img)` and then `dd`s it to `/dev/by-name/app`.
- Upload goes to the web UI endpoint `/uploadappdatas` in 32 KiB chunks of vendor JSON. Keys:
  `result,version,pos,itemsize,count,filesize,datasize,data`. A sample is in `work/cmp/vendor_pos0.json`.
  The write is then driven by repeated `/requestupdate` calls.
- Gotchas that broke early flashes:
  - the device's JSON parser needs **compact** JSON (`separators=(",",":")`). Spaces corrupt the image and the write aborts at about 10% with `{"result":0}`;
  - the tar must be a clean Linux ustar with only `app.img` then `appmd5sum.txt`. macOS bsdtar adds `._*` and PaxHeader members, which busybox tar rejects;
  - `appmd5sum.txt` needs its trailing newline.
- `UpdateServer` and `wpa_supplicant` live on the partition being overwritten. A bad image
  removes Wi-Fi flashing, and recovery is then over FEL (see README "Rollback").
- Flash only on bench power. In the car the dongle goes straight to CarPlay and the
  `192.168.5.1` page is not reliably reachable.
- The build marker `web/clbuild.txt` (`curl http://192.168.5.1/clbuild.txt`) shows which image is running.

## Injection: wrapper rename

- `build.sh` copies the stock `libCoreUtils.so` to `libCoreUtilsReal.so`. It gives the copy a
  **distinct soname** with patchelf, because musl de-duplicates by soname. The shim is then
  installed as `libCoreUtils.so` with `NEEDED libCoreUtilsReal.so`.
- CPAAProxyEx NEEDs `libCoreUtils.so` (#12) before `libAirPlay.so` (#13). The shim therefore
  precedes libAirPlay in lookup order and can interpose both CPAAProxyEx's CoreUtils imports
  and libAirPlay's exported functions. libAirPlay calls its own exports through its PLT
  (no `-Bsymbolic`). `dlsym(RTLD_NEXT)` reaches the real functions.
- Hard rule: **every symbol the shim imports must exist in the dongle's musl.** In v1132 an
  import of `__dlsym_time64`, from newer musl headers, stopped CPAAProxyEx from starting and
  soft-bricked the dongle. `check_imports.sh` (build step 5b) now refuses such images. Optional
  symbols are resolved at runtime with `dlsym`.
- Shim rules: send with `MSG_NOSIGNAL` (SIGPIPE would kill CPAAProxyEx), use a `__thread`
  re-entrancy guard in every hook, and use poll/timeouts so a stock thread never blocks.
  "Called from CPAAProxyEx" means the `dladdr` base equals that of `SetupProxyScreenSecurityInfo`.
- Shim lines written to `app.log` after startup are lost. **Log to `/dev/kmsg`** and read
  `dmesg.log` from `getlogs.py`. The kmsg ring is small, so pull logs right after a test,
  before unplugging the dongle.

## Phone side (dongle = AirPlay receiver)

### What iOS needs to open a cluster stream
- In `/info` (`AirPlayCopyServerInfo` hook):
  - root `enabledFeatures` += `altScreen`, `viewAreas`;
  - a second display: `type=111`, 1440x540, 290x108 mm, `maxFPS=30`, `features=0`, UUID `b7e6c5a0-2222-4000-8000-000000000002`, `initialURL`, `altScreenSuggestUIURLs`;
  - `viewAreas[2]` (wide and classic) and `initialViewArea=0`;
  - **no `displayUUID`**. This is the same contract the head-unit hook serves to a real iPhone.
- In **every SETUP response**, merge `altScreen` and `viewAreas` into the accepted features.
  iOS checks the displays against the features the receiver *accepts*, not only against `/info`.
  Without the merge the iPhone log shows `carEndpoint_checkCarPlayFeatureAcceptance failed:
  altScreen not defined but display structures found` and the session ends (v1144).
- Diagnostic split (v1140/1141/1142): stock `/info` is fine and features-only is fine.
  Display-only was rejected about 100 ms after `/info`.
- Stock `/info` for comparison: no root `enabledFeatures`, `features=0x6104040280`, one display
  of 1024x480, 188x88 mm, `features=2`.

### SETUP for stream 111
- Stock `AirPlayReceiverSessionSetup` skips 111 (`switch(type-100)` 0..10 → "Unsupported stream
  type", rc 0, no dataPort). It also allows only one screen object per session (`session+0x17e0`).
- The shim calls stock and then, for a 111 request, opens its own TCP listener (port 7100,
  or an ephemeral port as fallback). It appends `{clone of the request, dataPort, streamID=111}`
  to the stock response. iOS then connects and streams (v1146).
- Frame format = the 128-byte ScreenHeader `{u32 bodySize; u8 opcode; u8 small[3]; u8 params[120]}`.
  Opcode 0 = VIDEO_FRAME, 1 = VIDEO_CONFIG (avcC). The SPS reads 1440x540, which proves this is the cluster stream.

### Phone side: ChaCha20-Poly1305
The wireless screen link uses ChaCha20-Poly1305, not AES-CTR (`Screen_SetChaChaSecurityInfo`,
keylen 32). The details below were confirmed from libAirPlay v129 and on the car (v1148: thousands
of frames, `nal_ok=1 bad=0`):

- **Key:** `PairingSessionDeriveKey(*(void**)(session+0x3ec), salt, strlen(salt),
  "DataStream-Output-Encryption-Key", 32, 32, key)`. This is HKDF-SHA512 (libCoreUtils 0x5ec38).
  The salt is `"DataStream-Salt%llu"` of the stream's `streamConnectionID`. Stock derives only
  the Output key (FUN_0x12dcc, call site 0x165c4). The Input key builds are fallbacks that were never needed.
- **Per frame** (`AirPlayReceiverSessionScreen_ProcessFrames`, 0x1bab4..0x1bb78):
  - `chacha20_poly1305_init_64x64(ctx, key, &nonce8)`;
  - `add_aad(ctx, header, 128)`;
  - `decrypt(ctx, body, bodySize-16, body)` in place;
  - `verify(ctx, body+n1, body+bodySize-16, &err)`. Success means `err==0` and `n1+n2 == bodySize-16`.
  - The tag is the last 16 bytes of the body. The header is in the clear. Only opcode 0 is
    encrypted; VIDEO_CONFIG is plaintext.
  - The nonce is an 8-byte little-endian counter starting at 0, incremented **only after a good verify**.
- All primitives are exported by libCoreUtils, so no cipher is reimplemented.
- The stream has about one IDR. A late joiner needs `forceKeyFrame`, sent with
  `AirPlayReceiverSessionSendCommand` and a no-op completion (never NULL).

## Head-unit side (dongle = CarPlay client)

- CPAAProxyEx is a **single-screen bridge**:
  - one head-unit video socket (global 0x17e418, written by `_AirPlayClientSendSetupScreenCommandCompletion` FUN_0005c7ec only for type 0x6e);
  - one crypto context;
  - one config.
  `SendProxyScreenFrame` is called internally, not through the PLT, so it cannot be interposed.
- **Do not let CPAAProxyEx request 111 from the head unit.** Injecting a 111 stream into its
  SetupScreen (`arrray size = 2`) kills the session in about 0.6 s (v1150). The old gate-NOP mirror
  made 111 overwrite the single socket slot. If the head unit advertises two displays to the dongle
  (count:2), the session also dies in about 0.18 s. **The head-unit guard stays ON (count:1).**
- **Direct path (HU_DIRECT, the shipped design):**
  - the shim captures CPAAProxyEx's stream-110 AES-CTR key and IV at `AES_CTR_Init` called from
    `SetupProxyScreenSecurityInfo` (0x5b7c4, span 0x2b8);
  - it connects its own TCP socket to the head unit's :7100. The address is the peer of the
    dongle's stream-110 data socket (port 6030, falling back to the :5000 control peer);
  - it sends the cached VIDEO_CONFIG in the clear, then each authenticated AU sealed with
    AES-CTR over the whole AVCC body (no tag), then asks the phone for a key frame.
- The head-unit hook (`altscreen_hook/libaltscreen111_gen2.c`) does the matching part. When the
  dongle sender sets up 110 without 111, it opens the 111 listener and derives the key from 110's
  `streamConnectionID` (`AirPlay_DeriveAESKeySHA512ForScreen`). Both ends reach the same key
  without sending it. The CTR counters stay aligned because both sides advance by `bodySize`
  per VIDEO_FRAME and VIDEO_CONFIG advances neither.
- The relay socket is shared across phone 111 reconnects, because the head unit accepts one
  connection per SETUP. A head-unit re-SETUP (one is seen about 190 s into a session) bumps
  `g_hu_gen`: the relay closes, re-keys and reconnects. A failed connect is retried every 5 s,
  up to 12 times per key generation.
- Backpressure: if the head-unit socket is not writable within `HU_WRITE_MS` (60 ms), the AU is
  dropped **before** encryption, so CTR stays aligned, and a key frame is requested.
- Nonce desync: after 5 consecutive verify failures, the 111 socket is closed and iOS re-SETUPs
  with a fresh counter.

## Cluster content and layout

- `initialURL` = `maps:/car/instrumentcluster?showETA=yes&maneuverLayout=rightAligned&showSpeedLimit=no`.
  With it the phone draws maneuver and ETA. The bare `/map` URL is the "head unit draws RGI" variant.
  Change it with `-DCLUSTER_URL_INITIAL=...`.
- On the dongle path the **dongle** advertises the URL; the head unit serves stock `/info`. The
  MIB-menu maneuver toggle therefore has no effect on the dongle path.
- SafeArea for this A5 cockpit: height **270** (297 was wrong). Wide = 360,87,720,270 and
  classic = 520,87,400,270. Geometry can be overridden: `CLUSTER_W/H/FPS/WMM/HMM`, `CLUSTER_SAFE0_*` / `SAFE1_*`.
- Cockpit switch: CPAAProxyEx drops `updateViewArea`, `showUI` and similar head-unit commands
  (`_ProcessProxyRequestCommand` knows only `disableBluetooth`, `requestUI`, `changeModes`,
  `forceKeyFrame`, `setLimitedUI`). The head-unit hook instead writes `"VIEW"` plus a u32 LE view
  index on the 111 socket. The dongle reads it and sends `updateViewArea{index}` to the phone (v1160).
- Zoom needs no forwarding: the roller HID already reaches the phone.

## Build history

| code | what | status |
|---|---|---|
| 1126-1131 | early shims: AddScreenDisplay hook, probe, CF tracer, diag, mirror (gate-NOP) | obsolete |
| 1132-1134 | first dual-screen steps | 1132 soft-bricked (time64 import) |
| 1140-1142 | `/info` advert split (control / features / display) | diagnostic |
| 1143-1145 | display matches the head-unit contract; features in SETUP response | iOS accepts cluster |
| 1146-1147 | 111 listener and transport | phase A passed |
| 1148 | + ChaCha decrypt (Output key) | phase B passed |
| 1150/1151 | + 111 injection into CPAAProxyEx | **crashes CarPlay, never flash** |
| 1152/1153 | direct path, plaintext | never flash |
| 1154 | direct path, AES-CTR | first car pass: map on the VC |
| 1156 | maneuver/ETA URL, desync recovery, backpressure | |
| 1158 | SafeArea 270 | |
| 1160 | **same version as v1162, also contains enabled logging** (`build_dual.sh optA`, `CLUSTER_DIAG=1`, `v1160-20261021065350`). It first added the cockpit-switch back-channel and was rebuilt 2026-10-21 from the v1162 source; the first v1160 predated the review fixes | archived `fw/hw301_v1160.tar` |
| **1162** | release = same source with diagnostics off (`build_dual.sh release`, `v1162-20261020152434`) | archived `fw/hw301_v1162.tar` |

## Open items

- **Keystream reuse on the USB hop.** Streams 110 and 111 share one AES-CTR key and IV, because
  111 reuses 110's connection id. Someone with a capture of the internal dongle↔head-unit link
  could XOR the two streams. Over the air (ChaCha) is unaffected. The fix is a KDF that both
  ends apply, e.g. `SHA512(key110 || "cluster111")[:16]`. It is not written yet.
- **Automatic fallback.** If iOS rejects the session three times in a row, serve stock `/info`.
  The counter would live in a file on `/mnt/UDISK`. Not implemented: today a bad build needs a
  bench reflash.
- **Async head-unit writer thread.** Deferred until `dropped=` climbs in the logs.

## Dead ends (do not retry)

- Hooking `AirPlayInfoArrayAddScreenDisplay`: it is never called in the wireless flow.
  Display info reaches the receiver through a delegate callback.
- Binary-patching CPAAProxyEx into a two-screen pipeline: there is no code cave beyond a dead
  routine (FUN_000acdb0), the type register is dead at the socket store, and the change cannot
  be bisected.
- Making the head unit advertise two displays to the dongle (count:2): the head unit ends the session.
- Mirror build (v1129, gate-NOP at file offset 0x4c9a8): it shows the main screen on the VC. Superseded.
- Carlinkit firmware (ludwig-v repo) is the same single-screen bridge, so there was nothing to borrow.

## Reference material

- `work/ghidra/relay_decomp.txt` (from `DumpRelay.java`): the CPAAProxyEx single-screen video pipeline.
- `work/ghidra/dispparse.txt` (from `DispParse.java`): the head-unit display-info parser.
- `work/ghidra/cb_full.txt`: the receiver delegate callback (the display/audio info builder).
- `work/tools/xref.py`, `work/tools/findref.py`: string and address xrefs for ARM PIC binaries
  (need capstone and pyelftools in `work/.venv`).
