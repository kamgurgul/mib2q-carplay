#!/bin/sh
# M.I.B. -> Advanced Settings -> Run Custom Script: AltScreen advertisement OFF (A/B test).
#
# Creates /mnt/app/mibr-carplay111.noadvertise, which makes the hook return the
# stock /info dictionary byte-for-byte and stop adding enabledFeatures on the
# SETUP path. The hook then advertises no AltScreen display and accepts no
# stream-111 SETUP, so the unit negotiates CarPlay exactly as it does without us.
#
# Purpose: separate "our advertisement is rejected" from "our video path
# misbehaves". If the main screen stays up and the dongle connects with this
# marker in place, the cause is the advertisement, not the video.
# See docs/deploy/altscreen-mhi2q.md section 14 case D.
#
# Everything else (route guidance, maneuver arrow, cover art) is unaffected.
# Re-read per CarPlay session: reconnect the phone to apply, no reboot.
# Run altscreen_on to revert.
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

M=/mnt/app/mibr-carplay111.noadvertise
echo "custom.sh: disabling the AltScreen /info advertisement"
mount -uw /mnt/app 2>/dev/null || echo "custom.sh: mount -uw /mnt/app failed"
: > "$M" || { echo "FAILED: cannot create $M (is /mnt/app read-only?)"; exit 1; }
[ -e "$M" ] || { echo "FAILED: $M was not created"; exit 1; }
sync
echo "DONE. AltScreen advertisement OFF ($M)."
echo "Reconnect the phone or dongle to apply (no reboot needed)."
echo "Expected with it OFF: no CarPlay video on the cluster, stock main screen."
echo "Run altscreen_on to turn the cluster video back on."
