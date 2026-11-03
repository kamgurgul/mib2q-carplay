/*
 * Maps 30 FPS profile: the outgoing iAP2 IdentificationInfo (0x1D01) gets the accessory
 * PPID bf889dfdaea54096 when it has no top-level PPID. In CarExperienceAssets v40 that row
 * holds only features.userInfo.mapsRefreshRate=30, so Maps renders the cluster map at a
 * target of 30 FPS (measured upstream on a native iOS 26.6 vphone: 21 -> 29.7 pictures/s).
 * It selects a phone-side policy; the car's actual cadence is not guaranteed.
 * Off: /mnt/app/root/carplay_maps30.off (read at each Identify).
 *
 * Adapted from luka-dev/mib2q-carplay-rgi hook/altscreen/maps_profile.h (MHI2Q-2026-10-29).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef MAPS_PROFILE_H
#define MAPS_PROFILE_H

#include <stddef.h>
#include <stdint.h>
#include "../framework/hook_framework.h"

extern const hook_module_def_t maps_profile_module_def;

#define MAPS_PROFILE_PPID "bf889dfdaea54096"
#define MAPS_PROFILE_OFF_MARKER "/mnt/app/root/carplay_maps30.off"

/* Identify patcher, exported for the host test. Returns the new length (len = unchanged). */
size_t maps_profile_identify(hook_context_t* ctx, uint8_t* buf, size_t len, size_t max_len);

#endif
