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
typedef int (*altscreen_view_area_fn)(int view);
typedef int (*altscreen_set_rgi_fn)(int on);
typedef int (*altscreen_set_guidance_chrome_fn)(int on);

static altscreen_map_zoom_fn g_map_zoom;
static altscreen_view_area_fn g_view_area;
static altscreen_set_rgi_fn g_set_rgi;
static altscreen_set_guidance_chrome_fn g_set_guidance_chrome;
static int g_resolved;
static int g_missing_logged;

/* Only the bus reader thread runs the handlers, so the lazy resolve needs no lock. */
static void resolve(void) {
    if (g_resolved) return;
    g_map_zoom = (altscreen_map_zoom_fn)dlsym(RTLD_DEFAULT, "altscreen111_map_zoom");
    g_view_area = (altscreen_view_area_fn)dlsym(RTLD_DEFAULT, "altscreen111_view_area");
    g_set_rgi = (altscreen_set_rgi_fn)dlsym(RTLD_DEFAULT, "altscreen111_set_rgi");
    g_set_guidance_chrome = (altscreen_set_guidance_chrome_fn)dlsym(
        RTLD_DEFAULT, "altscreen111_set_guidance_chrome");
    g_resolved = 1;
}

static void on_alt_zoom(uint16_t type, uint8_t flags, const uint8_t* payload,
                        uint32_t len, void* ctx) {
    int steps;
    (void)type; (void)flags; (void)ctx;
    if (!payload || len < 1) return;
    steps = (int)(int8_t)payload[0];
    if (!steps) return;

    resolve();
    if (!g_map_zoom) {
        if (!g_missing_logged) {
            g_missing_logged = 1;
            LOG_WARN(LOG_MODULE, "CMD_ALT_ZOOM steps=%d dropped: AltScreen hook not loaded", steps);
        }
        return;
    }
    LOG_INFO(LOG_MODULE, "CMD_ALT_ZOOM steps=%d rc=%d", steps, g_map_zoom(steps));
}

/* CMD_ALT_ZONE [u8 mode 0=full/1=sport/2=classic][u16 LE ms]: the VC layout.
 * full = wide map -> ViewArea 0; classic small map window -> ViewArea 1;
 * Sport skin small map (shifted left of the rev counter) -> ViewArea 2. */
static void on_alt_zone(uint16_t type, uint8_t flags, const uint8_t* payload,
                        uint32_t len, void* ctx) {
    int view;
    (void)type; (void)flags; (void)ctx;
    if (!payload || len < 1) return;
    view = payload[0] == 0 ? 0 : (payload[0] == 1 ? 2 : 1);
    resolve();
    if (!g_view_area) {
        if (!g_missing_logged) {
            g_missing_logged = 1;
            LOG_WARN(LOG_MODULE, "CMD_ALT_ZONE mode=%u dropped: AltScreen hook not loaded", payload[0]);
        }
        return;
    }
    LOG_INFO(LOG_MODULE, "CMD_ALT_ZONE mode=%u -> view area %d rc=%d", payload[0], view, g_view_area(view));
}

/* CMD_ALT_RGI [u8 0=off 1=on]: Java's confirmed route-guidance presentation. The AltScreen
 * hook picks the cluster URL from it: bare map while our BAP/KDK path draws the guidance,
 * map + the app's own cards otherwise (Waze, or no route). */
static void on_alt_rgi(uint16_t type, uint8_t flags, const uint8_t* payload,
                       uint32_t len, void* ctx) {
    (void)type; (void)flags; (void)ctx;
    if (!payload || len < 1) return;
    resolve();
    if (!g_set_rgi) return;   /* base install: no AltScreen, nothing to switch */
    LOG_INFO(LOG_MODULE, "CMD_ALT_RGI %u rc=%d", payload[0], g_set_rgi(payload[0] ? 1 : 0));
}

static void on_alt_guidance_chrome(uint16_t type, uint8_t flags, const uint8_t* payload,
                                   uint32_t len, void* ctx) {
    (void)type; (void)flags; (void)ctx;
    if (!payload || len < 1) return;
    resolve();
    if (!g_set_guidance_chrome) return;
    if (payload[0] > 2) return;
    LOG_INFO(LOG_MODULE, "CMD_ALT_GUIDANCE_CHROME %u rc=%d", payload[0],
             g_set_guidance_chrome((int)payload[0]));
}

static void altzoom_init(void) {
    bus_on(CMD_ALT_ZOOM, on_alt_zoom, NULL);
    bus_on(CMD_ALT_ZONE, on_alt_zone, NULL);
    bus_on(CMD_ALT_RGI, on_alt_rgi, NULL);
    bus_on(CMD_ALT_GUIDANCE_CHROME, on_alt_guidance_chrome, NULL);
}

static void altzoom_shutdown(void) {
    bus_off(CMD_ALT_ZOOM);
    bus_off(CMD_ALT_ZONE);
    bus_off(CMD_ALT_RGI);
    bus_off(CMD_ALT_GUIDANCE_CHROME);
}

const hook_module_def_t altzoom_module_def = {
    .name = "altzoom",
    .priority = HOOK_PRIORITY_LOW,
    .on_init = altzoom_init,
    .on_shutdown = altzoom_shutdown
};
