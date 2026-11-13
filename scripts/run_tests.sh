#!/bin/sh
# Host-side C tests for the RGI hook framework and maneuver renderer.
# Java/renderer suites: test_route_info.sh, test_java_transports.sh,
# test_maneuver_native.sh.  Safe to run anywhere (no HU access).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
if [ "$(uname)" = Darwin ]; then DEAD_STRIP=-Wl,-dead_strip; else DEAD_STRIP=-Wl,--gc-sections; fi

# SANITIZE=1: every C test below is built with ASan + UBSan (any finding fails the run),
# and so are the HW301 bridge tests (test_hw301.sh). Other shell-script suites are not.
if [ "${SANITIZE:-0}" = 1 ]; then
    SAN_FLAGS="-g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"
    cc() { command cc "$@" $SAN_FLAGS; }
    export SANITIZE="$SAN_FLAGS"     # test_hw301.sh takes SANITIZE as compiler flags
    # handle_segv=0: tests that crash a child on purpose keep their own handlers;
    # out-of-bounds accesses are still caught by the instrumentation.
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1:handle_segv=0:handle_sigbus=0:allow_user_segv_handler=1}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"
    echo "SANITIZE=1: C tests built with ASan + UBSan"
fi

printf '%-32s ' maneuver_surface_test
cc -std=gnu99 -O1 -Wall -Wextra -Werror -Wno-unused-function \
    -ffunction-sections -fdata-sections -Imaneuver_render/hostcheck -Icommon \
    tests/maneuver_surface_test.c "$DEAD_STRIP" -lpthread -lm -o "$OUT/maneuver_surface"
"$OUT/maneuver_surface"

printf '%-32s ' gl_program_cache_test
cc -std=gnu99 -O1 -Wall -Wextra -Werror -Wno-unused-function \
    -Imaneuver_render/hostcheck -Icommon tests/gl_program_cache_test.c -o "$OUT/gl_program_cache"
"$OUT/gl_program_cache"

printf '%-32s ' coverart_safety_test
cc -std=gnu99 -O2 -Wall -Wextra -Werror -pedantic -Ihook \
    tests/coverart_safety_test.c hook/coverart/jpeg_safety.c \
    -o "$OUT/coverart_safety"
"$OUT/coverart_safety"

printf '%-32s ' coverart_stream_test
cc -std=gnu99 -O2 -Wall -Wextra -Werror -Ihook \
    tests/coverart_stream_test.c hook/coverart/coverart_stream.c \
    hook/framework/iap2_protocol.c -o "$OUT/coverart_stream"
"$OUT/coverart_stream"

printf '%-32s ' coverart_pipeline_test
cc -std=gnu99 -O1 -Wall -Wextra -Werror \
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable \
    -Ihook "-DCOVERART_DIR=\"$OUT/artwork\"" tests/coverart_pipeline_test.c \
    hook/coverart/jpeg_safety.c hook/coverart/coverart_stream.c \
    hook/framework/iap2_protocol.c -lpthread -lz -lm -o "$OUT/coverart_pipeline"
"$OUT/coverart_pipeline"

printf '%-32s ' rgd_lane_cache_test
python3 -I tests/test_rgd_lane_cache.py

printf '%-32s ' rgd_tlv_test
cc -std=gnu99 -O1 -Wall -Wextra -Werror -Wno-unused-variable -Wno-unused-function \
    -DENABLE_LOGGING=0 -Ihook tests/rgd_tlv_test.c hook/routeguidance/rgd_tlv.c \
    -o "$OUT/rgd_tlv"
"$OUT/rgd_tlv"

printf '%-32s ' rgd_timeout_race_test
# Real RGI handler and timer: route end serialized against an active update (luka-dev e9e1291).
cc -std=gnu99 -O1 -DENABLE_LOGGING=0 -Ihook -Dbus_send_text=bus_send_text_unused \
    -c hook/framework/bus.c -o "$OUT/rgd_race_bus.o"
cc -std=gnu99 -O1 -DENABLE_LOGGING=0 -Ihook tests/rgd_timeout_race_test.c "$OUT/rgd_race_bus.o" \
    hook/routeguidance/rgd_tlv.c hook/framework/iap2_protocol.c hook/framework/signal_guard.c \
    -lpthread -o "$OUT/rgd_timeout_race"
