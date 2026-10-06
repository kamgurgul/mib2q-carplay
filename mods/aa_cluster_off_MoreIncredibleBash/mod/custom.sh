#!/bin/sh
# M.I.B. -> Advanced Settings -> Run Custom Script: Android Auto cluster map OFF.
#
# Creates /mnt/app/root/aa_cluster.off. aa_startup.sh then starts the stock gal
# without the cluster-display hook: the phone sees no second display and Android
# Auto runs exactly as without this patch. Android Auto route guidance on the
# cluster arrow, HUD and lanes keeps working (it comes from the HMI, not the hook).
# Applies from the next phone connection; run aa_cluster_on to revert.
#
# QNX 6.5 /bin/sh is ksh; stays inside its portable subset.
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

M=/mnt/app/root/aa_cluster.off
echo "custom.sh: Android Auto cluster map OFF"
mount -uw /mnt/app 2>/dev/null || echo "custom.sh: mount -uw /mnt/app failed"
: > "$M" || { echo "FAILED: cannot create $M (is /mnt/app read-only?)"; exit 1; }
[ -e "$M" ] || { echo "FAILED: $M was not created"; exit 1; }
sync
echo "DONE. Android Auto cluster map OFF ($M)."
echo "Reconnect the phone to apply (no reboot needed)."
