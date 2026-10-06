/*
 * CarPlayApp — entry point + module lifecycle.
 *
 * Called from the stock hook in TerminalModeBapCombi$ActiveDeviceStateListener:
 *   CarPlayApp.onActivate(ctx)   // CarPlay device connected  (ctx = IContext)
 *   CarPlayApp.onDeactivate()    // disconnected
 * and from com.luka.carplay.aa.AaBridge for an Android Auto phone:
 *   CarPlayApp.onActivateAndroidAuto(ctx) / onDeactivateAndroidAuto()
 *
 * One phone session at a time owns the modules.  The owner decides which modules
 * run (MODULE_OWNERS) and how RgdModule takes over route guidance.  isActive()
 * keeps its original meaning, "a CarPlay session is active", because every caller
 * outside this package is CarPlay-specific (PDC screen guard, CarPlay cover art).
 *
 * TerminalMode callbacks only publish the desired active/context generation and
 * return.  A persistent daemon worker serializes every Module start/stop away
 * from the stock HMI lifecycle thread.  A module may return false from start()
 * if a service it needs is not up yet; a separate daemon retry keeps trying
 * until everything is up or the CarPlay generation ends.  Navigation can appear
 * much later than TerminalMode on a cold boot, so this must not have a fixed timeout.
 *
 * Java 1.4 / Foundation 1.1 (no generics/autoboxing).
 *
 * Copyright (c) 2026 LuKa (@LuKa_dev)
 */
package com.luka.carplay.core;

import com.luka.carplay.bus.CarplayBus;
import com.luka.carplay.framework.Log;
import com.luka.carplay.pdc.PdcSmallStageGuard;

import de.audi.app.terminalmode.IContext;
import de.audi.atip.base.IFrameworkAccess;

public final class CarPlayApp {
    private static final String TAG = "App";
    public static final String BUILD_ID = "@BUILD_ID@";

    /* Phone session owners. */
    public static final int OWNER_NONE         = 0;
    public static final int OWNER_CARPLAY      = 1;
    public static final int OWNER_ANDROID_AUTO = 2;
    private static final int OWNER_ANY         = OWNER_CARPLAY | OWNER_ANDROID_AUTO;

    /* Modules, in start order. AltScreenModule follows ScreenModule because it
     * feeds it the cluster-video liveness that selects cluster ctx 81/82 (CarPlay
     * AltScreen or the Android Auto cluster display; one renderer serves both). */
    private static final Module[] MODULES = new Module[] {
        new ScreenModule(), new AltScreenModule(), new RgdModule(), new SteeringWheelInputModule()
    };
    /* Sessions each module runs for, parallel to MODULES.  The roller-press module is
     * CarPlay-only: the patched CarPlay key controller swallows the collapsed DDS_SELECT
     * that the same press also produces, Android Auto's controller does not. */
    private static final int[] MODULE_OWNERS = new int[] {
        OWNER_ANY, OWNER_ANY, OWNER_ANY, OWNER_CARPLAY
    };

    private static final Object lock = new Object();
    private static final Object lifecycleLock = new Object();
    private static final boolean[] started = new boolean[MODULES.length];
    /* true while any phone session is active; owner says which. */
    private static volatile boolean active = false;
    private static volatile int owner = OWNER_NONE;
    /* Owner of the last applied activation (lifecycle worker under lifecycleLock). */
    private static int appliedOwner = OWNER_NONE;
    private static volatile FrameworkRef fwRef;
    private static Thread retryThread;
    private static int retryGeneration;
    private static int serviceChangeGeneration;
    private static Thread lifecycleThread;
    private static int lifecycleGeneration;
    private static int lifecycleAppliedGeneration;
    private static IContext desiredContext;
    /* Looked up by name so inserting a module ahead of RgdModule cannot retarget
     * the navigation-service rebind onto AltScreen. */
    private static final int RGD_MODULE_INDEX = indexOf("rgd");

    private static int indexOf(String name) {
        for (int i = 0; i < MODULES.length; i++) {
            if (name.equals(MODULES[i].name())) return i;
        }
        return -1;
    }

    private CarPlayApp() {}

