package com.luka.carplay.aa;

import com.luka.carplay.bus.CarplayBus;

/*
 * Android Auto adapter, against the shipping jar: an Android Auto event sequence through
 * AaRgState, read back with the real CarplayBus text parser that RouteGuidance uses; the lane
 * record; the cockpit view mapping; the VC map-view gate.
 */
public final class AaRouteStateTest {
    private static int checks;

    private static void check(boolean ok, String what) {
        checks++;
        if (!ok) throw new AssertionError(what);
    }

    private static CarplayBus.Data frame(AaRgState rg) throws Exception {
        byte[] b = rg.snapshot().getBytes("UTF-8");
        CarplayBus.Data d = CarplayBus.parseText(b, b.length);
        check(d != null && !d.overflowed(), "snapshot does not parse");
        return d;
    }

    private static void route(AaRgState rg) throws Exception {
        CarplayBus.Data d;
        /* Connect / free drive: Google Maps sends "depart, 0 m" with no route. */
        check(!rg.onTurn("Pacific Center Blvd", 3, AaManeuverMap.EV_DEPART, 0, 0, 1), "depart started a route");
        check(!rg.onDistance(0, 0, 1), "0 m distance started a route");
        check(!rg.isRouteActive(), "route active without a turn");
        d = frame(rg);
        check(d.num("route_state", -1) == 0 && d.num("maneuver_count", -1) == 0, "idle frame");

        /* First real turn starts route generation 1, before any distance. */
        check(rg.onTurn("Main St", 1, AaManeuverMap.EV_TURN, 0, 0, 1), "turn not published");
        d = frame(rg);
        check(d.num("route_state", -1) == 1 && d.num("visible_in_app", -1) == 1, "route active frame");
        check(d.num("route_generation", -1) == 1 && d.num("m0_ver", -1) == 1, "generation/version");
        check(d.num("m0_type", -1) == AaManeuverMap.MT_LEFT_TURN && d.num("m0_turn_angle", 0) == -90, "left turn");
        check("Main St".equals(d.str("m0_after_road")), "road");
        check(d.num("dist_maneuver_m", 0) == -1 && !d.has("m0_distance"), "distance before the first report");

        /* Distances: the first positive one is the step length (bargraph denominator). */
        check(rg.onDistance(500, 40, 1), "distance 500");
        check(rg.onDistance(120, 10, 1), "distance 120");
        check(!rg.onDistance(120, 9, 1), "unchanged distance republished");
        d = frame(rg);
        check(d.num("dist_maneuver_m", 0) == 120 && d.num("m0_distance", 0) == 500, "distance/step");

        /* Transient "depart toward X" ~0.5 s before the next turn is held, not shown. */
        check(!rg.onTurn("toward Goodyear St", 3, AaManeuverMap.EV_DEPART, 0, 0, 1), "transient depart shown");
        check(frame(rg).num("m0_type", -1) == AaManeuverMap.MT_LEFT_TURN, "transient depart replaced the turn");

        /* Next turn replaces the held one: new version, distance unknown until reported. */
        check(rg.onTurn("Oak Ave", 2, AaManeuverMap.EV_TURN, 0, 0, 1), "second turn");
        d = frame(rg);
        check(d.num("m0_ver", -1) == 2 && d.num("m0_type", -1) == AaManeuverMap.MT_RIGHT_TURN, "second turn frame");
        check(d.num("dist_maneuver_m", 0) == -1, "previous turn's distance carried over");

        /* Roundabout, right-hand traffic (counter-clockwise), exit 2 straight across. */
        check(rg.onTurn("Ring Rd", 2, AaManeuverMap.EV_ROUNDABOUT_ENTER_AND_EXIT, 180, 2, 1), "roundabout");
        d = frame(rg);
        check(d.num("m0_type", -1) == AaManeuverMap.MT_ROUNDABOUT_EXIT_1 + 1, "roundabout exit 2");
        check(d.num("m0_junction_type", -1) == AaManeuverMap.JUNCTION_ROUNDABOUT
            && d.num("m0_turn_angle", 99) == 0 && d.num("m0_driving_side", -1) == 0, "roundabout geometry");

        /* Lanes from the gal hook record: straight (not highlighted), right (highlighted). */
        String rec = "SQ5L1 3\nlanes=2\n1,0|5,1\nend=3\n";
        AaLaneFeed.Lanes lanes = AaLaneFeed.parse(rec.getBytes("US-ASCII"), rec.length());
        check(lanes != null && lanes.count == 2, "lane record");
        check(rg.onLanes(lanes) && !rg.onLanes(lanes), "lane change detection");
        d = frame(rg);
        check(d.num("lane_guidance_showing", 0) == 1 && d.num("lg0_lane_count", 0) == 2, "lanes shown");
        check("1000,90".equals(d.str("lg0_lane_directions")) && "0,2".equals(d.str("lg0_lane_status"))
            && "0|90".equals(d.str("lg0_lane_angles")), "lane directions/status/angles");

        /* Guidance end (valid == 2): inactive frame with explicit empty list, lanes cleared. */
        check(rg.onTurn("", 3, 0, 0, 0, AaRgState.NO_GUIDANCE), "end of guidance");
        d = frame(rg);
        check(d.num("route_state", -1) == 0 && "".equals(d.str("maneuver_list")), "route end frame");
        check(d.num("lane_guidance_showing", -1) == 0, "lanes not cleared at route end");
        check(!rg.onLanes(lanes), "lanes accepted without a route");

        /* The next route is a new generation; native focus ends it. */
        check(rg.onTurn("Elm St", 1, AaManeuverMap.EV_SHARP_TURN, 0, 0, 1), "next route");
        check(frame(rg).num("route_generation", -1) == 2, "next route generation");
        check(rg.onNavFocus(AaRgState.NAVFOCUS_NATIVE, 1) && !rg.isRouteActive(), "native focus ends the route");
    }

