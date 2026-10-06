/*
 * ScreenModule — instrument-cluster (LVDS2 / terminal 1) CONTEXT MANAGER.
 *
 * Owns the CarPlay cluster context and selects between exactly two contexts:
 *
 *   dc[80] = {98 maneuver, 101/102 KDK backing, 33 stock native map}   — nav active
 *   dc[74] = stock cluster                                             — otherwise
 *
 * The maneuver overlay (displayable 98, maneuver_render, transparent when idle)
 * composites over the head unit's OWN native map (displayable 33); there is no
 * CarPlay video plane on the cluster.  Every new CarPlay session leaves the cluster
 * on stock (74); we switch to ctx 80 once RouteGuidance has started the RGI
 * presentation through BAP (setNavActive(true)), and drop back to 74 once VC withdraws KDK visibility (Fct44)
 * after guidance ends, and on disconnect.
 *
 * The context tables (dc[80]) are declared to the native compositor at init in
 * DisplayManagerMIB2High; getMappedInternalContext is identity on MIB2High, so
 * switchContext(80) lands on exactly that declared context.
 *
 * There is exactly ONE persistent worker for the module lifetime and it is the SOLE
 * caller of DisplayManager.switchContext/setUpdateRate.  Single writer => two
 * switches can never race the bounce+settle, and no stale per-session worker can
 * exist.  start()/stop() only publish the desired context; the worker converges the
 * cluster to it.
 */
package com.luka.carplay.core;

import com.luka.carplay.framework.Log;

import de.audi.atip.hmi.view.IDisplayManager;
import de.audi.tghu.fwhmi.IDisplayManagerKombiControl;

public final class ScreenModule implements Module {

    private static final String TAG = "Screen";

    public static final int TERMINAL_CLUSTER  = 1;    /* LVDS2 */
    public static final int CTX_CLUSTER       = 80;   /* nav active: {98 maneuver, 101/102 backing, 33 stock map} */
    /* AltScreen: CarPlay's OWN map video (displayable 99, altscreen_render). */
    public static final int CTX_CLUSTER_VIDEO     = 81;   /* {99} video only */
    public static final int CTX_CLUSTER_VIDEO_NAV = 82;   /* {98,101,102,99} video + maneuver */
    public static final int CTX_STOCK_CLUSTER = 74;
    private static final int CTX_BOUNCE       = 72;   /* kombi map — never ours; forces a real ctx change */
    private static final int BOUNCE_SLEEP_MS  = 180;  /* preContextSwitchHook settle (proven driver) */
    private static final long CONTEXT_RECONCILE_MS = 250L;
    private static final int CLUSTER_FPS      = 30;   /* cluster encoder rate; MOST/encoder may cap below this */
    private static final int KOMBI_TYPE_G24   = 4;

    private static final Object LOCK = new Object();
    private IDisplayManager dm;                  /* current DisplayManager (guarded by LOCK); stable across sessions */
    private Thread worker;                        /* the ONE persistent switch worker (guarded by LOCK) */
    private static volatile Thread contextWriterThread;

    /* true while the cluster is on OUR context (set AFTER the physical switch completes). */
    private static volatile boolean clusterActive = false;
    private static volatile boolean platformSupported = true;
    private boolean enabled;

    /**
     * True from start() (BEFORE any switch) until stop() — i.e. this returns our *intent* to own the
     * cluster for the whole session, not the applied state.  CombiMapController's View-pin reads THIS:
     * pinning on the applied state leaves a ~180ms window during the first switch where a View press
     * could steal terminal 1 to the stock map (worker then thinks it still owns ctx → stuck).
     * Intent-based pin closes that window from t0. */
    public static boolean isConnected() { return platformSupported && connected; }

    /** DisplayManagerMIB2High uses this to distinguish our serialized 72/80/74
     * writes from stock screen-controller requests while CarPlay owns terminal 1. */
    public static boolean isClusterContextWriterThread() {
        return Thread.currentThread() == contextWriterThread;
    }

    static boolean isPlatformSupported(FrameworkRef fw) {
        try { return fw != null && fw.framework() != null
            && fw.framework().getKombiType() != KOMBI_TYPE_G24; }
        catch (Throwable t) { return true; }  /* only an explicit G24 value disables the feature */
    }

    /* desiredCtx = target published by start()/stop()/setNavActive(); currentCtx = what the worker last
     * applied.  Both guarded by LOCK; the single worker switches whenever they differ.
     * desiredCtx is a pure function of these two (guarded by LOCK):
     *   !connected         -> 74 (stock)
     *   connected, no nav  -> 74 (stock native map, no maneuver overlay)
     *   connected, nav     -> 80 (stock native map + backing + maneuver) */
    private static int desiredCtx = CTX_STOCK_CLUSTER;
    private static int currentCtx = -1;
    private static volatile boolean connected = false;
    private static volatile boolean navActive = false;
    private static volatile boolean altScreenActive = false;   /* altscreen_render live video */
    private static boolean navHidePending;

