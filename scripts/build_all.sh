#!/bin/bash
# Build every artifact and stage a complete release straight into the M.I.B.
# installer payload, mods/install_MoreIncredibleBash/mod/carplay/ (gitignored).
#
#   ./scripts/build_all.sh               # base + AltScreen
#   ALTSCREEN=0 ./scripts/build_all.sh   # base only (no cluster video)
#
# The individual build_*.sh scripts still write to build/; this script runs them
# in order, then replaces the payload with exactly one release so a stale file
# from an earlier build can never ride along. Copy mods/install_MoreIncredibleBash/
# to the SD card afterwards.
set -e

[ "$#" -eq 0 ] || { echo "usage: [ALTSCREEN=0] ./scripts/build_all.sh"; exit 2; }

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ALTSCREEN="${ALTSCREEN:-1}"
[[ "$ALTSCREEN" == "0" || "$ALTSCREEN" == "1" ]] || { echo "Invalid ALTSCREEN"; exit 1; }
B="$PROJECT_DIR/build"
DEST="$PROJECT_DIR/mods/install_MoreIncredibleBash/mod/carplay"

bash "$SCRIPT_DIR/build_hook.sh"
bash "$SCRIPT_DIR/build_renderers.sh"
bash "$SCRIPT_DIR/build_java.sh"
if [ "$ALTSCREEN" = 1 ]; then
    bash "$SCRIPT_DIR/build_altscreen_render.sh"
    bash "$SCRIPT_DIR/build_altscreen_hook.sh"
fi
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

for f in "${FILES[@]}"; do
    [ -s "$f" ] || { echo "ERROR: missing build output $f"; exit 1; }
done

echo "=== Staging $DEST ==="
mkdir -p "$DEST"
find "$DEST" -mindepth 1 ! -name .gitkeep -exec rm -rf {} +
cp "${FILES[@]}" "$DEST/"
(cd "$DEST" && ls -l | sed 1d)
echo "Release staged ($(( ${#FILES[@]} )) files, ALTSCREEN=$ALTSCREEN). Copy mods/install_MoreIncredibleBash/ to the SD card."
