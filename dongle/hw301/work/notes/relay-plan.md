# Full flow: CarPlay cluster map on the Virtual Cockpit via the HW301 dongle

## ✅ WORKING (2026-10-12): map on the Virtual Cockpit, stable on both screens
Achieved end to end. Verified on the car: main MMI + Virtual Cockpit both show
CarPlay (the map on the VC while Maps is up), stable (~1 min+, no crash loop).

The solution has THREE parts, all produced by one build:
`WRAP_LIB=libCoreUtils.so CHECK_SYM=CFArrayEnsureCreatedAndAppend
 CLUSTER_SRC=cluster_inject.c PATCH_CPAA="0x4c9a8:00 0x4c9a9:f0 0x4c9aa:20 0x4c9ab:e3"
 ./work/tools/build.sh 129`
  1. INJECT (libCoreUtils shim, `cluster_inject.c`): interpose
     CFArrayEnsureCreatedAndAppend; when the SetupScreen "streams" array gets the
     main stream dict (type 110), clone it as type 111 and append -> the dongle
     advertises a 2nd (cluster) stream to the head unit, which allocates it
     (data_port, err=0).
  2. GATE PATCH (CPAAProxyEx @file 0x4c9a8): NOP the `bne` after `cmp sb,#0x6e`
     in _AirPlayClientSendSetupScreenCommandCompletion so the per-stream setup
     loop sets up BOTH type 110 and 111 (two head-unit video sockets), not just
     110.
  3. DUAL-SEND MIRROR (same shim): interpose SocketConnect (caller offset
     0x4cac4 = the video connect site) to capture the two head-unit video fds,
     and interpose write() to mirror every frame written to one video socket onto
     the other. Both head-unit streams share crypto keys
     (SetupProxyScreenSecurityInfo uses one global) + stay counter-locked because
     every frame is mirrored, so the identical ciphertext decrypts on both. Main
     MMI (110) is thus fed -> session stays alive; VC (111) shows the stream.
Note: this MIRRORS the main CarPlay to the VC (both show the same image). A
dedicated cluster view (normal CarPlay on main, separate nav on VC) would require
the phone to render a distinct cluster stream, which needs a cluster DISPLAY
advertised to the PHONE (phone-facing /info in libAirPlay) — not done; the mirror
is the shipped result.
Recovery: `flash_local.py fw/hw301_v129.tar --version 129`.

## Phone-facing cluster advertisement (implemented, not flashed)

`work/src/cluster_screen.c` wraps `libAirPlay.so` and performs the head-unit
AltScreen contract toward the iPhone:

- `AirPlayCopyServerInfo` merges `altScreen` and `viewAreas` into
  `enabledFeatures` and appends a type-111 display (1440×540, the cluster
  UUID, `maps:/car/instrumentcluster`, suggest URLs, wide and classic safe areas).
- `AirPlayReceiverSessionSetup` calls stock first. If stock fails a request that
  contains type 111, it retries without 111 so stream 110 still negotiates.
  It does not invent a dataPort.
- Screen setup and frame callbacks log each distinct screen pointer.

`libAirPlay` reaches those functions through its PLT, which is why this shim
sees them. The head-unit socket is unchanged and the iPhone9,1 guard stays on.
Build, do not flash from the car:

```
WRAP_LIB=libAirPlay.so CHECK_SYM=AirPlayCopyServerInfo \
CLUSTER_SRC=cluster_screen.c ./work/tools/build.sh 129 1131
```

Disable without reflashing: `touch /mnt/UDISK/cluster_screen.off` (or
`CLUSTER_SCREEN=0`). A session where the setup response lists type 111, and a
second `screen setup` pointer appears, is the signal to wire that stream to a
second head-unit socket. Until then the Virtual Cockpit still shows nothing
new: this image only makes the iPhone encode the cluster.

---
(historical analysis below)

# (old plan) CarPlay cluster map on the Virtual Cockpit via the HW301 dongle

Goal: the Virtual Cockpit shows the CarPlay map, wirelessly, through this dongle.

