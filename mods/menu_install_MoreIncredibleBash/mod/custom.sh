#!/bin/sh
# M.I.B. -> Advanced Settings -> Run Custom Script: install the GEM "CarPlay-RGI" menu.
#
# Run once. Copies carplay_menu/ from this card to the head unit:
#   mib2q-carplay.esd        -> /mnt/app/eso/hmi/engdefs/            (the GEM screen)
#   scripts/carplay/         -> /mnt/app/eso/hmi/engdefs/scripts/carplay/
# After a reboot the Green Engineering Menu shows "CarPlay-RGI" with one button
# per mod (install/update, logs, AltScreen on/off, route guidance on/off,
# uninstall, remove menu). The buttons run the same scripts as the mods, now
# stored on the unit, so the /mod folder on the card no longer has to be swapped.
# "Install / update" still reads the release from the card (/mod/carplay/), and
# "Save logs" writes to the card.
#
# Re-run to update the menu after a new build. Remove it with the menu's own
# "Remove this menu" button (or delete the two paths above).
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

# GEM and its engdefs live on the MMX. Forward a manual RCC launch.
if [ ! -d /mnt/app/eso/hmi/lsd ] && [ -d /net/mmx/mnt/app/eso/hmi/lsd ]; then
    exec on -f mmx /bin/sh "$D/custom.sh" "$@"
fi

SRC=$D/carplay_menu
GEM_DIR=${CP_GEM_DIR:-/mnt/app/eso/hmi/engdefs}   # = the HMI's de.audi.gem.path.esdfiles
DST=$GEM_DIR/scripts/carplay
ESD=$GEM_DIR/mib2q-carplay.esd

[ -f "$SRC/mib2q-carplay.esd" ] && [ -f "$SRC/scripts/carplay/run.sh" ] || {
    echo "FAILED: $SRC is incomplete (build it with scripts/build_gem_menu.sh)"
    exit 1
}
[ -d "$GEM_DIR" ] || { echo "FAILED: no GEM definitions folder at $GEM_DIR"; exit 1; }

echo "custom.sh: installing the CarPlay-RGI GEM menu"
mount -uw /mnt/app 2>/dev/null || echo "custom.sh: mount -uw /mnt/app failed"

# Stage next to the target, then swap, so a failed copy never leaves half a menu.
STAGE=$DST.new.$$
rm -rf "$STAGE"
mkdir -p "$STAGE" || { echo "FAILED: cannot create $STAGE (is /mnt/app read-only?)"; exit 1; }
# File by file, never cp -R: QNX fs-dos cannot stat "." / ".." inside folders of
# some FAT cards ("cp: Can't get file status .../carplay/./actions/..."), so a
# recursive copy fails although every file is readable. Plain globs only.
copy_x() {   # copy_x <src> <dst>: copy one script and make it executable (FAT has no exec bit)
    cp "$1" "$2" && chmod 755 "$2" || { echo "FAILED: copying $1"; rm -rf "$STAGE"; exit 1; }
}
n_src=0
for f in "$SRC"/scripts/carplay/*.sh; do
    [ -f "$f" ] || continue
    copy_x "$f" "$STAGE/${f##*/}"
    n_src=$((n_src+1))
done
for f in "$SRC"/scripts/carplay/actions/*/custom.sh; do
    [ -f "$f" ] || continue
    a=${f%/custom.sh}; a=${a##*/}
    mkdir -p "$STAGE/actions/$a" || { echo "FAILED: mkdir actions/$a"; rm -rf "$STAGE"; exit 1; }
    copy_x "$f" "$STAGE/actions/$a/custom.sh"
done
[ -f "$STAGE/run.sh" ] && [ "$n_src" -gt 1 ] || { echo "FAILED: no scripts copied"; rm -rf "$STAGE"; exit 1; }
rm -rf "$DST" && mv "$STAGE" "$DST" || { echo "FAILED: installing $DST"; exit 1; }

cp "$SRC/mib2q-carplay.esd" "$ESD.new.$$" && chmod 644 "$ESD.new.$$" && mv "$ESD.new.$$" "$ESD" \
    || { echo "FAILED: installing $ESD"; rm -f "$ESD.new.$$"; exit 1; }
sync

n=0
for f in "$DST"/*.sh; do [ -x "$f" ] && n=$((n+1)); done
echo "DONE. $ESD + $n scripts in $DST."
echo "Reboot the head unit, then open the Green Engineering Menu -> CarPlay-RGI."
