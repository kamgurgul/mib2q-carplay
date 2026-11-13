package com.luka.carplay.core;

import java.lang.reflect.Field;

/**
 * Long roller press: phone map <-> Audi map. Picking the Audi map needs live phone video;
 * going back does not, because with the pause option the hidden phone map is no longer
 * live (the hook stopped its stream) and this press is what restarts it.
 */
public final class ScreenLongPressTest {
    static void check(boolean ok, String why) { if (!ok) throw new AssertionError(why); }

    static void set(String name, boolean value) throws Exception {
        Field f = ScreenModule.class.getDeclaredField(name);
        f.setAccessible(true);
        f.setBoolean(null, value);
    }

    static boolean get(String name) throws Exception {
        Field f = ScreenModule.class.getDeclaredField(name);
        f.setAccessible(true);
        return f.getBoolean(null);
    }

    public static void main(String[] args) throws Exception {
        set("connected", true);

        /* No live video, phone map selected: the press is ignored (native-nav start-up). */
        set("altScreenActive", false);
        set("userStockMap", false);
        ScreenModule.onSteeringWheelLongPress();
        check(!get("userStockMap"), "Audi map picked without live phone video");

        /* Live video: the press picks the Audi map. */
        set("altScreenActive", true);
        ScreenModule.onSteeringWheelLongPress();
        check(get("userStockMap"), "long press did not pick the Audi map");

        /* The pause option stopped the hidden stream: video is not live any more. The next
         * press must still bring the phone map back. */
        set("altScreenActive", false);
        ScreenModule.onSteeringWheelLongPress();
        check(!get("userStockMap"), "stuck on the Audi map once the paused video was not live");

        /* Not connected: nothing changes. */
        set("connected", false);
        set("altScreenActive", true);
        ScreenModule.onSteeringWheelLongPress();
        check(!get("userStockMap"), "long press acted without a session");
        System.out.println("ScreenLongPressTest: Audi map needs live video, return to the phone "
            + "map works while paused, no session = ignored PASS");
    }
}
