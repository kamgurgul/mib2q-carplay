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

printf '%-32s ' inject_generation_test
cc -std=gnu99 -O1 -Wall -Wextra -Werror -Wno-unused-function \
    -DENABLE_LOGGING=0 -Ihook -Ihook/framework tests/inject_generation_test.c -o "$OUT/inject_generation"
"$OUT/inject_generation"

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
    tests/altscreen_view_test.c altscreen_render/view.c -o "$OUT/altscreen_view"
"$OUT/altscreen_view"

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
    "-DAA_THEME_PATH=\"$OUT/aa_theme\"" \
    tests/aa_uiconfig_test.c -ldl -o "$OUT/aa_uiconfig"
"$OUT/aa_uiconfig"

printf '%-32s ' aa_hook_gate_test
# _GNU_SOURCE: glibc hides dladdr/RTLD_NEXT without it (QNX and macOS do not).
cc -std=gnu99 -O1 -Wall -Wextra -Werror -Wno-unused-function -pthread -D_GNU_SOURCE \
    "-DAA_UNPINNED_PATH=\"$OUT/aa_unpinned\"" tests/aa_hook_gate_test.c -ldl -o "$OUT/aa_hook_gate"
"$OUT/aa_hook_gate"

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

printf '%-32s ' aa_tee_port
# The gal hook's tee and altscreen_render's Android Auto source must agree.
hook_port=$(sed -n 's/^#define AA_TEE_PORT_DEFAULT *\([0-9]*\).*/\1/p' aa_hook/aa_hook.h)
render_port=$(sed -n 's/.*env_i("ALTR_AA_TEE_PORT", \([0-9]*\)).*/\1/p' altscreen_render/main.c | sort -u)
[ -n "$hook_port" ] && [ "$hook_port" = "$render_port" ] \
    || { echo "FAIL: hook tee port '$hook_port' vs renderer '$render_port'"; exit 1; }
echo "Android Auto tee port $hook_port: hook and renderer agree"

printf '%-32s ' aa_startup_test
sh scripts/test_aa_startup.sh
