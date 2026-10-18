#!/usr/bin/env bash
# Build the WORKING "map on Virtual Cockpit" image for the HW301 dongle (v1.29):
#   - inject a 2nd (cluster) CarPlay stream into the SetupScreen (cluster_inject.c)
#   - NOP the type-gate in CPAAProxyEx so both streams (110 main + 111 cluster) set up
#   - mirror video frames to both head-unit sockets (dual-send), keeping the session alive
# Output: work/out/hw301_v1129.tar   Flash on BENCH power with flash_local.py.
set -euo pipefail
cd "$(dirname "$0")/../.."
WRAP_LIB=libCoreUtils.so CHECK_SYM=CFArrayEnsureCreatedAndAppend CLUSTER_SRC=cluster_inject.c \
  PATCH_CPAA="0x4c9a8:00 0x4c9a9:f0 0x4c9aa:20 0x4c9ab:e3" ./work/tools/build.sh 129
