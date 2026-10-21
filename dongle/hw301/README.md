# HW301 CarPlay dongle

Firmware-mod workspace for the **HW301** wireless CarPlay dongle (setup Wi-Fi
`smartBox-xxxx`, page `http://192.168.5.1`; Allwinner V851S, Linux 4.9, musl).
The custom image adds the Audi Virtual Cockpit cluster map. Normal CarPlay stays
on the MMI, and a dedicated iOS cluster stream (111) goes to the head unit's altscreen hook.

Supported from **mib2q-carplay 1.3** (the head-unit hook opens the cluster stream for this
dongle only from that release). User guide: [docs/deploy/hw301-dongle.md](../../docs/deploy/hw301-dongle.md).
How it works, what was learned, and what failed: **[FINDINGS.md](FINDINGS.md)**.
The head-unit side lives in the repo root (`altscreen_hook/`, `scripts/`, `mods/`).

## Layout

| path | what |
|---|---|
| `fw/hw301_v129.tar`, `fw/v129/` | **official** stock v1.29: OTA tar + `app.img` + md5 (the build input and the rollback image) |
| `fw/hw301_v126.tar` | official stock v1.26 |
| `fw/hw301_v1162.tar` | **custom release** (marker `v1162-20261020152434`, `build_dual.sh release`) |
| `fw/hw301_v1160.tar` | **same version as v1162**, but it also contains enabled logging (diagnostic kmsg lines). It is a separate file and OTA code (1160, marker `v1160-20261021065350`) so the two can be told apart. `build_dual.sh optA` |
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

Needs Docker (muslcc toolchain, alpine) and `brew install squashfs`.

```bash
cd dongle/hw301
./work/tools/build_dual.sh release      # -> work/out/hw301_v1162.tar
./work/tools/build_dual.sh optA         # -> work/out/hw301_v1160.tar (v1162 + enabled logging)
```

The build refuses an image whose shim imports a symbol the dongle cannot resolve.

## Flash

**Bench only.** Power the dongle from a USB charger, unplugged from the car, and join its Wi-Fi.
Do not unplug while the percent is moving.

```bash
python3 work/tools/flash_local.py fw/hw301_v1162.tar --version 1162 --dry-run   # 127 chunks, sends nothing
python3 work/tools/flash_local.py fw/hw301_v1162.tar --version 1162
curl http://192.168.5.1/clbuild.txt                                             # v1162-20261020152434
```

The head unit also needs this repo's 1.3 (or newer) install with Cluster video ON.

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
