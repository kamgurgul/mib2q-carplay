#!/bin/sh
# GEM CarPlay-RGI menu on a fake unit: menu_install copies the screen + scripts,
# every .esd button points at an installed executable wrapper, actions are
# byte-identical to the mods, the dispatcher finds a card for logs, fails cleanly
# without a release, rejects unknown actions, and remove_menu cleans up.
# Every POSIX/ksh shell here (QNX 6.5 /bin/sh is pdksh).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL ($1): $2"; exit 1; }
bash "$ROOT/scripts/build_gem_menu.sh" > /dev/null
MOD=$ROOT/mods/menu_install_MoreIncredibleBash/mod
# QNX fs-dos cannot stat "."/".." inside FAT card folders: no recursive copy/find from the card.
grep -nE '^[^#]*(cp -R|cp -r|find )' "$MOD/custom.sh" && { echo "FAIL: recursive copy from the card in menu_install"; exit 1; }
shells=
for s in /bin/ksh /bin/mksh "$(command -v mksh 2>/dev/null)" /bin/dash /bin/sh; do [ -x "$s" ] && shells="$shells $s"; done
for sh in $shells; do
    rm -rf "$T/gem" "$T/card" "$T/src" "$T/sd"
    mkdir -p "$T/gem" "$T/card/mod" "$T/src"
    cp -R "$MOD/." "$T/card/mod/"
    G=$T/gem
    CP_GEM_DIR=$G "$sh" "$T/card/mod/custom.sh" > "$T/out" 2>&1 || { cat "$T/out"; fail "$sh" "menu_install"; }
    [ -f "$G/mib2q-carplay.esd" ] || fail "$sh" "no esd installed"
    # every button path in the .esd exists (relocated to the test GEM dir) and is executable
    for p in $(sed -n 's/.*sys 1 0x0100 "\(.*\)".*/\1/p' "$G/mib2q-carplay.esd"); do
        f=$G/${p#/mnt/app/eso/hmi/engdefs/}
        [ -x "$f" ] || fail "$sh" "button target missing/not executable: $p"
    done
    for a in install uninstall logging extract_lsd altscreen_on altscreen_off rgd_enable rgd_disable altscreen_grid altscreen_safearea; do
        cmp -s "$ROOT/mods/${a}_MoreIncredibleBash/mod/custom.sh" "$G/scripts/carplay/actions/$a/custom.sh" \
            || fail "$sh" "action $a differs from its mod"
    done
    R=$G/scripts/carplay/run.sh
    set +e
    CP_GEM_DIR=$G "$sh" "$R" bogus > /dev/null 2>&1; rc=$?
    [ $rc -eq 2 ] || fail "$sh" "unknown action rc=$rc"
    CP_GEM_DIR=$G CP_CARDS="$T/nocard" "$sh" "$R" install > "$T/out" 2>&1; rc=$?
    [ $rc -eq 1 ] && grep -q "no release on the SD card" "$T/out" || fail "$sh" "install without release rc=$rc"
    mkdir -p "$T/sd"; echo h > "$T/src/carplay_hook.log"
    CP_GEM_DIR=$G CP_CARDS="$T/nocard $T/sd" CP_LOG_SRC=$T/src "$sh" "$R" logs > "$T/out" 2>&1; rc=$?
    [ $rc -eq 0 ] && [ -f "$T/sd/carplay_logs/001/tmp/carplay_hook.log" ] || { cat "$T/out"; fail "$sh" "logs not saved to card rc=$rc"; }
    CP_GEM_DIR=$G "$sh" "$R" remove_menu > /dev/null 2>&1; rc=$?
    set -e
    [ $rc -eq 0 ] && [ ! -e "$G/mib2q-carplay.esd" ] && [ ! -d "$G/scripts/carplay" ] || fail "$sh" "remove_menu rc=$rc"
done
echo "GEM menu: install, button targets, actions = mods, card discovery, remove:$shells PASS"
