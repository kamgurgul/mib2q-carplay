/*
 * Maps 30 FPS profile through Identify (see maps_profile.h).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "maps_profile.h"

#include <string.h>
#include <unistd.h>

#include "../framework/common.h"
#include "../framework/iap2_protocol.h"
#include "../framework/logging.h"

DEFINE_LOG_MODULE(MAPS30);

#define MAPS_PARAM_PPID     34u
#define MAPS_IDENTIFY_MAX   8192u

#ifndef MAPS_PROFILE_OFF_PATH
#define MAPS_PROFILE_OFF_PATH MAPS_PROFILE_OFF_MARKER
#endif

size_t maps_profile_identify(hook_context_t* ctx, uint8_t* buf, size_t len, size_t max_len) {
    static const char ppid[] = MAPS_PROFILE_PPID;
    const size_t extra = 4u + sizeof(ppid);          /* length, id, 16 chars + NUL */
    size_t offset, ppid_count = 0, new_len;

    if (access(MAPS_PROFILE_OFF_PATH, F_OK) == 0) return len;
    /* Check the whole top-level parameter list before changing a byte. A nested
     * component parameter numbered 34 is not the accessory PPID. */
    if (!buf || len < IAP2_MIN_FRAME || len > max_len || len > MAPS_IDENTIFY_MAX ||
        buf[0] != IAP2_SYNC1 || buf[1] != IAP2_SYNC2 || read_be16(buf + 2) != len ||
        read_be16(buf + 4) != IAP2_MSG_IDENTIFY)
        return len;
    for (offset = IAP2_MIN_FRAME; offset < len; ) {
        uint16_t size;
        if (len - offset < 4u) {
            LOG_WARN(LOG_MODULE, "Identify: truncated parameter; Maps30 PPID skipped");
            return len;
        }
        size = read_be16(buf + offset);
        if (size < 4u || size > len - offset) {
            LOG_WARN(LOG_MODULE, "Identify: invalid parameter; Maps30 PPID skipped");
            return len;
        }
        if (read_be16(buf + offset + 2u) == MAPS_PARAM_PPID) ++ppid_count;
        offset += size;
    }
    if (ppid_count) {
        LOG_WARN(LOG_MODULE, "Identify: keeping %zu existing PPID parameter(s); Maps30 PPID not added", ppid_count);
        return len;
    }
    new_len = len + extra;
    if (new_len > MAPS_IDENTIFY_MAX) {
        LOG_WARN(LOG_MODULE, "Identify: no room for the Maps30 PPID");
        return len;
    }
    if (new_len > max_len) {
        size_t cap = 0;
        uint8_t* grown = hook_identify_grow(ctx, len, new_len, &cap);
        if (!grown || cap < new_len) {
            LOG_WARN(LOG_MODULE, "Identify: buffer growth failed; Maps30 PPID skipped");
            return len;
        }
        buf = grown;
    }
    write_be16(buf + len, (uint16_t)extra);
    write_be16(buf + len + 2u, (uint16_t)MAPS_PARAM_PPID);
    memcpy(buf + len + 4u, ppid, sizeof(ppid));
    write_be16(buf + 2, (uint16_t)new_len);
    /* The Encode dispatcher stores the returned length in the NmeArray. */
    LOG_WARN(LOG_MODULE, "Identify: +PPID %s (Maps target 30 FPS; %zu -> %zu bytes)", ppid, len, new_len);
    return new_len;
}

const hook_module_def_t maps_profile_module_def = {
    .name = "maps30",
    .priority = HOOK_PRIORITY_LOW,
    .on_identify = maps_profile_identify,
};
