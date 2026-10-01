/*
 * AltScreenModule — bridges altscreen_render's liveness signal to the cluster
 * context switcher.
 *
 * altscreen_render (the CarPlay cluster VIDEO renderer, displayable 99) rewrites
 * /tmp/altscreen_render.live (pid + mtime) while frames are reaching the screen
 * and removes it when the tee closes or video stalls. A leftover file from a
 * crashed writer goes stale. This module polls that file and tells
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
import java.io.FileInputStream;

public final class AltScreenModule implements Module {

    private static final String TAG = "AltScreen";

    /* Must match altscreen_render's ALTR_LIVE_FILE default. Overridable for tests. */
    private static final String LIVE_FILE =
        System.getProperty("carplay.altscreen.liveFile", "/tmp/altscreen_render.live");
    private static final long POLL_MS = 250L;
    /* Renderer rewrites the file at least every 200 ms while a frame is on screen.
     * Older than this means the writer is gone even if the file was never unlinked. */
    private static final long HEARTBEAT_MS = 1000L;
    /* Require the file to be missing for two polls before declaring video gone,
     * so a single dropped frame between the renderer's unlink/rewrite does not
     * flap the cluster context. */
    private static final int MISS_DEBOUNCE = 2;

    private final File liveFile = new File(LIVE_FILE);
    private Thread worker;
    private int generation;
    private boolean stopRequested;

    public String name() { return "altscreen"; }

    public boolean start(FrameworkRef fw) {
        if (fw == null || !fw.isReady()) return false;
        /* Only meaningful on a cluster that supports our CarPlay contexts. */
        if (!ScreenModule.isPlatformSupported(fw)) {
            Log.w(TAG, "disabled: platform has no CarPlay cluster contexts");
            return true;
        }
        Thread previous = null;
        final int gen;
        synchronized (this) {
            /* Reuse only when stop() has not retired this thread. Bumping generation
             * here would make the still-sleeping poller exit and leave nothing in its place
             * (stop+start is back-to-back on every activation). */
            if (worker != null && worker.isAlive() && !stopRequested) return true;
            stopRequested = false;
            gen = ++generation;
            if (worker != null && worker.isAlive()) {
                worker.interrupt();
                previous = worker;
            }
        }
        if (previous != null) {
            try { previous.join(1000); }
            catch (InterruptedException e) { /* caller is the lifecycle thread */ }
        }
        synchronized (this) {
            if (generation != gen || stopRequested) return true;
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
            generation++;
            stopRequested = true;
            if (worker != null) worker.interrupt();
        }
        /* Releasing our claim on the video context is idempotent. */
        ScreenModule.setAltScreenActive(false);
    }

    private void pollLoop(int gen) {
        int miss = MISS_DEBOUNCE;     /* start assuming no video */
        while (true) {
            synchronized (this) {
                if (gen != generation) return;
            }
            boolean live = videoLive();
            /* Compare against ScreenModule, not a private latch: start() clears
             * altScreenActive, and a reused poller must publish that edge again. */
            boolean published = ScreenModule.isAltScreenActive();

            if (live) {
                miss = 0;
                if (!published) ScreenModule.setAltScreenActive(true);
            } else if (miss < MISS_DEBOUNCE) {
                miss++;
            } else if (published) {
                ScreenModule.setAltScreenActive(false);
            }

            try { Thread.sleep(POLL_MS); }
            catch (InterruptedException e) { return; }
        }
    }

    /* File exists, was rewritten recently, and its pid is still a live process. */
    private boolean videoLive() {
        try {
            if (!liveFile.isFile()) return false;
            long modified = liveFile.lastModified();
            long age = System.currentTimeMillis() - modified;
            if (modified <= 0L || age < 0L || age > HEARTBEAT_MS) return false;
            int pid = readPid(liveFile);
            if (pid <= 0) return false;
            File proc = new File("/proc/" + pid);
            if (proc.isDirectory()) return true;
            /* No process list at all: mtime is the only signal this unit can give. */
            return !new File("/proc").isDirectory();
        } catch (Throwable t) {
            return false;
        }
    }

    private static int readPid(File f) {
        FileInputStream in = null;
        try {
            in = new FileInputStream(f);
            byte[] buf = new byte[64];
            int n = in.read(buf);
            if (n <= 0) return -1;
            String s = new String(buf, 0, n);
            int i = s.indexOf("pid=");
            if (i < 0) return -1;
            i += 4;
            int end = i;
            while (end < s.length()) {
                char c = s.charAt(end);
                if (c < '0' || c > '9') break;
                end++;
            }
            if (end == i) return -1;
            return Integer.parseInt(s.substring(i, end));
        } catch (Throwable t) {
            return -1;
        } finally {
            if (in != null) {
                try { in.close(); } catch (Throwable t) { /* best-effort */ }
            }
        }
    }
}
