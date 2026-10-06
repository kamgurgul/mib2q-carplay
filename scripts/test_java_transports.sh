#!/bin/bash
# Compile shipping Java 1.4 sources, then exercise input and local transports.
set -euo pipefail
PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
. "$PROJECT_DIR/scripts/stock_env.sh"
stock_require jar jdk
TEST_JDK="$JDK"
bash "$PROJECT_DIR/scripts/build_java.sh"
TEST_DIR=$(mktemp -d)
trap 'rm -rf "$TEST_DIR"' EXIT
"$TEST_JDK/bin/javac" -encoding UTF-8 -cp "$PROJECT_DIR/build/carplay_hook.jar" -d "$TEST_DIR" \
    "$PROJECT_DIR/tests/CarplayBusTransportTest.java" \
    "$PROJECT_DIR/tests/RendererServerTransportTest.java" \
    "$PROJECT_DIR/tests/TouchpadControllerTest.java"
"$TEST_JDK/bin/java" -cp "$TEST_DIR:$PROJECT_DIR/build/carplay_hook.jar" TouchpadControllerTest
"$TEST_JDK/bin/java" -cp "$TEST_DIR:$PROJECT_DIR/build/carplay_hook.jar" com.luka.carplay.bus.CarplayBusTransportTest
"$TEST_JDK/bin/java" -cp "$TEST_DIR:$PROJECT_DIR/build/carplay_hook.jar" com.luka.carplay.rgd.RendererServerTransportTest

# NAVSD INITIALIZING/READY must pass straight through the RGI gate
# (the altscreen INITIALIZING takeover and AltScreenStartupTest do not apply here).
mkdir -p "$TEST_DIR/nav-init"
"$TEST_JDK/bin/javac" -encoding UTF-8 \
    -cp "$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR" \
    -d "$TEST_DIR/nav-init" \
    "$PROJECT_DIR/tests/GatedCombiServiceInitStateTest.java"
"$TEST_JDK/bin/java" \
    -cp "$TEST_DIR/nav-init:$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR" \
    com.luka.carplay.rgd.GatedCombiServiceInitStateTest

# Android Auto adapter: event sequence -> RouteGuidance text frames, lanes, views, VC gate.
mkdir -p "$TEST_DIR/aa"
"$TEST_JDK/bin/javac" -encoding UTF-8 \
    -cp "$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR" -d "$TEST_DIR/aa" \
    "$PROJECT_DIR/tests/AaRouteStateTest.java"
"$TEST_JDK/bin/java" -cp "$TEST_DIR/aa:$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR" \
    com.luka.carplay.aa.AaRouteStateTest

# Exercise the actual lifecycle worker against controllable external modules.
mkdir -p "$TEST_DIR/lifecycle"
"$TEST_JDK/bin/javac" -encoding UTF-8 -d "$TEST_DIR/lifecycle" \
    "$PROJECT_DIR/java_patch/com/luka/carplay/core/CarPlayApp.java" \
    "$PROJECT_DIR/java_patch/com/luka/carplay/core/Module.java" \
    "$PROJECT_DIR/tests/CarPlayAppLifecycleTest.java" \
    "$PROJECT_DIR/tests/stubs/app-lifecycle/com/luka/carplay/core/LifecycleFixtures.java" \
    "$PROJECT_DIR/tests/stubs/app-lifecycle/com/luka/carplay/bus/CarplayBus.java" \
    "$PROJECT_DIR/tests/stubs/app-lifecycle/com/luka/carplay/framework/Log.java" \
    "$PROJECT_DIR/tests/stubs/app-lifecycle/com/luka/carplay/pdc/PdcSmallStageGuard.java" \
    "$PROJECT_DIR/tests/stubs/app-lifecycle/de/audi/app/terminalmode/IContext.java" \
    "$PROJECT_DIR/tests/stubs/app-lifecycle/de/audi/atip/base/IFrameworkAccess.java"
for scenario in publication during-start replug failure bounce owners; do
    "$TEST_JDK/bin/java" -cp "$TEST_DIR/lifecycle" com.luka.carplay.core.CarPlayAppLifecycleTest "$scenario"
done
# Parking resource policy also uses the freshly built shipping JAR.
bash "$PROJECT_DIR/scripts/test_pdc.sh"