## Data path (two CarPlay links, dongle in the middle)
```
iPhone  ──wireless AirPlay/CarPlay──▶  DONGLE  ──USB CarPlay──▶  Head unit (MHI2Q)
         (dongle = AirPlay receiver)            (dongle = CarPlay control client)
```
- Phone→dongle: the dongle is the AirPlay **receiver**; it advertises its display(s)
  to the phone; the phone sends one H.264 video stream per advertised display.
- Dongle→head unit: the dongle is a CarPlay **control client**; the head unit (with
  this project's AltScreen) can advertise a **cluster display**; the dongle must send
  it a video stream for that display.
- Today the dongle brokers exactly ONE display (1024x480 main). The map needs a
  SECOND display (cluster, ~1440x540) brokered end to end.

## Open question that splits the work (resolve FIRST, at runtime)
Does the head unit already advertise a cluster display to the dongle over the
CarPlayControlClient channel?
  - YES → the dongle is just dropping the 2nd display; we "only" need to relay it
    (advertise it to the phone + pump the returned stream to the head unit).
  - NO  → we must synthesize the 2nd display toward the phone AND drive the head
    unit's AltScreen negotiation from the dongle. Much larger.

## Phases
0. **Instrumentation / introspection (current).** Generalized wrapper build
   (`WRAP_LIB`) lets us interpose any lib. Dump the display negotiation:
   what CarPlayControlClient* carries from the head unit, and what the AirPlay
   receiver advertises to the phone (ServerControl params, CopyProperty, the
   delegate). Output decides YES/NO above and the exact injection point.
1. **Advertise a 2nd display to the phone.** Inject a cluster display into
   whatever builds the receiver's display list (delegate callback or internal
   dict builder → likely a CPAAProxyEx binary patch, since it's not an
   interposable symbol). Verify the phone opens a 2nd video stream
   (handleVideoInit with a 2nd DisplayType).
2. **Receive + route the 2nd stream.** recvCarPlayVideoFrame is single-display;
   make it distinguish the cluster stream (by DisplayType/stream id).
3. **Send the 2nd stream to the head unit.** Today SetupProxyScreen/
   SendProxyScreenFrame are a single global screen. Add a 2nd ProxyScreen bound
   to the head unit's cluster display, and pump cluster frames to it.
4. **Match the car side.** 1440x540 type-111 AltScreen the project already
   consumes over a cable; ensure geometry/codec match.

## Phase-0 result (2026-10-10): display setup is in-protocol, not interposable
CF tracer (wrapping libCoreUtils): the display geometry 1024x480 is NEVER set via
CFDictionarySetInt64 (only audio formats are). So the display is NOT advertised to
the phone via a CF "displays" dict, and NOT via AirPlayInfoArrayAddScreenDisplay
(probe: never called). It is carried in the CarPlay control protocol: the dongle's
internal SetupProxyScreen -> SetupScreen command + SendProxyScreenVideoConfigFrame
(sps/pps 1024x480). All of this is internal to the stripped PIE CPAAProxyEx; the
geometry originates from set_carplay_screen_size2() fed by the head unit.
Cross-checked with the head-unit side (project docs): the dongle is the sender
that never requests stream 111. So the head unit DOES advertise the cluster to the
dongle; the dongle's proxy simply brokers a single screen and never asks for it.

CONCLUSION: there is no interposition shortcut. Enabling the map requires
BINARY-PATCHING CPAAProxyEx to broker a second screen end to end:
  - advertise/negotiate a 2nd screen toward the phone (SetupScreen path),
  - request + receive its video (recvCarPlayVideoFrame, make display-aware),
  - set up a 2nd ProxyScreen toward the head unit and pump frames (type 111).
That is a large reverse-engineering + patching effort on a stripped,
position-independent binary, with real (recoverable) brick risk and uncertain
success. The delivery/inject/log/probe/trace toolchain all works and is reusable;
it just can't shortcut the in-binary protocol work.

Concrete next technical step if pursuing: reverse set_carplay_screen_size2 and the
SetupProxyScreen/SendProxyScreen* flow in v129 (all are exported symbols), map the
single-screen state machine, and prototype a 2nd-screen patch.