    /** True for any of OUR CarPlay cluster contexts (drift-reconciled at 250 ms). */
    private static boolean isOurCarPlayContext(int c) {
        return c == CTX_CLUSTER || c == CTX_CLUSTER_VIDEO || c == CTX_CLUSTER_VIDEO_NAV;
    }

    /** Recompute desiredCtx from connected/navActive and wake the worker. Caller must NOT hold LOCK. */
    private static void republish() {
        synchronized (LOCK) {
            if (connected && altScreenActive) {
                /* CarPlay's own map video is live: show it, with the maneuver
                 * overlay on top when guidance is active. */
                desiredCtx = navActive ? CTX_CLUSTER_VIDEO_NAV : CTX_CLUSTER_VIDEO;
            } else if (connected && navActive) {
                desiredCtx = CTX_CLUSTER;               /* stock map + maneuver overlay */
            } else {
                desiredCtx = CTX_STOCK_CLUSTER;
            }
            LOCK.notifyAll();
        }
    }

    /** Fed by AltScreenModule from altscreen_render's liveness signal. When the
     *  CarPlay video plane (99) is producing frames the cluster switches to the
     *  video context; when it stops we fall back to the stock-map maneuver
     *  context (or plain stock). */
    public static void setAltScreenActive(boolean active) {
        if (altScreenActive == active) return;
        altScreenActive = active;
        Log.i(TAG, "altScreen video " + (active ? "live -> ctx 81/82" : "stopped -> ctx 74/80"));
        republish();
    }

    public static boolean isAltScreenActive() { return altScreenActive; }

    /** Presentation latch, not merely route intent.  RouteGuidance may set true only after the
     *  BAP presentation has started (bap.onStart()); the renderer's FRAME_READY is not waited for.
     *  Navigation owns the context; VC alone controls KDK opacity.  On route end, retain the
     *  composition until VC withdraws visibility (Fct44), without a guessed timer. */
    public static void setNavActive(boolean active) {
        synchronized (LOCK) {
            navHidePending = !active && navActive
                && com.luka.carplay.cluster.ClusterLayerController.isKdkVisible();
            navActive = active || navHidePending;
        }
        republish();
    }

    /** Called after the layer controller has applied the received Fct44 visibility.
     *  A View fade-out must not release context while the route remains active. */
    public static void onVcKdkVisibility(boolean visible) {
        boolean release = false;
        synchronized (LOCK) {
            if (!visible && navHidePending) {
                navHidePending = false;
                navActive = false;
                release = true;
            }
        }
        if (release) republish();
    }

    /** The cluster-layer visibility gate read by CombiMapController.  It follows the confirmed BAP
     *  presentation and, after route end, VC's own KDK withdrawal; never a stray stock KDK bit. */
    public static boolean isNavActive() { return navActive; }

    /* ------------------------------------------------------------
     * Cluster map view size (Audi View button / NAV_VIEW_SIZE_CHOICE).
     * There is no CarPlay video to resize on this branch; the flag drives only LOCAL geometry —
     * which KDK stage (popup/in-tube) and which native map plane (33/58) the maneuver overlay must
     * follow.  No hook command is sent.
     * ------------------------------------------------------------ */
    public static final int VIEWAREA_FULLSCREEN  = 0;
    public static final int VIEWAREA_SMALLSCREEN = 1;
    private static volatile boolean smallScreenViewArea = false;
    private static volatile ViewAreaModeListener viewAreaModeListener;

    /** Lightweight notification for consumers whose cluster presentation differs by view area. */
    public interface ViewAreaModeListener {
        void onViewAreaModeChanged(int mode);
    }

    public static boolean isSmallScreenViewArea() { return smallScreenViewArea; }

    public static void setViewAreaModeListener(ViewAreaModeListener listener) {
        viewAreaModeListener = listener;
    }

    public static void clearViewAreaModeListener(ViewAreaModeListener listener) {
        if (viewAreaModeListener == listener) viewAreaModeListener = null;
    }

    /** Called by the stock NAV_VIEW_SIZE_CHOICE model: value 0=fullscreen, value 1=smallscreen. */
    public static void setViewAreaMode(int mode) {
        boolean small = (mode == VIEWAREA_SMALLSCREEN);
        if (smallScreenViewArea == small) return;
        smallScreenViewArea = small;
        ViewAreaModeListener listener = viewAreaModeListener;
        if (listener != null) {
            try { listener.onViewAreaModeChanged(small ? VIEWAREA_SMALLSCREEN : VIEWAREA_FULLSCREEN); }
            catch (Throwable t) { Log.w(TAG, "viewArea listener failed: " + t); }
        }
    }

