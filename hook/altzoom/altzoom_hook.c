/*
 * AltScreen map zoom bridge - see altzoom_hook.h.
 *
 * The bus dispatches with its handler lock held, so the handler only resolves
 * the AltScreen entry point once and calls it; that entry point merely queues
 * the step under its own lock. Without the AltScreen hook (base install, or
 * ALTSCREEN=0) the symbol is absent and the step is dropped with one log line.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <dlfcn.h>
#include <stdint.h>

#include "../framework/common.h"
#include "../framework/logging.h"
#include "../framework/bus.h"
#include "../framework/bus_protocol.h"
#include "altzoom_hook.h"

DEFINE_LOG_MODULE(ALTZOOM);

typedef int (*altscreen_map_zoom_fn)(int mapscale_steps);

static altscreen_map_zoom_fn g_map_zoom;
static int g_resolved;
static int g_missing_logged;

static void on_alt_zoom(uint16_t type, uint8_t flags, const uint8_t* payload,
                        uint32_t len, void* ctx) {
    int steps;
    (void)type; (void)flags; (void)ctx;
    if (!payload || len < 1) return;
    steps = (int)(int8_t)payload[0];
    if (!steps) return;

    /* Only the bus reader thread runs this, so the lazy resolve needs no lock. */
    if (!g_resolved) {
        g_map_zoom = (altscreen_map_zoom_fn)dlsym(RTLD_DEFAULT, "altscreen111_map_zoom");
        g_resolved = 1;
    }
    if (!g_map_zoom) {
        if (!g_missing_logged) {
            g_missing_logged = 1;
            LOG_WARN(LOG_MODULE, "CMD_ALT_ZOOM steps=%d dropped: AltScreen hook not loaded", steps);
        }
        return;
    }
    LOG_INFO(LOG_MODULE, "CMD_ALT_ZOOM steps=%d rc=%d", steps, g_map_zoom(steps));
}

static void altzoom_init(void) {
    bus_on(CMD_ALT_ZOOM, on_alt_zoom, NULL);
}

static void altzoom_shutdown(void) {
    bus_off(CMD_ALT_ZOOM);
}

const hook_module_def_t altzoom_module_def = {
    .name = "altzoom",
    .priority = HOOK_PRIORITY_LOW,
    .on_init = altzoom_init,
    .on_shutdown = altzoom_shutdown
};
