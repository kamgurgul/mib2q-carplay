package com.luka.carplay.aa;

import com.luka.carplay.bus.CarplayBus;
import com.luka.carplay.rgd.RouteGuidance;
import com.luka.carplay.rgd.BAPBridge;
import com.luka.carplay.framework.Log;
import de.audi.atip.interapp.combi.bap.navi.CombiBAPServiceNavi;
import de.audi.atip.interapp.combi.bap.navi.data.CombiBAPDestinationInfo;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.lang.reflect.InvocationHandler;
import java.nio.file.Files;
import java.nio.file.Paths;

/** Native parser fixture -> actual Java reader/state/parser/BAP trip outputs. */
public final class AaTripDetailsTest {
    private static int checks;
    private static void check(boolean ok, String why) {
        checks++; if (!ok) throw new AssertionError(why);
    }
    private static Object get(Object o, String name) throws Exception {
        Field f = o.getClass().getDeclaredField(name); f.setAccessible(true); return f.get(o);
    }
    private static void set(Object o, String name, Object value) throws Exception {
        Field f = o.getClass().getDeclaredField(name); f.setAccessible(true); f.set(o, value);
    }
    private static CarplayBus.Data frame(AaRgState rg) throws Exception {
        byte[] b = rg.snapshot().getBytes("UTF-8"); return CarplayBus.parseText(b, b.length);
    }
    private static AaRgState route() {
        AaRgState rg = new AaRgState();
        rg.onTurn("Next road", 1, AaManeuverMap.EV_TURN, 0, 0, 1);
        return rg;
    }
    private static void checksum(byte[] b) {
        int h = 0x811c9dc5;
        for (int i = 0; i < 316; i++) h = (h ^ (b[i] & 255)) * 16777619;
        for (int i = 0; i < 4; i++) b[316 + i] = (byte)(h >>> (24 - i * 8));
    }
    private static void records(String path) throws Exception {
        byte[] b = Files.readAllBytes(Paths.get(path));
        AaTripFeed.Trip t = AaTripFeed.read(path);
        check(t != null && t.meters == 12000 && t.seconds == 900, "native fixture values");
        check("Kraków: Dworzec Główny".equals(t.destination), "native UTF-8 destination");
        check(t.stateMs == 100500 && t.positionMs == 100500, "native sample times (estimate refreshes name)");
        for (int i = 0; i < b.length; i++) {
            b[i] ^= 1; check(AaTripFeed.parse(b) == null, "torn/corrupt byte " + i); b[i] ^= 1;
        }
        check(AaTripFeed.parse(new byte[319]) == null && AaTripFeed.parse(null) == null, "short/missing record");
        byte[] invalid = b.clone(); invalid[32] = (byte)0xff; checksum(invalid);
        check(AaTripFeed.parse(invalid) == null, "invalid UTF-8");
        invalid = b.clone(); java.util.Arrays.fill(invalid, 32, 288, (byte)'x'); checksum(invalid);
        check(AaTripFeed.parse(invalid) == null, "unterminated name");
        invalid = b.clone(); invalid[288] = 1; checksum(invalid);
        check(AaTripFeed.parse(invalid) == null, "unknown record extension");
        check(AaTripFeed.read(path + ".missing") == null, "missing file");
    }