    /* ------------------------------------------------------------
     * Route-info toggle (steering-wheel OK press).
     * Flips the cluster route-info text line between the next turn-to street and the
     * trip summary (ETA / arrival clock + remaining).  Driven by SteeringWheelInputModule.
     * ------------------------------------------------------------ */

    /** Route-info toggle seam driven by the raw MFW left-roller press listener. */
    public interface InfoModeListener {
        void onInfoModeToggle();
    }
    private static volatile InfoModeListener infoModeListener;

    public static void setInfoModeListener(InfoModeListener listener) {
        infoModeListener = listener;
    }

    public static void clearInfoModeListener(InfoModeListener listener) {
        if (infoModeListener == listener) infoModeListener = null;
    }

    /** Raw DSI key 40 (left steering-wheel roller press).  SteeringWheelInputModule already gates
     *  this callback to the confirmed VC map tab; the toggle is meaningful only with active RGI. */
    public static void onSteeringWheelOkPressed() {
        if (!isConnected() || !isNavActive()) return;
        InfoModeListener listener = infoModeListener;
        if (listener == null) return;
        listener.onInfoModeToggle();
    }

    public String name() { return "screen"; }

    public boolean start(FrameworkRef fw) {
        if (fw == null || !fw.isReady() || fw.framework() == null) return false;
        if (!isPlatformSupported(fw)) {
            platformSupported = false;
            enabled = false;
            Log.w(TAG, "disabled: G24 cluster has no ctx 80 maneuver composition");
            return true;
        }
        platformSupported = true;
        enabled = true;

        IDisplayManager d = null;
        try {
            if (fw.framework().getHMIService() != null) {
                d = fw.framework().getHMIService().getDisplayManager();
            }
        } catch (Throwable t) {
        }
        if (d == null) return false;                 /* DM not up yet → retry */
        if (d instanceof IDisplayManagerKombiControl) {
            com.luka.carplay.cluster.ClusterLayerController.bind(
                (IDisplayManagerKombiControl)d, TERMINAL_CLUSTER);
        }

        /* Publish the new session target before a new worker can observe currentCtx=-1. */
        synchronized (LOCK) {
            /* A replaced DisplayManager invalidates the cached currentCtx — reset so the worker
             * re-applies the desired ctx to the new one.  (In practice the same object each session.) */
            if (dm != d) { dm = d; currentCtx = -1; }
            connected = true;
            navActive = false;
            altScreenActive = false;
            navHidePending = false;
            desiredCtx = CTX_STOCK_CLUSTER;
        }
        synchronized (LOCK) {
            /* Create the single persistent worker once; recreate only if it never started or died.
             * Assign the field ONLY after start() succeeds so a throw leaves worker==null for retry. */
            if (worker == null || !worker.isAlive()) {
                Thread w = new Thread(new Runnable() { public void run() { switchLoop(); } }, "carplay-cluster-switch");
                w.setDaemon(true);
                w.start();
                worker = w;
            }
            LOCK.notifyAll();
        }
        Log.i(TAG, "ready (DisplayManager acquired, session reset -> ctx 74)");
        return true;
    }

    public void stop() {
        if (!enabled) return;
        enabled = false;
        /* Disconnect: publish stock (74); the single persistent worker restores it.  We deliberately
         * do NOT kill the worker or null dm — the worker being the sole always-live DM writer is what
         * makes stale-worker races impossible (no per-session worker to outlive its session). */
        synchronized (LOCK) {
            connected = false;
            navActive = false;
            altScreenActive = false;
            navHidePending = false;
        }
        republish();
    }

    /* ============================================================
     * Switch worker — the single serialized DM writer.
     * ============================================================ */

    private void switchLoop() {
        contextWriterThread = Thread.currentThread();
        while (true) {
            int target; IDisplayManager d; boolean reconcileOnly = false;
            synchronized (LOCK) {
                while (dm == null) {
                    try { LOCK.wait(); } catch (InterruptedException e) { /* persistent worker */ }
                }
                if (desiredCtx == currentCtx) {
                    try {
                        if (isOurCarPlayContext(desiredCtx))
                            LOCK.wait(CONTEXT_RECONCILE_MS);
                        else
                            LOCK.wait();
                    } catch (InterruptedException e) { /* persistent worker */ }
                    if (dm == null || desiredCtx != currentCtx) continue;
                    if (!isOurCarPlayContext(desiredCtx)) continue;
                    reconcileOnly = true;
                }
                target = desiredCtx; d = dm;
            }
            if (reconcileOnly) {
                int actual;
                try { actual = d.getCurrentContextID(TERMINAL_CLUSTER); }
                catch (Throwable t) {
                    Log.w(TAG, "context reconcile read failed: " + t);
                    continue;
                }
                if (actual != target) {
                    boolean retry = false;
                    synchronized (LOCK) {
                        if (dm == d && desiredCtx == target && currentCtx == target) {
                            currentCtx = -1;
                            retry = true;
                            LOCK.notifyAll();
                        }
                    }
                    if (retry)
                        Log.w(TAG, "physical context drift actual=" + actual
                            + " desired=" + target + " -> reconcile");
                }
                continue;
            }
            applySwitch(target, d);
        }
    }

