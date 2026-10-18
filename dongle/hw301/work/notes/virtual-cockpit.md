# HW301 dongle — adding Virtual Cockpit (CarPlay cluster) support

Goal: make the wireless adapter advertise a **second CarPlay display** (the
Virtual Cockpit cluster) to the iPhone, negotiate its video stream, and relay
it — the same stream type (111 / AltScreen) the car side already consumes over
a USB cable. See the project README TODO and
`docs/deploy/altscreen-mhi2q.md`.

## Target firmware
Unit reports **`HW301_20250630126`** → versioncode **126** (v1.2.6), build
2025-06-30. All patch work targets `fw/v126`. Rootfs extracted to
`work/extract/v126`. (v129 kept only for comparison.)

## Status
- [x] Reproducible unpack/repack pipeline, byte-exact round trip (`work/tools/repack.sh`)
- [x] Local flasher that replaces the vendor server (`work/tools/flash_local.py`)
- [x] Static-analysis tooling (venv: capstone + pyelftools; `work/tools/xref.py`)
- [x] Located the CarPlay display-setup code in `CPAAProxyEx`
- [x] Cross-toolchain set up + verified (`work/tools/xcc.sh`)
- [x] Found clean interposition point + recovered its signature (v126)
- [x] Step-1 shim written + compiled (`work/src/cluster_shim.c`)
- [x] Injection mechanism designed + proven on musl (wrapper-rename; see below)
- [x] `build.sh` produces a ready-to-flash modified tar + verifies it
- [ ] Rollback test: re-flash stock v126 with `flash_local.py` — ON DEVICE, do first
- [ ] Flash the shim build and observe whether the phone opens a 2nd stream
- [ ] Relay that stream to the car as type 111 (the remaining hard part)

## Injection: "wrapper rename" (proven, no CPAAProxyEx edit)
`build.sh <code>` (default code 1126) assembles the image:
  1. `libAirPlay.so` (stock) → copied to `libAirPlayReal.so`, soname **set to
     libAirPlayReal.so** (patchelf) so musl does not dedup it against the shim.
  2. `cluster_shim.c` built AS `libAirPlay.so` (soname `libAirPlay.so`, NEEDED
     `libAirPlayReal.so` + `libc.so`), installed over the stock file.
  3. CPAAProxyEx is untouched; it still NEEDs `libAirPlay.so` → loads the shim →
     shim overrides `AirPlayInfoArrayAddScreenDisplay`, forwards everything else
     to `libAirPlayReal.so` via its NEEDED and `dlsym(RTLD_NEXT)`.

Validated empirically on arm64 musl (alpine) in `work/exp/`:
  - TEST 1 — exact topology (exe NEEDs only the shim; shim NEEDs real): override
    wins, `RTLD_NEXT` reaches the real impl, real-only symbols resolve transitively.
  - musl loads a dependency **by filename**, ignoring its internal soname.
  - musl would **dedup two libs sharing a soname** → hence step 1 sets a distinct
    soname on the renamed real lib. Confirmed distinct sonames load both.
Facts that make it safe: only `CPAAProxyEx` references `libAirPlay.so`, and only
`libAirPlay.so` defines the symbol (checked across all libs).

## Ready artifact
`work/out/hw301_v1126.tar` — stock v126 + shim. app.img 4,734,976 B (stock v126
4,730,880; +4 KB, fits the partition). Flasher dry-run: 145 chunks, well-formed,
identical structure to vendor chunks. NOT flashed.

## Interposition point (v126) — the key find
`CPAAProxyEx` advertises its single CarPlay display by calling an **imported**
libAirPlay function, so we can override it without touching the binary's code:

  `AirPlayInfoArrayAddScreenDisplay(holder, uuid, features, primaryInputDevice,
     widthPixels, heightPixels, widthPhysical, heightPhysical, maxFPS)`

  - Exported by `libAirPlay.so @0x1afe8` (Apple AirPlay SDK). It *appends* one
    display dict to the array in `holder`.
  - Signature recovered by disassembly: r0=holder, r1=uuid, r2=features,
    r3=primaryInputDevice, then 5 stack args (w,h,wPhys,hPhys,fps). Dict keys
    seen near the setters: widthPixels/heightPixels/widthPhysical/... .
  - `work/src/cluster_shim.c` forwards the main display unchanged, then calls the
    real function again (same holder, uuid=NULL→SDK makes a fresh one, 1440x540)
    to append the cluster display. Built `.so`: ARM EABI5 hard-float (matches
    dongle libs), exports the symbol, NEEDED=libc.so only. Env tunables
    CLUSTER_W/H/FPS and CLUSTER_SHIM=0 kill-switch.

