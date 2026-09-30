/*
 * AltScreenModule — bridges altscreen_render's liveness signal to the cluster
 * context switcher.
 *
 * altscreen_render (the CarPlay cluster VIDEO renderer, displayable 99) writes
 * /tmp/altscreen_render.live while it is drawing fresh decoded frames and removes
 * it when the tee closes or video stalls. This module polls that file and tells
 * ScreenModule, which is the single cluster-context writer:
 *
 *   live   -> ScreenModule.setAltScreenActive(true)  -> ctx 81 (video) / 82 (video+maneuver)
 *   gone   -> ScreenModule.setAltScreenActive(false) -> ctx 74 (stock) / 80 (stock map+maneuver)
 *
 * Poll (not push) keeps this decoupled from the native renderer's lifecycle: a
 * crashed/replaced renderer simply stops refreshing the file and the cluster
 * falls back within one debounce window. No socket, no blocking.
 *
 * Order in CarPlayApp.MODULES matters: AltScreenModule must start AFTER
 * ScreenModule (it feeds it) — see CarPlayApp.MODULES.
 *
 * Java 1.4 / Foundation 1.1 (no generics, no autoboxing).
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.luka.carplay.core;

import com.luka.carplay.framework.Log;

import java.io.File;

public final class AltScreenModule implements Module {

    private static final String TAG = "AltScreen";

    /* Must match altscreen_render's ALTR_LIVE_FILE default. Overridable for tests. */
    private static final String LIVE_FILE =
        System.getProperty("carplay.altscreen.liveFile", "/tmp/altscreen_render.live");
    private static final long POLL_MS = 250L;
    /* Require the file to be missing for two polls before declaring video gone,
     * so a single dropped frame between the renderer's unlink/rewrite does not
     * flap the cluster context. */
    private static final int MISS_DEBOUNCE = 2;

    private final File liveFile = new File(LIVE_FILE);
    private Thread worker;
    private int generation;

    public String name() { return "altscreen"; }

    public boolean start(FrameworkRef fw) {
        if (fw == null || !fw.isReady()) return false;
        /* Only meaningful on a cluster that supports our CarPlay contexts. */
        if (!ScreenModule.isPlatformSupported(fw)) {
            Log.w(TAG, "disabled: platform has no CarPlay cluster contexts");
            return true;
        }
        final int gen;
        synchronized (this) {
            gen = ++generation;
            if (worker != null && worker.isAlive()) return true;
            Thread t = new Thread(new Runnable() {
                public void run() { pollLoop(gen); }
            }, "carplay-altscreen-poll");
            t.setDaemon(true);
            worker = t;
            t.start();
        }
        Log.i(TAG, "ready (watching " + LIVE_FILE + ")");
        return true;
    }

    public void stop() {
        synchronized (this) {
            generation++;             /* wake/retire the current poll loop */
        }
        /* Releasing our claim on the video context is idempotent. */
        ScreenModule.setAltScreenActive(false);
    }

    private void pollLoop(int gen) {
        int miss = MISS_DEBOUNCE;     /* start assuming no video */
        boolean published = false;
        while (true) {
            synchronized (this) {
                if (gen != generation) return;
            }
            boolean live;
            try { live = liveFile.exists(); }
            catch (Throwable t) { live = false; }

            if (live) {
                miss = 0;
                if (!published) { ScreenModule.setAltScreenActive(true); published = true; }
            } else if (miss < MISS_DEBOUNCE) {
                miss++;
            } else if (published) {
                ScreenModule.setAltScreenActive(false);
                published = false;
            }

            try { Thread.sleep(POLL_MS); }
            catch (InterruptedException e) { Thread.currentThread().interrupt(); return; }
        }
    }
}