## MILESTONE (2026-10-11): phone ACCEPTS an injected 2nd CarPlay stream
cluster_inject.c (wraps libCoreUtils, hooks CFArrayEnsureCreatedAndAppend) clones
the main stream dict, sets type=111, appends it to the SetupScreen "streams"
array. On device, verified in app.log:
  - `appended cluster stream (type=111)` (our injection fired)
  - SetupScreen request body grew 85 -> 209 bytes
  - phone response: `arrray size = 2`, `type = 110` then `type = 111`
  - MAIN SCREEN STILL WORKS; MFi-auth errors are pre-existing noise (present with
    a working main screen), not caused by us.
So the phone round-trips a 2-stream SETUP. This is further than documented prior
art for this platform.

## CURRENT BLOCKER: dongle drops stream 111; handler not locatable with CLI tools
In `_AirPlayClientSendSetupScreenCommandCompletion` the per-stream loop sets up
110 fully (`data_port=6030`, `init video socket`) but for 111 logs only
`type = 111` then moves on - no port, no socket, no error. Two candidate causes,
unresolved: (a) dongle gates setup to the main stream type; (b) phone's 209-byte
response gives 111 no dataPort because it has no cluster DISPLAY to drive (we only
added a stream, not a display; "type 111" may not be a real phone-side stream type
- the AltScreen/111 concept is head-unit-facing).
Could not locate/patch this handler: its format strings (0x14fa88 "arrray size",
0x156ea8 "data_port..", 0x156d2c fname) have NO detectable xref - not via inline
add-pc, pointer words, movw/movt, nor cross-reg offset; radare2 `aaaa`+`axt` and
value-search `/v4` ALSO find nothing. The binary addresses rodata through a
scheme (likely a GOT/base register, possibly Thumb regions) that defeats capstone
hand-resolvers and r2's auto-analysis. A probe interposing CFDictionaryGetInt64
(3-arg, verified) logged nothing during setup -> the handler reads type/port via a
different getter (CFNumber or CFDictionaryGetInt64Ranged) or a parsed struct.

## Honest status
Finishing needs: (1) locate+patch the setup handler to wire stream 111 (Phase 2),
(2) likely advertise a cluster DISPLAY so the phone actually drives 111, (3) relay
to the head unit's cluster (single-ProxyScreen -> dual, Phase 3). Each is a binary
patch of a stripped PIC binary that resists CLI static analysis. Realistic routes:
interactive Ghidra/IDA (with manual PIC+Thumb setup) to pinpoint handlers, or slow
runtime trial-and-error (one car trip per iteration). The flash/inject/log/probe
toolchain all works and is reusable.

## FULL PIPELINE MAP (Ghidra, 2026-10-11) — dual-screen = rewrite, not patch
Decompiled the whole video path. It is a SINGLE-screen bridge end to end:
  phone --H264--> recvCarPlayVideoFrame(frame,len,isConfig)
       --> writeCarLifeVideoFrame(frame,len,nalType)
       --> SendProxyScreenVideoConfigFrame / SendProxyScreenFrame
       --writen(single socket, AES-CTR/chacha20)--> head unit
Single-instance state throughout:
  - ONE head-unit video socket (SendProxyScreenFrame writes `*(int*)(…0x5c430)`),
    ONE crypto context (…0x5c428/0x5c514), ONE config.
  - handleVideoInit(DisplayType,…) nominally takes a DisplayType but stores W/H/
    insets into ONE getProxySessionInfos() struct (+0x240/0x244/0x230…); the type
    is not used to fork a second pipeline.
  - recvCarPlayVideoFrame reads ONE global video socket; writeCarLifeVideoFrame
    forwards to ONE SendProxyScreen* (or the gal/CarLife path when this+0x2424==2).
We PATCHED the phone-facing SETUP handler (FUN_0005c7ec) so the dongle sets up
BOTH phone streams 110 and 111 (verified: data_port 6030 + 7100, both err=0, two
sockets). But there is NO second send path to the head unit and no second
recv/crypto/config context, so only one stream can be carried; routing 111 to the
single screen blanks the main screen and the car restarts.

