# Phase B implementation spec - decrypt the stream-111 cluster video

Status going in: Phase A (v1146/v1147) proved iOS streams the 1440x540 cluster
to the dongle's own listener on :7100; frames arrive but are ChaCha20-Poly1305
ciphertext. This spec turns them into plaintext H.264 inside the shim, using
ONLY stock primitives exported by libCoreUtils (no cipher is reimplemented). All
addresses/strings are from the 2026-10-19 research in dual-screen-plan.md
(libAirPlay/libCoreUtils v129).

All work is in `work/src/cluster_dual.c`. Build with `build_dual.sh step2a` but
flip `STEP_DECRYPT` to 1 for the decrypt build (give it a new code, e.g. 1148).

## B0. First pin the per-frame layout (Ghidra, no car trip)
This is the only unknown left. Decompile, in libAirPlay v129:
  - `_AirPlayReceiverSessionScreen_ProcessFrame` (the per-frame worker), and
  - the function it calls to ChaCha-decrypt (look for chacha20_poly1305_init_*
    / _decrypt / _verify).
Extract exactly:
  (a) NONCE: how it is formed per frame. Expected: an 8- or 12-byte little-endian
      frame counter that starts at 0 and increments by 1 per frame on the stream
      (init_64x64 takes a 64-bit counter/nonce). Confirm width and start value.
  (b) AAD: whether any bytes are authenticated (likely none, or the 128-byte
      screen header). Note length + source.
  (c) TAG: the 16-byte Poly1305 tag position (expected: last 16 bytes of the
      frame body; ciphertext = body minus the trailing 16).
  (d) Whether the 128-byte ScreenHeader is sent in the clear (Phase A already
      shows VIDEO_CONFIG body is plaintext, so the header is almost certainly
      clear and only the body after it is encrypted - confirm).
Write (a)-(d) into dual-screen-plan.md before coding.

## B1. Resolve the new stock functions (in bind_all(), best-effort next_sym)
Add these function pointers (match libCoreUtils signatures; names exported):
    PairingSessionDeriveKey(void *session, const void *salt, size_t saltLen,
        const void *info, size_t infoLen, size_t outLen, uint8_t *outKey) -> OSStatus
    chacha20_poly1305_init_64x64(ctx, const uint8_t key[32], uint64_t nonce) -> void/int
    chacha20_poly1305_decrypt(ctx, const void *in, size_t n, void *out) -> size_t
    chacha20_poly1305_verify(ctx, const uint8_t tag[16]) -> int/OSStatus  (0 = ok)
    chacha20_poly1305_final(ctx, ...) if the init/decrypt/verify triplet needs it
Confirm each argument order from the Ghidra decomp of ProcessFrame (the calls
there show the real prototypes). Resolve all with next_sym (optional, so
check_imports.sh is unaffected) and bail out of decrypt if any is NULL.

## B2. Derive the stream-111 key (once, when the receiver starts)
Inputs available in rx_thread: `a.session` (retained AirPlay session),
`a.cid` (the 111 streamConnectionID, uint64).
  pair = *(void**)((char*)a.session + 0x3ec);     // pinned offset
  char salt[48]; snprintf(salt, sizeof salt, "DataStream-Salt%llu",
                          (unsigned long long)a.cid);
  uint8_t key[32];
  PairingSessionDeriveKey(pair, salt, strlen(salt),
      "DataStream-Output-Encryption-Key", 32 /*strlen*/, 32, key);
Notes:
  - "DataStream-Output-Encryption-Key" is the phone's TX key; the dongle decrypts
    the inbound stream with it. If nal_ok stays 0, retry with
    "DataStream-Input-Encryption-Key" (one-line swap, one car trip).
  - keep `key` only as long as needed; memset it to 0 on thread exit.
  - gate all of this behind `if (STEP_DECRYPT)`.

## B3. Decrypt each VIDEO_FRAME (in the OP_VIDEO_FRAME branch of rx_thread)
Per frame, with the layout from B0 (adjust to what Ghidra shows):
  - body = the bytes after the 128-byte header (header already stripped by the
    read loop: `body`/`h` are separate; `body` is the h.bodySize payload).
  - ct_len = h.bodySize - 16;  tag = body + ct_len;   (if tag is trailing)
  - chacha20_poly1305_init_64x64(&ctx, key, frame_counter);
  - chacha20_poly1305_decrypt(&ctx, body, ct_len, body);   // in place
  - if (chacha20_poly1305_verify(&ctx, tag) != 0) { bad++; ... }   // else ok
  - frame_counter++;   (per-stream, starts 0; exact rule from B0)
Then the EXISTING checks run on the now-plaintext `body`:
  - au_ok(body, ct_len, nal_len, &has_idr) -> nal_ok should become 1
  - the first IDR + SPS (1440x540) prove the key+nonce are right
  - dump_au still writes /mnt/UDISK/c111.h264 when cluster_dump.on exists
Set `ok`/`bad`/`idr` from these as today; the frame-stats line already prints them.

## B4. Validate (one car trip; keep idevicesyslog running)
Build `STEP_DECRYPT=1` as v1148, flash, connect CarPlay + Maps, getlogs.
Pass: dmesg shows `111 frames=... nal_ok=1 bad=0 idr>=1`, and the dumped
c111.h264 (if a shell is available) or the SPS line reads 1440x540 cluster map.
Fail nal_ok=0: wrong key (swap Output/Input), wrong nonce start/width, or the
tag is not trailing - re-check B0.

## B5. Then Phase C (relay) - already written
With plaintext frames, the step-3 relay path (STEP_RELAY, hu1 build) forwards
them to the head unit: it re-encrypts with the HU screen key (captured at
SetupProxyScreenSecurityInfo) and sends on the 111 socket. Build step 3 with
STEP_DECRYPT=1; everything else (shared relay, reconnect, key-frame request) is
in place. Head-unit guard stays ON; no head-unit change.

## Guardrails (unchanged, do not regress)
- Every new symbol via next_sym, optional; decrypt disabled if any is missing.
- No new hard imports (check_imports.sh / build step 5b must still pass).
- Keep a full FEL dump + restore-app-v129 before flashing; recovery is ~5 min.
- Daily driving between tests: stock v129 or v1141.