    /* Car log 020/021: Google starts a route parked, with DEPART at 0 m and no distance > 0. */
    private static void parkedStart() throws Exception {
        AaRgState rg = new AaRgState();
        String road = "w kierunku: Marii Konopnickiej";
        /* free drive before the route: the depart stays held */
        check(!rg.onTurn(road, 3, AaManeuverMap.EV_DEPART, 0, 0, 1) && !rg.onDistance(0, 0, 1), "idle depart shown");
        /* navigation focus PROJECTED = a real route: the held depart is shown now */
        check(rg.onNavFocus(AaRgState.NAVFOCUS_PROJECTED, 1) && rg.isRouteActive(), "depart not shown on focus");
        CarplayBus.Data d = frame(rg);
        check(d.num("route_state", -1) == 1 && d.num("m0_type", -1) == AaManeuverMap.MT_START_ROUTE, "depart frame");
        check(road.equals(d.str("m0_after_road")), "depart road");
        check(rg.onDistance(0, 0, 1) && frame(rg).num("dist_maneuver_m", -9) == 0, "0 m distance on the route");
        /* order reversed: focus first, then the depart event */
        AaRgState r2 = new AaRgState();
        check(!r2.onNavFocus(AaRgState.NAVFOCUS_PROJECTED, 1), "focus without maneuver started a route");
        check(r2.onTurn(road, 3, AaManeuverMap.EV_DEPART, 0, 0, 1) && r2.isRouteActive(), "depart after focus");
        /* mid-route transient depart is still held */
        check(r2.onTurn("Oak", 2, AaManeuverMap.EV_TURN, 0, 0, 1), "turn");
        check(!r2.onTurn("toward X", 3, AaManeuverMap.EV_DEPART, 0, 0, 1), "mid-route depart shown");
        /* native focus ends it and clears the projected state */
        check(r2.onNavFocus(AaRgState.NAVFOCUS_NATIVE, 1) && !r2.isRouteActive(), "native focus");
        check(!r2.onTurn(road, 3, AaManeuverMap.EV_DEPART, 0, 0, 1), "depart shown after native focus");
    }

    private static void laneRecords() throws Exception {
        String torn = "SQ5L1 3\nlanes=1\n1,0\nend=4\n", empty = "SQ5L1 3\nlanes=0\n\nend=3\n";
        check(AaLaneFeed.parse(torn.getBytes("US-ASCII"), torn.length()) == null, "torn record");
        check(AaLaneFeed.parse(empty.getBytes("US-ASCII"), empty.length()) == AaLaneFeed.NONE, "empty record");
        check(AaLaneFeed.angle(8) == -180 && AaLaneFeed.angle(0) == 1000, "lane angles");
    }

    private static void views() {
        check(AaClusterView.viewFor(true, "LayoutMIB2HighB9Sport") == AaClusterView.FULL, "large map = full");
        check(AaClusterView.viewFor(false, "LayoutMIB2HighB9Sport") == AaClusterView.SPORT, "sport");
        check(AaClusterView.viewFor(false, "LayoutMIB2HighB9") == AaClusterView.CLASSIC, "classic");
        check(AaClusterView.record(AaClusterView.SPORT).length() == 8
            && AaClusterView.record(AaClusterView.FULL).length() == 8
            && AaClusterView.record(AaClusterView.CLASSIC).length() == 8, "fixed 8-byte records");
    }

    private static void gate() {
        final int[] forwards = new int[1];
        final boolean[] last = new boolean[1];
        /* Like ClusterGalSink: the scheduled forward applies takeEffective(). */
        VcMapViewGate.Sink sink = new VcMapViewGate.Sink() {
            public void forwardLater() { forwards[0]++; last[0] = VcMapViewGate.takeEffective(); }
        };
        VcMapViewGate.resetForTest();
        /* Outside the video contexts stock passes through. */
        check(VcMapViewGate.onStockGalState(sink, true), "stock true outside video");
        /* Video shown: re-forward false (InfoStates 6 would hide plane 99). */
        VcMapViewGate.onClusterVideoShown(true);
        check(forwards[0] == 1 && !last[0], "video shown -> false");
        check(!VcMapViewGate.onStockGalState(sink, true), "stock true suppressed while video shown");
        check(!VcMapViewGate.onStockGalState(sink, false), "stock false stays false");
        /* Video left with stock false: nothing to re-forward. */
        VcMapViewGate.onClusterVideoShown(false);
        check(forwards[0] == 1, "needless re-forward");
        /* Stock true again, video shown and left: false, then stock true restored. */
        check(VcMapViewGate.onStockGalState(sink, true), "stock true outside video (2)");
        VcMapViewGate.onClusterVideoShown(true);
        check(forwards[0] == 2 && !last[0], "video shown again -> false");
        VcMapViewGate.onClusterVideoShown(false);
        check(forwards[0] == 3 && last[0], "stock value restored when video leaves");
        VcMapViewGate.resetForTest();
    }

    public static void main(String[] args) throws Exception {
        route(new AaRgState());
        parkedStart();
        laneRecords();
        views();
        gate();
        System.out.println("AaRouteStateTest: " + checks + " checks (AA sequence -> CarplayBus frames, parked start, "
            + "lanes, views, VC map-view gate) PASS");
    }
}