Conclusion (evidence-backed): making the Virtual Cockpit show the map requires a
SECOND, parallel screen pipeline inside the proxy — a second head-unit screen
connection (its own SETUP + socket + crypto), a second receive path, and routing
phone-111 -> head-unit-cluster — i.e. reimplementing the proxy's screen manager.
The existing exported primitives (SetupProxyScreen/SendProxyScreenFrame/…) all act
on the single global context and cannot be re-invoked for a second screen. This is
a rewrite, not a sequence of byte patches or small code caves.

## What was definitively achieved
- Toolchain: flash/inject/log/probe + Ghidra headless, all working & reusable.
- Phone side SOLVED: inject stream 111 into SETUP; phone accepts and allocates a
  port (err=0). Handler binary-patched to set up stream 111.
- Dongle side CHARACTERIZED: single-screen bridge; dual-screen needs a rewrite.

## DEDICATED CLUSTER plan (2026-10-12): head unit advertises the real cluster
Root cause of "mirror only": the dongle receives `Recv proxy disply count:1` —
the HEAD UNIT advertises just the main display to the dongle. By design: this
project (altscreen_hook/libaltscreen111_gen2.c, sender_listed) hardcodes the
dongle (model=iPhone9,1 build=18D70) as a no-advertise sender and serves it the
stock /info (no cluster), because the stock dongle dropped the session on the
advertisement. We fixed the dongle's handling, so we can opt it back in.

