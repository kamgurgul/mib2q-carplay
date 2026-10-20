# Phase C, direct path - deliver the cluster without the 2-screen crash

Why v1150 crashed: injecting 111 into the dongle->head-unit SetupScreen makes
stock CPAAProxyEx present two screens to the head unit; it is a single-screen
bridge and the session dies in ~0.6 s (`arrray size = 2` ... `SendProxyScreenFrame
failed err:-1`). So the dongle must NOT make CPAAProxyEx request 111.

Design: the dongle's own relay opens a direct TCP connection to the head unit
and seals each decrypted AU with the same AES-CTR screen key other dongles
install in `SetupProxyScreenSecurityInfo` (this firmware logs `use aes ctr
security`). The head unit opens its stream-111 listener for the dongle sender
and decrypts with that key. CPAAProxyEx stays single-screen. Framing is the
existing 128-byte ScreenHeader (opcode 1 = config/avcC, plaintext; opcode 0 =
frame, AES-CTR of the whole AVCC body, no tag). v1152 sent plaintext; do not
flash it.

## Head-unit side (altscreen_hook/libaltscreen111_gen2.c)
Goal: for the dongle sender (model iPhone9,1 build 18D70), open the stream-111
listener at session setup and AES-decrypt frames, WITHOUT a 111 SETUP from
CPAAProxyEx. The key is stream 110's `streamConnectionID` via the existing
`AirPlay_DeriveAESKeySHA512ForScreen` path inside `start_alt_receiver`. That is
the id `SetupProxyScreenSecurityInfo` hashes into the key the dongle captures,
so both sides start CTR at the same IV. VIDEO_CONFIG stays plaintext. The frame
loop is unchanged (it already AES-CTR-decrypts `bodySize` bytes and submits
the AVCC AU).

Trigger: in `mibr_session_setup`, when the sender is listed (dongle) and the
request has type 110 but no 111, call `start_alt_receiver(cid_110)`. Do not
edit the SETUP response and do not change `advertise_enabled()` (guard stays
ON). A later 110 teardown stops this listener so the next SETUP re-inits CTR.
Real iPhones still take the normal 111 SETUP path.

## Dongle side (dongle/hw301/work/src/cluster_dual.c)
`HU_DIRECT` (direct build). When set:
- the `CFArrayEnsureCreatedAndAppend` injection is OFF (no 111 to CPAAProxyEx);
- the relay AES-CTR-seals each authenticated AU with the key captured from the
  main screen's `SetupProxyScreenSecurityInfo` (`head-unit relay aes on`);
- `cluster_dual.plainhu` is the only plaintext escape;
- the relay still connects to the head unit :7100 on the first decrypted frame,
  sends the cached config in the clear, then the sealed frames, and still
  re-requests a key frame.
`g_hu_gen` still advances from that AES_CTR_Init, which is the relay's
"head-unit session up" trigger.
Build: `build_dual.sh optA` -> v1154 (step3, decrypt, HU_DIRECT, Output key).

## Bring-up order
1. Flash the new head-unit hook; keep the dongle on v1148 (Phase B). Confirm
   the head unit opens :7100 for the dongle (head-unit log:
   `dongle sender: stream111 listener cid=... aes=1` and
   `waiting for iPhone AltScreen connection on 0.0.0.0:7100`) with CarPlay up.
2. Flash the dongle v1154. Expect: dongle `head-unit screen key captured`,
   `head-unit relay aes on`, `head-unit 111 connected :7100`,
   `relayed=` climbing, `send_err=0`, NO `arrray size = 2`, session stable;
   head unit `AltScreen stream 111 connected` -> `valid ... video config` ->
   `first complete Screen VIDEO_FRAME accepted`; map on the Virtual Cockpit.
   `AES-CTR decrypt failed` means the 110 connection id on the two sides
   differed. Do not fall back to plaintext unless that log is the failure.
3. If the head unit shows the map but stutters, that is Phase D item 3
   (off-thread writer). If the session still dies, re-check that the dongle log
   shows `arrray size = 1` (injection truly off).

## Recovery
Dongle: `flash_local.py fw/hw301_v129.tar --version 129` (FEL if no LED).
Head unit: keep the previous libaltscreen111_mhi2q.so to swap back.

