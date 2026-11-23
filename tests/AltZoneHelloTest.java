package com.luka.carplay.cluster;

import com.luka.carplay.bus.CarplayBus;

import java.io.DataInputStream;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.net.Socket;

/**
 * Issue #68: a new phone session's hook (bus HELLO) gets the cockpit layout at once, before
 * the phone reads /info, so initialViewArea is the visible map's view. Runs the shipping
 * ClusterLayerController against the real CarplayBus on a loopback peer.
 */
public final class AltZoneHelloTest {
    private static int failures;

    private static void check(boolean ok, String why) {
        if (!ok) { System.out.println("FAIL: " + why); failures++; }
    }

    private static Field field(String name) throws Exception {
        Field f = ClusterLayerController.class.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }

    private static void set(String name, Object value) throws Exception { field(name).set(null, value); }

    private static Object geometry(int smallStageDX) throws Exception {
        Class g = Class.forName("com.luka.carplay.cluster.ClusterLayerController$Geometry");
        Constructor c = g.getDeclaredConstructors()[0];
        c.setAccessible(true);
        int[] v = {984, 139, 1091, 110, 59, 27, 210, 153, 0, 0, 328, 180, smallStageDX, 0, 0, 26};
        Object[] args = new Object[v.length + 1];
        for (int i = 0; i < v.length; ++i) args[i] = Integer.valueOf(v[i]);
        args[v.length] = smallStageDX != 0 ? "LayoutTestSport" : "LayoutTest";
        return c.newInstance(args);
    }

    private static void state(boolean haveVcStage, boolean vcPopup, boolean stockPopup,
                              boolean haveLayout, int smallStageDX) throws Exception {
        set("haveVcStage", Boolean.valueOf(haveVcStage));
        set("vcPopup", Boolean.valueOf(vcPopup));
        set("lastStockPopup", Boolean.valueOf(stockPopup));
        set("haveLayout", Boolean.valueOf(haveLayout));
        set("lastGeometry", geometry(smallStageDX));
    }

    /** Next outbound frame: its type and first payload byte. */
    private static int[] frame(DataInputStream in) throws Exception {
        check(in.readInt() == CarplayBus.MAGIC, "magic");
        in.readInt();
        int type = in.readUnsignedShort();
        in.readUnsignedByte();
        in.readByte();
        int len = in.readInt();
        byte[] body = new byte[len];
        in.readFully(body);
        return new int[] {type, len > 0 ? body[0] : -1, len};
    }

    private static void expectZone(DataInputStream in, int zone, String why) throws Exception {
        ClusterLayerController.sendAltViewAreaToNewHook();
        int[] f = frame(in);
        check(f[0] == CarplayBus.CMD_ALT_ZONE && f[2] == 3 && f[1] == zone,
              why + ": CMD_ALT_ZONE " + zone + " (got type 0x" + Integer.toHexString(f[0]) + " zone " + f[1] + ")");
    }

    public static void main(String[] args) throws Exception {
        CarplayBus bus = CarplayBus.getInstance();
        bus.start();
        Socket peer = null;
        for (int i = 0; i < 300 && peer == null; ++i) {
            try { peer = new Socket("127.0.0.1", CarplayBus.PORT); }
            catch (java.io.IOException e) { Thread.sleep(10); }
        }
        check(peer != null, "bus listening");
        for (int i = 0; i < 300 && !bus.isConnected(); ++i) Thread.sleep(10);
        check(bus.isConnected(), "bus connected");
        peer.setSoTimeout(3000);
        DataInputStream in = new DataInputStream(peer.getInputStream());

        state(true, true, false, true, 0);
        expectZone(in, 0, "VC large map");
        state(true, false, true, true, 0);
        expectZone(in, 2, "VC small map, classic skin");
        state(true, false, true, true, -476);
        expectZone(in, 1, "VC small map, Sport skin (small-stage offset)");
        state(false, true, false, true, -476);
        expectZone(in, 1, "no FctID 54 yet: the stock stage decides");
        state(true, true, false, false, -476);
        expectZone(in, 0, "large map without the stock layout");

        /* Small map without the stock layout: the skin is unknown, nothing is sent (the hook
         * keeps wide). The marker frame proves no zone went out before it. */
        state(true, false, true, false, 0);
        ClusterLayerController.sendAltViewAreaToNewHook();
        check(bus.sendBinary(CarplayBus.CMD_ALT_ZOOM, new byte[] {9}), "marker sent");
        int[] f = frame(in);
        check(f[0] == CarplayBus.CMD_ALT_ZOOM && f[1] == 9, "unknown skin: no zone before the marker");

        /* The per-video-session resend still happens: HELLO does not mark the zone sent. */
        check(((Integer) field("lastZoneSent").get(null)).intValue() == -1, "video start still sends the zone");

        peer.close();
        bus.stop();
        if (failures > 0) {
            System.out.println("AltZoneHelloTest: " + failures + " failure(s)");
            System.exit(1);
        }
        System.out.println("AltZoneHelloTest: wide/classic/Sport zone at bus HELLO, stock-stage fallback, "
            + "unknown skin left at wide, video-start resend kept PASS");
    }
}
