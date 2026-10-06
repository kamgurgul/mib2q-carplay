#!/bin/bash
# Build a complete, versioned release: release/mib2q-carplay-<version>/ and its .zip.
#
#   ./scripts/build_release.sh              # version 1.1, full build first
#   VERSION=1.1 ./scripts/build_release.sh
#   NO_BUILD=1 ./scripts/build_release.sh   # reuse the current staged build
#
# Layout:
#   SD-CARD/mod/custom.sh, command.sh   one-run setup (CarPlay + Green Engineering Menu)
#   SD-CARD/mod/install/                CarPlay installer (install mod), reads ../../carplay
#   SD-CARD/mod/menu/                   GEM menu installer (menu_install mod)
#   SD-CARD/carplay/                    the release incl. carplay_hook.jar, + VERSION
#   extras/MIB-mods/<name>/mod/         single M.I.B. scripts for use without the menu
#   RELEASE_NOTES.md, THIRD_PARTY_NOTICES.md, LICENSE, licenses/, SHA256SUMS.txt, VERSION
set -euo pipefail

[ "$#" -eq 0 ] || { echo "usage: [VERSION=x.y] [NO_BUILD=1] ./scripts/build_release.sh"; exit 2; }

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${VERSION:-1.1}"
NAME="mib2q-carplay-$VERSION"
OUT="$PROJECT_DIR/release/$NAME"
MODS="$PROJECT_DIR/mods"
STAGED="$MODS/install_MoreIncredibleBash/mod/carplay"
NOTES="$PROJECT_DIR/docs/release/RELEASE_NOTES-$VERSION.md"
FFMPEG_LICENSE_URL="https://raw.githubusercontent.com/FFmpeg/FFmpeg/n6.1.5/COPYING.LGPLv2.1"

[ -f "$NOTES" ] || { echo "ERROR: missing $NOTES"; exit 1; }

if [ "${NO_BUILD:-0}" != 1 ]; then
    bash "$PROJECT_DIR/scripts/build_all.sh"
fi

RELEASE_FILES="libcarplay_hook.so maneuver_render flag_atlas.rgba carplay_startup.sh
carplay_monitor.sh carplay_processes.sh carplay_cleanup.sh carplay_hook.jar
carplay_child.json altscreen_render libaltscreen111_mhi2q.so
libaa_cluster_hook.so aa_startup.sh aa_child.json"
for f in $RELEASE_FILES; do
    [ -s "$STAGED/$f" ] || { echo "ERROR: $STAGED/$f missing - run without NO_BUILD=1"; exit 1; }
done
[ -s "$MODS/menu_install_MoreIncredibleBash/mod/carplay_menu/mib2q-carplay.esd" ] \
    || bash "$PROJECT_DIR/scripts/build_gem_menu.sh"

echo "=== Assembling $OUT ==="
rm -rf "$OUT" "$OUT.zip"
mkdir -p "$OUT/SD-CARD/mod/install" "$OUT/SD-CARD/mod/menu" "$OUT/SD-CARD/carplay" \
         "$OUT/extras/MIB-mods" "$OUT/licenses"

# SD card: release payload
for f in $RELEASE_FILES; do cp "$STAGED/$f" "$OUT/SD-CARD/carplay/"; done
echo "$VERSION" > "$OUT/SD-CARD/carplay/VERSION"

# SD card: one-run setup + the two installers it calls
cp "$PROJECT_DIR/deploy/release/setup_custom.sh" "$OUT/SD-CARD/mod/custom.sh"
cp "$MODS/install_MoreIncredibleBash/mod/command.sh" "$OUT/SD-CARD/mod/command.sh"
cp "$MODS/install_MoreIncredibleBash/mod/custom.sh" "$OUT/SD-CARD/mod/install/custom.sh"
cp "$MODS/menu_install_MoreIncredibleBash/mod/custom.sh" "$OUT/SD-CARD/mod/menu/custom.sh"
cp -R "$MODS/menu_install_MoreIncredibleBash/mod/carplay_menu" "$OUT/SD-CARD/mod/menu/"

# Extras: single M.I.B. mods (the setup covers install + menu)
for m in uninstall logging altscreen_on altscreen_off rgd_enable rgd_disable \
         altscreen_grid altscreen_safearea extract_lsd aa_cluster_on aa_cluster_off touchpad_dpad_on touchpad_dpad_off; do
    src="$MODS/${m}_MoreIncredibleBash/mod"
    [ -s "$src/custom.sh" ] || { echo "ERROR: missing $src/custom.sh"; exit 1; }
    mkdir -p "$OUT/extras/MIB-mods/$m/mod"
    for f in "$src"/*; do
        case "${f##*/}" in last-carplay-*.log) continue ;; esac
        cp -R "$f" "$OUT/extras/MIB-mods/$m/mod/"
    done
    # Older M.I.B. release zips run command.sh: every mod gets the forwarder.
    [ -f "$OUT/extras/MIB-mods/$m/mod/command.sh" ] \
        || cp "$MODS/install_MoreIncredibleBash/mod/command.sh" "$OUT/extras/MIB-mods/$m/mod/command.sh"
done

# Documents
cp "$NOTES" "$OUT/RELEASE_NOTES.md"
cp "$PROJECT_DIR/docs/release/THIRD_PARTY_NOTICES.md" "$OUT/THIRD_PARTY_NOTICES.md"
cp "$PROJECT_DIR/LICENSE" "$OUT/LICENSE"
echo "$VERSION" > "$OUT/VERSION"
curl -fsSL "$FFMPEG_LICENSE_URL" -o "$OUT/licenses/FFmpeg-COPYING.LGPLv2.1" \
    || { echo "ERROR: could not fetch the FFmpeg LGPL text ($FFMPEG_LICENSE_URL)"; exit 1; }

find "$OUT" -name '*.sh' -exec chmod 755 {} +
chmod 755 "$OUT/SD-CARD/carplay/maneuver_render" "$OUT/SD-CARD/carplay/altscreen_render"

if command -v sha256sum >/dev/null 2>&1; then SHA="sha256sum"; else SHA="shasum -a 256"; fi
(cd "$OUT" && find SD-CARD extras -type f | LC_ALL=C sort | xargs $SHA) > "$OUT/SHA256SUMS.txt"

(cd "$PROJECT_DIR/release" && rm -f "$NAME.zip" && zip -qr "$NAME.zip" "$NAME")
echo "Release: $OUT"
echo "Zip:     $PROJECT_DIR/release/$NAME.zip ($(du -h "$PROJECT_DIR/release/$NAME.zip" | cut -f1))"
