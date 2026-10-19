#!/usr/bin/env bash
# Build the HW301 dual-screen (Virtual Cockpit) images from cluster_dual.c.
# No dongle shell needed: the step is baked in at compile time, one image each.
#
#   build_dual.sh [step]      step = 1 | 2 | 3 | all   (default: all)
#
#   step 1 -> hw301_v1132.tar  advertise cluster display + log SETUP/keys only
#   step 2 -> hw301_v1133.tar  + open 111 listener, append dataPort, decrypt, count
#   step 3 -> hw301_v1134.tar  + inject 111 to the head unit and relay (full)
#
# Flash ONE at a time on BENCH power, car-test, getlogs, then the next step:
#   ./work/.venv/bin/python work/tools/flash_local.py work/out/hw301_v<code>.tar --version <code>
# Recover any time by flashing stock fw/hw301_v129.tar --version 129.
set -euo pipefail
cd "$(dirname "$0")/../.."

build_step() {
  local step="$1" code="$2" advert="${3:-3}" extra="${4:-}"
  echo "######## building dual-screen STEP $step ADVERT=$advert ${extra:-} -> v$code ########"
  WRAP_LIB=libCoreUtils.so \
  CHECK_SYM=AirPlayReceiverSessionSetup \
  CLUSTER_SRC=cluster_dual.c \
  CLUSTER_CFLAGS="-DCLUSTER_STEP=$step -DCLUSTER_ADVERT=$advert ${extra}" \
    ./work/tools/build.sh 129 "$code"
}

# Diagnostic step-1 variants (2026-10-19): iOS ends the session right after our
# modified /info. CLUSTER_ADVERT isolates the cause. Flash one per car trip.
#   advert0 (v1140) stock /info, logged only  -> MUST keep CarPlay up (control)
#   advert1 (v1141) merge enabledFeatures only
#   advert2 (v1142) append the cluster display only
case "${1:-all}" in
  1) build_step 1 1132 ;;
  2) build_step 2 1133 ;;
  3) build_step 3 1134 ;;
  advert0) build_step 1 1140 0 ;;
  advert1) build_step 1 1141 1 ;;
  advert2) build_step 1 1142 2 ;;
  # v1143: both edits, display now identical to the head-unit hook's proven one
  # (viewAreas always, no displayUUID) after v1142 showed the display is rejected.
  # v1145: + altScreen/viewAreas in every SETUP response (iOS validates the
  # display against the receiver-accepted features; iPhone log, v1144)
  hu1) build_step 1 1145 3 ;;
  # v1146: step 2a transport - open the 111 listener, return its dataPort, count
  # frames (no decrypt: the wireless screen is ChaCha; STEP_DECRYPT stays 0).
  step2a) build_step 2 1147 3 ;;   # v1147 = v1146 + NULL guard + 10 s frame stats
  # v1148: Phase B. Same listener as step 2a, plus ChaCha open of VIDEO_FRAME
  # (Output key). nal_ok stays 0 -> rebuild with step2b-input (Input key).
  step2b) build_step 2 1148 3 "-DSTEP_DECRYPT=1" ;;
  step2b-input) build_step 2 1149 3 "-DSTEP_DECRYPT=1 -DCLUSTER_KEY_INPUT=1" ;;
  # v1150: Phase C. Step 3 (inject 111 to the head unit + relay) WITH decrypt.
  # The plain "3" target (v1134) has STEP_DECRYPT=0 and therefore never relays.
  # Use step3-input only if Phase B needed the Input key (v1149).
  step3) build_step 3 1150 3 "-DSTEP_DECRYPT=1" ;;
  step3-input) build_step 3 1151 3 "-DSTEP_DECRYPT=1 -DCLUSTER_KEY_INPUT=1" ;;
  advert)
    build_step 1 1140 0
    build_step 1 1141 1
    build_step 1 1142 2
    echo "######## advert diagnostics v1140/v1141/v1142 in work/out/ ########"
    ;;
  all)
    build_step 1 1132
    build_step 2 1133
    build_step 3 1134
    echo "######## all three images in work/out/ ########"
    ;;
  *) echo "usage: $0 [1|2|3|all|advert|advert0|advert1|advert2|hu1|step2a|step2b|step2b-input|step3|step3-input]"; exit 1 ;;
esac