## VERIFICATION (2026-10-20, encrypted direct path, both sides build clean)
Functional review: SHOULD decrypt and show the map.
- Key match: dongle captures CPAAProxyEx's stream-110 AES-CTR key+iv at
  SetupProxyScreenSecurityInfo; the head unit re-derives 110's key from
  main_cid (AirPlay_DeriveAESKeySHA512ForScreen). Same pairing, same cid, same
  derivation -> identical key+iv, which is why real 110 already works. The key
  is never transmitted; both sides arrive at it independently.
- Counter alignment: both init CTR once at that iv and advance by bodySize per
  VIDEO_FRAME; VIDEO_CONFIG is plaintext on both and advances neither. TCP keeps
  order, so the two CTR spans stay aligned. `AES-CTR decrypt failed` on the head
  unit would mean the two cids diverged (re-SETUP race) - not a reason to use
  plaintext.
- IDR: the stream carries ~1 IDR; the relay requests forceKeyFrame on connect
  and sends the cached avcC first, so the head unit gets config then a fresh IDR.
- Teardown: dongle never matches has_alt (no 111 in its requests); the
  110-teardown branch stops the listener and clears g_dongle_alt_cid so the next
  110 SETUP re-keys. Branch order (has_alt / full_session / has_main-dongle) is
  correct.

SECURITY GAP vs real dongles (the one real deviation): other dongles get a
distinct 111 connection id from a real 111 SETUP, so their 110 and 111 AES-CTR
keys DIFFER. This build reuses 110's key+iv for 111, so 110 (main) and 111
(cluster) share a keystream. A full capture of the dongle<->head-unit USB link
(usb0, inside the device) could XOR the two and recover both. Over-the-air
(phone<->dongle) is unaffected (ChaCha, intact). So this is "encrypted on the
local hop" but NOT the distinct-key security real dongles have.
Fix options, in order of preference:
  1. Distinct 111 key from the shared 110 key via a KDF both sides apply, e.g.
     key111/iv111 = SHA512(key110||"cluster111")[:16] computed identically on
     the dongle (on the captured key) and the head unit (on the derived key).
     No proxy rewrite, no key transport; libCoreUtils exports SHA512_* and the
     head unit already links AirPlay_DeriveAESKeySHA512ForScreen. Removes the
     reuse. (Needs a few lines of hashing on each side - not yet written.)
  2. True mimic: issue a real 111 SETUP to the head unit (distinct cid) - the
     proxy rewrite the notes judged out of scope; crashes stock CPAAProxyEx.
Until (1) lands, treat v1154 as a working-but-keystream-reusing build: fine to
prove the map on the cluster, not the final security posture.

BUILD STATUS: head-unit hook built (build/libaltscreen111_mhi2q.so, encrypted
path); dongle v1154 built (optA), import check 43/43, dry-run OK. Not committed,
not flashed.

## FEATURES after the car pass (2026-10-20)
Car-verified working on v1154: cluster map on the VC, CarPlay on the MMI,
roller ZOOM works (the knob HID / changeMapZoomLevel reaches the phone through
CPAAProxyEx - no forwarding needed). Missing: the maneuver arrow/ETA.

