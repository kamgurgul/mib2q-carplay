package com.luka.carplay.core;

import com.luka.carplay.framework.Log;
import de.audi.app.terminalmode.IContext;
import de.audi.atip.base.IFrameworkAccess;
import de.audi.atip.hmi.HMIService;
import de.audi.atip.hmi.view.IDisplayManager;
import java.io.File;
import java.lang.reflect.*;

/**
 * Long roller press: phone map <-> Audi map. Picking the Audi map needs live phone video;
 * going back does not, because with the pause option the hidden phone map is no longer
 * live (the hook stopped its stream) and this press is what restarts it.
 *
 * White-box: relies by reflection on ScreenModule.mapPreference / desiredCtx / LOCK /
 * phoneMapHiddenLocked / platformSupported / altScreenActive and NavReadyMarker.started.
 * Update this test when any of them is renamed.
 */
public final class ScreenLongPressTest {
    static void check(boolean ok, String why) { if (!ok) throw new AssertionError(why); }

    static void set(String name, boolean value) throws Exception {
        Field f = ScreenModule.class.getDeclaredField(name);
        f.setAccessible(true);
        f.setBoolean(null, value);
    }

    static void preference(ClusterMapPreference preference) throws Exception {
        Field f = ScreenModule.class.getDeclaredField("mapPreference");
        f.setAccessible(true);
        f.set(null, preference);
    }

    static void context(int expected) throws Exception {
        Field f = ScreenModule.class.getDeclaredField("desiredCtx");
        f.setAccessible(true);
        check(f.getInt(null) == expected, "expected context " + expected + ", got " + f.getInt(null));
    }

    static boolean hidden() throws Exception {
        Method m = ScreenModule.class.getDeclaredMethod("phoneMapHiddenLocked");
        m.setAccessible(true);
        Field lock = ScreenModule.class.getDeclaredField("LOCK"); lock.setAccessible(true);
        synchronized (lock.get(null)) { return (Boolean) m.invoke(null); }
    }

    static <T> T proxy(Class<T> type, String getter, Object result) {
        return type.cast(Proxy.newProxyInstance(type.getClassLoader(), new Class<?>[]{type},
            (p, method, args) -> {
                if (method.getName().equals(getter)) return result;
                if (method.getReturnType() == Integer.TYPE) return 0;
                if (method.getReturnType() == Boolean.TYPE) return false;
                if (method.getReturnType() == Long.TYPE) return 0L;
                return null;
            }));
    }

    static FrameworkRef framework() {
        IDisplayManager dm = proxy(IDisplayManager.class, "", null);
        HMIService hmi = proxy(HMIService.class, "getDisplayManager", dm);
        IFrameworkAccess fw = proxy(IFrameworkAccess.class, "getHMIService", hmi);
        return new FrameworkRef(proxy(IContext.class, "getFramework", fw));
    }

    public static void main(String[] args) throws Exception {
        Log.setLevel(-1);
        File file = new File(args[0], "cluster_map_source");
        file.getParentFile().mkdirs();
        check(file.getParentFile().isDirectory() && !file.exists(), "fresh test directory");
        ClusterMapPreference p = new ClusterMapPreference(file);
        preference(p);
        // Exercise the actual start/stop lifecycle without the unit's navigation watcher.
        Field watching = NavReadyMarker.class.getDeclaredField("started");
        watching.setAccessible(true); watching.setBoolean(null, true);
        ScreenModule screen = new ScreenModule();
        FrameworkRef fw = framework();
        check(screen.start(fw), "screen start");

        /* No live video, phone map selected: the press is ignored (native-nav start-up). */
        ScreenModule.onSteeringWheelLongPress();
        check(!p.isStockMap() && !file.exists(), "Audi map picked/saved without live phone video");

        /* Live video: the press picks the Audi map. */
        ScreenModule.setAltScreenActive(true); context(ScreenModule.CTX_CLUSTER_VIDEO);
        ScreenModule.onSteeringWheelLongPress();
        check(p.isStockMap() && !ScreenModule.isAltScreenShown(), "long press did not pick the Audi map");
        context(ScreenModule.CTX_STOCK_CLUSTER); check(hidden(), "Audi choice did not hide phone stream");
        // Reconnect immediately, even if the disk write is still pending.
        screen.stop(); check(p.isStockMap() && !hidden(), "disconnect reset map preference");
        check(screen.start(fw) && p.isStockMap(), "reconnect reset map preference");
        ClusterMapPreferenceTest.saved(p);

        // Simulate a fresh HMI process loading only the committed preference.
        screen.stop();
        p = new ClusterMapPreference(file); preference(p);
        check(screen.start(fw) && p.isStockMap(), "restart lost Audi map preference");
        check(hidden(), "restored Audi choice must pause the phone stream from session start");
        ScreenModule.setAltScreenActive(true); context(ScreenModule.CTX_STOCK_CLUSTER);
        ScreenModule.setNavActive(true); context(ScreenModule.CTX_CLUSTER);

        /* The pause option stopped the hidden stream: video is not live any more. The next
         * press must still bring the phone map back. */
        ScreenModule.setAltScreenActive(false);
        ScreenModule.onSteeringWheelLongPress();
        check(!p.isStockMap() && !hidden(), "stuck on Audi map once the paused video was not live");
        ScreenModule.setAltScreenActive(true); context(ScreenModule.CTX_CLUSTER_VIDEO_NAV);
        ScreenModule.setNavActive(false); context(ScreenModule.CTX_CLUSTER_VIDEO);
        ScreenModule.setAltScreenActive(false); context(ScreenModule.CTX_STOCK_CLUSTER);
        check(!p.isStockMap(), "video fallback replaced phone preference");
        ScreenModule.setAltScreenActive(true); context(ScreenModule.CTX_CLUSTER_VIDEO);
        ClusterMapPreferenceTest.saved(p);
        screen.stop();
        p = new ClusterMapPreference(file); preference(p);
        check(screen.start(fw) && !p.isStockMap(), "restart lost phone map preference");
        ScreenModule.setAltScreenActive(true); context(ScreenModule.CTX_CLUSTER_VIDEO);

        set("platformSupported", false);
        ScreenModule.onSteeringWheelLongPress();
        check(!p.isStockMap(), "unsupported cluster changed preference");
        set("platformSupported", true);

        /* Not connected: nothing changes. */
        screen.stop();
        set("altScreenActive", true);
        ScreenModule.onSteeringWheelLongPress();
        check(!p.isStockMap(), "long press acted without a session");
        check(!new ClusterMapPreference(file).isStockMap(), "ignored press changed saved choice");
        System.out.println("ScreenLongPressTest: persisted Audi/phone choices, reconnect, restart, paused "
            + "stream resume, video fallback and ignored presses PASS");
    }
}