    private static void lifecycle(AaTripFeed.Trip t) throws Exception {
        final long utc = 1793448000000L;
        AaRgState rg = new AaRgState();
        check(!rg.onTrip(t, 101500, utc) && !rg.isRouteActive(), "trip metadata starts navigation");
        rg = route();
        check(rg.onTrip(t, 101500, utc), "initial trip");
        CarplayBus.Data d = frame(rg);
        long eta = utc / 1000 + 899; // sample is already one second old
        check(d.num64("eta_seconds", -1) == eta, "ETA used system clock / ignored sample age");
        check(d.num("dist_dest_m", -1) == 12000 && d.num("time_remaining_seconds", -1) == 900, "trip numbers");
        check(!rg.onTrip(t, 105000, utc + 3500), "replayed sample changed ETA");
        check(!rg.onTrip(null, 105000, utc + 3500), "temporary failed read clears fresh data");
        rg.onTurn("Another road", 2, AaManeuverMap.EV_TURN, 0, 0, 1);
        check(frame(rg).num64("eta_seconds", -1) == eta, "next maneuver lost the trip");
        // Same numeric estimate at a later sample must move the ETA forward.
        AaTripFeed.Trip fresh = new AaTripFeed.Trip(t.destination, 12000, 900, 106000, 106000);
        check(rg.onTrip(fresh, 106000, utc + 4500), "fresh identical duration ignored");
        check(frame(rg).num64("eta_seconds", -1) == utc / 1000 + 904, "fresh sample ETA");
        // State remains fresh, but must not refresh an old distance/time sample.
        fresh = new AaTripFeed.Trip(t.destination, 12000, 900, 137000, 106000);
        check(rg.onTrip(fresh, 137000, utc + 35500), "old estimate not expired");
        d = frame(rg);
        check(t.destination.equals(d.str("destination")) && d.num("dist_dest_m", 0) == -1
            && d.num64("eta_seconds", 0) == -1, "independent field expiry");
        check(rg.onTrip(null, 168001, utc + 66501) && frame(rg).str("destination").length() == 0, "stale name");
        check(rg.onTrip(new AaTripFeed.Trip("New stop", -1, -1, 168002, 0), 168002, utc), "new destination");
        check(frame(rg).num("time_remaining_seconds", 0) == -1, "new destination kept old duration");
        rg.endRoute("cancel");
        check(frame(rg).str("destination").length() == 0 && frame(rg).num("dist_dest_m", 0) == -1, "route clear");
        rg.onTurn("New route", 1, AaManeuverMap.EV_TURN, 0, 0, 1);
        check(!rg.onTrip(t, 101500, utc), "old file revived after cancellation");
        long now = System.currentTimeMillis() + 1;
        fresh = new AaTripFeed.Trip("Fresh route", 0, 0, now, now);
        check(rg.onTrip(fresh, now, utc) && frame(rg).num("dist_dest_m", -1) == 0, "zero-distance arrival");
        rg.resetSession(); rg.onTurn("Reconnected", 1, AaManeuverMap.EV_TURN, 0, 0, 1);
        check(!rg.onTrip(t, 101500, utc) && frame(rg).num64("eta_seconds", 0) == -1, "reconnect replay");
        check(!AaTripFeed.fresh(101, 100, 0), "future sample accepted after clock rollback");
    }

    private static void bap(AaTripFeed.Trip t) throws Exception {
        AaRgState aa = route(); aa.onTrip(t, 101500, 1793448000000L);
        RouteGuidance rg = new RouteGuidance();
        Method parse = RouteGuidance.class.getDeclaredMethod("parse", CarplayBus.Data.class); parse.setAccessible(true);
        parse.invoke(rg, frame(aa));
        RouteGuidance.State s = (RouteGuidance.State)get(rg, "state");
        check(s.distDestM == 12000 && s.etaSeconds == 1793448899, "RouteGuidance trip parsing");
        final Object[][] outputs = new Object[3][];
        CombiBAPServiceNavi service = (CombiBAPServiceNavi)Proxy.newProxyInstance(
            AaTripDetailsTest.class.getClassLoader(), new Class[]{CombiBAPServiceNavi.class}, new InvocationHandler() {
                public Object invoke(Object p, Method m, Object[] args) {
                    if (m.getName().equals("updateDistanceToDestination")) outputs[0] = args;
                    if (m.getName().equals("updateTimeToDestination")) outputs[1] = args;
                    if (m.getName().equals("updateDestinationInfo")) outputs[2] = args;
                    return null;
                }
            });
        BAPBridge b = new BAPBridge();
        set(b, "initialized", Boolean.TRUE); set(b, "appConnectorNavi", service);
        // Only the trip fields are dirty; no renderer or HU lifecycle is started.
        s.dirtyMask = RouteGuidance.State.DIRTY_DESTINATION | RouteGuidance.State.DIRTY_DIST_DEST
            | RouteGuidance.State.DIRTY_ETA | RouteGuidance.State.DIRTY_TIME_REMAINING;
        b.update(s);
        check(outputs[0] != null && outputs[1] != null && outputs[2] != null, "trip did not reach BAP outputs");
        check(((Number)outputs[0][0]).intValue() > 0, "BAP destination distance is invalid");
        check(t.destination.equals(((CombiBAPDestinationInfo)outputs[2][0]).getDestination().getCity()),
            "BAP destination text");
        check(((Integer)outputs[1][0]).intValue() == 1 && ((Long)outputs[1][2]).longValue() == 1793448899L,
            "BAP arrival clock");
        s.clearDirty(); parse.invoke(rg, frame(aa));
        check(s.dirtyMask == 0, "sticky replay dirtied trip/route");
        aa.endRoute("cancel"); parse.invoke(rg, frame(aa));
        check(s.distDestM == -1 && s.etaSeconds == -1 && "".equals(s.destination), "shared state retained canceled trip");
    }

    public static void main(String[] args) throws Exception {
        Log.setLevel(-1);
        records(args[0]);
        AaTripFeed.Trip t = AaTripFeed.read(args[0]);
        lifecycle(t); bap(t);
        System.out.println("AaTripDetailsTest: " + checks + " checks PASS (native record, UTF-8, clocks, expiry, replay, lifecycle, BAP)");
    }
}