"$OUT/rgd_timeout_race"

printf '%-32s ' inject_generation_test
cc -std=gnu99 -O1 -Wall -Wextra -Werror -Wno-unused-function \
    -DENABLE_LOGGING=0 -Ihook -Ihook/framework tests/inject_generation_test.c -o "$OUT/inject_generation"
"$OUT/inject_generation"

printf '%-32s ' iap2_dispatch_test
cc -std=gnu99 -O1 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable \
    -Wno-unused-but-set-variable -ffunction-sections -fdata-sections -Ihook \
    tests/iap2_dispatch_test.c hook/framework/iap2_protocol.c "$DEAD_STRIP" \
    -lpthread -ldl -o "$OUT/iap2_dispatch"
"$OUT/iap2_dispatch"

printf '%-32s ' state_trace_test
cc -std=gnu99 -O2 -Wall -Wextra -Werror -Ihook \
    -DENABLE_LOGGING=0 -DENABLE_STATE_TRACE=1 \
    tests/state_trace_test.c hook/framework/state_trace.c \
    hook/framework/iap2_protocol.c -o "$OUT/state_trace"
"$OUT/state_trace" && echo OK

printf '%-32s ' signal_guard_test
cc -std=gnu99 -O2 -Wall -Wextra -Werror -pedantic -Ihook \
    tests/signal_guard_test.c hook/framework/signal_guard.c \
    -o "$OUT/signal_guard"
"$OUT/signal_guard"

printf '%-32s ' bus_transport_test
cc -std=gnu99 -O2 -Wall -Wextra -Werror \
    -Wno-unused-variable -Wno-unused-but-set-variable -Ihook \
    tests/bus_transport_test.c hook/framework/signal_guard.c \
    -lpthread -o "$OUT/bus_transport"
"$OUT/bus_transport"

printf '%-32s ' state_trace_logger_test
cc -std=gnu99 -O2 -Wall -Wextra -Werror -Ihook \
    -DENABLE_LOGGING=0 -DENABLE_STATE_TRACE=1 \
    tests/state_trace_logger_test.c hook/framework/logging.c \
    -lpthread -ldl -o "$OUT/state_trace_logger"
"$OUT/state_trace_logger" && echo OK

printf '%-32s ' local_protocols
python3 scripts/check_local_protocols.py

printf '%-32s ' install_dio_test
sh scripts/test_install_dio.sh

printf '%-32s ' logging_mib_test
sh scripts/test_logging_mib.sh
printf '%-32s ' gem_menu_test
sh scripts/test_gem_menu.sh

printf '%-32s ' supervisor_lifecycle_test
sh scripts/test_supervisor_lifecycle.sh

printf '%-32s ' install_listing_test
sh scripts/test_install_listing.sh

printf '%-32s ' install_payload_test
sh scripts/test_install_payload.sh

printf '%-32s ' altscreen_view_test
cc -std=gnu99 -O2 -Wall -Wextra -Werror -pedantic -Ialtscreen_render \
    tests/altscreen_view_test.c altscreen_render/view.c -lm -o "$OUT/altscreen_view"
"$OUT/altscreen_view"

printf '%-32s ' altscreen_policy_test
# Renderer session decisions: bounded hardware starts, yield stamp, hang-only persistence,
# key-frame pacing, frozen picture on a new connection.
mkdir -p "$OUT/altscreen_policy.d"
cc -std=gnu99 -O1 -Wall -Wextra -Werror -pedantic -Ialtscreen_render \
    tests/altscreen_policy_test.c altscreen_render/policy.c -o "$OUT/altscreen_policy"
"$OUT/altscreen_policy" "$OUT/altscreen_policy.d"

printf '%-32s ' alt111_video_test
# CarPlay stream-111 core: a slow renderer skips to the next IDR instead of a reconnect.
cc -std=gnu99 -O1 -Wall -Wextra -Werror -pedantic -Ialtscreen_hook/include \
    tests/alt111_video_test.c altscreen_hook/src/alt111_video.c -o "$OUT/alt111_video"
"$OUT/alt111_video"