    /** Current HMI framework access, or null before activate / after deactivate. */
    public static IFrameworkAccess framework() {
        FrameworkRef r = fwRef;
        return r != null ? r.framework() : null;
    }

    /** True while a CarPlay device is connected (onActivate..onDeactivate).
     *  REPLACE mode gates roller/key capture on this (whole-session takeover),
     *  not on cluster-tab focus.  False during an Android Auto session. */
    public static boolean isActive() { return active && owner == OWNER_CARPLAY; }

    /** True while an Android Auto phone owns the session. */
    public static boolean isAndroidAutoActive() { return active && owner == OWNER_ANDROID_AUTO; }

    /** True while any phone session (CarPlay or Android Auto) is active. */
    public static boolean isSessionActive() { return active; }

    /** OWNER_CARPLAY, OWNER_ANDROID_AUTO, or OWNER_NONE without a session. */
    public static int sessionOwner() { return active ? owner : OWNER_NONE; }

    /** Bring the transport up ALWAYS-ON, independent of any CarPlay session.
     *  Called from the patched TerminalModeBapCombi.init() (component start / boot),
     *  so the C hook can connect once and stay connected — onActivate/onDeactivate
     *  then only gate the CarPlay modules, not the socket.  Idempotent.  Also seeds
     *  fwRef so framework() is available before the first activate. */
    public static void startTransport(Object context) {
        CarplayBus.getInstance().start();            /* idempotent */
        if (context instanceof IContext) {
            synchronized (lock) {
                if (fwRef == null) fwRef = new FrameworkRef((IContext) context);
                ensureLifecycleWorkerLocked();
            }
        }
        Log.i(TAG, "transport up (bus always-on) build=" + BUILD_ID);
    }

    public static void onActivate(Object context) { publishActivate(context, OWNER_CARPLAY); }

    /** Android Auto device ACTIVATING/ACTIVE (AaBridge).  A newer session of either kind
     *  replaces the current one: the worker restarts the modules for the new owner. */
    public static void onActivateAndroidAuto(Object context) {
        publishActivate(context, OWNER_ANDROID_AUTO);
    }

    private static void publishActivate(Object context, int newOwner) {
        if (!(context instanceof IContext)) {
            Log.e(TAG, "context is not IContext: " + context);
            return;
        }
        Log.refreshLevel();
        synchronized (lock) {
            ensureLifecycleWorkerLocked();
            /* ACTIVATING may be repeated for the same TMDevice.  Publishing the
             * same desired edge twice must not restart modules that are already
             * converging on the worker/retry path. */
            if (active && desiredContext == context && owner == newOwner) return;
            active = true;                           /* visible to stock HMI immediately */
            owner = newOwner;
            desiredContext = (IContext) context;
            serviceChangeGeneration++;
            lifecycleGeneration++;
            lock.notifyAll();
        }
    }

    /** ownerFilter: only end a session of that owner (OWNER_ANY = whichever is active),
     *  so a late disconnect of the previous phone cannot end the next phone's session. */
    private static int publishDeactivate(int ownerFilter) {
        synchronized (lock) {
            ensureLifecycleWorkerLocked();
            if (!active && desiredContext == null) return lifecycleGeneration;
            if (active && (owner & ownerFilter) == 0) return lifecycleGeneration;
            active = false;                          /* release stock HMI immediately */
            owner = OWNER_NONE;
            desiredContext = null;
            serviceChangeGeneration++;
            lifecycleGeneration++;
            lock.notifyAll();
            return lifecycleGeneration;
        }
    }

    public static void onDeactivate() { publishDeactivate(OWNER_CARPLAY); }

    /** Android Auto device gone (AaBridge). */
    public static void onDeactivateAndroidAuto() { publishDeactivate(OWNER_ANDROID_AUTO); }

