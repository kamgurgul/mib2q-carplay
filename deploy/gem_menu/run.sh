#!/bin/sh
# GEM (Green Engineering Menu) -> CarPlay-RGI -> <button>: dispatcher.
#
# Installed by the menu_install mod to /mnt/app/eso/hmi/engdefs/scripts/carplay/
# next to actions/<name>/custom.sh, which are verbatim copies of the M.I.B. mods
# (mods/<name>_MoreIncredibleBash/mod/custom.sh), so every button runs exactly
# the script the matching mod runs. Each GEM entry calls a one-line wrapper that
# runs this file with the action name.
#
# Only these actions need the SD card, and they find it themselves:
#   install      - the release (libcarplay_hook.so ...) in <card>/mod/carplay/
#                  or <card>/carplay/, i.e. a staged install_MoreIncredibleBash
#   logs, lsd    - any writable card/USB stick for the output folder
#   safearea     - <card>/carplay_safearea.conf (wide VC map) and/or
#                  <card>/carplay_safearea_small.conf (classic small map), x=/y=/w=/h=
#                  like the altscreen_safearea mod's safearea.conf
#
# QNX 6.5 /bin/sh is ksh; stays inside its portable subset.
# Copyright (c) 2026
# SPDX-License-Identifier: GPL-3.0-or-later
set -u
PATH=/proc/boot:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/usr/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/sbin
export PATH
unset LD_PRELOAD

case $0 in */*) M=${0%/*} ;; *) M=. ;; esac
M=$(cd "$M" && pwd) || exit 1
A=$M/actions
GEM_DIR=${CP_GEM_DIR:-/mnt/app/eso/hmi/engdefs}
ESD=$GEM_DIR/mib2q-carplay.esd
CARDS=${CP_CARDS:-/fs/sda0 /fs/sdb0 /fs/usb0_0 /net/mmx/fs/sda0 /net/mmx/fs/sdb0 /net/mmx/fs/usb0_0}

can_write() { : > "$1/.carplay_write_test" 2>/dev/null && rm -f "$1/.carplay_write_test"; }

# First present card/stick that is (or can be remounted) writable.
writable_card() {
    for c in $CARDS; do
        [ -d "$c" ] || continue
        can_write "$c" || mount -uw "$c" 2>/dev/null
        can_write "$c" && { echo "$c"; return 0; }
    done
    return 1
}

# First card holding a staged release.
release_dir() {
    for c in $CARDS; do
        for r in "$c/mod/carplay" "$c/carplay"; do
            [ -f "$r/libcarplay_hook.so" ] && { echo "$r"; return 0; }
        done
    done
    return 1
}

run_action() {
    [ -f "$A/$1/custom.sh" ] || { echo "FAILED: action $1 missing in $A (re-run menu_install)"; exit 1; }
    exec /bin/sh "$A/$1/custom.sh"
}

ACT=${1:-}
echo "CarPlay-RGI menu: $ACT"
case $ACT in
    install)
        R=$(release_dir) || {
            echo "FAILED: no release on the SD card."
            echo "Copy install_MoreIncredibleBash/mod/ to the card as /mod/ (it holds carplay/)."
            exit 1
        }
        echo "release: $R"
        CP_INSTALL_RES=$R
        export CP_INSTALL_RES
        run_action install
        ;;
    logs|lsd)
        C=$(writable_card) || { echo "FAILED: no writable SD card or USB stick found"; exit 1; }
        CP_CARD=$C
        export CP_CARD
        [ "$ACT" = logs ] && run_action logging
        run_action extract_lsd
        ;;
    safearea)
        # carplay_safearea.conf = wide VC map (ViewArea 0),
        # carplay_safearea_small.conf = classic small map window (ViewArea 1).
        n=0
        for name in carplay_safearea.conf:mibr-carplay111-safearea.conf \
                    carplay_safearea_small.conf:mibr-carplay111-safearea-small.conf; do
            F=
            for c in $CARDS; do
                [ -f "$c/${name%%:*}" ] && { F=$c/${name%%:*}; break; }
            done
            [ -n "$F" ] || continue
            echo "values: $F"
            CP_SAFEAREA_SRC=$F CP_SAFEAREA_DST=/mnt/app/root/${name#*:} \
                /bin/sh "$A/altscreen_safearea/custom.sh" || exit 1
            n=$((n+1))
        done
        [ "$n" -gt 0 ] || { echo "FAILED: no carplay_safearea.conf / carplay_safearea_small.conf in the root of the SD card"; exit 1; }
        ;;
    safearea_reset)
        echo reset > /tmp/carplay_safearea_reset.conf || exit 1
        for f in mibr-carplay111-safearea.conf mibr-carplay111-safearea-small.conf; do
            CP_SAFEAREA_SRC=/tmp/carplay_safearea_reset.conf CP_SAFEAREA_DST=/mnt/app/root/$f \
                /bin/sh "$A/altscreen_safearea/custom.sh" || exit 1
        done
        ;;
    singleview)
        # Fallback: advertise one ViewArea (as every test before the classic layout).
        M=/mnt/app/root/mibr-carplay111-viewareas.single
        mount -uw /mnt/app 2>/dev/null
        if [ -e "$M" ]; then
            rm -f "$M"; sync
            echo "DONE. Two ViewAreas (wide + classic) from the next CarPlay connection."
        else
            : > "$M" || { echo "FAILED: cannot create $M"; exit 1; }
            sync
            echo "DONE. Single ViewArea (wide only) from the next CarPlay connection."
        fi
        ;;
    grid)
        run_action altscreen_grid
        ;;
    hwdec)
        # altscreen_render reads this per CarPlay connection. A failed hardware
        # setup or decoder error removes it again (software decode continues).
        M=/mnt/app/root/altscreen_render.hwdecode
        mount -uw /mnt/app 2>/dev/null
        if [ -e "$M" ]; then
            rm -f "$M"; sync
            echo "DONE. Hardware decoder OFF (software decode) from the next CarPlay connection."
        else
            : > "$M" || { echo "FAILED: cannot create $M"; exit 1; }
            sync
            echo "DONE. Hardware decoder ON from the next CarPlay connection."
            echo "If it fails it switches itself OFF; 'Save logs' shows why (hw_decode: lines)."
        fi
        ;;
    omxprobe)
        # altscreen_render consumes the marker within ~1 s and logs the Qualcomm
        # OMX decoder's requirements to /tmp/altscreen_render.log (no decoding).
        mount -uw /mnt/app 2>/dev/null
        : > /mnt/app/root/altscreen_render.omxprobe || { echo "FAILED: cannot create the probe marker"; exit 1; }
        sync
        echo "DONE. Probe requested; altscreen_render runs it within ~1 s."
        echo "Then press 'Save logs to SD' and send altscreen_render.log."
        ;;
    uninstall|altscreen_on|altscreen_off|rgd_enable|rgd_disable)
        run_action "$ACT"
        ;;
    remove_menu)
        mount -uw /mnt/app 2>/dev/null
        rm -f "$ESD"
        rm -rf "$M"
        sync
        if [ -e "$ESD" ] || [ -d "$M" ]; then
            echo "FAILED: menu files still present (is /mnt/app read-only?)"
            exit 1
        fi
        echo "DONE. CarPlay-RGI menu removed; it disappears after the next reboot."
        ;;
    *)
        echo "usage: run.sh install|uninstall|logs|lsd|altscreen_on|altscreen_off|rgd_enable|rgd_disable|grid|safearea|safearea_reset|singleview|hwdec|omxprobe|remove_menu"
        exit 2
        ;;
esac