printf '%-32s ' alt111_settings_test
# The shipping gen2 URL builder: MMI CarPlay settings, guidance edges (needs no jar).
mkdir -p "$OUT/alt111_settings.d"
cc -std=gnu99 -O1 -Wall -Wextra -Werror -Wno-unused-function \
    -Ialtscreen_hook/include tests/alt111_settings_test.c altscreen_hook/src/alt111_profile.c \
    altscreen_hook/src/alt111_control.c altscreen_hook/src/alt111_video.c \
    altscreen_hook/src/alt111_resync.c -lpthread -ldl -o "$OUT/alt111_settings"
"$OUT/alt111_settings" "$OUT/alt111_settings.d"

printf '%-32s ' alt111_control_test
cc -std=gnu99 -O2 -Wall -Wextra -Werror -pedantic -Ialtscreen_hook/include \
    tests/alt111_control_test.c altscreen_hook/src/alt111_control.c \
    altscreen_hook/src/alt111_profile.c -o "$OUT/alt111_control"
"$OUT/alt111_control"

printf '%-32s ' hw301_bridge_tests
sh scripts/test_hw301.sh

printf '%-32s ' aa_navxlate_test
cc -std=gnu99 -O1 -Wall -Wextra -Werror -Wno-unused-function \
    tests/aa_navxlate_test.c -o "$OUT/aa_navxlate"
"$OUT/aa_navxlate"

printf '%-32s ' aa_trip_test
cc -std=gnu99 -O1 -Wall -Wextra -Werror -pthread -fsanitize=address,undefined \
    tests/aa_trip_test.c -o "$OUT/aa_trip"
"$OUT/aa_trip"

printf '%-32s ' aa_uiconfig_test
# -rdynamic: the hook finds the fake queueOutgoing via dlsym(RTLD_DEFAULT) (Linux needs the export).
cc -rdynamic -std=gnu99 -O1 -Wall -Wextra -Werror -Wno-unused-function \
    "-DAA_VIEW_PATH=\"$OUT/aa_view\"" "-DAA_FILE_INSETS=\"$OUT/aa_insets\"" "-DAA_FILE_DPI=\"$OUT/aa_dpi\"" \
    "-DAA_THEME_PATH=\"$OUT/aa_theme\"" "-DAA_FILE_FPS=\"$OUT/aa_fps\"" \
    tests/aa_uiconfig_test.c -ldl -o "$OUT/aa_uiconfig"
"$OUT/aa_uiconfig"

printf '%-32s ' aa_hook_gate_test
# _GNU_SOURCE: glibc hides dladdr/RTLD_NEXT without it (QNX and macOS do not).
cc -std=gnu99 -O1 -Wall -Wextra -Werror -Wno-unused-function -pthread -D_GNU_SOURCE \
    "-DAA_UNPINNED_PATH=\"$OUT/aa_unpinned\"" tests/aa_hook_gate_test.c -ldl -o "$OUT/aa_hook_gate"
"$OUT/aa_hook_gate"

printf '%-32s ' aa_receiver_profile_test
python3 -I tests/test_aa_receiver_profile.py

printf '%-32s ' maps_profile_test
cc -std=gnu99 -O1 -Wall -Wextra -Werror -DENABLE_LOGGING=0 -Ihook \
    "-DMAPS_PROFILE_OFF_PATH=\"$OUT/maps30.off\"" tests/maps_profile_test.c \
    hook/mapsprofile/maps_profile.c -o "$OUT/maps_profile"
"$OUT/maps_profile"

printf '%-32s ' tel_hook_test
cc -std=gnu99 -O1 -Wall -Wextra -Werror -Wno-unused-function \
    -DENABLE_LOGGING=0 -Ihook tests/tel_hook_test.c hook/telephony/tel_hook.c \
    hook/framework/iap2_protocol.c -o "$OUT/tel_hook"
"$OUT/tel_hook"

printf '%-32s ' vehicle_hook_test
cc -std=gnu99 -O1 -Wall -Wextra -Werror -Wno-unused-function \
    -DENABLE_LOGGING=0 -DVEHICLE_HOOK_TESTING -Ihook tests/vehicle_hook_test.c hook/vehicle/vehicle_hook.c \
    hook/framework/iap2_protocol.c -o "$OUT/vehicle_hook"
"$OUT/vehicle_hook"

