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
#   safearea     - <card>/carplay_safearea.conf (wide VC map),
#                  <card>/carplay_safearea_small.conf (classic small map) and/or
#                  <card>/carplay_safearea_sport.conf (Sport small map), x=/y=/w=/h=
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

# CarPlay cluster map content switch: $1 = file suffix, $2 = default (yes|no), $3 = label.
# Absent file = default; the toggle writes the other value or removes the file again.
# With a CarPlay cluster session up, the hook re-sends showUI with the new URL.
cluster_switch() {
    F=/mnt/app/root/mibr-carplay111-nav.$1
    cur=$2
    [ -f "$F" ] && cur=$(cat "$F" 2>/dev/null)
    case $cur in yes|1|user) cur=yes ;; *) cur=no ;; esac
    if [ "$cur" = yes ]; then new=no; else new=yes; fi
    mount -uw /mnt/app 2>/dev/null
    if [ "$new" = "$2" ]; then
        rm -f "$F"
    else
        echo "$new" > "$F" || { echo "FAILED: cannot write $F"; exit 1; }
    fi
    sync
    if [ "$new" = yes ]; then echo "DONE. $3 ON on the cluster map."; else echo "DONE. $3 OFF on the cluster map."; fi
    if grep -q '^command_ready=1' /tmp/mibr-alt111-gen2.status 2>/dev/null; then
        : > /tmp/mibr-alt111-show-only
        echo "Applied to the running CarPlay session (reconnect if the VC does not change)."
    else
        echo "Applies at the next CarPlay connection."
    fi
}

# Opt-in marker switch: $1 = file, $2 = text when ON, $3 = text when OFF.
marker_toggle() {
    mount -uw /mnt/app 2>/dev/null
    if [ -e "$1" ]; then
        rm -f "$1"; sync
        echo "DONE. $3"
    else
        : > "$1" || { echo "FAILED: cannot create $1"; exit 1; }
        sync
        echo "DONE. $2"
    fi
}