## Deployment mechanism (app-partition only — no base firmware)
- The launcher that execs `CPAAProxyEx` is **not** in `app.img` (confirmed: no
  script in the partition references it) — it lives in base firmware. So we
  CANNOT use `LD_PRELOAD` via an init script by reflashing `app.img` alone.
- Instead inject a **DT_NEEDED** on `cluster_shim.so` into `CPAAProxyEx` and ship
  the `.so` in `/mnt/app/lib`. Everything stays in `app.img`.
- **Ordering requirement:** ELF resolves the executable's symbols in DT_NEEDED
  load order. `libAirPlay.so` is NEEDED #10. Our shim's entry must come **before**
  it so the shim's `AirPlayInfoArrayAddScreenDisplay` wins; and `dlsym(RTLD_NEXT)`
  then finds libAirPlay's real one (loaded later). `patchelf --add-needed`
  appends (→ after libAirPlay → loses), and patchelf isn't in the toolchain
  image. TODO: either install patchelf, or insert the DT_NEEDED as the first
  entry via pyelftools, then verify order with `readelf -d`. Validate symbol
  precedence locally first (x86 musl in the container: two libs, same symbol,
  check which wins by load order) before trusting it on-device.

## Step 2 (relay) — data path mapped (v129), and the blocker
Traced the video path through CPAAProxyEx:
  phone --AirPlay--> `recvCarPlayVideoFrame(buf,len,flag)` --> `writeCarLifeVideoFrame`
  --> single **ProxyScreen** --> `SendProxyScreenFrame` --USB CarPlay--> head unit.
Findings:
  - `handleVideoInit(gal::DisplayType, …)` / `handleVideoStart/Stop(DisplayType)`
    ARE display-typed (AirPlay receiver callbacks), BUT the frame path
    `recvCarPlayVideoFrame` carries **no display id** and funnels every frame to
    the one main screen (`sendMainScreenModes`, a single first-frame flag).
  - `SetupProxyScreenEx` and `SendProxyScreenFrame` operate on a **single global
    screen context** (one mutex-guarded singleton at base+0x260 / +GOT). There is
    exactly ONE screen presented to the head unit.
**Blocker:** the dongle's head-unit-facing CarPlay is single-display. Our shim can
make the phone *send* a 2nd (cluster) stream to the dongle, but the dongle has
nowhere to put it: no second ProxyScreen, and the head unit never negotiates a
cluster display with the dongle. Relaying the map therefore requires implementing
a **second display on the dongle's SENDER side** (a 2nd ProxyScreen setup + a 2nd
`SendProxyScreenFrame` path + routing cluster frames to it by DisplayType + the
head unit advertising that display to the dongle). That is large binary-level work
in a stripped binary, not a shim — and still gated on Step-1 logs.

## Step 2 implementation plan (phased) + patch targets (v129)
The relay cannot be interposed (SendProxyScreen*/SetupProxyScreen* are internal
to the stripped executable, not library imports), so Step 2 is in-binary work.
Call sites found for the sender-side injection:
  - `SetupProxyScreenEx` callers: 0x3734c, 0x37400
  - `SetupProxyScreen`   callers: 0x48c44, 0x48e44
  - `handleVideoInit(DisplayType,…)`: vtable-dispatched (0 direct bl) — it is the
    gal/AirPlay receiver video callback; DisplayType distinguishes streams here.
  - frame path `recvCarPlayVideoFrame`→`writeCarLifeVideoFrame`→`SendProxyScreenFrame`
    is single-screen and must become display-aware.

Phases:
  0. **Read the live negotiation (REQUIRED FIRST, from app.log).** Determines
     which of two very different builds we are in:
       (a) head unit already advertises a cluster display to the dongle, and the
           phone opens a 2nd stream once our shim offers it → medium patch: route
           the 2nd stream to a 2nd ProxyScreen that already has a peer. OR
       (b) neither side offers a 2nd display → large: synthesize the whole 2nd
           display on the sender and drive the head-unit AltScreen negotiation.
     Signals in `app.log`: `[cluster_shim] CLUSTER … rc=`; a 2nd `handleVideoInit`
     with a different DisplayType; any head-unit display/stream-111 setup line.
  1. Make the sender path display-aware: a 2nd ProxyScreen context + 2nd
     SendProxyScreenFrame; route cluster-DisplayType frames to it.
  2. Ensure the head unit negotiates the cluster display with the dongle (depends
     on 0a/0b).
  3. Decode/scale as needed; match the 1440x540 type-111 the car side expects.
