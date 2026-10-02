#!/bin/sh
# M.I.B. -> Advanced Settings -> Run Custom Script: AltScreen calibration grid ON/OFF.
#
# Toggles /mnt/app/root/altscreen_render.grid. While it exists, altscreen_render
# draws a ruler over the CarPlay cluster video (canvas 1440x540, origin top-left):
#   horizontal lines every 60 px:  60 red, 120 orange, 180 yellow, 240 green,
#                                  300 cyan, 360 blue, 420 magenta, 480 white
#   grey vertical lines every 180 px, grey border on the canvas edges.
# Photograph the VC in each view: the lowest visible colour line and whether the
# grey bottom border shows give the visible canvas rows for the SafeArea.
# Takes effect within ~1 s while the map is moving; no reconnect, no reboot.
# Run again to turn it off.
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

if [ ! -d /mnt/app/eso/hmi/lsd ] && [ -d /net/mmx/mnt/app/eso/hmi/lsd ]; then
    exec on -f mmx /bin/sh "$D/custom.sh" "$@"
fi

M=/mnt/app/root/altscreen_render.grid
mount -uw /mnt/app 2>/dev/null || echo "custom.sh: mount -uw /mnt/app failed"
if [ -e "$M" ]; then
    rm -f "$M"
    [ -e "$M" ] && { echo "FAILED: $M still present (is /mnt/app read-only?)"; exit 1; }
    sync
    echo "DONE. Calibration grid OFF."
else
    : > "$M" || { echo "FAILED: cannot create $M (is /mnt/app read-only?)"; exit 1; }
    sync
    echo "DONE. Calibration grid ON (red 60 / orange 120 / yellow 180 / green 240 /"
    echo "cyan 300 / blue 360 / magenta 420 / white 480 px). Run again to turn it off."
fi
