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

# Vehicle data -> CMD_VEHICLE wire layout (hook/vehicle reads the same 14 bytes).
"$TEST_JDK/bin/javac" -encoding UTF-8 -cp "$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR" \
    -d "$TEST_DIR" "$PROJECT_DIR/tests/VehicleDataEncodingTest.java"
"$TEST_JDK/bin/java" -cp "$TEST_DIR:$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR" \
    com.luka.carplay.core.VehicleDataEncodingTest

# Cluster phone tab: Android Auto's FctID 14 type 6 reaches the VC as HFP 3.
"$TEST_JDK/bin/javac" -encoding UTF-8 -cp "$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR" \
    -d "$TEST_DIR" "$PROJECT_DIR/tests/GatedPhoneAndroidAutoTest.java"
"$TEST_JDK/bin/java" -cp "$TEST_DIR:$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR" \
    com.luka.carplay.telephony.GatedPhoneAndroidAutoTest

# Cluster phone tab: a favorite's contact-formatted number dials as a clean tel: URL.
"$TEST_JDK/bin/javac" -encoding UTF-8 -cp "$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR" \
    -d "$TEST_DIR" "$PROJECT_DIR/tests/GatedPhoneDialTest.java"
"$TEST_JDK/bin/java" -cp "$TEST_DIR:$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR" \
    com.luka.carplay.telephony.GatedPhoneDialTest

# Cluster phone tab: dio_manager's orphaned call entries hidden, hang-up never stuck.
"$TEST_JDK/bin/javac" -encoding UTF-8 -cp "$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR" \
    -d "$TEST_DIR" "$PROJECT_DIR/tests/GatedPhoneCallsTest.java"
"$TEST_JDK/bin/java" -cp "$TEST_DIR:$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR" \
    com.luka.carplay.telephony.GatedPhoneCallsTest

# Power-on: a startDomain the target app dropped is re-sent, against the real stock DomainHandler.
"$TEST_JDK/bin/javac" -encoding UTF-8 -cp "$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR" \
    -d "$TEST_DIR" "$PROJECT_DIR/tests/DomainStartRescueTest.java"
"$TEST_JDK/bin/java" -cp "$TEST_DIR:$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR" \
    com.luka.carplay.core.DomainStartRescueTest

# Siri on a short voice-key press is opt-in (marker); default = stock car voice control.
"$TEST_JDK/bin/javac" -encoding UTF-8 -cp "$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR" \
    -d "$TEST_DIR" "$PROJECT_DIR/tests/SiriShortPressTest.java"
"$TEST_JDK/bin/java" -cp "$TEST_DIR:$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR" SiriShortPressTest

# Android Auto adapter: event sequence -> RouteGuidance text frames, lanes, views, VC gate.
mkdir -p "$TEST_DIR/aa"
"$TEST_JDK/bin/javac" -encoding UTF-8 \
    -cp "$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR" -d "$TEST_DIR/aa" \
    "$PROJECT_DIR/tests/AaRouteStateTest.java"
"$TEST_JDK/bin/java" -cp "$TEST_DIR/aa:$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR" \
    com.luka.carplay.aa.AaRouteStateTest

# Real native rich-navigation parser -> record -> Java -> existing BAP trip outputs.
cc -std=gnu99 -O1 -Wall -Wextra -Werror -pthread \
    "$PROJECT_DIR/tests/aa_trip_test.c" -o "$TEST_DIR/aa_trip"
"$TEST_DIR/aa_trip" "$TEST_DIR/aa_trip.bin"
"$TEST_JDK/bin/javac" -encoding UTF-8 \
    -cp "$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR" -d "$TEST_DIR/aa" \
    "$PROJECT_DIR/tests/AaTripDetailsTest.java"
"$TEST_JDK/bin/java" -cp "$TEST_DIR/aa:$PROJECT_DIR/build/carplay_hook.jar:$STOCK_JAR" \
    com.luka.carplay.aa.AaTripDetailsTest "$TEST_DIR/aa_trip.bin"

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

