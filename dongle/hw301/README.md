# HW301 CarPlay dongle

Reverse-engineering + firmware-mod workspace for the **HW301** wireless CarPlay
dongle (Allwinner SoC, Linux 4.9, musl, ARM EABI5; CarPlay stack in `CPAAProxyEx`
+ `libAirPlay.so`). Goal: drive the Audi Virtual Cockpit cluster map through the
dongle. The dongle is an AirPlay **receiver** to the phone (wireless) and a
CarPlay **sender** to the MHI2Q head unit (USB) — a single-screen bridge that
impersonates an iPhone (`iPhone9,1` / build `18D70`) over iAP2.

Everything about *this dongle* lives under `dongle/hw301/`. The head-unit side of
the integration lives in the repo root (`altscreen_hook/`, `scripts/`, `mods/`).

## Layout (tracked)
- `work/notes/` — the investigation record. Start with **`relay-plan.md`**
  (full pipeline map, pinned memory addresses, strategy) and `virtual-cockpit.md`.
- `work/src/` — injected shim sources (`cluster_inject.c`, `cluster_diag.c`, …).
- `work/tools/` — `build.sh` (wrapper-rename inject + CPAA binary patch),
  `repack.sh` (OTA tar), `flash_local.py` (flash without the vendor server),
  `getlogs.py` (pull device logs locally), `patch_bytes.py`.
- `work/ghidra/` — headless Ghidra scripts (`*.java`) and saved decompilation
  dumps (`*.txt`, e.g. `relay_decomp.txt`, `dispparse.txt`, `cb_full.txt`).
- `work/cmp/` — small reference captures (vendor JSON).
- `fw/` — **original stock firmware** (archived for recovery): the flashable OTA
  tars `hw301_v126.tar` / `hw301_v129.tar`, each version's squashfs `app.img`
  (the build input) + `appmd5sum.txt`, and the vendor version metadata. The
  unpacked `fw/v*/root/` trees are *not* committed (derive with `unsquashfs app.img`).
  Also `hw301_v1162.tar` (`v1162/app.img`, marker `v1162-20261020152434`): the
  custom release image (`build_dual.sh release`, HU_DIRECT, diagnostics off).
  It is not stock. Flash it with `--version 1162`. Roll back with `hw301_v129.tar`.
- `dongle_dump.py`, `fw_download.py` — fetch stock firmware / dump flash.

## Not committed (regenerable / vendor / owner data — see `../.gitignore`)
- `fw/v*/root/` — unpacked stock rootfs → `unsquashfs fw/v<ver>/app.img`.
- `dongle_dump/` — raw flash dumps → `dongle_dump.py`.
- `work/out/`, `work/build-v*/`, `work/extract/`, `work/rollback/` — build outputs
  and unpacked trees → rebuild with `work/tools/build.sh`.
- `work/logs/` — device logs (owner data) → `work/tools/getlogs.py`.
- `work/ghidra/proj/` — Ghidra project DB → re-import the stock binary.
- `work/.venv/`, `work/exp/` — venv and scratch test stubs.

## Build / flash (bench power only — see the FLASHING RULE in the notes)
```
work/tools/build.sh 129 <code>        # produce work/out/hw301_v<code>.tar
work/.venv/bin/python work/tools/flash_local.py work/out/hw301_v<code>.tar --version <code>
work/.venv/bin/python work/tools/getlogs.py --out work/logs
```
Recover a bad flash by flashing the stock image (`fw/hw301_v129.tar`) on bench power.
