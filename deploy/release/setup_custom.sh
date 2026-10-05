#!/bin/sh
# mib2q-carplay release setup - M.I.B. -> Advanced Settings -> Run Custom Script.
#
# One run does the whole first install from this SD card:
#   1. installs CarPlay from <card>/carplay/   (mod/install/custom.sh, the install mod)
#   2. installs the Green Engineering Menu page "CarPlay-RGI" (mod/menu/custom.sh)
# Then reboot. Later updates: copy a new carplay/ folder to the card and use
# GEM -> CarPlay-RGI -> "Install / update CarPlay" (no M.I.B. needed).
#
# QNX 6.5 /bin/sh is ksh; stays inside its portable subset.
# Copyright (c) 2026
# SPDX-License-Identifier: GPL-3.0-or-later
set -u
PATH=/proc/boot:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/usr/bin
export PATH
unset LD_PRELOAD

case $0 in */*) D=${0%/*} ;; *) D=. ;; esac
D=$(cd "$D" && pwd) || exit 1

# The HMI files live on the MMX; forward a manual RCC launch.
if [ ! -d /mnt/app/eso/hmi/lsd ] && [ -d /net/mmx/mnt/app/eso/hmi/lsd ]; then
    exec on -f mmx /bin/sh "$D/custom.sh" "$@"
fi

CARD=${D%/mod}
VERSION=$(cat "$CARD/carplay/VERSION" 2>/dev/null || echo unknown)
echo "mib2q-carplay $VERSION setup"

[ -f "$CARD/carplay/libcarplay_hook.so" ] || {
    echo "FAILED: $CARD/carplay/ not found - copy the whole release to the card root."
    exit 1
}

echo "=== 1/2 Install CarPlay ==="
CP_INSTALL_RES=$CARD/carplay
export CP_INSTALL_RES
/bin/sh "$D/install/custom.sh" || { echo "FAILED: CarPlay install (see above). Nothing else was changed."; exit 1; }

echo "=== 2/2 Install the Green Engineering Menu page ==="
/bin/sh "$D/menu/custom.sh" || { echo "WARN: menu not installed (CarPlay itself is installed)."; }

sync
echo ""
echo "DONE. mib2q-carplay $VERSION installed."
echo "Disconnect CarPlay, wait a few seconds and reboot the head unit."
echo "After the reboot: Green Engineering Menu -> CarPlay-RGI."
