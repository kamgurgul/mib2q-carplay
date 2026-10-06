/*
 * VcMapViewGate — keep the Virtual Cockpit in its map view while the Android Auto
 * cluster video is shown.
 *
 * Adapted from wasimlhr/mib2q-android-auto-cluster (GPL-3.0-or-later).
 *
 * Stock chain when Android Auto takes navigation focus:
 *   AndroidAuto2NavHandler.requestDSIUpdate(NAVI, DEVICE)
 *     -> GALHandler.updateNaviAppState(true) -> ClusterService.updateGALState(true)
 *     -> CombiBAPListener.setGALState(true) -> BAP NavSD FctID 38 InfoStates = 6
 *   and the VC leaves its map content for the "navigation on mobile device" layout,
 *   which hides plane 99 (the phone's own cluster map).
 *
 * This gate replaces exactly that one input (true -> false) while terminal 1 physically
 * carries a cluster-video context (81/82).  Everything else in the InfoStates computation
 * stays stock, and outside the video contexts stock behaviour is unchanged.
 *
 * Threading: the stock path runs on the caller's thread exactly as stock does (only the value
 * changes).  Video-context edges come from ScreenModule's switch worker; the re-forward goes
 * through Sink.forwardLater() (the Navigation dispatcher) and reads the value when it runs
 * (takeEffective), so a late runnable never applies a stale value.
 *
 * Java 1.4.  SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.luka.carplay.aa;

import com.luka.carplay.framework.Log;

public final class VcMapViewGate {
    private static final String TAG = "AA";

    /** Re-forward target: ClusterGalSink (ClusterService on NavigationJobs). */
    public interface Sink {
        /** Schedule takeEffective() -> CombiBAPListener.setGALState(value). */
        void forwardLater();
    }

    private static final Object LOCK = new Object();
    private static boolean haveStock;
    private static boolean stockGal;
    private static boolean videoShown;
    private static int forwarded = -1;              /* last value handed to CombiBAPListener */
    private static Sink sink;

    private VcMapViewGate() {}

    /** The one rule: the phone-navigation flag reaches the VC unless cluster video is shown. */
    public static boolean effective(boolean stockGalState, boolean clusterVideoShown) {
        return stockGalState && !clusterVideoShown;
    }

    /** ClusterService.updateGALState(flag), on the stock caller's thread.  Returns the value
     *  to hand to CombiBAPListener.setGALState in place of flag. */
    public static boolean onStockGalState(Sink s, boolean flag) {
        boolean eff, shown, repeat;
        synchronized (LOCK) {
            if (s != null) sink = s;
            shown = videoShown;
            eff = effective(flag, shown);
            /* GALHandler re-sends on every terminal-mode app-state change: log changes only */
            repeat = haveStock && stockGal == flag && forwarded == (eff ? 1 : 0);
            haveStock = true;
            stockGal = flag;
            forwarded = eff ? 1 : 0;
        }
        if (!repeat) {
            Log.i(TAG, "vcmap: stock setGALState(" + flag + ")"
                + (eff != flag ? " suppressed while cluster video is shown" : " forwarded"));
        }
        return eff;
    }

    /** ScreenModule worker, after every completed context switch on terminal 1. */
    public static void onClusterVideoShown(boolean shown) {
        Sink s;
        boolean eff;
        synchronized (LOCK) {
            if (videoShown == shown) return;
            videoShown = shown;
            if (!haveStock || sink == null) return;
            eff = effective(stockGal, shown);
            if (forwarded == (eff ? 1 : 0)) return;
            s = sink;
        }
        Log.i(TAG, "vcmap: cluster video " + (shown ? "shown" : "left") + " -> setGALState(" + eff + ")");
        try {
            s.forwardLater();
        } catch (Throwable t) {
            Log.w(TAG, "vcmap: re-forward failed: " + t);
        }
    }

    /** Called by the Sink when its scheduled forward runs: the value to apply now. */
    public static boolean takeEffective() {
        synchronized (LOCK) {
            boolean eff = effective(stockGal, videoShown);
            forwarded = eff ? 1 : 0;
            return eff;
        }
    }

    /** Host tests only. */
    public static void resetForTest() {
        synchronized (LOCK) {
            haveStock = false;
            stockGal = false;
            videoShown = false;
            forwarded = -1;
            sink = null;
        }
    }
}
