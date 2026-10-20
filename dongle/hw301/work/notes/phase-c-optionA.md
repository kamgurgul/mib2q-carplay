# Phase C, option A - deliver the cluster to the head unit without the 2-screen crash

Why v1150 crashed: injecting 111 into the dongle->head-unit SetupScreen makes
stock CPAAProxyEx present two screens to the head unit; it is a single-screen
bridge and the session dies in ~0.6 s (`arrray size = 2` ... `SendProxyScreenFrame
failed err:-1`). So the dongle must NOT make CPAAProxyEx request 111.

Design: the dongle's own relay opens a direct TCP connection to the head unit
and sends the decrypted cluster as PLAINTEXT H.264; the head unit opens its
listener for the dongle by itself and reads plaintext (no key coordination, no
injection, CPAAProxyEx stays single-screen = stable). Two small changes, one per
side. The dongle->HU framing is the existing 128-byte ScreenHeader
(opcode 1 = config/avcC, 0 = frame) with a PLAINTEXT body (no AES, no tag).

## Head-unit side (altscreen_hook/libaltscreen111_gen2.c)
Goal: for the dongle sender (model iPhone9,1 build 18D70), open the stream-111
listener at session setup and accept plaintext frames, WITHOUT a 111 SETUP from
CPAAProxyEx and WITHOUT AES.

1. Add a flag `static int g_alt_plain;` next to `g_sender_noadvertise`.
   In `note_sender()`, set `g_alt_plain = (model==iPhone9,1 && build==18D70)`
   (reuse the exact test in `sender_listed`). Log it.

2. New `start_alt_receiver_plain(void)` (copy of `start_alt_receiver` minus the
   key): skip the `g_master_valid` check and the `g_derive_screen`/
   `g_aes_ctr_init` block; set `g_alt_aes_valid = 0`; bind the listener the same
   way (`bind_listener_stream111`, port 7100 then the PF fallbacks); spawn
   `alt_receiver_thread`. Set a new `static int g_alt_plain_mode = 1;` for this
   receiver so the frame loop knows to skip decryption.

3. In `alt_receiver_thread`, the `K_SCREEN_VIDEO_FRAME` case: when
   `g_alt_plain_mode`, do NOT call `g_aes_ctr_update`; submit `body` directly:
   ```
   if (g_alt_plain_mode) { /* body already plaintext */ }
   else if (!g_alt_aes_valid || g_aes_ctr_update(...)!=K_NO_ERR) { ...fail... }
   ```
   VIDEO_CONFIG is already plaintext and unchanged.

4. Trigger in `mibr_session_setup`: after the stock 110 setup succeeds, when
   `g_alt_plain` and no 111 receiver is running
   (`g_alt_thread_started==0`), call `start_alt_receiver_plain()`.
   Do NOT change `advertise_enabled()`/the /info path (guard stays ON: the
   dongle still gets stock /info; only the listener is opened for it).
   Keep the normal (keyed) path for all other senders unchanged.

5. Build with the existing MHI2Q target (scripts/build_altscreen_hook.sh);
   install the new libaltscreen111_mhi2q.so. This is the vehicle-proven file:
   change only the dongle-sender branch, keep Run143/other senders byte-for-byte.

## Dongle side (dongle/hw301/work/src/cluster_dual.c) - IMPLEMENTED below
New compile switch `HU_DIRECT` (option-A build). When set:
- the `CFArrayEnsureCreatedAndAppend` injection is OFF (no 111 to CPAAProxyEx);
- the relay sends PLAINTEXT (never AES-re-encrypts; `g_relay.enc` forced 0);
- the relay still connects to the head unit :7100 on the first decrypted frame
  and sends the cached config then frames, and still re-requests a key frame.
`g_hu_gen` still advances from the MAIN screen's SetupProxyScreenSecurityInfo
(AES_CTR_Init capture), which is the relay's "head-unit session up" trigger, so
the relay connects without the injection.
Build: `build_dual.sh optA` -> v1152 (step3, decrypt, HU_DIRECT, Output key).

## Bring-up order
1. Flash the new head-unit hook; keep the dongle on v1148 (Phase B). Confirm
   the head unit opens :7100 for the dongle (head-unit log:
   `waiting for iPhone AltScreen connection on 0.0.0.0:7100`) with CarPlay up.
2. Flash the dongle v1152. Expect: dongle `head-unit 111 connected :7100`,
   `relayed=` climbing, `send_err=0`, NO `arrray size = 2`, session stable;
   head unit `AltScreen stream 111 connected` -> `valid ... video config` ->
   `first complete Screen VIDEO_FRAME accepted`; map on the Virtual Cockpit.
3. If the head unit shows the map but stutters, that is Phase D item 3
   (off-thread writer). If the session still dies, re-check that the dongle log
   shows `arrray size = 1` (injection truly off).

## Recovery
Dongle: `flash_local.py fw/hw301_v129.tar --version 129` (FEL if no LED).
Head unit: keep the previous libaltscreen111_mhi2q.so to swap back.
