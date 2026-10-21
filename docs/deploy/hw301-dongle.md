---
title: HW301 wireless dongle - custom firmware for the Virtual Cockpit map
tags: [deploy, dongle, hw301, wireless, carplay]
status: verified
sources:
  - code: dongle/hw301/work/src/cluster_dual.c, altscreen_hook/libaltscreen111_gen2.c
  - car: v1154 test 2026-10-20 (map on the VC, CarPlay on the MMI, roller zoom)
---

# HW301 wireless dongle

Supported from **mib2q-carplay 1.3**. A head unit with 1.2 or older never opens the
cluster stream for this dongle, even with the custom firmware flashed.

An **HW301** is the wireless CarPlay dongle whose setup Wi-Fi is named like `smartBox-xxxx`
and whose setup page is `http://192.168.5.1`. With its stock firmware (v1.26 or v1.29) it
shows CarPlay on the MMI only, and the Virtual Cockpit keeps the Audi map. This happens
because the dongle presents itself to the car as an iPhone 7 (`iPhone9,1` / `18D70`) and
never opens the cluster stream.

The custom app image **v1162** keeps the stock main-screen behaviour and adds the phone's
cluster map on the Virtual Cockpit:

- the iPhone draws a dedicated cluster view, not a copy of the MMI screen;
- the steering-wheel roller zooms the map;
- the phone draws the turn card and ETA on the cluster;
- switching between the wide and classic cockpit layouts resizes the map *(car test pending)*.

It is CarPlay only. It does not add Android Auto on the cluster.

## You need

- The head unit on **mib2q-carplay 1.3 or newer**, with **Cluster video ON**. The SD-card
  installer does not flash the dongle, and flashing the dongle installs nothing on the head unit.
- A computer with Python 3 and a checkout of this repository.
- A USB charger or power bank to power the dongle on the bench.

| File | What it is |
| --- | --- |
| `dongle/hw301/fw/hw301_v1162.tar` | the custom image you flash (marker `v1162-20261020152434`) |
| `dongle/hw301/fw/hw301_v1160.tar` | the same version as v1162, but it also contains enabled logging. Flash it only to collect logs for a bug report (`--version 1160`, marker `v1160-20261021065350`) |
| `dongle/hw301/fw/hw301_v129.tar` | official stock v1.29, the rollback image |
| `dongle/hw301/fw/hw301_v126.tar` | official stock v1.26 |
| `dongle/hw301/fw/rollback/restore-app-v129-at-0x7e0000.bin` | FEL recovery image (see below) |

> [!WARNING]
> Flash the dongle only on a bench, powered from a USB charger and **unplugged from the car**.
> The update overwrites the partition that also holds the Wi-Fi flasher. If an image is bad,
> only FEL recovery over USB brings the dongle back; it cannot be fixed from the car. Do not
> unplug the dongle while the flash percent is moving. Other `smartBox-xxxx` boards are not
> HW301; do not flash this image onto them.

## Flash v1162

1. Install or update the head unit to 1.3 first ([install](install.md)). Leave **Cluster video ON**.
2. Power the HW301 from a USB charger. Do not connect it to the car.
3. Join its Wi-Fi and open `http://192.168.5.1`. If the page does not load, you are not on
   the dongle's network.
4. Check the image without writing anything:

   ```bash
   python3 dongle/hw301/work/tools/flash_local.py dongle/hw301/fw/hw301_v1162.tar --version 1162 --dry-run
   ```

   The dry run prints the tar size and `127` chunks, and says it is not sending.
5. Flash it:

   ```bash
   python3 dongle/hw301/work/tools/flash_local.py dongle/hw301/fw/hw301_v1162.tar --version 1162
   ```

   Wait until both the upload bar and the flash percent finish.
6. Optional check: `curl http://192.168.5.1/clbuild.txt` prints `v1162-20261020152434`.
7. Unplug the dongle and plug it back into the charger once. Then install it in the car.
8. Connect the phone through the dongle. The Virtual Cockpit shows the phone's map. A long
   press of the left steering-wheel roller switches between that map and the Audi map. A short
   press still toggles the route-info line.

## Back to stock

Use the same bench setup. This returns the dongle to the vendor's v1.29 (MMI only):

```bash
python3 dongle/hw301/work/tools/flash_local.py dongle/hw301/fw/hw301_v129.tar --version 129
```

`curl http://192.168.5.1/clbuild.txt` then returns 404, because stock images have no marker.

## Recovery when the dongle no longer starts

Signs: after a flash the dongle shows no LED, no `smartBox` Wi-Fi and no USB device. Only
its app partition is damaged. You can write the stock v1.29 image back over USB in Allwinner
FEL mode with [xfel](https://github.com/xboot/xfel), without a programmer or soldering.

The full procedure covers entering FEL, backing up the flash first, writing
`restore-app-v129-at-0x7e0000.bin` at `0x7e0000` and verifying the write. It is in
[dongle/hw301/README.md](../../dongle/hw301/README.md#rollback).

## Troubleshooting

| Symptom | Try |
| --- | --- |
| No map on the cockpit, CarPlay on the MMI works | Head unit on 1.3 with **Cluster video ON**? `clbuild.txt` shows v1162? Reconnect the phone. |
| CarPlay does not start at all after the flash | Flash stock v1.29 (above) and report it with the dongle logs. |
| Dongle dead after a flash (no LED, no Wi-Fi) | [FEL recovery](#recovery-when-the-dongle-no-longer-starts). |

Dongle logs: on bench power, run `python3 dongle/hw301/work/tools/getlogs.py --out dongle/hw301/work/logs`
and attach `dmesg.log`. The script does not upload anything to the vendor.

How the image works is described in [dongle/hw301/FINDINGS.md](../../dongle/hw301/FINDINGS.md).