printf '%-32s ' nav_boost_test
# The test interposes libc with ld --wrap, which macOS ld64 lacks; it runs on Linux (CI).
if printf 'int main(void){return 0;}\n' | cc -x c - -Wl,--wrap=open -o "$OUT/wrap_probe" 2>/dev/null; then
    cc -std=gnu99 -O1 -Wall -Wextra -Werror -Itests/stubs/qnx -Dmain=nav_boost_main \
        -c navboost/nav_boost.c -o "$OUT/nav_boost.o"
    cc -std=gnu99 -O1 -Wall -Wextra -Werror -Itests/stubs/qnx tests/nav_boost_test.c "$OUT/nav_boost.o" \
        -Wl,--wrap=open,--wrap=close,--wrap=opendir,--wrap=readdir,--wrap=closedir,--wrap=write \
        -o "$OUT/nav_boost"
    "$OUT/nav_boost"
else
    echo "SKIP: linker has no --wrap"
fi

printf '%-32s ' aa_tee_test
cc -std=gnu99 -O1 -Wall -Wextra -Werror -Iaa_hook \
    tests/aa_tee_test.c aa_hook/aa_tee.c -lpthread -o "$OUT/aa_tee"
"$OUT/aa_tee"

printf '%-32s ' aa_yield_test
cc -std=gnu99 -O1 -Wall -Wextra -Werror -Iaa_hook \
    "-DAA_HW_YIELD_PATH=\"$OUT/hw_yield\"" "-DAA_HW_ACTIVE_PATH=\"$OUT/hw_active\"" \
    -DAA_HW_YIELD_WAIT_MS=400u tests/aa_yield_test.c aa_hook/aa_yield.c -lpthread -o "$OUT/aa_yield"
"$OUT/aa_yield"

printf '%-32s ' aa_focus_test
# Opt-in video focus of the Android Auto cluster sink: pause while hidden, key-frame refocus.
mkdir -p "$OUT/aa_focus.d"
cc -std=gnu99 -O1 -Wall -Wextra -Werror -Iaa_hook tests/aa_focus_test.c -o "$OUT/aa_focus"
"$OUT/aa_focus" "$OUT/aa_focus.d"

printf '%-32s ' altscreen_nal_fuzz
# Annex-B scanners on every phone byte: the hardware decoder's SPS/PPS split and the
# Android Auto tee's key-frame gate (structured + random, exact-size buffers).
cc -std=gnu99 -O1 -Wall -Wextra -Werror -Ialtscreen_render -Iaa_hook \
    tests/altscreen_nal_fuzz.c aa_hook/aa_tee.c -lpthread -o "$OUT/altscreen_nal_fuzz"
"$OUT/altscreen_nal_fuzz"

printf '%-32s ' hw_yield_files
# The main-screen yield: both hooks write the files altscreen_render reads.
for f in yield active; do
    F=$(echo "$f" | tr a-z A-Z)
    aa=$(sed -n "s/^#define AA_HW_${F}_PATH *\"\([^\"]*\)\".*/\1/p" aa_hook/aa_hook.h)
    cp=$(sed -n "s/^#define ALTR_HW_${F}_PATH *\"\([^\"]*\)\".*/\1/p" altscreen_hook/libaltscreen111_gen2.c)
    rd=$(sed -n "s/^#define ALTR_HW_${F}_FILE *\"\([^\"]*\)\".*/\1/p" altscreen_render/main.c)
    [ -n "$aa" ] && [ "$aa" = "$cp" ] && [ "$aa" = "$rd" ] \
        || { echo "FAIL: hw_$f file: AA hook '$aa', CarPlay hook '$cp', renderer '$rd'"; exit 1; }
done
echo "hw_yield / hw_active: both hooks and the renderer agree"

printf '%-32s ' pause_hidden_marker
# One opt-in marker for both hooks; Java writes the Android Auto record the gal hook reads.
cp_m=$(sed -n 's/.*g_pause_hidden_marker = "\([^"]*\)".*/\1/p' altscreen_hook/libaltscreen111_gen2.c)
aa_m=$(sed -n 's/.*AA_MARKER_DIR "\(cluster_video[^"]*\)".*/\1/p' aa_hook/aa_hook.h)
aa_h=$(sed -n 's/^#define AA_HIDDEN_PATH *"\([^"]*\)".*/\1/p' aa_hook/aa_hook.h)
jv_h=$(sed -n 's/.*HIDDEN_PATH = "\([^"]*\)".*/\1/p' java_patch/com/luka/carplay/aa/AaClusterView.java)
[ -n "$cp_m" ] && [ "$cp_m" = "/mnt/app/root/$aa_m" ] && [ -n "$aa_h" ] && [ "$aa_h" = "$jv_h" ] \
    || { echo "FAIL: pause marker CarPlay '$cp_m' AA '$aa_m', hidden record hook '$aa_h' Java '$jv_h'"; exit 1; }