Each phase flashes + tests + keeps the stock rollback ready.

## Honest scope checkpoint
Step 1 (this shim) only tests whether the **iPhone will advertise/open a second
video stream** when offered a second display. It does NOT yet put the map on the
cluster: the opened stream still has to be decoded and handed to the car as the
type-111 AltScreen stream the car side already consumes over USB. That relay is
the next, larger step. Step 1 is worth doing because if the phone refuses the
second stream, the whole approach is dead and we've risked nothing but a flash.

## SOLVED (2026-10-09): flashing works — root cause was upload JSON format
The dongle's hand-rolled JSON parser needs the chunk body EXACTLY like the vendor:
compact, no space after `:` or `,` (bare `/`, NOT escaped). Python's default
`json.dumps` adds spaces → the parser mis-extracts `data` → reassembled image is
subtly corrupt → write aborts ~10% with `{"result":0}`. Fix: `vendor_json()` =
`json.dumps(obj, separators=(",",":"))`. Also required: clean Linux ustar tar (no
macOS `._*`/PaxHeader), trailing newline in appmd5sum.txt, and driving the sliced
write via repeated `/requestupdate`. With all that, the write climbs 1→100 and
persists. VERIFIED on device: after re-plug, `/clbuild.txt` served, and the shim's
constructor logged to app.log AND dmesg:
  `cluster_shim: LOADED pid=546 real_AirPlayInfoArrayAddScreenDisplay=0xb6696fe8`
So delivery + wrapper-rename injection + logging all work end to end. (The earlier
"umount/killall name mismatch" theory was WRONG — on-device update works fine.)

## NEW BLOCKER: our hook is not on the wireless display-advertisement path
Only the shim's `constructor` fired; `AirPlayInfoArrayAddScreenDisplay` is never
called during the wireless session (no ENTER/main/CLUSTER lines). Findings:
  - CPAAProxyEx imports it and has ONE call site (0x5afa4) but that path isn't
    exercised in the wireless flow (likely the wired/`/info`-GET path).
  - v129 CPAAProxyEx has TWO display-key clusters: A @0x150ee0 (full set:
    displays/widthPixels/heightPixels/primaryInputDevice/maxFPS/viewAreas/
    displayUUID — the AddScreenDisplay path) and B @0x156188 (displays/width/
    height/displayUUID/viewAreas only — a second, internal builder).
  - The internal builder (B) assembles the dict itself (CFDictionarySetValue from
    libCoreUtils) and is INTERNAL to CPAAProxyEx → not interposable via the lib
    wrapper. References are GOT-indirect (PIE), so static xref is slow.
Options to hook the real path:
  1. Runtime probe: expand the shim to log other libAirPlay imports actually
     called in-session (needs correct/≤4-arg signatures to forward safely).
  2. Binary-patch CPAAProxyEx's internal builder (B) to emit a 2nd display (now
     feasible since flashing works).
  3. Interpose the CF calls (fragile; used everywhere).
REMINDER: even with the 2nd display advertised to the phone, the sender side to
the head unit is still single ProxyScreen (see "Step 2 ... blocker"), so the full
map relay remains a larger in-binary project.

## Probe results (2026-10-09): display path is a delegate callback, not interposable
Ran a 30-function logging probe (naked ARM trampolines; note: must push an EVEN
reg count — `push {r0-r3,r4,lr}` — or sp misaligns and the proxy crashes; that
briefly broke main-screen CarPlay, fixed). During a live wireless session:
  - CALLED: AirPlayCreateModesDictionary(x2), AirPlayInfoArrayAddAudioFormat/
    Latency, AirPlayInfoArrayAddHIDDevice, AirPlayReceiverServerCreate/Control/
    SetDelegate, AirPlayReceiverSessionSetDelegate/CopyProperty/ForceKeyFrame/
    GetDeviceID/MakeModeStateFromDictionary/SendCommand/SendiAPMessage,
    CarPlayControlClient{Connect,CreateWithServer,DispatchEvent,Restart,Start},
    CarPlayController{CopyName,GetBluetoothMacAddress,GetInterfaceName}.
  - NOT called: AirPlayInfoArrayAddScreenDisplay (#5). And libAirPlay has ZERO
    internal callers of it either. So the `/info` "displays" array is NOT built
    via the SDK helper anywhere in the wireless flow.
Conclusion: the dongle provides display info to the AirPlay receiver server
through a DELEGATE (server calls back into CPAAProxyEx via function pointers set
by AirPlayReceiverServerSetDelegate). That callback is internal to the stripped
PIE → not reachable by name interposition. To add a 2nd display now requires
either (a) binary-patching CPAAProxyEx's internal display-info builder (cluster B
keys @0x156188), or (b) wrapping the delegate vtable (needs the delegate struct
layout). Both are substantial; and the sender-side single-ProxyScreen limit still
blocks the actual relay to the VC afterwards.
Net: the delivery/inject/log/probe TOOLCHAIN works and is reusable; the cluster
feature is a large in-binary project from here with uncertain payoff.

