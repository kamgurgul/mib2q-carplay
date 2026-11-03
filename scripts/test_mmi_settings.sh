#!/bin/bash
# Settings persistence/localization, real HMI model bank, and native map URL policy.
set -euo pipefail
PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
. "$PROJECT_DIR/scripts/stock_env.sh"
stock_require jar jdk
[ "${SKIP_JAVA_BUILD:-0}" = 1 ] || bash "$PROJECT_DIR/scripts/build_java.sh"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
CP="$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR:$OSGI_LIBS"
"$JDK/bin/javac" -encoding UTF-8 -cp "$CP" -d "$OUT" \
    "$PROJECT_DIR/tests/CarPlaySettingsTest.java" "$PROJECT_DIR/tests/CoverArtClearTest.java" \
    "$PROJECT_DIR/tests/SettingsModelTest.java"
"$JDK/bin/java" -cp "$OUT:$CP" com.luka.carplay.connectivity.CarPlaySettingsTest "$OUT"
"$JDK/bin/java" -cp "$OUT:$CP" CoverArtClearTest
"$JDK/bin/java" -Xverify:none -cp "$OUT:$CP" com.luka.carplay.connectivity.SettingsModelTest
cc -std=gnu99 -O1 -Wall -Wextra -Werror -Wno-unused-function \
    -I"$PROJECT_DIR/altscreen_hook/include" "$PROJECT_DIR/tests/alt111_settings_test.c" \
    "$PROJECT_DIR/altscreen_hook/src/alt111_profile.c" "$PROJECT_DIR/altscreen_hook/src/alt111_control.c" \
    "$PROJECT_DIR/altscreen_hook/src/alt111_video.c" "$PROJECT_DIR/altscreen_hook/src/alt111_resync.c" \
    -lpthread -ldl -o "$OUT/alt-settings"
"$OUT/alt-settings" "$OUT"