    /** Perform ONE context switch (bounce + settle + select).  Only this thread ever writes the DM,
     *  so there is no cross-worker race; the loop re-runs if desiredCtx changed during the settle.
     *  Note: a stop()/start() landing in the tiny window between the post-sleep recheck and the
     *  switchContext write can still cause ONE transient physical write before the next loop restores
     *  the newly-desired ctx — it is self-healing.  Closing it fully needs LOCK held across a DSI IPC
     *  call → deadlock risk, not worth it for a cosmetic transient on connect/disconnect. */
    private void applySwitch(int ctx, IDisplayManager d) {
        try {
            if (ctx != CTX_STOCK_CLUSTER) {
                int from;
                synchronized (LOCK) { from = currentCtx; }
                /* Bounce only when the MOST encoder was off (stock 74, or never applied).
                 * 80/81/82 already have it running; a ctx 72 detour flashes the stock map
                 * and disowns planes 98 and 99. */
                if (from == CTX_STOCK_CLUSTER || from < 0) {
                    int bounce = (ctx != CTX_BOUNCE) ? CTX_BOUNCE : CTX_STOCK_CLUSTER;
                    d.switchContext(bounce, TERMINAL_CLUSTER, null);
                    try { Thread.sleep(BOUNCE_SLEEP_MS); }
                    catch (InterruptedException ie) { Thread.currentThread().interrupt(); }
                    /* Coalesce: if the desired target or the DM changed during the settle, abandon THIS
                     * switch (cluster is on the bounce ctx) and let the loop apply the latest desired. */
                    synchronized (LOCK) {
                        if (dm != d || desiredCtx != ctx) {
                            currentCtx = -1;
                            Log.i(TAG, "switch(" + ctx + ") superseded during bounce → " + desiredCtx);
                            return;
                        }
                    }
                }
                d.switchContext(ctx, TERMINAL_CLUSTER, null);
                d.setUpdateRate(TERMINAL_CLUSTER, CLUSTER_FPS);   /* (idempotent when already running) */
                clusterActive = true;
            } else {
                /* Preserve the stop-before-switch ordering, but never leave terminal 1
                 * parked at 0 FPS. On this A5/MHI2Q the stock
                 * KOMBI_KDK_VIA_DISPLAYABLES branch bypasses CombiMapController's
                 * optional 10/1/0 updateFrameRate() path, so 10 is not an authoritative
                 * restore value here. Return the terminal to the same full 30 Hz rate
                 * used by the live cluster encoder; stock may change it later if it has
                 * an applicable producer. */
                d.setUpdateRate(TERMINAL_CLUSTER, 0);
                try {
                    d.switchContext(CTX_STOCK_CLUSTER, TERMINAL_CLUSTER, null);
                } finally {
                    d.setUpdateRate(TERMINAL_CLUSTER, CLUSTER_FPS);
                }
                clusterActive = false;
            }
            synchronized (LOCK) { if (dm == d) currentCtx = ctx; }
            /* Context composition is now final: replay the last KDK popup geometry so a
             * navActive edge cannot leave planes 98/101/102 at their previous opacity. */
            com.luka.carplay.cluster.ClusterLayerController.reapply();
            /* Android Auto: keep the VC in its map view while plane 99 is on terminal 1. */
            com.luka.carplay.aa.VcMapViewGate.onClusterVideoShown(
                ctx == CTX_CLUSTER_VIDEO || ctx == CTX_CLUSTER_VIDEO_NAV);
            Log.i(TAG, "cluster -> ctx " + ctx + " (active=" + clusterActive + ")");
        } catch (Throwable t) {
            Log.w(TAG, "switch(" + ctx + ") failed: " + t);
            synchronized (LOCK) { currentCtx = -1; }
            /* Throttle the retry: the bounce write and the stock path have no settle sleep, so a
             * persistently-throwing switchContext would otherwise hot-spin (busy loop + log flood). */
            try { Thread.sleep(BOUNCE_SLEEP_MS); } catch (InterruptedException ie) { Thread.currentThread().interrupt(); }
        }
    }
}