    /** Component teardown is not the hot TMDevice callback.  Let it wait for
     * the already-published async cleanup before TerminalMode closes its OSGi
     * trackers; the bound prevents a broken module from hanging HMI shutdown. */
    public static void onDeactivateAndWait() {
        int generation = publishDeactivate(OWNER_ANY);
        long deadline = System.currentTimeMillis() + 3000L;
        synchronized (lock) {
            while (lifecycleAppliedGeneration != generation
                    && lifecycleGeneration == generation) {
                long remaining = deadline - System.currentTimeMillis();
                if (remaining <= 0L) {
                    Log.w(TAG, "deactivate cleanup timed out generation=" + generation);
                    break;
                }
                try { lock.wait(remaining); }
                catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
        }
    }


    /** Caller holds lock.  Started at TerminalMode component init, so the hot
     * ACTIVATING callback normally performs only field stores + notifyAll(). */
    private static void ensureLifecycleWorkerLocked() {
        if (lifecycleThread != null && lifecycleThread.isAlive()) return;
        Thread t = new Thread(new Runnable() {
            public void run() { lifecycleLoop(); }
        }, "carplay-lifecycle");
        t.setDaemon(true);
        lifecycleThread = t;
        try {
            t.start();
        } catch (Throwable x) {
            lifecycleThread = null;
            Log.e(TAG, "lifecycle worker start failed: " + x);
        }
    }

    private static boolean lifecycleCurrent(int generation, boolean wantActive,
                                            IContext context) {
        synchronized (lock) {
            return generation == lifecycleGeneration && active == wantActive
                && (!wantActive || desiredContext == context);
        }
    }

    private static void clearStarted() {
        synchronized (lock) {
            for (int i = 0; i < MODULES.length; i++) started[i] = false;
        }
    }

    private static void stopModules() {
        /* A false-returning start may still have acquired partial resources, so
         * every new lifecycle generation stops all modules in reverse order. */
        for (int i = MODULES.length - 1; i >= 0; i--) {
            try { MODULES[i].stop(); }
            catch (Throwable t) { Log.w(TAG, MODULES[i].name() + " stop: " + t); }
        }
        clearStarted();
    }

    private static void applyLifecycle(int generation, boolean wantActive,
                                       IContext context) {
        synchronized (lifecycleLock) {
            try {
                if (!lifecycleCurrent(generation, wantActive, context)) return;
                stopRetry();

                if (!wantActive) {
                    Log.i(TAG, "onDeactivate async apply generation=" + generation
                        + " owner=" + ownerName(appliedOwner));
                    // Disconnect need not produce another HMI/parking callback.
                    // Restore the OPS screen and APS drawer on the lifecycle
                    // worker, outside the hot device callback/state lock.
                    if (appliedOwner == OWNER_CARPLAY) {
                        try { PdcSmallStageGuard.carPlayDisconnected(); }
                        catch (Throwable t) { Log.w(TAG, "OPS presentation cleanup: " + t); }
                    }
                    appliedOwner = OWNER_NONE;
                    stopModules();
                    return;
                }

                /* FrameworkRef construction calls into stock IContext and therefore
                 * also belongs here, not on ActiveDeviceStateListener. */
                FrameworkRef next = new FrameworkRef(context);
                if (!lifecycleCurrent(generation, true, context)) return;
                int sessionOwner;
                synchronized (lock) { fwRef = next; sessionOwner = owner; }

                Log.i(TAG, "onActivate async apply generation=" + generation
                    + " owner=" + ownerName(sessionOwner) + " build=" + BUILD_ID);
                CarplayBus.getInstance().start();        /* idempotent; off stock lifecycle thread */
                /* A CarPlay -> Android Auto hand-over is an activation with no deactivate
                 * in between: release CarPlay's OPS presentation here too. */
                if (appliedOwner == OWNER_CARPLAY && sessionOwner != OWNER_CARPLAY) {
                    try { PdcSmallStageGuard.carPlayDisconnected(); }
                    catch (Throwable t) { Log.w(TAG, "OPS presentation cleanup: " + t); }
                }
                appliedOwner = sessionOwner;
                stopModules();
                if (!lifecycleCurrent(generation, true, context)) return;
                if (!startPass(generation)
                        && lifecycleCurrent(generation, true, context)) {
                    startRetry(generation);
                }
            } finally {
                synchronized (lock) {
                    /* Publish before releasing lifecycleLock: a Navigation worker
                     * waiting for this activation must not discard its edge in
                     * a gap between module startup and generation publication.
                     * External module calls remain outside this smaller lock. */
                    lifecycleAppliedGeneration = generation;
                    lock.notifyAll();
                }
            }
        }
    }

    /** Grace window to absorb a transient TMDevice deactivate bounce (Mode A) before
     * committing a full teardown; a re-activation within it drops the stale edge. */
    private static final long DEACTIVATE_DEBOUNCE_MS = 400L;

    private static void lifecycleLoop() {
        while (true) {
            int generation;
            boolean wantActive;
            IContext context;
            synchronized (lock) {
                while (lifecycleAppliedGeneration == lifecycleGeneration) {
                    try { lock.wait(); }
                    catch (InterruptedException e) { /* persistent worker */ }
                }
                generation = lifecycleGeneration;
                wantActive = active;
                context = desiredContext;

                /* Debounce a deactivate edge (Mode A): a rough reconnect/replug bounces the
                 * stock TMDevice ACTIVATING<->INVALID<->ACTIVE, publishing a transient inactive
                 * edge superseded by an active one ~ms later.  Applying it at once does a full
                 * BAPBridge takeover teardown that then has to re-init — and if a dio reconnect
                 * lands mid-teardown the session collapses.  Wait briefly; if a newer edge
                 * arrives (the re-activation), drop this stale inactive edge and coalesce to it. */
                if (!wantActive) {
                    long deadline = System.currentTimeMillis() + DEACTIVATE_DEBOUNCE_MS;
                    while (lifecycleGeneration == generation) {
                        long remaining = deadline - System.currentTimeMillis();
                        if (remaining <= 0L) break;
                        try { lock.wait(remaining); }
                        catch (InterruptedException e) { Thread.currentThread().interrupt(); break; }
                    }
                    if (lifecycleGeneration != generation) {
                        lifecycleAppliedGeneration = generation;   /* stale inactive edge — drop it */
                        lock.notifyAll();
                        continue;                                  /* re-snapshot the newer edge */
                    }
                }
            }
            try {
                applyLifecycle(generation, wantActive, context);
            } catch (Throwable t) {
                Log.e(TAG, "lifecycle apply failed generation=" + generation + ": " + t);
            }
        }
    }

    /** Navigation's OSGi service can disappear/reappear without a CarPlay reconnect.  The RGD
     * module owns a paired ServiceReference, so rebuild only that module on the exact service
     * edge instead of continuing to call a stale AppConnectorNavi instance.  ClusterService
     * invokes this from NavigationJobs; the small worker keeps teardown/start outside that
     * dispatcher and coalesces rapid remove+add pairs. */
    public static void onNavigationServiceChanged() {
        final int generation;
        final int activeLifecycleGeneration;
        final IContext activeContext;
        synchronized (lock) {
            if (!active) return;
            generation = ++serviceChangeGeneration;
            activeLifecycleGeneration = lifecycleGeneration;
            activeContext = desiredContext;
        }
        Thread t = new Thread(new Runnable() {
            public void run() {
                if (!sleepInterruptibly(50)) return;
                synchronized (lifecycleLock) {
                    synchronized (lock) {
                        if (!active || generation != serviceChangeGeneration
                                || activeLifecycleGeneration != lifecycleGeneration
                                || lifecycleAppliedGeneration
                                    != activeLifecycleGeneration) return;
                    }
                    stopRetry();
                    if (RGD_MODULE_INDEX < 0) {
                        Log.w(TAG, "rgd rebind: module missing");
                        return;
                    }
                    try { MODULES[RGD_MODULE_INDEX].stop(); }
                    catch (Throwable x) { Log.w(TAG, "rgd rebind stop: " + x); }
                    synchronized (lock) { started[RGD_MODULE_INDEX] = false; }
                    Log.i(TAG, "Navigation service changed - rebinding RGD");
                    if (!startPass(activeLifecycleGeneration)
                            && lifecycleCurrent(activeLifecycleGeneration, true,
                                                activeContext)) {
                        startRetry(activeLifecycleGeneration);
                    }
                }
            }
        }, "carplay-rgd-rebind");
        t.setDaemon(true);
        t.start();
    }

    /* start every not-yet-started module; return true when all are started. */
    private static boolean startPass(int expectedLifecycleGeneration) {
        FrameworkRef fw;
        synchronized (lock) {
            if (!active || expectedLifecycleGeneration != lifecycleGeneration
                    || fwRef == null || !fwRef.isReady()) {
                return false;
            }
            fw = fwRef;
        }

        /* Every caller holds lifecycleLock, which serializes module start/stop and
         * keeps active/fwRef stable for this pass.  Never hold the smaller state
         * lock across Module.start(): ScreenModule/RgdModule enter the static
         * ScreenNavStatusGate monitor, while NavigationJobs enters that monitor
         * before calling onNavigationServiceChanged() (which takes this lock).
         * Holding both in the old order made cold boot an ABBA deadlock. */
        boolean allUp = true;
        for (int i = 0; i < MODULES.length; i++) {
            synchronized (lock) {
                if (!active || expectedLifecycleGeneration != lifecycleGeneration
                        || fwRef != fw) return false;
                if (started[i]) continue;
                /* Not for this kind of session: counts as up, never started
                 * (stopModules() still stops it, which is idempotent). */
                if ((MODULE_OWNERS[i] & owner) == 0) {
                    started[i] = true;
                    continue;
                }
            }
            boolean ok;
            try { ok = MODULES[i].start(fw); }
            catch (Throwable t) { Log.w(TAG, MODULES[i].name() + " start threw: " + t); ok = false; }
            boolean current;
            synchronized (lock) {
                current = active && expectedLifecycleGeneration == lifecycleGeneration
                    && fwRef == fw;
                started[i] = current && ok;
            }
            if (!current) {
                /* A disconnect/reconnect was published while external start()
                 * ran.  Undo even a false-returning partial start here; the
                 * worker will apply the newer generation next. */
                try { MODULES[i].stop(); }
                catch (Throwable t) { Log.w(TAG, MODULES[i].name() + " stale-start stop: " + t); }
                return false;
            }
            if (ok) {
                Log.i(TAG, "started " + MODULES[i].name());
            } else {
                /* MODULES is an ordered dependency chain: RGI must not consume/replay a
                 * snapshot before the cluster module has reset navActive and taken cluster
                 * ownership (stock ctx 74, ready to switch to ctx 80 on the first maneuver). */
                allUp = false;
                break;
            }
        }
        if (allUp) Log.i(TAG, "all modules started (" + MODULES.length + ")");
        return allUp;
    }

    private static void startRetry(final int expectedLifecycleGeneration) {
        stopRetry();
        final int generation;
        synchronized (lock) { generation = ++retryGeneration; }
        Thread t = new Thread(new Runnable() {
            public void run() {
                int n = 0;
                try {
                    while (true) {
                        /* Fast convergence during normal startup, then a low-rate indefinite wait
                         * for late Navigation/CombiBAP publication. */
                        long delay = n < 50 ? 200L : 2000L;
                        if (!sleepInterruptibly(delay)) return;
                        synchronized (lifecycleLock) {
                            synchronized (lock) {
                                if (!active || generation != retryGeneration
                                        || expectedLifecycleGeneration != lifecycleGeneration
                                        || retryThread != Thread.currentThread()) return;
                            }
                            if (startPass(expectedLifecycleGeneration)) return;
                        }
                        n++;
                        if (n == 50) Log.w(TAG, "modules still pending after 10 s; continuing low-rate retry");
                    }
                } finally {
                    synchronized (lock) {
                        if (retryThread == Thread.currentThread()) retryThread = null;
                    }
                }
            }
        }, "carplay-retry");
        t.setDaemon(true);
        synchronized (lock) { retryThread = t; }
        t.start();
    }

    private static void stopRetry() {
        Thread t;
        synchronized (lock) { retryGeneration++; t = retryThread; retryThread = null; }
        if (t != null) t.interrupt();
    }

    private static String ownerName(int o) {
        switch (o) {
            case OWNER_CARPLAY:      return "carplay";
            case OWNER_ANDROID_AUTO: return "android-auto";
            default:                 return "none";
        }
    }

    private static boolean sleepInterruptibly(long ms) {
        try { Thread.sleep(ms); return true; }
        catch (InterruptedException e) { Thread.currentThread().interrupt(); return false; }
    }
}
