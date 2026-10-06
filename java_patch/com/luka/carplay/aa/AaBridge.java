/*
 * AaBridge — Android Auto phone -> the shared cluster/HUD stack.
 *
 * Adapted from AaLukaBridge in wasimlhr/mib2q-android-auto-cluster (GPL-3.0-or-later).
 *
 * This class writes NO BAP.  It only
 *   1. drives the module lifecycle: Android Auto device ACTIVATING/ACTIVE ->
 *      CarPlayApp.onActivateAndroidAuto (ScreenModule, AltScreenModule, RgdModule);
 *      device gone -> CarPlayApp.onDeactivateAndroidAuto;
 *   2. turns Android Auto's next-turn/distance DSI events into the same EVT_RGD_UPDATE text
 *      frame the CarPlay C hook produces for an iPhone (AaRgState) and hands it to
 *      RouteGuidance in-process through CarplayBus.injectLocal (sticky, so a RouteGuidance
 *      that starts later still gets the latest frame).  BAPBridge stays the single BAP writer;
 *   3. adds the lanes of the current step, which the gal hook writes to /tmp/aa_lanes
 *      (AaLaneFeed), while a route is active.
 *
 * Hook points: the rebuilt AndroidAuto2ListenerDistributor constructor calls attach(); the
 * rebuilt AndroidAuto2NavHandler constructor calls subscribeNavigation().  The lifecycle signal
 * is the terminal-mode IDeviceManager active-device callback (the same listener type the
 * CarPlay hook uses inside TerminalModeBapCombi), registered from attach().
 *
 * Kill switch: /mnt/app/carplay_aa.disabled (or /tmp/carplay_aa.disabled), read at attach and
 * at subscription: Android Auto then runs completely stock.
 *
 * Safety: every entry point catches Throwable; BAP/cluster work runs on the module threads or
 * on this bridge's own workers, never on the Android Auto DSI dispatcher.
 *
 * Java 1.4 / Foundation 1.1.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.luka.carplay.aa;

import com.luka.carplay.bus.CarplayBus;
import com.luka.carplay.core.CarPlayApp;
import com.luka.carplay.framework.Log;

import de.audi.app.terminalmode.IContext;
import de.audi.app.terminalmode.device.IActiveDeviceStateListener;
import de.audi.app.terminalmode.device.IDeviceManager;
import de.audi.app.terminalmode.device.TMDevice;
import de.audi.app.terminalmode.dsi.androidauto2.DSIAndroidAuto2DefaultListener;
import de.audi.app.terminalmode.smartphone.androidauto2.AndroidAuto2ListenerDistributor;

import java.io.File;

import org.dsi.ifc.androidauto2.DSIAndroidAuto2;

public final class AaBridge extends DSIAndroidAuto2DefaultListener implements IActiveDeviceStateListener {
    private static final String TAG = "AA";
    private static final String[] DISABLE_MARKERS = {
        "/mnt/app/carplay_aa.disabled", "/tmp/carplay_aa.disabled"
    };
    /* Safety net: end a route Android Auto stopped talking about. */
    private static final long WATCHDOG_MS = 15L * 60L * 1000L;
    private static final long LANE_POLL_MS = 500L;

    private static AaBridge instance;

    private final IContext context;
    private final AaRgState rg = new AaRgState();
    private final Object queueLock = new Object();
    private byte[] queued;
    private boolean sessionActive;          /* guarded by this */
    private volatile long lastNavUpdateMs;
    private volatile int nTurn, nDist, nFrames, nDelivered;

    private AaBridge(IContext context) {
        this.context = context;
    }

    static boolean disabled() {
        for (int i = 0; i < DISABLE_MARKERS.length; i++) {
            try { if (new File(DISABLE_MARKERS[i]).exists()) return true; }
            catch (Throwable t) { /* never let a marker probe break Android Auto */ }
        }
        return false;
    }

    /** Called from the AndroidAuto2ListenerDistributor constructor. Never throws. */
    public static synchronized void attach(AndroidAuto2ListenerDistributor distributor, IContext context) {
        try {
            if (disabled()) {
                Log.w(TAG, "disabled by marker; Android Auto stays stock");
                return;
            }
            if (instance != null) {
                Log.i(TAG, "attach: adding the existing bridge to a new distributor");
                distributor.addSingleListener(instance);
                return;
            }
            AaBridge b = new AaBridge(context);
            instance = b;
            distributor.addSingleListener(b);
            b.startWorker();
            b.startWatchdog();
            b.startLanePoller();
            IDeviceManager dm = context.getDeviceManager();
            dm.addActiveDeviceListener(b);
            Log.i(TAG, "attached (build " + CarPlayApp.BUILD_ID + ")");
            TMDevice active = dm.getActiveDevice();
            if (active != null) b.updateActiveDeviceState(active);
        } catch (Throwable t) {
            Log.w(TAG, "attach failed: " + t);
        }
    }

    /**
     * Called from the AndroidAuto2NavHandler constructor.  The stock handler subscribes only a
     * few Android Auto attributes, and gal forwards next-turn data only when subscribed.  The
     * constants are read from the unit's own DSIAndroidAuto2 at runtime (getstatic). Never throws.
     */
    public static void subscribeNavigation(DSIAndroidAuto2 dsi) {
        try {
            if (dsi == null) {
                Log.w(TAG, "subscribeNavigation: no DSI");
                return;
            }
            if (disabled()) {
                Log.w(TAG, "disabled by marker; navigation subscription skipped");
                return;
            }
            int[] attrs = new int[]{
                    DSIAndroidAuto2.ATTR_CALLSTATE,
                    DSIAndroidAuto2.ATTR_TELEPHONYSTATE,
                    DSIAndroidAuto2.ATTR_NOWPLAYINGDATA,
                    DSIAndroidAuto2.ATTR_PLAYBACKSTATE,
                    DSIAndroidAuto2.ATTR_PLAYPOSITION,
                    DSIAndroidAuto2.ATTR_COVERARTURL,
                    DSIAndroidAuto2.ATTR_NAVIGATIONNEXTTURNEVENT,
                    DSIAndroidAuto2.ATTR_NAVIGATIONNEXTTURNDISTANCE};
            dsi.setNotification(attrs, null);
            StringBuffer sb = new StringBuffer();
            for (int i = 0; i < attrs.length; i++) sb.append(i == 0 ? "" : ",").append(attrs[i]);
            Log.i(TAG, "subscribed Android Auto attributes " + sb);
        } catch (Throwable t) {
            Log.w(TAG, "subscribeNavigation failed: " + t);
        }
    }

    /* ------------------------------------------------------------------ session lifecycle */

    private static boolean sessionState(TMDevice d) {
        if (d == null || !d.isAndroidAutoDevice()) return false;
        TMDevice.ConnectionState s = d.connectionState();
        return s != null && (s.is(TMDevice.ConnectionState.ACTIVATING) || s.is(TMDevice.ConnectionState.ACTIVE));
    }

    private static boolean sessionGone(TMDevice d) {
        if (d == null || !d.isAndroidAutoDevice()) return true;
        TMDevice.ConnectionState s = d.connectionState();
        return s == null || s.is(TMDevice.ConnectionState.INVALID) || s.is(TMDevice.ConnectionState.NOT_ATTACHED)
            || s.is(TMDevice.ConnectionState.ATTACHED);
    }

    public void updateActiveDeviceState(TMDevice d) {
        try {
            boolean start = false, stop = false;
            synchronized (this) {
                if (!sessionActive && sessionState(d)) {
                    sessionActive = true;
                    start = true;
                } else if (sessionActive && sessionGone(d)) {
                    sessionActive = false;
                    stop = true;
                }
            }
            if (start) {
                Log.i(TAG, "device " + d.connectionState() + " -> session start");
                /* baseline: no route, so a sticky frame from an earlier session is never replayed */
                rg.resetSession();
                publish();
                AaCoverArt.sessionStart();
                CarPlayApp.onActivateAndroidAuto(context);
            } else if (stop) {
                Log.i(TAG, "device " + (d == null ? "null" : String.valueOf(d.connectionState()))
                    + " -> session end");
                /* clean route end first (RouteGuidance may still be running for ~400 ms) */
                rg.resetSession();
                publish();
                AaCoverArt.sessionEnd();
                CarPlayApp.onDeactivateAndroidAuto();
            }
        } catch (Throwable t) {
            Log.w(TAG, "device state handler failed: " + t);
        }
    }

    /* ------------------------------------------------------------------ Android Auto events */

    public void updateNavigationNextTurnEvent(String road, int turnSide, int event, int turnAngle,
                                              int turnNumber, int valid) {
        try {
            nTurn++;
            lastNavUpdateMs = System.currentTimeMillis();
            boolean changed = rg.onTurn(road, turnSide, event, turnAngle, turnNumber, valid);
            /* verbose (Info) only for events that change something; repeats are ~1 Hz */
            if (changed || nTurn <= 3) Log.i(TAG, "turn road='" + road + "' side=" + turnSide + " event=" + event
                + " angle=" + turnAngle + " number=" + turnNumber + " valid=" + valid + " -> " + rg.lastAction());
            if (changed) publish();
        } catch (Throwable t) {
            Log.w(TAG, "turn handler failed: " + t);
        }
    }

    public void updateNavigationNextTurnDistance(int distanceMeters, int timeSeconds, int valid) {
        try {
            nDist++;
            lastNavUpdateMs = System.currentTimeMillis();
            boolean changed = rg.onDistance(distanceMeters, timeSeconds, valid);
            if (nDist <= 3 || nDist % 20 == 0) Log.i(TAG, "distance m=" + distanceMeters
                + " s=" + timeSeconds + " valid=" + valid + " -> " + rg.lastAction());
            if (changed) publish();
        } catch (Throwable t) {
            Log.w(TAG, "distance handler failed: " + t);
        }
    }

    /** Album art: gal's file path; the work runs on AaCoverArt's worker. */
    public void updateCoverArtUrl(org.dsi.ifc.global.ResourceLocator loc, int valid) {
        try {
            AaCoverArt.request(loc != null ? loc.getUrl() : null, valid);
        } catch (Throwable t) {
            Log.w(TAG, "cover art handler failed: " + t);
        }
    }

    public void navFocusRequestNotification(int focus, int valid) {
        try {
            boolean changed = rg.onNavFocus(focus, valid);
            Log.i(TAG, "navFocus focus=" + focus + " valid=" + valid + " -> " + rg.lastAction());
            if (changed) publish();
        } catch (Throwable t) {
            Log.w(TAG, "navFocus handler failed: " + t);
        }
    }

    /* ------------------------------------------------------------------ frame delivery */

    /** Latest-wins hand-off to the worker; full frames make coalescing lossless. */
    private void publish() {
        synchronized (queueLock) {
            String s = rg.snapshot();
            try {
                queued = s.getBytes("UTF-8");
            } catch (Throwable t) {
                queued = s.getBytes();
            }
            nFrames++;
            queueLock.notifyAll();
        }
    }

    private void startWorker() {
        Thread t = new Thread("carplay-aa-rgd") {
            public void run() {
                while (true) {
                    byte[] p;
                    try {
                        synchronized (queueLock) {
                            while (queued == null) queueLock.wait();
                            p = queued;
                            queued = null;
                        }
                        if (CarplayBus.getInstance().injectLocal(CarplayBus.EVT_RGD_UPDATE,
                                CarplayBus.FLAG_STICKY, p)) nDelivered++;
                    } catch (Throwable e) {
                        Log.w(TAG, "rgd worker: " + e);
                    }
                }
            }
        };
        t.setDaemon(true);
        t.start();
    }

    /* Lanes of the current Android Auto step (gal hook record, AaLaneFeed), polled while a route
     * is active; a change is published as a full frame like a turn or distance. */
    private void startLanePoller() {
        Thread t = new Thread("carplay-aa-lanes") {
            public void run() {
                while (true) {
                    try {
                        Thread.sleep(LANE_POLL_MS);
                        if (!rg.isRouteActive()) continue;       /* route end already cleared the lanes */
                        if (rg.onLanes(AaLaneFeed.read(AaLaneFeed.PATH))) {
                            Log.i(TAG, "lanes " + (rg.lanes().length() == 0 ? "none" : rg.lanes())
                                + " event=" + rg.laneEvent());
                            publish();
                        }
                    } catch (Throwable e) {
                        Log.w(TAG, "lanes: " + e);
                    }
                }
            }
        };
        t.setDaemon(true);
        t.start();
    }

    private void startWatchdog() {
        Thread t = new Thread("carplay-aa-watchdog") {
            private long lastHb;
            public void run() {
                while (true) {
                    try {
                        Thread.sleep(5000L);
                        long now = System.currentTimeMillis();
                        if (rg.isRouteActive() && now - lastHb >= 30000L) {
                            lastHb = now;
                            Log.i(TAG, "hb route gen=" + rg.routeGeneration() + " v=" + rg.maneuverVersion()
                                + " maneuver=" + rg.current() + " m=" + rg.distance() + " turns=" + nTurn
                                + " dists=" + nDist + " frames=" + nFrames + " delivered=" + nDelivered
                                + " lastUpdateAgoS=" + ((now - lastNavUpdateMs) / 1000));
                        }
                        if (rg.isRouteActive() && now - lastNavUpdateMs > WATCHDOG_MS
                                && rg.endRoute("safety net: no Android Auto navigation update for "
                                    + (WATCHDOG_MS / 60000) + " min")) {
                            Log.w(TAG, rg.lastAction());
                            publish();
                        }
                    } catch (Throwable e) {
                        Log.w(TAG, "watchdog: " + e);
                    }
                }
            }
        };
        t.setDaemon(true);
        t.start();
    }
}