- MANEUVER/ETA (v1156): the dongle advertised the bare cluster URL, so the phone
  rendered a plain map with no turn card. initialURL is now
  `maps:/car/instrumentcluster?showETA=yes&maneuverLayout=rightAligned&showSpeedLimit=no`
  (the head-unit hook's RGI-off URL, which a real iPhone honours), so the phone
  draws the maneuver + ETA into the cluster video. Overridable
  -DCLUSTER_URL_INITIAL=... ; the bare `/map` is the RGI-on choice (head unit
  draws its own maneuver). If v1156 shows a DOUBLE maneuver (phone + head-unit
  RGI), rebuild with the bare /map URL.  Car test: build_dual.sh optA -> v1156.
- MULTIPLE MAP SIZES: geometry is now -D overridable (CLUSTER_W/H/FPS/WMM/HMM),
  default 1440x540. A different cluster builds with e.g.
  `CLUSTER_CFLAGS="... -DCLUSTER_W=1010 -DCLUSTER_H=376" build_dual.sh ...`.
  (Compile-checked at 1010x376.) Not a runtime file because the dongle has no
  shell; per-variant build instead.

## HARDENING STATUS
1. KDF for a distinct 111 key (remove 110/111 keystream reuse on the USB hop) - TODO
   (crypto; the one real security item, closest to what other dongles do).
2. Nonce-desync recovery - DONE (v1156). After DESYNC_LIMIT=5 consecutive
   chacha_open verify fails the receiver drops the 111 socket; iOS re-SETUPs
   with a fresh counter. Resets on any good frame. Log: "111 nonce desync ...".
3. Head-unit backpressure - DONE (v1156), as bounded drop rather than a separate
   writer thread: the read path polls the head-unit socket for writability for
   HU_WRITE_MS=60 ms; if not writable it DROPS the AU *before* AES (so the CTR
   counter stays aligned - a dropped frame must not advance it) and asks the
   phone for a key frame. The car pass showed the head unit keeping exact pace
   (relayed==frames, send_err=0), so this only engages if the head unit stalls.
   A true async writer thread (zero read-path wait) remains possible if 60 ms
   drops ever show up as `dropped=` climbing in the log; deferred until observed.
4. Firmware pinning - DROPPED. The shim and v129 ship as one flashed image
   (build.sh extracts from fw/v129), so the shim never runs against firmware it
   was not built for; a vendor OTA replaces the whole app partition (shim
   included) rather than orphaning it. Nothing to pin.
Zoom forwarding is NOT needed (zoom works). Cockpit-layout (updateViewArea)
switching the cluster view: unverified - check whether switching layouts moves
the cluster SafeArea; if not, that is the one command that may still need
forwarding.

## SAFE AREA + COCKPIT SWITCHING (2026-10-20, from a car photo)
- SAFE AREA FIXED (v1158): the dongle advertised safe-area height 297; the
  head-unit hook's calibrated value for this A5 cockpit is 270
  (ALT111_DEFAULT_SAFE_H). The phone laid the puck/turn-text into a box 27 px
  too tall, so content sat wrong on the VC. add_views now uses 270 and the
  head unit's origins (wide 360,87,720,270; classic 520,87,400,270), all
  -D overridable (CLUSTER_SAFE0_*/SAFE1_*).
- COCKPIT SWITCHING does not resize the map: switching wide<->classic should
  send `updateViewArea` so the phone re-renders into the other SafeArea. Over
  the dongle the head unit serves stock /info (guard on) and its gen2 control
  worker is NOT armed in the dongle path (start_alt_receiver only, no
  gen2_control_projection_on), so no updateViewArea is sent; and even if it
  were, it would go to CPAAProxyEx (dropped), not the phone. So the cluster
  stays on initialViewArea=0. Fixing it needs the head unit, on a cockpit
  layout change, to tell the dongle, and the dongle to send updateViewArea to
  the phone - the same command-forwarding gap. NOT implemented.

## WHO ADVERTISES THE CLUSTER URL (answers the MIB-menu maneuver question)
The MIB-menu maneuver toggle switches the head-unit-advertised URL between
ALT_URL_RGI_ON (/map?maneuverLayout=no: head unit draws the arrow, phone
suppresses its turn card) and ALT_URL_RGI_OFF (root ?maneuverLayout=rightAligned:
phone draws its own). That selection is in active_alt_url_copy and is only used
when advertise_enabled() is true.
  - WIRED (or any non-guarded sender): the head unit advertises, so the MIB
    toggle works - disabling head-unit maneuver rendering makes the phone draw
    its own maneuver (app-dependent: Apple Maps/Waze draw a turn card; Google
    Maps' cluster rendering is minimal).
  - DONGLE: advertise_enabled() is FALSE (guard on, stock /info), so the head
    unit does NOT advertise the URL - the DONGLE does (CLUSTER_URL_INITIAL,
    compile-time). The MIB toggle has NO effect on the dongle path. To change
    dongle behaviour, rebuild with -DCLUSTER_URL_INITIAL=... (ALT_URL_RGI_ON =
    bare /map for head-unit-draws; the current default is the RGI_OFF phone-draws
    URL).
