package com.luka.carplay.core;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import de.audi.app.terminalmode.IContext;

public final class CarPlayAppLifecycleTest {
    static Object stateLock;
    private static Object value(String name) throws Exception {
        Field f = CarPlayApp.class.getDeclaredField(name);
        f.setAccessible(true);
        return f.get(null);
    }
    private static void set(String name, Object value) throws Exception {
        Field f = CarPlayApp.class.getDeclaredField(name);
        f.setAccessible(true);
        f.set(null, value);
    }
    private static void check(boolean ok, String message) {
        if (!ok) throw new AssertionError(message);
    }
    private static void settled() throws Exception {
        long deadline = System.currentTimeMillis() + 5000;
        do {
            synchronized (stateLock) {
                if (value("lifecycleGeneration").equals(value("lifecycleAppliedGeneration"))) return;
            }
            Thread.sleep(5);
        } while (System.currentTimeMillis() < deadline);
        throw new AssertionError("lifecycle did not settle");
    }
    private static Thread rebindThread() {
        for (Thread t : Thread.getAllStackTraces().keySet())
            if (t.isAlive() && t.getName().equals("carplay-rgd-rebind")) return t;
        return null;
    }
    private static void awaitRebind(boolean blocked) throws Exception {
        long deadline = System.currentTimeMillis() + 5000;
        do {
            Thread t = rebindThread();
            if (blocked ? t != null && t.getState() == Thread.State.BLOCKED : t == null) return;
            Thread.sleep(5);
        } while (System.currentTimeMillis() < deadline);
        throw new AssertionError("rebind did not reach expected state blocked=" + blocked);
    }
    private static void blockLastModule(IContext context) throws Exception {
        synchronized (LifecycleTestModule.gate) {
            LifecycleTestModule.blockIndex = 2;
            LifecycleTestModule.entered = -1;
            LifecycleTestModule.released = false;
        }
        CarPlayApp.onActivate(context);
        long deadline = System.currentTimeMillis() + 5000;
        synchronized (LifecycleTestModule.gate) {
            while (LifecycleTestModule.entered != 2) {
                long remaining = deadline - System.currentTimeMillis();
                check(remaining > 0, "lifecycle worker did not enter last module");
                LifecycleTestModule.gate.wait(remaining);
            }
        }
    }
    private static void releaseModule() {
        synchronized (LifecycleTestModule.gate) {
            LifecycleTestModule.released = true;
            LifecycleTestModule.gate.notifyAll();
        }
    }
    public static void main(String[] args) throws Exception {
        stateLock = value("lock");
        IContext context = new IContext() { };
        String scenario = args[0];
        if (scenario.equals("publication")) {
            /* Exercise the release/publication boundary directly. With the old
             * split publication, this exact Navigation edge is discarded. */
            synchronized (stateLock) {
                set("active", Boolean.TRUE); set("owner", Integer.valueOf(CarPlayApp.OWNER_CARPLAY));
                set("desiredContext", context);
                set("lifecycleGeneration", Integer.valueOf(1));
            }
            Method apply = CarPlayApp.class.getDeclaredMethod("applyLifecycle",
                int.class, boolean.class, IContext.class);
            apply.setAccessible(true);
            apply.invoke(null, Integer.valueOf(1), Boolean.TRUE, context);
            CarPlayApp.onNavigationServiceChanged();
            awaitRebind(false);
            check(LifecycleTestModule.count(1) == 2, "Navigation edge lost after lifecycle release");
        } else if (scenario.equals("during-start") || scenario.equals("replug")) {
            blockLastModule(context);
            CarPlayApp.onNavigationServiceChanged();
            awaitRebind(true); /* real worker waits on real lifecycle monitor */
            if (scenario.equals("replug")) {
                CarPlayApp.onDeactivate();
                CarPlayApp.onActivate(new IContext() { });
            }
            releaseModule(); settled(); awaitRebind(false);
            int expectedOther = scenario.equals("replug") ? 2 : 1;
            check(LifecycleTestModule.count(0) == expectedOther, "unexpected screen-module restart");
            check(LifecycleTestModule.count(1) == 2, "missing rebind or stale generation rebind");
            check(LifecycleTestModule.count(2) == expectedOther, "unexpected input restart");
        } else if (scenario.equals("failure")) {
            FrameworkRef.failNext = true;
            CarPlayApp.onActivate(context); settled();
            check(LifecycleTestModule.count(0) == 0, "injected constructor failure ignored");
            CarPlayApp.onDeactivateAndWait();
            CarPlayApp.onActivate(context); settled();
            check(LifecycleTestModule.count(1) == 1, "worker failed to recover after exception");
        } else if (scenario.equals("owners")) {
            /* CarPlay session: every module, CarPlay-only input included. */
            CarPlayApp.onActivate(context); settled();
            check(CarPlayApp.isActive() && !CarPlayApp.isAndroidAutoActive(), "CarPlay owner");
            check(CarPlayApp.sessionOwner() == CarPlayApp.OWNER_CARPLAY, "CarPlay owner id");
            check(LifecycleTestModule.count(0) == 1 && LifecycleTestModule.count(2) == 1, "CarPlay modules");
            /* Phone swap straight to Android Auto: shared modules restart, the CarPlay-only
             * input does not, CarPlay callers see no CarPlay session, and CarPlay's OPS
             * presentation is released although no deactivate came in between. */
            IContext aa = new IContext() { };
            CarPlayApp.onActivateAndroidAuto(aa); settled();
            check(!CarPlayApp.isActive() && CarPlayApp.isAndroidAutoActive() && CarPlayApp.isSessionActive(),
                "Android Auto owner");
            check(LifecycleTestModule.count(0) == 2 && LifecycleTestModule.count(1) == 2
                && LifecycleTestModule.count(3) == 2, "shared modules not restarted for Android Auto");
            check(LifecycleTestModule.count(2) == 1, "CarPlay-only input started for Android Auto");
            check(com.luka.carplay.pdc.PdcSmallStageGuard.disconnects == 1, "OPS not released on hand-over");
            /* A late CarPlay disconnect must not end the Android Auto session. */
            CarPlayApp.onDeactivate(); Thread.sleep(500); settled();
            check(CarPlayApp.isAndroidAutoActive() && LifecycleTestModule.count(0) == 2, "stale CarPlay deactivate");
            /* Repeated ACTIVATING of the same Android Auto device is not a restart. */
            CarPlayApp.onActivateAndroidAuto(aa); settled();
            check(LifecycleTestModule.count(0) == 2, "repeated Android Auto activation restarted modules");
            /* Android Auto gone: no CarPlay OPS cleanup for an Android session. */
            CarPlayApp.onDeactivateAndroidAuto(); Thread.sleep(500); settled();
            check(!CarPlayApp.isSessionActive() && CarPlayApp.sessionOwner() == CarPlayApp.OWNER_NONE,
                "Android Auto session not ended");
            check(com.luka.carplay.pdc.PdcSmallStageGuard.disconnects == 1, "OPS cleanup for Android Auto");
        } else if (scenario.equals("bounce")) {
            CarPlayApp.onActivate(context); settled();
            CarPlayApp.onDeactivate(); Thread.sleep(25); CarPlayApp.onActivate(context); settled();
            for (int i = 0; i < 3; ++i)
                check(LifecycleTestModule.count(i) == 2, "existing debounce restart behavior changed");
        } else throw new AssertionError("unknown scenario");
        CarPlayApp.onDeactivateAndWait(); settled();
        check(!CarPlayApp.isActive(), "disconnect remained active");
        check(!LifecycleTestModule.badLockOrder, "module ran under small state lock");
        System.out.println("CarPlayAppLifecycleTest: " + scenario + " PASS");
    }
}
