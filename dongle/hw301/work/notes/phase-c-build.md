# Phase C - last step: build and test the relay to the Virtual Cockpit

Goal: the iPhone's cluster map on the Virtual Cockpit, normal CarPlay on the MMI.

## Prerequisite (do not skip)
Phase B must pass first: v1148 (or v1149) logs `111 frames=... nal_ok=1 bad=0`
with idr >= 1. Phase C forwards only frames that decrypted and passed the H.264
check, so if Phase B fails, Phase C shows nothing.
Which image passed decides the Phase C build:
  - v1148 passed (Output key) -> build `step3`        -> v1150
  - v1149 passed (Input key)  -> build `step3-input`  -> v1151

## What to build
Nothing new to write. Everything is in `work/src/cluster_dual.c`, and the two
targets already exist in `work/tools/build_dual.sh`:

```bash
cd /Users/kamilgurgul/bin/audi/mib2q-carplay/dongle/hw301
./work/tools/build_dual.sh step3          # -> work/out/hw301_v1150.tar
# or, only if Phase B needed the Input key:
./work/tools/build_dual.sh step3-input    # -> work/out/hw301_v1151.tar
```
Flags: `-DCLUSTER_STEP=3 -DCLUSTER_ADVERT=3 -DSTEP_DECRYPT=1` (+
`-DCLUSTER_KEY_INPUT=1` for v1151).
Do NOT use the old target `3` (v1134): it has STEP_DECRYPT=0 and never relays.

Build checks (the script does them; the build stops if one fails):
- step 5b: `all 43 non-weak imports of libCoreUtils.so resolve on the dongle`
- 0 hard imports of chacha/Pairing (runtime lookups only)
- `python3 work/tools/flash_local.py work/out/hw301_v1150.tar --version 1150 --dry-run`
  -> 127 chunks

## What the step-3 build does on top of Phase B (all already in the code)
1. Dongle -> head unit SETUP: clones the main stream (110) as type 111,
   keeping its streamConnectionID (`injected head-unit cluster stream type=111`).
   The head-unit hook answers with `data_port=7100 type=111` (proven 2026-10-11/12
   with the sender guard ON).
2. Captures the head unit's screen AES key where CPAAProxyEx derives it
   (`head-unit screen key captured hu_gen=N`). Because the clone has the same
   streamConnectionID, that key is valid for 111.
3. On the first decrypted cluster frame: connects to the head unit's :7100
   (address = peer of the dongle's stream-110 socket), sends the cached
   VIDEO_CONFIG (plain), asks the iPhone for a key frame, then forwards every
   decrypted frame re-encrypted with the head-unit key (AES-CTR, header bodySize
   = plaintext length, no tag).
4. One shared head-unit socket; survives iPhone 111 reconnects; reconnects and
   re-keys on a head-unit re-SETUP; retries a failed connect every 5 s, up to 12.
5. Head unit: NO change. Sender guard (`sender_listed`, iPhone9,1/18D70) stays ON.

## Test (one car trip)
1. Flash on bench power (Mac on `smartBox-54EE`):
   ```bash
   python3 work/tools/flash_local.py work/out/hw301_v1150.tar --version 1150
   curl http://192.168.5.1/clbuild.txt     # expect v1150-...
   ```
2. In the car: iPhone cabled to the Mac with the log capture running
   (`idevicesyslog -u <udid> > work/logs/iphone-syslog-v1150.log`).
   Plug in the dongle, connect CarPlay, open Maps, start a route, wait 5-10 min.
   Watch the Virtual Cockpit: the map view on the cluster.
3. Before unplugging the dongle:
   ```bash
   python3 work/tools/getlogs.py --out work/logs
   grep -a cluster_dual work/logs/dmesg.log | tail -60
   ```
4. Head unit: copy `/tmp/altscreen111.log` (your usual access).

## Pass criteria
Dongle dmesg, in this order:
```
cluster_dual: injected head-unit cluster stream type=111
cluster_dual: head-unit screen key captured hu_gen=1
cluster_dual: 111 chacha key derived info=DataStream-Output-Encryption-Key ...
cluster_dual: 111 video config ... sps=1440x540
cluster_dual: head-unit 111 connected <addr>%<if>:7100
cluster_dual: forceKeyFrame requested rc=0
cluster_dual: 111 frames=... nal_ok=1 ... relayed=<climbing> send_err=0
```
Head unit `/tmp/altscreen111.log`:
```
GEN2 SETUP contains stream111 ...
AltScreen stream 111 connected
gen2: stream111 valid transactional video config received
gen2: first complete Screen VIDEO_FRAME accepted as AU
```
Car: map on the Virtual Cockpit, CarPlay on the MMI, both stable 10+ min.

## If it fails - where to look
| Symptom | Likely cause | Next |
|---|---|---|
| no `injected head-unit cluster stream` | inject hook did not match the SETUP dict | send dmesg; check `bind ok ... cpaa_base` non-zero |
| no `head-unit screen key captured` | SPSSI caller range missed | send dmesg (`bind ok ... spssi=`) |
| `relay: no head-unit screen key captured yet` | as above | same |
| `head-unit addr not found` | no peer :6030/:5000 found | send dmesg + app.log |
| `head-unit connect err=` / `gave up after 12 tries` | HU listener not up / PF | head-unit log: was `SETUP contains stream111` logged? |
| `head-unit send failed` | HU closed the socket | head-unit log around that time |
| HU log: `AES-CTR decrypt failed` or invalid video | HU key/counter mismatch | try the plaintext fallback (below) |
| relayed climbs, HU accepts, cluster still black | renderer side | `/tmp/altscreen_render.log` on the head unit |

Plaintext fallback (only if the HU rejects our encryption): needs a head-unit
hook change to accept unencrypted 111 frames from iPhone9,1/18D70, plus
`touch /mnt/UDISK/cluster_dual.plainhu` (needs a dongle shell) or a build flag.
Not needed unless the HU log shows decrypt errors.

## Recovery and daily use
- Recovery any time: `python3 work/tools/flash_local.py fw/hw301_v129.tar --version 129`
  (FEL restore if the dongle shows no LED: see "INCIDENT + RECOVERY").
- Daily driving between tests: stock v129 or v1141.
