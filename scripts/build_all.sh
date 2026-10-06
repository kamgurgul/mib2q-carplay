#!/bin/bash
# Build every artifact and stage a complete release straight into the M.I.B.
# installer payload, mods/install_MoreIncredibleBash/mod/carplay/ (gitignored).
#
#   ./scripts/build_all.sh               # base + AltScreen + Android Auto
#   ALTSCREEN=0 ./scripts/build_all.sh   # no cluster video renderer / CarPlay video hook
#   ANDROID_AUTO=0 ./scripts/build_all.sh   # no Android Auto gal launcher / cluster hook
#
# Android Auto route guidance (cluster arrow, HUD, lanes) lives in the jar and works
# with ANDROID_AUTO=0 too; ANDROID_AUTO adds the Android Auto cluster map video, which
# also needs the AltScreen renderer.
#
# The individual build_*.sh scripts still write to build/; this script runs them
# in order, then replaces the payload with exactly one release so a stale file
# from an earlier build can never ride along. Copy mods/install_MoreIncredibleBash/
# to the SD card afterwards.
set -e

[ "$#" -eq 0 ] || { echo "usage: [ALTSCREEN=0] [ANDROID_AUTO=0] ./scripts/build_all.sh"; exit 2; }

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ALTSCREEN="${ALTSCREEN:-1}"
[[ "$ALTSCREEN" == "0" || "$ALTSCREEN" == "1" ]] || { echo "Invalid ALTSCREEN"; exit 1; }
ANDROID_AUTO="${ANDROID_AUTO:-1}"
[[ "$ANDROID_AUTO" == "0" || "$ANDROID_AUTO" == "1" ]] || { echo "Invalid ANDROID_AUTO"; exit 1; }
B="$PROJECT_DIR/build"
DEST="$PROJECT_DIR/mods/install_MoreIncredibleBash/mod/carplay"

bash "$SCRIPT_DIR/build_hook.sh"
bash "$SCRIPT_DIR/build_renderers.sh"
bash "$SCRIPT_DIR/build_java.sh"
if [ "$ALTSCREEN" = 1 ]; then
    bash "$SCRIPT_DIR/build_altscreen_render.sh"
    bash "$SCRIPT_DIR/build_altscreen_hook.sh"
fi
[ "$ANDROID_AUTO" = 1 ] && bash "$SCRIPT_DIR/build_aa_hook.sh"
# GEM CarPlay-RGI menu (menu_install mod): bundles the current mod scripts.
bash "$SCRIPT_DIR/build_gem_menu.sh"

FILES=(
    "$B/libcarplay_hook.so"
    "$B/maneuver_render"
    "$B/carplay_hook.jar"
    "$PROJECT_DIR/maneuver_render/resources/flag_atlas.rgba"
    "$PROJECT_DIR/deploy/smartphone_integrator/carplay_startup.sh"
    "$PROJECT_DIR/deploy/smartphone_integrator/carplay_monitor.sh"
    "$PROJECT_DIR/deploy/smartphone_integrator/carplay_processes.sh"
    "$PROJECT_DIR/deploy/smartphone_integrator/carplay_cleanup.sh"
    "$PROJECT_DIR/deploy/smartphone_integrator/carplay_child.json"
)
[ "$ALTSCREEN" = 1 ] && FILES+=("$B/altscreen_render" "$B/libaltscreen111_mhi2q.so")
[ "$ANDROID_AUTO" = 1 ] && FILES+=("$B/libaa_cluster_hook.so"
    "$PROJECT_DIR/deploy/smartphone_integrator/aa_startup.sh"
    "$PROJECT_DIR/deploy/smartphone_integrator/aa_child.json")

for f in "${FILES[@]}"; do
    [ -s "$f" ] || { echo "ERROR: missing build output $f"; exit 1; }
done

echo "=== Staging $DEST ==="
mkdir -p "$DEST"
find "$DEST" -mindepth 1 ! -name .gitkeep -exec rm -rf {} +
cp "${FILES[@]}" "$DEST/"
(cd "$DEST" && ls -l | sed 1d)
echo "Release staged ($(( ${#FILES[@]} )) files, ALTSCREEN=$ALTSCREEN ANDROID_AUTO=$ANDROID_AUTO). Copy mods/install_MoreIncredibleBash/ to the SD card."
