/*
 * RgdModule — phone route-guidance feature (CarPlay and Android Auto) as a CarPlayApp Module.
 *
 * Adapter over the ported RouteGuidance/BAPBridge chain.  start() implements the
 * proven retry-until-ready gate: CombiBAPServiceNavi (and the ClusterService BAP
 * listener it drives) appears a beat after CarPlay activate, so we return false
 * until the service is registered — CarPlayApp.startRetry() calls us again.
 *
 * Copyright (c) 2026 LuKa (@LuKa_dev)
 */
package com.luka.carplay.core;

import com.luka.carplay.framework.Log;
import com.luka.carplay.rgd.RouteGuidance;

import de.audi.atip.interapp.combi.bap.navi.CombiBAPServiceNavi;

import java.io.File;

final class RgdModule implements Module {

    private static final String TAG = "RgdModule";
    /* Runtime kill-switch for route guidance (the cluster maneuver arrow, HUD and
     * BAP takeover). Present => this module stays inert; CarPlay video (AltScreen,
     * ctx 81) and the rest of the patch keep working. Same idiom as the verbose
     * marker: read at every CarPlay session start, so creating/removing it takes
     * effect on the next phone reconnect — no reboot. Persistent marker survives a
     * reboot; the /tmp one does not. */
    private static final String[] DISABLE_MARKERS = {
        "/mnt/app/carplay_rgd.disabled", "/tmp/carplay_rgd.disabled"
    };
    private RouteGuidance rg;
    private FrameworkRef.ServiceHandle naviHandle;

    private static boolean routeGuidanceDisabled() {
        for (int i = 0; i < DISABLE_MARKERS.length; i++) {
            try { if (new File(DISABLE_MARKERS[i]).exists()) return true; }
            catch (Throwable t) { /* never let a marker probe break startup */ }
        }
        return false;
    }

    public String name() { return "rgd"; }

    public boolean start(FrameworkRef fw) {
        if (fw == null || !fw.isReady()) return false;             /* framework not up → retry */
        if (!ScreenModule.isPlatformSupported(fw)) {
            Log.w(TAG, "disabled on unsupported G24 cluster");
            return true;
        }
        if (routeGuidanceDisabled()) {
            /* Report started so CarPlayApp stops retrying; do NOT engage the BAP
             * takeover, so stock route guidance is left alone and the cluster is
             * free for CarPlay video. If a session was already running, stop it. */
            if (rg != null) { rg.stop(); rg.disengageTakeover(); rg = null; }
            Log.w(TAG, "route guidance disabled by marker; skipping BAP takeover");
            return true;
        }

        if (naviHandle == null) naviHandle = fw.getServiceHandle(CombiBAPServiceNavi.class);
        Object navi = naviHandle != null ? naviHandle.service() : null;
        if (navi == null) {
            return false;
        }

        /* Init + subscribe ONCE.  Guard against re-running rg.init() on a retry (it builds a
         * fresh BAPBridge each call → would re-wrap the gate every retry).  rg.start() is
         * idempotent (returns early if already running). */
        if (rg == null) {
            RouteGuidance r = new RouteGuidance();
            if (!r.init(navi)) {                                  /* BAPBridge init (ClusterService) not ready */
                return false;
            }
            rg = r;
        }
        /* REPLACE: don't report started until the RG gate is actually shut.  engageTakeover
         * returns false while ClusterService isn't up yet → CarPlayApp keeps retrying, so a
         * connected session with no CarPlay navigation still gets stock RG blocked.
         * Android Auto takes over per phone route instead: BAPBridge.onStart() stops native
         * guidance and shuts the gate for each route and onShutdown() reopens it, so Audi
         * navigation keeps working while an Android phone is connected. */
        if (CarPlayApp.sessionOwner() == CarPlayApp.OWNER_CARPLAY && !rg.engageTakeover()) {
            return false;
        }
        if (!rg.isRunning()) rg.start();                           /* subscribe only after gate is shut */
        return true;
    }

    public void stop() {
        if (rg != null) { rg.stop(); rg.disengageTakeover(); rg = null; }
        if (naviHandle != null) { naviHandle.release(); naviHandle = null; }
    }
}