echo "pause marker $cp_m and hidden record $aa_h agree"

printf '%-32s ' frame_rate_file
# One file for the MMI setting, the CarPlay hook and the Android Auto hook.
cp_f=$(sed -n 's/.*g_fps_override_path = "\([^"]*\)".*/\1/p' altscreen_hook/libaltscreen111_gen2.c)
aa_f=$(sed -n 's/^#define AA_FILE_FPS *AA_MARKER_DIR "\([^"]*\)".*/\1/p' aa_hook/aa_hook.h)
grep -q "\"root/$aa_f\"" java_patch/com/luka/carplay/connectivity/CarPlaySettings.java \
    && [ -n "$aa_f" ] && [ "$cp_f" = "/mnt/app/root/$aa_f" ] \
    || { echo "FAIL: frame rate file CarPlay '$cp_f', Android Auto '$aa_f', or not the MMI setting's"; exit 1; }
echo "frame rate file $cp_f: both hooks and the MMI setting agree"

printf '%-32s ' disconnect_freeze_contract
# altscreen_render keeps window 99's last picture after a session ends until Java has
# switched the cockpit back to the stock context: longer than Java's worst case.
J=java_patch/com/luka/carplay/core/AltScreenModule.java
freeze=$(sed -n 's/^#define ALTR_DISCONNECT_FREEZE_MS *\([0-9]*\)u.*/\1/p' altscreen_render/main.c)
poll=$(sed -n 's/.*long POLL_MS = \([0-9]*\)L;.*/\1/p' "$J")
miss=$(sed -n 's/.*int MISS_DEBOUNCE = \([0-9]*\);.*/\1/p' "$J")
[ -n "$freeze" ] && [ -n "$poll" ] && [ -n "$miss" ] \
    && [ "$freeze" -gt $(( poll * (miss + 1) + 250 )) ] \
    || { echo "FAIL: freeze ${freeze:-?} ms vs Java ${poll:-?} ms x (${miss:-?}+1) + 250 ms switch"; exit 1; }
echo "last picture held ${freeze} ms > Java's $(( poll * (miss + 1) )) ms to restore the stock map"

printf '%-32s ' aa_tee_port
# The gal hook's tee and altscreen_render's Android Auto source must agree.
hook_port=$(sed -n 's/^#define AA_TEE_PORT_DEFAULT *\([0-9]*\).*/\1/p' aa_hook/aa_hook.h)
render_port=$(sed -n 's/.*env_i("ALTR_AA_TEE_PORT", \([0-9]*\)).*/\1/p' altscreen_render/main.c | sort -u)
[ -n "$hook_port" ] && [ "$hook_port" = "$render_port" ] \
    || { echo "FAIL: hook tee port '$hook_port' vs renderer '$render_port'"; exit 1; }
echo "Android Auto tee port $hook_port: hook and renderer agree"

printf '%-32s ' aa_startup_test
sh scripts/test_aa_startup.sh

printf '%-32s ' rgi_persistence_contract
# RouteManager stores DSIResponseContainer.isRgActive() at standby: phone RGI must use
# ClusterService.setCarPlayRgiActive(), never forge the navigation-wide flag (stale route resume).
B=java_patch/com/luka/carplay/rgd/BAPBridge.java
C=java_patch/de/audi/tghu/navi/app/cluster/ClusterService.java
if grep -nE '^[^*/]*\.setRgActive\(' "$B"; then echo "FAIL: BAPBridge forges DSIResponseContainer.setRgActive"; exit 1; fi
grep -q 'csRef.setCarPlayRgiActive(active)' "$B" && grep -q 'this.carplayRgiActive || this.env.getContainer().isRgActive()' "$C" \
    || { echo "FAIL: CarPlay RGI flag not wired through ClusterService"; exit 1; }
echo "phone RGI flag stays in ClusterService (no persisted rgActive forge)"
