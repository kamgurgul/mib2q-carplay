#!/bin/sh
# M.I.B. -> Advanced Settings -> Run Custom Script: DISABLE CarPlay route guidance.
#
# Writes the persistent marker /mnt/app/carplay_rgd.disabled. RgdModule reads it at
# every CarPlay session start, so route guidance (the cluster maneuver arrow, HUD
# and BAP takeover) stays off from the NEXT phone reconnect — no reboot. CarPlay
# video (AltScreen, ctx 81), cover art, PDC and touchpad are unaffected. Reverse
# with the rgd_enable script.
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

# GEM runs on MMX; the marker lives with lsd on MMX. Forward a manual RCC launch
# (false once on MMX, so no loop).
if [ ! -d /mnt/app/eso/hmi/lsd ] && [ -d /net/mmx/mnt/app/eso/hmi/lsd ]; then
    exec on -f mmx /bin/sh "$D/custom.sh" "$@"
fi

MARKER=/mnt/app/carplay_rgd.disabled
echo "custom.sh: disabling CarPlay route guidance (CarPlay video only)"
mount -uw /mnt/app 2>/dev/null || true
if : > "$MARKER"; then
    # /tmp copy makes it effective this boot even before /mnt/app is re-read.
    : > /tmp/carplay_rgd.disabled 2>/dev/null || true
    sync
    echo "DONE. Route guidance OFF."
    echo "Reconnect the phone to apply (no reboot needed). Run rgd_enable to turn it back on."
else
    echo "FAILED to write $MARKER"; exit 1
fi