## (SUPERSEDED) earlier theory: self-flash does not persist on this unit
Empirically, across many attempts, flashing never writes the app partition:
  - upload of all 127 chunks succeeds; `/requestupdate` advances percent 1→3→8→10
    then returns `{"result":0}` and the update state resets (`pos:0,percent:0`).
  - the web build marker (`/clbuild.txt`) always 404s afterwards, and every boot
    banner is the stock `CPAAProxe 20260122129 start!` — the device always runs
    stock; the shim has NEVER loaded (no `cluster_shim` line via app.log or kmsg).
Root cause (strong inference, not yet shell-confirmed): the updater does
`umount /mnt/app` before `dd`, but the running proxy is `CPAAProxyEx` while the
updater's `killall` targets `CarPlayProxy`/`CarLifeProxy`/`CarLinkProxy` (name
mismatch), so the proxy is never stopped (app.log shows an unbroken 30s
"enable apple device udc" cadence through every flash), `/mnt/app` stays busy,
`umount` fails (~10% mark), and the updater aborts without writing. UpdateServer
logs this only to stdout/`/tmp/appupdate.log` (not app.log/dmesg), so we can't
read the exact errno remotely.
Fixed along the way (real defects, but not the cause): macOS bsdtar injected
`._*`/`PaxHeader` members (now a clean Linux ustar tar, build.sh asserts it);
appmd5sum.txt now has the trailing newline; flasher drives the sliced write via
repeated `/requestupdate` and detects the reset/`result:0` abort.
Consequence: the shim/relay work is sound but UNDELIVERABLE via the web updater
on this unit in its running state. Earlier "cover art on VC" was stock behavior,
not our change (our image never ran).
Remaining delivery avenues (all unverified / out-of-band):
  - P2P update mode (`setupp2p`): vendor's `BoxUpdate-P2P`; may stop the proxy,
    but switches to Wi-Fi Direct (drops our AP) and expects an Android client.
  - Hardware flash: Allwinner FEL over USB (sunxi-fel) or serial/eMMC access to
    write the `app` partition directly, bypassing the broken updater. Needs
    physical access + tools; real brick risk.
  - Use a different wireless adapter that implements the cluster natively
    (issue #5 Baseus / Carlinkit 2Air).

## FLASHING RULE
Flashing is the only irreversible, brick-risk step (UpdateServer + wpa_supplicant
live on the `app` partition being overwritten). Do NOT flash without: (a) the
user present, (b) a confirmed stock-v126 rollback tar, (c) a successful dry-run,
(d) having re-flashed *stock* once to prove the Wi-Fi flash path recovers.

## Toolchain (done)
Docker image `muslcc/x86_64:arm-linux-musleabihf` (gcc 11.2.1, unprefixed,
pre-targeted to arm-linux-musleabihf). Run via `work/tools/xcc.sh <cmd...>`
with `dongle/` mounted at `/src`. Verified: output `.so` has the **same ABI
flags as the dongle libs** (`0x5000400`, Version5 EABI, hard-float) and only
`NEEDED libc.so`, which musl self-resolves → LD_PRELOAD shim will load on-device.
  `./work/tools/xcc.sh gcc -shared -fPIC -Os work/src/x.c -o work/out/x.so`

## Platform facts (confirmed)
- SoC: Allwinner (sunxi), Linux 4.9, musl libc, ARM EABI5 (32-bit LE).
- Wi-Fi/BT: AIC8800 (`aic8800_fdrv.ko`), `blueware` (BlueKitchen BT stack).
- All CarPlay logic: one dynamically-linked ARM binary, `bin/CPAAProxyEx`.
  It is **not fully stripped** — `.dynsym` keeps many CarPlay/AirPlay symbols,
  which is what makes this tractable.
- OTA image `app.img` = xz SquashFS, block 262144, all-root. **Unsigned**:
  UpdateServer only checks `md5(app.img) == appmd5sum.txt` before `dd` to
  `/dev/by-name/app`. So a self-built image with a matching md5 flashes.
- v1.26 vs v1.29: AA code split into `libAutoProxy.so`; bundled OpenSSL 1.1;
  no CarPlay cluster code added in either. `altScreen` string absent from both.

## Flash path (how our own image reaches the device)
Web UI `requestuploadappdatasDatas()` POSTs vendor chunk JSON to the dongle's
`/uploadappdatas`; dongle reassembles `/tmp/appupdate/appupdate.tar`, checks
md5, `dd`s `app.img`. `flash_local.py` reproduces that chunk JSON from a local
tar (schema confirmed from the live server: keys
`result,version,pos,itemsize,count,filesize,datasize,data`). Nothing contacts
the vendor (`43.138.184.52`) or the car.

**Brick risk:** `UpdateServer` and `wpa_supplicant` live on the `app` partition
that gets overwritten. A bad image removes the ability to re-flash over Wi-Fi;
recovery then needs Allwinner FEL / serial console. Keep stock v1.29 tar as the
known-good rollback and test that `flash_local.py` can re-flash it first.

## Where the display is advertised (addresses are v1.29 `CPAAProxyEx`)
Exported symbols (`.dynsym`, so real entry points):

| addr | symbol | role |
|---|---|---|
| 0x5c0d0 | `SetupProxyScreen` | tail-calls `SetupProxyScreenEx(0x1388)` (5000) |
| 0x5c05c | `SetupProxyScreenEx` | sets up proxy screen singleton |
| 0x5c05c→ | (calls 0x5bae0, 0x35d34) | screen security / mode init |
| 0x59014 | `set_carplay_screen_size2(i,i,i,i,i)` | stores W/H/… into a global struct |
| 0x58efc | `set_carplay_screen_size(i,i,f)` | legacy single-size setter |
| 0x35624 | `getProxyInitModes` | copies a 44-byte init-modes template |
| 0x35940 | `_SendAirPlayModeStatesToProxy` | pushes mode state to phone |
| 0x5b31c | `sendMainScreenModes(i,i,i,i)` | main-screen mode change |
| 0x3fe34 | `CarPlayProxyApp::handleVideoInit(gal::DisplayType, MediaCodecType, int,int, Insets&)` | **video init is already keyed by DisplayType** |
| 0x3fcec | `handleVideoStart(gal::DisplayType)` | |
| 0x3fd24 | `handleVideoStop(gal::DisplayType)` | |

Display dictionary keys live in two `.rodata` clusters:
`0x150ee0` and `0x156188` — each has `displays / widthPixels / heightPixels /
viewAreas / displayUUID`. One of these builds the CFDictionary array sent to the
phone with a **single** entry. That array is the thing to extend.

## Patch strategy (ranked by risk)
1. **Preferred — hijack the display-array builder.** Find the function that
   assembles the `displays` CFArray (references the `0x156188` key cluster),
   and append a second display dict (cluster UUID, `widthPixels=1440`,
   `heightPixels=540`, a `viewAreas` entry, a distinct `displayUUID`). Then the
   phone will offer a second video stream. This is new code in a binary with no
   free space → needs a code cave or an added section, plus fixing up the
   literal pools. Then route that stream's `handleVideoInit/Start` (DisplayType
   already distinguishes it) out over TCP so `altscreen_render` on the car can
   consume it — matching the existing cable path.
2. **Fallback — LD_PRELOAD shim.** The startup scripts (`/mnt/app/...`) launch
   `CPAAProxyEx`. Add a small cross-compiled `.so` that interposes the CFArray
   build (or the Apple `AirPlayReceiverServer` display-info callback) and injects
   the second display, leaving the stock binary untouched. Far lower brick risk,
   independently testable, removable. **Start here.** Needs an arm-linux-musl
   cross toolchain (see below).
3. **Last resort — rewrite the proxy's CarPlay display negotiation.** Large,
   and still gated on the phone agreeing to send stream 111 and the car
   accepting a wireless-origin cluster stream.

## Open questions that change the plan
- Which firmware version is actually on the unit? (`getboxinfos` → shown next
  to `HW301_` in the web UI; our dump didn't save it.)
- Issue #5's working adapters (Baseus / Carlinkit 2Air) reportedly show the
  cluster map on v1.1. If true, they already advertise the second display — a
  far cheaper answer than patching this one. Need their `/tmp/altscreen111.log`
  (`SETUP … types=111`) and the phone identity they present.

## Toolchain still needed
- `arm-linux-musleabihf` cross toolchain (for option 2's `.so` and any new code
  in option 1). Not yet installed on this Mac; `brew`/docker available.