Change made (head unit, this repo): sender suppression FULLY DISABLED for now.
sender_listed() always returns 0 (original logic kept under #if 0), so the head
unit advertises the AltScreen/cluster to every sender, including the dongle. No
marker/mod needed. Restore by flipping the #if 0 to #if 1 in
altscreen_hook/libaltscreen111_gen2.c.

Intended flow (dedicated cluster, not mirror):
  head unit advertises cluster to dongle (count:2)
  -> dongle proxies BOTH displays to the phone (phone-facing "displays" property,
     delegate callback FUN_0005a068 @0x5a068 returns cached display array; parser
     FUN_00038d00 caches the full head-unit display array at session+0xc8 but its
     per-display loop breaks after the main (type 0x6e/0) display)
  -> phone renders a dedicated InstrumentCluster, sends stream 111 to the dongle
  -> dongle relays stream 111 to the head unit's cluster.
First experiment = head-unit change + marker + STOCK dongle (the mirror hacks
would fight a real cluster). Observe whether the stock dongle now gets count:2,
proxies 2 displays to the phone, renders a dedicated cluster, and whether the
session survives (the historical drop). Head-unit log: /tmp/altscreen111.log
("GEN2 sender ... AltScreen advertisement on"). Dongle log: getlogs ->
"Recv proxy disply count:2". Then iterate dongle fixes against the real behavior.

## OPERATIONAL CONSTRAINT (learned 2026-10-10)
Flash/recover ONLY on bench power (USB charger / power bank). In the car the
dongle goes straight into CarPlay mode and the config AP (smartBox / 192.168.5.1)
is not reliably up, so you cannot reach the flasher there. Test cycle:
  1. bench power -> flash the build to try (smartBox Wi-Fi, flash_local.py)
  2. move dongle to the car -> connect iPhone -> test CarPlay
  3. bring dongle back to bench power -> getlogs.py (app.log is persistent)
  4. if broken: bench power -> flash stock (fw/hw301_v129.tar) to recover
A dead CarPlay connection in the car is recoverable: it's only the app
partition; stock restore on bench power always brings it back.

## Tooling status (works)
flash (`flash_local.py`, compact vendor JSON), inject (`build.sh` wrapper-rename,
now any `WRAP_LIB`), log (app.log + /dev/kmsg), probe (naked trampolines, EVEN
reg push for 8-byte sp alignment). Stock rollback: `fw/hw301_v129.tar`.

## DEDICATED CLUSTER — rewrite design (2026-10-12, fresh Ghidra confirm)
Decision: pursue the true dedicated cluster (not mirror). Head-unit guard stays
OFF (altscreen_hook returns 0; count:2 advertised). Observed on car (guard off +
v1129 cluster build): phone CANNOT connect — CarPlay session is CREATED then the
head unit immediately sends "session ended", dongle tears down socket 35 (111),
`SendProxyScreenFrame failed`, iAP2/BT drop. Root: the mirror hacks fight the real
count:2, and the dongle is a single-screen bridge.

Fresh decomp saved: work/ghidra/relay_decomp.txt (DumpRelay.java). Confirms the
single-instance pipeline and the exact globals (CPAAProxyEx, GOT base DAT_0005c688):
  - head-unit DATA socket fd:   *(int*)(base+0x5c430)   (SendProxyScreenFrame)
  - head-unit CONFIG socket fd: *(int*)(base+0x5c1e0)   (SendProxyScreenVideoConfigFrame; config is UNENCRYPTED)
  - AES-CTR ctx:                 base+0x5c55c
  - ChaCha20-Poly1305 ctx:       base+0x5c428 ; key base+0x5c514 ; nonce base+0x5c534 (incremented in place per frame)
  - SETUP handler FUN_0005c7ec stores the connected socket into ONE slot *0x5cafc,
    and ONLY for the stream whose type==0x6e (110). The gate NOP makes 111 run the
    same code -> it OVERWRITES *0x5cafc (and re-runs SetupProxyScreenSecurityInfo
    on the single global ctx). => only the LAST stream survives; routing 111 blanks main.
  - SendProxyScreenFrame/...ConfigFrame are INTERNAL calls (not PLT) -> NOT
    interposable from a libCoreUtils shim. The dual pipeline MUST be binary patches
    in CPAAProxyEx (+ a receive-side wire), not an LD_PRELOAD shim.

Required dual pipeline (each piece is the real work):
  (A) 2nd socket slot + 2nd crypto ctx: carve a scratch BSS/code-cave region for
      a parallel {socket fd, AES/ChaCha ctx, nonce, config}. SetupProxyScreenSecurityInfo
      derives from the pairing session with a FIXED salt (ProxyPairingSessionDeriveKey,
      DAT_0005ba48 salt string) -> both screens would derive the SAME key; the per-stream
      differentiation must come from a per-stream nonce/salt. Check whether the phone's
      SetupScreen per-stream dict carries distinct security material (it should: AES-CTR
      "use aes ctr security" was logged per stream) and store it per slot.
  (B) Patch FUN_0005c7ec: when type==0x6f(111) store its socket into slot#2 and run
      a slot#2 SetupProxyScreenSecurityInfo (do NOT clobber slot#1).
  (C) Route: SendProxyScreenFrame(main 110) -> slot#1; the cluster frames -> slot#2.
      Needs a 2nd RECEIVE path: recvCarPlayVideoFrame (CarPlayProxyApp @0x43970) only
      handles ONE phone video stream; libAirPlay demuxes streams before it. Must wire a
      callback for the phone's 2nd (cluster) video stream and feed writeCarLifeVideoFrame
      /SendProxyScreenFrame-equivalent targeting slot#2.
  (D) handleVideoInit stores W/H into ONE getProxySessionInfos() struct; cluster W/H
      must go to a 2nd field set.

First testable experiment (max info / low brick risk): keep guard OFF, flash STOCK
v129 (no gate NOP, no shim). Expect: dongle gets count:2, SETUP handler sets up only
type 110 (one socket), phone connects, main works, NO cluster. If the session SURVIVES
with stock+count:2 -> the teardown is caused by our 2-stream-to-one-socket inconsistency,
and the rewrite is the only path. If it STILL drops -> the head unit itself rejects a
dongle that advertises but can't drive 2 displays, and we also need (B)+(C) before any
car test can stay up. Recover anytime: flash fw/hw301_v129.tar on bench.

Verification constraint: every iteration = bench flash -> car trip -> bench getlogs.
No host-side way to validate. Implement in small, individually-flashable patches.

## PINNED MEMORY MAP (2026-10-12, Globals2.java, link-time addrs)
- Head-unit video socket fd: GLOBAL @ 0x17e418 (init 0xffffffff=-1).
  * written by FUN_0005c7ec (_AirPlayClientSendSetupScreenCommandCompletion) @0x5cb00 `str r8,[r12]`, r12=*(0x5cc0c=0x12191c)->0x17e418, for the stream matching type==0x6e.
  * read by SendProxyScreenFrame @0x5c428 and SendProxyScreenVideoConfigFrame @0x5c1d8.
  * => gate-NOP makes 111 overwrite 0x17e418 (clobbers main). Dual pipeline needs a 2nd fd slot + per-stream routing here.
- Screen/crypto state struct base @ 0x17f2e0 (fields through ~0x17f540; +0x260=0x17f540 used as a mutex/obj ptr in many sites).
- CPAAProxyEx NEEDED includes libCoreUtils.so AND libAirPlay.so (shim is mapped in).

## OPEN UNCERTAINTY (gates the rewrite): do the shim interposes fire?
Last no-lock log: shim LOADED x3 but `appended cluster` x0 and `video socket fd registered` x0.
Either (a) CPAAProxyEx binds CoreUtils symbols to libCoreUtilsReal.so (shim bypassed), or
(b) interposes fire but inner guards never match (CFArray value lacks streamConnectionID; SocketConnect caller offset != VIDEO_CONNECT_OFF 0x4cac4). Must resolve with a verbose diagnostic build (cluster_diag.c) BEFORE writing relay patches, else every car trip is a guess.

## DIAGNOSTIC RESULT (2026-10-12, v1130 cluster_diag, lock OFF/count:2)
TWO decisive results:
1. SHIM INTERPOSES DO NOT FIRE. Log shows only "[cluster_diag] LOADED"; zero
   SocketConnect / CFArrayEnsureCreatedAndAppend calls. CPAAProxyEx binds those
   CoreUtils symbols to the real lib (libCoreUtilsReal/other), NOT the shim.
   => wrapper-rename/LD_PRELOAD cannot intercept the relay. The old "mirror"
      build's shim inject + dual-send NEVER RAN; only its CPAA gate-NOP patch did.
   => ALL dongle relay changes must be CPAAProxyEx (or libAirPlay) BINARY PATCHES.
2. HEAD UNIT DROPS ANY HALF-DRIVEN count:2. This build had NO gate-NOP: clean,
   arrray size=1, one socket, main only. Still: count:2 -> SESSION_CREATED [21.711]
   -> session ended [21.891] (0.18s). So the teardown is NOT our 2-into-1-socket
   hack; the HEAD UNIT itself ends the session when it advertises 2 displays and the
   dongle doesn't drive the 2nd. => NO incremental beachhead on count:2; the whole
   dual pipeline must work before any session stays up (un-bisectable, all-or-nothing).

## STRATEGY PIVOT (2026-10-12): decouple the two links, stay on count:1
Abandon count:2-to-dongle (head unit kills it). The ONLY config that held a session
AND showed the map on the VC = guard ON (count:1 to dongle) + CPAA gate-NOP. In that
mode the head unit only asks the dongle to drive 1 display (survives), and the head
unit's OWN AltScreen hook (libaltscreen111, ours, safe to iterate) renders stream 111
to the cluster. Plan:
  - Head unit: guard ON (count:1). [staged]
  - Dongle (CPAA binary patch): advertise a 2nd DISPLAY (not just a 2nd stream) to the
    PHONE so the phone renders genuine cluster content on 111 (vs mirroring 110).
    Target: phone-facing "displays" property builder FUN_0005a068 @0x5a068 (returns the
    cached array at session+0xc8) -> inject a synthetic cluster display entry.
  - OPEN: confirm how 111 actually reached the VC in the working count:1 path (single
    head-unit socket 0x17e418). Re-examine the working-mirror log before patching.

## CROSS-CHECK: Carlinkit firmware (ludwig-v repo, 2026-10-12) — same single-screen bridge
Pulled ludwig-v/wireless-carplay-dongle-reverse-engineering (blobless) and inspected the
newest extracted rootfs (A15W_2025.02.25.1521) + Nulled/ binaries. Findings:
- Carlinkit ALSO impersonates an iOS device: dedicated binary /usr/sbin/fakeiOSDevice +
  an /etc/uuid_sign activation blob (Nulled/README: replace fakeiOSDevice + dummy uuid_sign
  to "bypass activation"). Same role as HW301's iPhone9,1/18D70 persona; just factored into a
  separate binary. fakeiOSDevice is packed (ELF no section headers, encrypted strings) so the
  exact model/build string isn't readable.
- Phone-facing AirPlay identity is just name=CarPlay model=Magic-Car-Link-1.00 (etc/airplay*.conf)
  — the accessory name the phone shows, NOT a display config.
- NO display/screen/cluster/resolution keys in ANY airplay conf. AppleCarPlay binary is packed.
  No evidence of a 2-screen design. Same single-screen wired-CarPlay bridge as HW301.
Conclusion: nothing to borrow for dual-screen; confirms the 2nd display must be driven by our
patch (count:1 head unit + inject a cluster display toward the phone). Different HW (i.MX6 vs
Allwinner) so no offsets transfer anyway.

## DUAL-SEND-SOCKET patch feasibility (2026-10-12, DualSock.java)
Disassembled FUN_0005c7ec 0x5c9a0..0x5cb20:
- type-gate: 0x5c9a4 `cmp r9,#0x6e` / 0x5c9a8 `bne` (r9 = STREAM TYPE here). gate-NOP = 0x5c9a8.
- 0x5cac0 `bl SocketConnect`; 0x5cac4 `subs r9,r0,#0` -> r9 CLOBBERED (now rc). Type NOT live after.
- 0x5cb00 `str r8,[r12]` with r12=&0x17e418 (r8 = socket fd). This is the single-slot store.
Blockers for an in-place dual-send patch:
  1. Type (r9) is dead by the store site -> routing by type there needs the type stashed earlier.
  2. NO code cave: scan found 0 runs of >=48 zero bytes in the exec region (functions packed
     back-to-back). A conditional detour has nowhere to live without repurposing a dead
     function or adding an ELF segment (both high-risk, and untestable on host).
  3. Free BSS at 0x17e400..0x17e414 (zero) is available for a 2nd socket-fd word IF a detour existed.
Conclusion: the dual pipeline cannot be done with safe in-place byte patches. It needs EITHER a
repurposed dead-code cave (must prove the function is never called in the CarPlay path) or a new
segment — plus the matching changes in SendProxyScreenFrame (single socket 0x17e418) and the
receive/route path. All un-bisectable (session only survives once the whole chain works) and
unverifiable off-device. The proven shippable result remains the count:1 mirror (map on VC, same
content both screens) via the gate-NOP alone.

## CODE CAVE FOUND (2026-10-13) — dual pipeline is implementable
DeadCave.java + raw pointer scan of CPAAProxyEx:
- Method: a function is a safe cave only if it has ZERO Ghidra xrefs AND its address
  (ARM & Thumb forms) appears ZERO times as a stored word (no function-pointer table).
  Validation: FUN_0002f5c4 (looks like live teardown) has a pointer @0x15c068 -> correctly
  flagged NOT dead. So zero-static-xref alone is insufficient (binary dispatches via pointers).
- SAFE CAVE: **FUN_000acdb0, 9860 bytes, ARM (TMode=0), zero xrefs, zero stored pointers.**
  Monolithic library routine (big +0x4000 buffer), not a callback. Overwrite from 0xacdb0.
  Backup: FUN_00079ff8 (568 B, also zero/zero).
- Branch reach: whole .text (0x2e4b0..0x14f733) is within ARM b/bl ±32MB of 0xacdb0. OK.
This unblocks the invasive route. Build order (each a cave detour, test on count:1 so the
session survives): (5) inject 2nd display to phone [FUN_0005a068] -> does iPhone send distinct
111? then (1-4) dual send socket + crypto + receive/route.

## INJECTION TARGET CORRECTION (2026-10-13) — cluster = AltScreen feature+URL, not a display
Decompiled libAirPlay AirPlayInfoArrayAddScreenDisplay @0x2afe8 (PLT-callable from the cave
via CPAAProxyEx stub 0x2df90). Signature:
  AirPlayInfoArrayAddScreenDisplay(int* arr, CFStr uuid, i64 p3, i64 p4, i64 p5,
                                   i64 p6, i64 p7, i64 p8, i64 p9)
  dict keys (from libAirPlay strings): uuid, widthPixels, heightPixels, widthPhysical,
  heightPhysical, maxFPS, features, primaryInputDevice (+ extendedFeatures elsewhere).
  Main display seen on the wire: features=2 primaryInputDevice=0 1024x480 @30 188x88mm.
BUT reading our own head-unit hook (altscreen_hook/libaltscreen111_gen2.c) shows the GEN2
cluster is advertised NOT as a 2nd display entry but via:
  - root enabledFeatures (bit26 mode) advertising AltScreen capability, AND
  - altScreen URLs: maps:/car/instrumentcluster[/map|/instructioncard],
    UUID b7e6c5a0-2222-4000-8000-000000000002, canvas 1440x540, stream 111 @ port 7100.
So the iPhone renders the cluster (sends stream 111) when the RECEIVER advertises the
AltScreen enabledFeatures + the instrumentcluster URL to it. With count:1 (guard on) the head
unit does NOT advertise altScreen to the dongle, so the dongle never forwards it to the phone.
=> PIECE 5 (revised): the dongle must SYNTHESIZE the altScreen enabledFeatures + instrumentcluster
   URL advertisement in its phone-facing /info (builder FUN_0005a068 / the /info response),
   independent of the head unit. Only then does the phone send distinct 111 content.
   Then pieces 1-4 (dual send socket/crypto/receive/route in CPAAProxyEx via cave 0xacdb0) carry
   that 111 to the head unit's AltScreen -> VC.

## STATUS after option A (2026-10-13)
Invasive route is VIABLE: safe cave secured (0xacdb0). Remaining = a multi-piece cave-detour
build (synthesize altScreen advert to phone; dual send socket+crypto; 2nd receive/route),
each flashed+car-tested; final distinct-cluster result is only observable once the chain is whole.
No off-device validation. Proven fallback remains the count:1 mirror (map on VC, same content).

## DECISIVE BLOCKER (2026-10-13): the dongle AirPlay stack has NO cluster protocol
Verified by strings (readable, not obfuscated — display/screen strings present as control):
- CPAAProxyEx + libAirPlay HAVE generic screen/display: `displays`, `displayUUID`,
  `SetupScreen`, `ScreenStreamCreate/Start/SetWidthHeight`, `extendedFeatures`,
  "Recv proxy disply infos: UUID:%s features:%lld inputPrimitDevice:%lld ...".
- They have ZERO instrument-cluster / AltScreen protocol: no `altScreen`, `instrumentcluster`,
  `enabledFeatures`, `maps:/car/...`, `AirPlayScreenType`, `secondary`, `cluster` (the only
  "reference" hits are the bundled x264 encoder).
Meaning: the dongle's AirPlay RECEIVER cannot advertise an instrument-cluster display to the
iPhone, so iOS/Maps never sends a dedicated cluster (nav-only) stream through it. The gate-NOP
only makes the phone duplicate the MAIN screen onto a 2nd stream (111) -> the head unit's
AltScreen hook renders that duplicate to the VC = the MIRROR. That is the hardware ceiling.
A dedicated cluster would require IMPLEMENTING the CarPlay instrument-cluster receiver protocol
inside libAirPlay (enabledFeatures bit + altScreen URL + cluster display designation + distinct
stream routing) -- not a cave detour but a substantial protocol feature in a stripped 3rd-party
binary. Not realistically achievable by binary modding. The code cave (0xacdb0) is real and the
dual-send plumbing is patchable, but there is no distinct cluster content to carry.

## FINAL (2026-10-13): achievable result = the MIRROR
Dedicated cluster is NOT feasible on HW301 (AirPlay stack lacks the cluster protocol).
Shippable: count:1 (head-unit guard ON) + dongle gate-NOP build (v1129) = map on the Virtual
Cockpit, same content as the main screen, stable. Everything else (cave, pinned map, detour
methodology) is documented above for any future stack that DOES implement the cluster protocol.
