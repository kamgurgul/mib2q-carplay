# HW301 CarPlay dongle

Firmware-mod workspace for the **HW301** wireless CarPlay dongle (setup Wi-Fi
`smartBox-xxxx`, page `http://192.168.5.1`; Allwinner V851S, Linux 4.9, musl).
The custom image adds the Audi Virtual Cockpit cluster map. Normal CarPlay stays
on the MMI, and a dedicated iOS cluster stream (111) goes to the head unit's altscreen hook.

Supported from **mib2q-carplay 1.3** (the head-unit hook opens the cluster stream for this
dongle only from that release). User guide: [docs/deploy/hw301-dongle.md](../../docs/deploy/hw301-dongle.md).
How it works, what was learned, and what failed: **[FINDINGS.md](FINDINGS.md)**.
The head-unit side lives in the repo root (`altscreen_hook/`, `scripts/`, `mods/`).

## Newest image: v1176

`fw/hw301_v1176.tar` (marker `v1176-20261104084344`, `build_dual.sh release`) is the
newest custom image: v1172 plus the route guidance bridge, which relays iAP2 route
guidance (0x5200-0x5204) between the car and the iPhone, so the HUD and the cluster
maneuver arrow work through the dongle. Route guidance passed a first car test on
2026-10-28. `fw/hw301_v1177.tar` is the same version with logging (`build_dual.sh optA`).

v1172 (`fw/hw301_v1172.tar`, marker `v1172-20261027093548`) is made for mib2q-carplay 1.4. It replaces the dongle's single-screen
proxy boundary and uses the car's normal second-display advertisement, SETUP and
commands. Stream 111 has its own connection ID, returned data port and encryption
state. The dongle reports `model=HW301`; the head unit needs no HW301-specific protocol.
`fw/hw301_v1173.tar` is the same version with logging (`build_dual.sh optA`).

The previous release `fw/hw301_v1167.tar` stays supported through the head unit's
legacy listener. **v1172 passed a first car test on 2026-10-27** (as v1173); layout
and menu switching, unplug/replug and phone Wi-Fi loss are still to be tested.
Builds do not flash hardware. Architecture, ABI
evidence, test coverage and the car result are in
[FINDINGS.md](FINDINGS.md#current-source-v1172-native-proxy-candidate).

## Layout

| path | what |
|---|---|
| `fw/hw301_v129.tar`, `fw/v129/` | **official** stock v1.29: OTA tar + `app.img` + md5 (the build input and the rollback image) |
| `fw/hw301_v126.tar` | official stock v1.26 |
| `fw/hw301_v1176.tar` | **newest custom image** (marker `v1176-20261104084344`, `build_dual.sh release`): v1172 plus the route guidance bridge (iAP2 0x5200-0x5204 between car and phone, so the HUD and cluster maneuvers work). First car pass 2026-10-28. See FINDINGS "Route guidance bridge" |
| `fw/hw301_v1177.tar` | **same version as v1176**, but it also contains enabled logging. OTA code 1177, marker `v1177-20261104084350`. `build_dual.sh optA` |
| `fw/hw301_v1172.tar` | previous custom image (marker `v1172-20261027093548`, `build_dual.sh release`): native two-screen proxy; the car's cluster display, menu switches and layouts reach the phone. Made for mib2q-carplay 1.4 |
| `fw/hw301_v1173.tar` | **same version as v1172**, but it also contains enabled logging (diagnostic kmsg lines). Separate file and OTA code (1173, marker `v1173-20261027093537`). `build_dual.sh optA` |
| `fw/hw301_v1167.tar` | previous custom release (marker `v1167-20261025192423`): v1166 plus ETA, speed limit and compass on the wide cockpit layout; the classic and Sport small maps keep only the turn card |
| `fw/hw301_v1166.tar` | previous custom release (marker `v1166-20261021090922`, `build_dual.sh release`): v1164 plus no ETA on the cockpit (`showETA=no`), key frame requests from the head unit (`KEYF` back-channel) and a late key frame 3 s after the relay attaches, so the cockpit map starts after a cold start |
| `fw/hw301_v1164.tar` | previous custom release (marker `v1164-20261021080230`): adds the Sport-skin ViewArea, 300 px small-map SafeAreas, initial URL without `maneuverLayout` |
| `fw/hw301_v1162.tar` | older custom release (marker `v1162-20261020152434`) |
| `fw/hw301_v1160.tar` | **same version as v1162** (not v1164/v1166), but it also contains enabled logging (diagnostic kmsg lines). It is a separate file and OTA code (1160, marker `v1160-20261021065350`) so the two can be told apart. `build_dual.sh optA` |
| `fw/rollback/restore-app-v129-at-0x7e0000.bin` | FEL restore image of the `app` partition (stock v129 `app.img` padded to 0x3f0000) |
| `work/src/cluster_dual.c` | the shim (installed as `libCoreUtils.so`) |
| `work/tools/` | `build_dual.sh` → `build.sh` → `repack.sh`, `check_imports.sh`, `xcc.sh`, `flash_local.py`, `getlogs.py`, `fel_backup.sh`, `xref.py`/`findref.py` |
| `work/ghidra/` | saved decompilation dumps + the scripts that made them (the other exploratory scripts are kept locally, git-ignored) |
| `work/cmp/vendor_pos0.json` | a vendor upload chunk (reference for the flash JSON format) |
| `dongle_dump.py`, `fw_download.py` | dump the web UI / fetch stock firmware from the vendor |

Not committed (`../.gitignore`): `work/out/`, `work/build-v*/` (build output),
`work/logs/` (device logs), `work/rollback/` (your own FEL flash dumps),
`work/third_party/` (xfel), `fw/v*/root/` (`unsquashfs fw/v129/app.img`).

## Build

Needs Docker (muslcc toolchain, alpine) and squashfs-tools (`brew install squashfs`, or
`apt install squashfs-tools` on Linux/WSL).

```bash
cd dongle/hw301
./work/tools/build_dual.sh release      # -> work/out/hw301_v1176.tar
./work/tools/build_dual.sh optA         # -> work/out/hw301_v1177.tar (v1176 + logging)
```

The build refuses an image whose shim imports a symbol the dongle cannot resolve.
It also checks the SHA-256 hashes of stock `CPAAProxyEx`, `libAirPlay.so`,
`libCoreUtils.so` and `libiAP2Link.so` with [check_native_abi.py](work/tools/check_native_abi.py).
The native proxy uses inspected v129 offsets (`+0x3ec`, `0x190`, `0x2b8`), so a
`Native proxy ABI mismatch` means the input binaries need to be checked before
building against them. Use the tracked stock v129 image. A rebuild gets a new build marker; the tracked
`fw/hw301_v1176.tar` is the release build of the car-tested source (the 2026-10-28 car test
ran build `v1176-20261028100645` of the same code, which was never committed).
Images are ignored by `dongle/.gitignore` (`*.tar`); add a new one with `git add -f`.

## Flash

**Bench only.** Power the dongle from a USB charger, unplugged from the car, and join its Wi-Fi.
Do not unplug while the percent is moving.

```bash
python3 work/tools/flash_local.py fw/hw301_v1176.tar --version 1176 --dry-run   # 127 chunks, sends nothing
python3 work/tools/flash_local.py fw/hw301_v1176.tar --version 1176
curl http://192.168.5.1/clbuild.txt                                             # v1176-20261104084344
```

The head unit needs this repo's 1.4 install (1.3 or newer also works, without the wide-view
switch; v1172 and newer on 1.3.x are not tested) with Cluster video ON.

