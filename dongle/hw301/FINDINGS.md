# HW301 findings

What is known about the HW301 dongle and how the Virtual Cockpit cluster map is
delivered through it. This is the condensed record. The day-by-day investigation
notes were removed in the cleanup; read them in git history (`git log -- dongle/hw301/work/notes`).
Addresses are stock **v1.29** (`CPAAProxyEx` build `20260122129`, link base 0x10000)
unless stated otherwise.

## Released legacy result

Shipped in mib2q-carplay **1.3** (needs the 1.3 head-unit hook). Car-verified with v1154:
normal CarPlay on the MMI (stream 110, stock path) **and** a dedicated iOS cluster view
(stream 111, 1440x540) on the Virtual Cockpit. It is not a mirror. CPAAProxyEx is not
byte-patched. Roller zoom works.

Built on top of that: the phone drawing maneuver and ETA
(v1156), SafeArea 270 (v1158), and re-rendering the map on a wide/classic cockpit switch
(v1160 back-channel). The release image v1164 includes all of these, plus the Sport-skin
ViewArea, 300 px small-map SafeAreas and the initial URL without `maneuverLayout` (issue #20).
v1166 adds key frame requests from the head unit (cold-start fix, see *Cluster content and layout*).
v1167 gives the wide layout ETA, speed limit and compass, and keeps the small maps bare.

```
iPhone ==Wi-Fi AirPlay==> HW301 (receiver: libAirPlay) ==USB CarPlay==> MHI2Q
  110 ------------------> stock CPAAProxyEx --------------------------> 110 (stock)
  111 --> cluster_dual.c: listen, ChaCha open --> AES-CTR seal, TCP --> :7100 altscreen hook
                                   <------------- "VIEW"/"KEYF"+u32 back-channel (layout switch, key frame)
```

## Current source: v1172 native proxy candidate

The source now replaces CPAAProxyEx's single-screen boundary on the dongle.
The unpublished v1168 private-protocol experiment is superseded. No special
`hw301Cluster` metadata, hello/rekey exchange, fixed cluster port or video-socket
control protocol is used by the native candidate. The archived v1167 release
and its historical behavior are described separately below.

### Recovered proxy boundaries

The v129 executable has one screen socket, one screen connection ID and one
screen cipher context. Appending a second display/stream to its internal arrays
is insufficient. The previous `CFArrayEnsureCreatedAndAppend` interposer also
had a wrong return ABI (`void` instead of OSStatus); it has been removed entirely.
This is a concrete defect, not proof of the original crash's precise cause: no
crash backtrace is available. The replacement keeps all type-111 state outside
those stock single-screen objects.

`work/ghidra/DumpNativeProxy.java` reproduces the focused decompilation. Relevant
stock v129 functions and observations:

| Boundary | Evidence / replacement |
| --- | --- |
| Car `/info`, `FUN_00038d00` | Parses main display only, then copies the entire display array to proxy info +0xc8. Interpose `CFBinaryPlistV0CreateWithData`, retain the real type-111 descriptor, return a copy without 111 to stock. |
| Phone `/info`, delegate `FUN_0005a068` | Stock reads that proxy array. `AirPlayCopyServerInfo` appends the retained car descriptor and negotiates altScreen/viewAreas. |
| Outgoing HTTP plist, `CreateHTTPRequestMsgWithPList` 0x5cefc, size 0x190 | `CFBinaryPlistV0Create` returns an allocated byte buffer, not CFData. Restrict interception to this function's calls; add 111 to the initial native screen SETUP dictionary rather than intercepting array appends. |
| Screen SETUP, `FUN_0005b5b4` | Stock emits type 110 with connection ID, main UUID and maxFPS. Add a separate type-111 entry with a fresh random ID, the car UUID and car maxFPS. |
| SETUP completion, `FUN_0005c7ec` | Capture type-111 `dataPort`, then remove that entry before the stock completion sees it. Get the actual peer via `HTTPClientGetPeerAddress(gAirPlayClient, ...)`, preserving IPv6 scope. |
| `SetupProxyScreenSecurityInfo` 0x5b7c4, size 0x2b8 | AES branch hashes `gSessionAesKey` (GOT 0x17e1a8 -> 0x17ea14), not `gMasterAesKey`. Verify its main-screen derivation against the observed AES initialization, then derive 111 with its own connection ID. |
| Car command handler `_ProcessProxyRequestCommand` 0x36b38 | Unknown commands are forwarded to an existing phone session. Earlier notes claiming they were always dropped were incomplete. Capture cluster commands before this handler so they also survive a temporarily absent phone. |
| Phone control delegate `FUN_0005854c` | Generic commands are wrapped as `{type, params}` and forwarded with `AirplayClientSendCmdCmd`; this includes suggestUI. No new private reverse channel is needed. |

The build pins CPAAProxyEx, libAirPlay, libCoreUtils and libiAP2Link to the inspected stock
v129 hashes using `work/tools/check_native_abi.py`. The phone pairing-session
pointer at session +0x3ec remains a private v129 ABI dependency.

### Native negotiation and encryption

The dongle identifies itself as `model=HW301` in its session SETUP and keeps the
stock `osBuildVersion=18D70`, so the car still sees a real iOS build string. The
head unit's legacy guard needs both `iPhone9,1` and `18D70`, so the new model
alone takes the normal path; a `noadvertise.senders` line like `iPhone9,1 *`
cannot catch it either. The existing head-unit path therefore advertises its normal
second display, exactly as for a supported sender. No HW301 identity branch or
private descriptor was added to the head unit. The suppression/listener path for
unmodified legacy `iPhone9,1/18D70` firmware and custom-listed legacy senders is
preserved.

The phone receives the car's dimensions, physical size, UUID, FPS, view areas,
safe areas, initial URL, suggested URLs and unknown descriptor fields intact.
An absent or invalid second display disables its advertisement; there is no
local geometry fallback. View areas are optional. `viewareas.single` supplies
one view and continues to work; disabling view areas also leaves the normal
second display available. Geometry changes take effect after a fresh /info and
phone session.

Car SETUP includes independent 110 and 111 connection IDs. Only the car's
returned port is used. Phone-facing SETUP/TEARDOWN also filter 111 before calling
stock; a 111-only teardown never becomes an empty, whole-session stock teardown.
The shim owns the additional phone listener and car socket.

For a mixed phone SETUP, a missing car descriptor/connection ID or a failed
cluster key derivation, listener bind, worker creation or response allocation
leaves stock's result intact. The response omits 111 while preserving the main
and audio streams. A failed 111-only SETUP returns an error. An invalid car
111 data port or peer address clears the pending ID and key material so the
next main-screen SETUP can negotiate 111 again with a fresh ID.

Phone video is authenticated with the stock ChaCha20-Poly1305 primitives, using
the header as AAD and per-connection nonces. Key derivation must succeed before
the phone receives a successful SETUP response. Car video uses normal
`AirPlay_DeriveAESKeySHA512ForScreen(sessionKey, 16, cid111, key, iv)` and an
independent continuous AES-CTR context. Config packets stay clear. Failed phone
authentication is never relayed; AES errors close the socket without emitting
plaintext. Dropped unsent frames do not advance CTR. USB AES-CTR does not provide
per-frame authentication. The v129 USB AES path is supported; a future paired
HomeKit car transport is rejected for 111 until its sender cipher is implemented.

The car socket and cipher survive phone-only stream/session teardown and
main-display-only teardown/re-setup. An existing 111 stream is not added again
to later main SETUPs or retired by their new main-screen keys. Stock's full
car teardown (`_TeardownProxyClient`, 0x38220) calls `HTTPClientInvalidate` before
clearing `gAirPlayClient`; that boundary retires stream 111 and its keys. A failed
relay can be replaced by a fresh 111 SETUP when stock next sets up the main display. A new car socket
requires fresh native SETUP and a new connection ID; a failed accepted socket is
not reopened with its old CTR key/IV.

### Native controls and retry behavior

Car showUI/stopUI, updateViewArea and forceKeyFrame use the standard `/command`
connection. The dongle retains their complete parameter dictionaries (including
UUID, adjacency, animation duration and future fields), with separate versions
and phone-generation checks. showUI completion must succeed before view replay.
Failures retry at most once per second, including asynchronous completion errors.
Completion tickets remain owned by their callbacks across teardown. If stock
drops a completion entirely, its small ticket remains allocated; it cannot be
freed at teardown without risking a late callback using freed memory. This is
limited to the in-flight slots per abandoned generation, not across unlimited
reconnects.
There is no synthetic view payload or URL policy on the dongle. The latest car
intent is replayed after a phone reconnect and cleared on car teardown. Phone
suggestUI uses the existing generic stock delegate back to the car.

The head-unit transport uses `poll()` for the legacy control send/listener path,
sets `ETIMEDOUT` on a send timeout, and leaves native phone/proxy reads blocking.
Legacy controls are serviced while video is idle. Descriptor numbers above the
QNX `FD_SETSIZE` limit no longer suppress those reads or listener polling.

### Route guidance bridge (v1176)

Route guidance (iAP2 0x5200-0x5204: HUD maneuvers and the cluster arrow) never
worked through the dongle, on any image. Dongle log 2026-10-28: the car, with the
head-unit hook, sends StartRouteGuidanceUpdate `0x5200` every 2 s and then every
30 s (the hook's retry). Stock never forwards it, and the phone never sends `0x5201`/`0x5202`.
Stock v129 relays iAP2 between the car (USB link, the dongle is the device) and
the phone (iAP over CarPlay, the dongle is the accessory) from fixed tables:

| Boundary | Evidence |
| --- | --- |
| Car to phone, `iap2_dev_recv_data` 0x3ee7c | Switch on the message ID (0x3f120-0x3f590). Only 0x4154-0x4164, 0x10da-0x10dd, 0x4c00-0x4c09, 0x5000-0x5003 and a few others reach the phone (`iAP2LinkQueueSendData(gOverCarplayLink, ..., gOverCarPlayCtrlSession)`). 0x52xx falls to the default and is dropped. |
| Phone to car, handler 0x5eec4 | Range check before `iap2_send_message_to_proxy_usb(buf, len, 0)`. 0x5201/0x5202/0x5204 are dropped. |
| Phone Identify, 0x1d00 case | `_makeiAP2IdInfoMessage` 0x5ed9c writes parameters `00 01 02 03 04 05 06 07 08 09 0c 0d 10 11 12 14 16 18` (no 0x1E) with the fixed lists `CPSendIAP2MsgIds` (29 IDs) and `CPRecvIAP2MsgIds` (13), with no 0x52xx. Component IDs used: USB host 1, Bluetooth 2, vehicle information 3, wireless CarPlay 4. |
| Start cache, `.rodata` 0x152014 | `{start, stop}` pairs (4154/4156, 4157/4159, 4170/4172, 10da/10dc, 4c00/4c02, 4c03/4c05, 5000/5002). `iap2_set_overcarplay_linksession` replays these starts to a new phone link (`rewrite start msg id`). |

The bridge in `cluster_dual.c` wraps only libiAP2Link imports. That library is
loaded after the shim and is not `-Bsymbolic`; `iAP2LinkRunLoopCreateDevice`
reaches `iAP2LinkRunLoopCreate` through its PLT.

- `iAP2LinkRunLoopCreate` (type 1 = car, 0 = phone; the data callback is the
  sixth argument) swaps the stock callbacks `iap2_dev_recv_data` and
  `CarPlayIAP2Session::iAP2LinkDataReadyCB` for wrappers. Other links (Bluetooth,
  HID) keep their callbacks. The stock callback still sees every message.
- From the car's Identify the bridge keeps the 0x1E RouteGuidanceDisplayComponent
  verbatim (component 0x0010 from the hook). The phone 0x1d00 reply is rewritten in
  `iAP2LinkQueueSendData`: 0x5200/0x5203 are added to list 6, 0x5201/0x5202/0x5204
  to list 7, and the car's component is appended. A car without the component, an
  unexpected layout or no room leaves the stock Identify. If the phone rejects the
  Identify (0x1d03), the bridge turns itself off until the next restart.
- Car 0x5200/0x5203 go to the phone exactly like stock's own forwards. The last
  0x5200 is kept and replayed when stock attaches a new phone link, as it does for its
  own start cache. A new car Identify or 0x5203 clears it.
- Phone 0x5201/0x5202/0x5204 go to the car through `iap2_send_message_to_proxy_usb`,
  reassembled across link packets in the same way the stock handler does.
- Off: `-DCLUSTER_RGD=0`, `/mnt/UDISK/cluster_rgd.off`, or the shim's global off switch.
  Log lines start with `cluster_dual: rgd`; `rgd bridge on create=1 send=1 car=1 phone=1`
  confirms the binding at startup.

`check_native_abi.py` now also pins `lib/libiAP2Link.so` (argument order of
`iAP2LinkRunLoopCreate`). The host tests cover the Identify rewrite and its
refusals, the cached start and its replay, the split maneuver message, the other
session being ignored, the rejection fallback and no rewrite outside the phone callback.
First car test 2026-10-28 (dongle log `work/logs/applogs-2026-10-28_10-15-25-*.tar`,
~91 s session): `rgd car component=0x0010 len=86`; five car 0x5200 kept before the
phone link; phone Identify 361 -> 457 bytes, accepted (no 0x1d03); the cached 0x5200
replayed at 18.1 s; phone 0x5201, 0x5204 and 0x5202 relayed to the car from 64 s, after
which the car stopped retrying. The cluster stream ran alongside (1,047 frames, `bad=0
dropped=0 send_err=0`). Not yet covered: a long route, rerouting, phone Wi-Fi reconnect
mid-route (replay path) and a head-unit log of the same session.

### Validation and build

```bash
SANITIZE=-fsanitize=address,undefined sh scripts/test_hw301.sh
bash scripts/build_altscreen_hook.sh
bash dongle/hw301/work/tools/build_dual.sh release
```

Host tests exercise the production interposers with owned CF graphs and crypto
primitive doubles: normal /info filtering/forwarding, fresh native IDs, returned
ports/IPv6 scope, key-derivation inputs, filtering in both directions, full and
stream-only phone teardown, command failures/replay/stale completions, failed
crypto and high descriptor numbers. Fault injection also covers mixed versus
111-only SETUP failures (metadata, ID, derivation, both bind attempts, worker and
response allocation), unchanged stock errors, invalid car ports/peers, and a
fresh successful negotiation after each bad reply. Tests require local listener binding.
Target ARM/QNX builds and import checks verify linkage, not live interoperability.
No build or test flashes hardware. The first car test (below) covered the main
path. Phone Wi-Fi reconnect, unplug/replug, native suggestUI/menu/layout behavior
and a cold start with the dongle already plugged in are still to be tested in the
car before this candidate can be called a release.

Validation recorded on 2026-10-27:

- The review regressions passed with AddressSanitizer and UBSan on macOS:
  optional-cluster failure isolation, invalid reply recovery, independent stream
  lifetimes and real local listener binding. The GCC tests also passed on Linux.
- The ARM dongle candidate was rebuilt after the review fixes. All 41 non-weak
  shim imports resolve; inspected stock ABI hashes and image unpack/repack checks
  passed. The unchanged head-unit artifact retains its earlier QNX build validation.
- In the earlier broader run, repository tests passed through `vehicle_hook_test` on macOS. Its linker cannot
  run `nav_boost_test` (`--wrap`); that test and the remaining suite passed on Linux.
- OTA dry run: 4,143,616 bytes, 127 chunks, version 1172; no network upload.

Images rebuilt after the identity change (only `model` is rewritten) and the
head-unit wide-view switch:

| file | marker | SHA-256 |
|---|---|---|
| `fw/hw301_v1172.tar` (release, newest image) | `v1172-20261027093548` | `6db930a21bc6458f2e133319dbf9da6ee78f8b42f9f48c8cbf82bb73b5295203` |
| `fw/hw301_v1173.tar` (v1172 + logging) | `v1173-20261027093537` | `39a8ec6bf6db715e0b088d73a387edc3041980aa41b7ca90f42e8a4e1cee579a` |
| `build/libaltscreen111_mhi2q.so` (head unit) | | `245a1ce70e5d9bc007c7449a7f4616a8ae9b5ebfdf08492f722ea3bc66e31e6c` |

### First car test (2026-10-27, v1173)

Dongle logs `work/logs/applogs-2026-10-27_10-00-46-*.tar`, an ~8.5-minute session;
no head-unit logs were saved.

- Car SETUP (7.317 s) precedes the car `/info` (7.422 s), as the model-SETUP
  descriptor reset assumes. Stock CPAAProxyEx saw one display
  (`Recv proxy disply count:1`).
- The phone received the car's 111 descriptor, connected stream 111 and the
  dongle connected the car's returned port 7100 with AES on (`head-unit relay aes on`).
- 8,400 cluster frames, `bad=0 dropped=0 send_err=0`; every frame after the first
  was relayed. 15-30 fps while moving, ~1 fps on a static map.
- Two main-screen hand-overs to the MMI (110 teardown at 189.6 s / re-setup at
  273.8 s; 384.0 s / 402.4 s) left stream 111 running without a reconnect.
- MMI CarPlay, two calls, navigation prompts and two Siri sessions worked.
- The car sent showUI/updateViewArea only at connect (7.8 s / 8.0 s), so no
  layout or menu switch was exercised in this session.
- Cluster picture restarts: at 428-461 s iOS re-sent the video config ~6 times on
  both streams together (route guidance start); at 235-262 s the car sent ~8
  forceKeyFrame requests while the MMI showed the Audi screen. Cause of the
  latter needs the head-unit log.
- The kmsg ring kept only the last ~15 KB: lines before 10.6 s (`LOADED`,
  `native SETUP`, `native car display accepted`, `independent AES ready`) were
  already overwritten, mostly by the periodic frame-rate lines.

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
- `app.log` (`/mnt/UDISK/logs`, ~256 KB ring) survives power cycles. It apparently does not
  survive a `/getlogs` pull: the 2026-10-28 pull held only the 3 boots since the 2026-10-27
  pull, which had held 7.

## Historical v1167 phone side (dongle = AirPlay receiver)

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

## Historical v1167 head-unit side (dongle = CarPlay client)

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

## Historical v1167 cluster content and layout

- URL per cockpit layout (v1167):
  - wide (ViewArea 0, also `initialURL`): `maps:/car/instrumentcluster?showETA=yes&showSpeedLimit=yes&showCompass=yes`
  - classic and Sport (ViewArea 1, 2): `maps:/car/instrumentcluster?showETA=no&showSpeedLimit=no&showCompass=no`

  On a `VIEW` back-channel change whose URL differs from the last one sent, the dongle sends
  `showUI {uuid, url}` and then `updateViewArea`. The last URL is global (reset by every `/info`),
  so a new stream-111 thread does not re-send it. v1166 had the small URL for every layout
  (v1156-v1164 had `showETA=yes`). No `maneuverLayout`: `rightAligned` pushed the puck left
  of the card (issue #20), same as the head-unit hook's RGI-off URL. The bare `/map` URL is the "head unit draws RGI" variant.
  Change them with `-DCLUSTER_URL_WIDE=...` / `-DCLUSTER_URL_SMALL=...`.
- On the dongle path the **dongle** advertises the URL; the head unit serves stock `/info`. The
  MIB-menu maneuver toggle therefore has no effect on the dongle path.
- SafeArea for this A5 cockpit: height **270** (297 was wrong). Wide = 360,87,720,270,
  classic = 520,64,400,300 and Sport skin small map = 566,64,360,300 (*car test pending*). The
  small maps are 300 tall so third-party apps keep their turn card; Sport is mib2q-carplay-rgi's
  calibrated rect in our shifted canvas.
  The order must match the hook's ViewArea indices (0 wide, 1 classic, 2 sport): the VIEW
  back-channel carries only the index. Geometry can be overridden: `CLUSTER_W/H/FPS/WMM/HMM`,
  `CLUSTER_SAFE0_*` / `SAFE1_*` / `SAFE2_*`; the head unit's `mibr-carplay111-safearea*.conf` files
  do not reach the dongle.
- Cockpit switch: CPAAProxyEx drops `updateViewArea`, `showUI` and similar head-unit commands
  (`_ProcessProxyRequestCommand` knows only `disableBluetooth`, `requestUI`, `changeModes`,
  `forceKeyFrame`, `setLimitedUI`). The head-unit hook instead writes `"VIEW"` plus a u32 LE view
  index on the 111 socket. The dongle reads it and sends `updateViewArea{index}` to the phone (v1160).
- Key frames after a cold start (v1166): the phone sends an IDR only when a stream starts or
  when asked. After power-on the dongle's 111 stream reached the head unit about 1 s before the
  head unit's cluster renderer attached. Both IDRs had already passed, so the renderer never
  primed (`carplay_logs/hw301_coldstart`: `source_idrs=2 consumer_primed=0`, `keyframe intent
  rc=1`). On the dongle path the hook has no stream-111 control session (`control_session=0`),
  so its forceKeyFrame went nowhere. Now the hook sends `"KEYF"` + u32 (ignored) on the
  back-channel whenever a renderer attaches. The dongle calls `ask_keyframe` for it, at most
  once per second, and also asks once more 3 s after the relay attaches (`KF_LATE_S`), which
  covers older head-unit builds. A v1164 dongle ignores `KEYF`: its accumulator slides past
  unknown bytes until the next `VIEW`.
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
| 1162 | previous release = same source as 1160 with diagnostics off (`v1162-20261020152434`) | archived `fw/hw301_v1162.tar` |
| 1164 | release: third ViewArea (Sport skin small map 566,64,360,300), classic 520,64,400,300, `initialURL` without `maneuverLayout=rightAligned` (`build_dual.sh release`, `v1164-20261021080230`). Sport ViewArea *car test pending* | archived `fw/hw301_v1164.tar` |
| **1166** | release: v1164 + no ETA (`showETA=no`) + `KEYF` back-channel key frame requests and a late key frame 3 s after the relay attaches; fixes the blank cockpit map after a cold start (`build_dual.sh release`, `v1166-20261021090922`). *Car test pending* | archived `fw/hw301_v1166.tar` |
| **1167** | release: v1166 + per-layout URL: wide = ETA, speed limit and compass; classic / Sport = turn card only; `showUI` on a layout change (`build_dual.sh release`, `v1167-20261025192423`). *Car test pending* | archived `fw/hw301_v1167.tar` |
| 1168-1171 | private `hw301Cluster` `/info` extension, HELLO/KEY1 rekey and URL1 back-channel (release, optA and Input-key variants) | superseded by 1172, not shipped |
| **1172** | native proxy: car display and commands passed through, own 111 ID/port/AES, `model=HW301` with stock `18D70` (`build_dual.sh release`, `v1172-20261027093548`). First car pass 2026-10-27 (as 1173) | last car-tested image, `fw/hw301_v1172.tar` |
| 1173 | **same version as v1172**, also contains enabled logging (`build_dual.sh optA`, `v1173-20261027093537`). First car pass 2026-10-27 | `fw/hw301_v1173.tar` |
| **1176** | v1172 + route guidance bridge (`build_dual.sh release`, `v1176-20261104084344`). First car pass 2026-10-28 with build `v1176-20261028100645` of the same source (not committed; release image rebuilt 2026-11-04) | **newest image**, `fw/hw301_v1176.tar` |
| 1177 | **same version as v1176**, also contains enabled logging (`build_dual.sh optA`, `v1177-20261104084350`) | `fw/hw301_v1177.tar` |

## Open items

- **Route guidance bridge (v1176/v1177), remaining part.** The first pass worked. Still open:
  a long route with rerouting, phone Wi-Fi loss mid-route (the cached 0x5200 replay) and a
  head-unit log in the same session.
- **Main-screen validation, remaining part.** The 2026-10-27 test confirmed MMI
  CarPlay, Siri, calls and prompts with the candidate identity and the SETUP before
  `/info` order. Unplug/replug and phone Wi-Fi loss/reconnect are still open.
  Incoming mixed display/stream filtering still fails closed if allocating its
  filtered copy fails, rather than exposing 111 to the stock single-screen
  parser. Host tests cover that SETUP failure; main-screen continuity under
  memory pressure remains unverified on hardware.
- **v1172 native on-car validation, remaining part.** Main-only teardown/re-setup
  passed twice. Still open: the head-unit log line `model=HW301` and the 111
  connection ID (no head-unit log was saved), cold start, phone-only Wi-Fi loss,
  111-only reconnect, car reconnect, dynamic URL/layout switches (including the
  wide-view switch) and suggestUI round trips. Save the head-unit log in the same
  session as the dongle log.
- **Siri start cut.** The car answered the dongle's speech audio SETUP
  (`audio_type=5`, 16 kHz mono with microphone) after 1.91 s and 1.58 s, against
  0.06-0.36 s for media and prompts, while iOS was already speaking. Likely
  pre-existing; no earlier log has a Siri session. Options: hold the phone-facing
  speech SETUP reply until the car's completes, or buffer up to ~2 s. Needs a
  head-unit log to see where the car spends the time.
- **Car key frame requests during MMI screen changes.** ~8 forceKeyFrame in 25 s
  while the MMI showed the Audi screen, each restarting the cluster picture.
  Check the head-unit resync logic against `altscreen111.log`.
- **Dongle log volume.** The frame-rate line (every 10 s and every 300 frames),
  the per-config line and `phone screen security` overwrite the start of the
  session in the small kmsg ring. Rate-limit them when the logging is next touched.
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
- Feeding two displays directly into the unmodified stock proxy ended the session.
  v1172 revisits native advertisement with a replacement boundary that hides 111
  from both stock parsers and owns its socket/crypto separately.
- Mirror build (v1129, gate-NOP at file offset 0x4c9a8): it shows the main screen on the VC. Superseded.
- Carlinkit firmware (ludwig-v repo) is the same single-screen bridge, so there was nothing to borrow.

## Reference material

- `work/ghidra/relay_decomp.txt` (from `DumpRelay.java`): the CPAAProxyEx single-screen video pipeline.
- `work/ghidra/dispparse.txt` (from `DispParse.java`): the head-unit display-info parser.
- `work/ghidra/cb_full.txt`: the receiver delegate callback (the display/audio info builder).
- `work/tools/xref.py`, `work/tools/findref.py`: string and address xrefs for ARM PIC binaries
  (need capstone and pyelftools in `work/.venv`).