# Read-only status page: what the cluster video renderer and the hooks report right now.
# Paths overridable for the host test (CP_TMP, CP_ROOT).
status_page() {
    TD=${CP_TMP:-/tmp}
    RD=${CP_ROOT:-/mnt/app/root}
    echo "== Cluster video (altscreen_render)"
    if [ -f "$TD/altscreen_render.status" ]; then
        grep -E '^(state|source|decoder|hw_off|frames|connections|keyframe_requests|damaged)=' \
            "$TD/altscreen_render.status"
    else
        echo "renderer not running (no $TD/altscreen_render.status)"
    fi
    if [ -f "$TD/altscreen_render.live" ]; then echo "cockpit: phone map live"; else echo "cockpit: phone map not live"; fi
    for m in "$RD/altscreen_render.noasm" "$TD/altscreen_render.noasm" \
             "$RD/altscreen_render.hwdecode.off" "$TD/altscreen_render.hwdecode.off" \
             "$RD/altscreen_render.aa.hwdecode.off" "$TD/altscreen_render.aa.hwdecode.off" \
             "$RD/altscreen_render.hwdecode.delayed"; do
        [ -e "$m" ] && echo "marker: $m"
    done
    echo "== CarPlay cluster stream (stream 111)"
    if [ -f "$TD/mibr-alt111-gen2.status" ]; then
        grep -E '^(command_ready|projection_desired|shown_ack|source_aus|source_idrs|delivered_aus|dropped_aus|resyncs|nav_url)=' \
            "$TD/mibr-alt111-gen2.status"
    else
        echo "no CarPlay cluster session this boot"
    fi
    echo "== Android Auto cluster display"
    if [ -f "$TD/aa_cluster_hook.log" ]; then
        grep -E 'init (enabled|disabled)|cluster\.(start|stop)|focus\.' "$TD/aa_cluster_hook.log" | tail -n 4
    else
        echo "no Android Auto session this boot"
    fi
    echo "== Options"
    for m in cluster_video.pause_hidden aa_cluster.keyframe_refocus aa_cluster.daynight aa_cluster.off \
             aa_cluster.uiconfig_legacy aa_cluster.insets \
             mibr-carplay111-view.animation mibr-carplay111-viewareas.single; do
        [ -e "$RD/$m" ] && echo "set: $RD/$m"
    done
    return 0
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
        # carplay_safearea_small.conf = classic small map window (ViewArea 1),
        # carplay_safearea_sport.conf = Sport skin small map (ViewArea 2).
        n=0
        for name in carplay_safearea.conf:mibr-carplay111-safearea.conf \
                    carplay_safearea_small.conf:mibr-carplay111-safearea-small.conf \
                    carplay_safearea_sport.conf:mibr-carplay111-safearea-sport.conf; do
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
        [ "$n" -gt 0 ] || { echo "FAILED: no carplay_safearea.conf / carplay_safearea_small.conf / carplay_safearea_sport.conf in the root of the SD card"; exit 1; }
        ;;
    safearea_reset)
        echo reset > /tmp/carplay_safearea_reset.conf || exit 1
        for f in mibr-carplay111-safearea.conf mibr-carplay111-safearea-small.conf \
                 mibr-carplay111-safearea-sport.conf; do
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
            echo "DONE. Three ViewAreas (wide + classic + sport) from the next CarPlay connection."
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
        # Hardware decode is the default; these markers turn it OFF, for CarPlay and
        # Android Auto alike. altscreen_render writes them itself after a real hardware
        # failure (Android Auto: the /tmp one only, issue #44). Read per phone connection.
        M=/mnt/app/root/altscreen_render.hwdecode.off
        B=/tmp/altscreen_render.hwdecode.off
        AM=/mnt/app/root/altscreen_render.aa.hwdecode.off
        AB=/tmp/altscreen_render.aa.hwdecode.off
        mount -uw /mnt/app 2>/dev/null
        rm -f /mnt/app/root/altscreen_render.hwdecode   # opt-in marker of older builds
        if [ -e "$M" ] || [ -e "$B" ] || [ -e "$AM" ] || [ -e "$AB" ]; then
            # The start records too: a fresh try is what ON asks for.
            rm -f "$M" "$B" "$AM" "$AB" "$B.starts" "$AB.starts"; sync
            echo "DONE. Hardware decoder ON (default) from the next phone connection."
        else
            : > "$M" || { echo "FAILED: cannot create $M"; exit 1; }
            : > "$AM" || { echo "FAILED: cannot create $AM"; exit 1; }
            sync
            echo "DONE. Hardware decoder OFF (software decode) from the next phone connection."
        fi
        ;;
    hwdelay)
        # The cluster map starts on the hardware decoder at once, CarPlay and Android Auto (no
        # switch, no blink); the hooks keep it off the video core while the main screen's own
        # decoder changes state. This marker brings back the 2-minute wait after an MMI start
        # (software decode until then) for units that still freeze when the decoder opens early
        # (issues #10, #14). Read per phone connection.
        M=/mnt/app/root/altscreen_render.hwdecode.delayed
        mount -uw /mnt/app 2>/dev/null
        if [ -e "$M" ]; then
            rm -f "$M"; sync
            echo "DONE. Cluster map hardware decoder starts at once (default) from the next connection."
        else
            : > "$M" || { echo "FAILED: cannot create $M"; exit 1; }
            sync
            echo "DONE. Cluster map hardware decoder waits 2 minutes after an MMI start (software until then), CarPlay and Android Auto."
        fi
        ;;
    verbose)
        # Full diagnostics that survive a reboot: hook/Java/renderer INFO logs, the boot slog
        # snapshots and the power-on navigation boost trace. Default OFF so the boot stays light.
        # "Save logs" also turns verbose on, but only until the next reboot (/tmp).
        M=/mnt/app/carplay_verbose
        mount -uw /mnt/app 2>/dev/null
        if [ -e "$M" ]; then
            rm -f "$M"; sync
            echo "DONE. Verbose logging OFF from the next boot (WARN only)."
        else
            : > "$M" || { echo "FAILED: cannot create $M"; exit 1; }
            sync
            echo "DONE. Verbose logging ON, also after a reboot. Press again to turn it off."
        fi
        ;;
    siri_short)
        # Opt-in: with CarPlay connected, a short press of the steering-wheel voice key opens
        # Siri instead of the car's own voice control (the long press always opens Siri).
        # Read on every key press, so it applies at once.
        M=/mnt/app/carplay_siri_short.on
        mount -uw /mnt/app 2>/dev/null
        if [ -e "$M" ]; then
            rm -f "$M"; sync
            echo "DONE. Short voice-key press: car voice control (default). Long press: Siri."
        else
            : > "$M" || { echo "FAILED: cannot create $M"; exit 1; }
            sync
            echo "DONE. Short voice-key press: Siri while CarPlay is connected."
        fi
        ;;
    aa_daynight)
        # Opt-in: the Android Auto cluster map follows the MMI day/night design (Light by
        # day, Dark by night) instead of always Dark. Experimental. The theme is picked when
        # the phone connects: changing it in a running session drops Android Auto (#19).
        M=/mnt/app/root/aa_cluster.daynight
        mount -uw /mnt/app 2>/dev/null
        if [ -e "$M" ]; then
            rm -f "$M"; sync
            echo "DONE. Android Auto cluster map always dark (default)."
        else
            : > "$M" || { echo "FAILED: cannot create $M"; exit 1; }
            sync
            echo "DONE. Android Auto cluster map follows MMI day/night (opt-in)."
            echo "If Android Auto on the phone crashes, press this again to turn it off."
        fi
        echo "Applies at the next phone connection (a day/night change while connected waits for it too)."
        ;;
    status)
        status_page
        ;;
    pause_hidden)
        # Opt-in, both phone types: while the cockpit shows no phone map (another VC tab, or the
        # Audi map picked with a long roller press) the phone stops the cluster stream (CarPlay
        # stopUI, Android Auto native video focus) and resumes when the map is back. Applies at
        # the next change of the cockpit view.
        marker_toggle /mnt/app/root/cluster_video.pause_hidden \
            "Cluster map paused while the cockpit hides it (opt-in, test). The last picture stays until the phone map is back." \
            "Cluster map keeps streaming while hidden (default)."
        ;;
    aa_refocus)
        # Opt-in, Android Auto: when the cluster map renderer needs a key frame the tee no longer
        # holds, bounce the cluster display's video focus (native, then projected after 200 ms)
        # so the phone sends one, at most every 10 s. Without it the map waits for the phone's
        # next periodic key frame (a minute or two). Read live.
        marker_toggle /mnt/app/root/aa_cluster.keyframe_refocus \
            "Android Auto cluster map asks the phone for a key frame when it needs one (opt-in, test)." \
            "Android Auto cluster map waits for the phone's own key frames (default)."
        ;;
    noasm)
        # The software decoder (fallback when the hardware decoder is off) uses FFmpeg's
        # ARM/NEON assembly from 1.5. This marker selects its plain C code instead, slower
        # but the decoder of 1.4. The renderer sets it for the boot itself after an illegal
        # instruction. Read when the renderer starts: applies after the next reboot.
        rm -f /tmp/altscreen_render.noasm
        marker_toggle /mnt/app/root/altscreen_render.noasm \
            "Software decoder: plain C code, no NEON (after the next reboot)." \
            "Software decoder: ARM/NEON assembly (default, after the next reboot)."
        ;;
    cluster_maneuver)
        cluster_switch showManeuver yes "Turn card"
        ;;
    cluster_eta)
        cluster_switch showETA no "ETA"
        ;;
    cluster_speed)
        cluster_switch showSpeedLimit no "Speed limit"
        ;;
    cluster_compass)
        cluster_switch showCompass no "Compass"
        ;;
    cluster_wide_all)
        # Priority switch: the wide cockpit view gets turn card, ETA, speed limit and
        # compass whatever the four switches above say; the small maps keep them.
        cluster_switch wideAll no "All map features (wide view only)"
        ;;
    reacquire)
        # Test: re-send the cluster showUI in the running session, so iOS picks
        # which navigation app draws the VC map again (/tmp, consumed by the hook).
        : > /tmp/mibr-alt111-gen2-reacquire || { echo "FAILED: cannot create the marker"; exit 1; }
        echo "DONE. Cluster map re-requested (watch the VC; altscreen111.log: explicit reacquire)."
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
    uninstall|altscreen_on|altscreen_off|rgd_enable|rgd_disable|aa_cluster_on|aa_cluster_off|touchpad_dpad_on|touchpad_dpad_off)
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
        echo "usage: run.sh install|uninstall|logs|lsd|verbose|status|altscreen_on|altscreen_off|rgd_enable|rgd_disable|aa_cluster_on|aa_cluster_off|aa_daynight|aa_refocus|pause_hidden|touchpad_dpad_on|touchpad_dpad_off|grid|safearea|safearea_reset|singleview|cluster_maneuver|cluster_eta|cluster_speed|cluster_compass|cluster_wide_all|reacquire|hwdec|hwdelay|noasm|siri_short|omxprobe|remove_menu"
        exit 2
        ;;
esac