Test logs, pulled on bench power after a car trip:

```bash
python3 work/tools/getlogs.py --out work/logs
grep -a cluster_dual work/logs/dmesg.log | tail -60
```

## Rollback

### 1. Over Wi-Fi (the dongle still boots: LED on, `smartBox` AP up)

Flash the official image with the same bench setup:

```bash
python3 work/tools/flash_local.py fw/hw301_v129.tar --version 129
curl http://192.168.5.1/clbuild.txt    # 404 = stock (only custom images carry the marker)
```

v1.26 (`fw/hw301_v126.tar --version 126`) also works.

### 2. Over FEL (soft-brick: no LED, no Wi-Fi, no USB after a flash)

Only the `app` partition (0x7e0000, 0x6c0000 long) is written by an OTA, so a soft-brick
is fixed by writing stock `app.img` back there over USB. You do not need a programmer or
any soldering. Recovery takes about 5 minutes.

1. **Get xfel** v1.4.0 or newer ([github.com/xboot/xfel](https://github.com/xboot/xfel), `brew install libusb`,
   `make`). Put the binary at `work/third_party/xfel/xfel`, or set `XFEL=/path/to/xfel`.
2. **Enter FEL.**
   - Unplug the dongle and open it.
   - On the 8-pin flash chip above the SoC, short **pin 2** (DO, the second leg from the top
     on the left; pin 1 has the dot) to GND. The metal shell of the USB plug is GND.
   - Plug the dongle into the Mac and release the short after about 2 s.
   - `xfel version` should print `ID=0x00188600(V851/V853)`.
3. **Back up first** (read-only). This reads the whole 16 MiB twice and compares the reads:
   ```bash
   ./work/tools/fel_backup.sh            # -> work/rollback/flash-<ts>-1.bin, flash-latest.bin
   ```
   Keep this dump. It holds the dongle's own `env`/`private` partitions, which are not in the repo.
4. **Write the stock app partition.** The image is exactly 0x3f0000 bytes, 64 KiB aligned
   (xfel erases whole blocks):
   ```bash
   X=work/third_party/xfel/xfel
   $X spinor write 0x7e0000 fw/rollback/restore-app-v129-at-0x7e0000.bin
   ```
5. **Verify** by reading back and comparing. The first 4124672 bytes must match the stock md5:
   ```bash
   $X spinor read 0x7e0000 0x3f0000 /tmp/readback.bin
   cmp /tmp/readback.bin fw/rollback/restore-app-v129-at-0x7e0000.bin && echo OK
   head -c 4124672 /tmp/readback.bin | md5 ; cat fw/v129/appmd5sum.txt   # d5c46c2d363f97a1f274bd041aaea723
   ```
6. Unplug the dongle and power it from a USB charger. The LED and the `smartBox` AP come back on stock v1.29.

To make a restore image from a different stock `app.img`, pad it to the next 64 KiB boundary
with the bytes that follow it in your own dump at 0x7e0000+size. Write the result at 0x7e0000.
