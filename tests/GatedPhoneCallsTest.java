package com.luka.carplay.telephony;

import de.audi.atip.interapp.combi.bap.phone.CombiBAPServicePhone;
import de.audi.atip.interapp.combi.bap.phone.data.CombiBAPCallState;
import de.audi.atip.interapp.combi.bap.phone.data.FsgSetup;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import org.dsi.ifc.carplay.CallState;

/** CarPlay calls on the VC phone tab: dio_manager's orphaned call entries (a call first keyed
 *  by number, then by CallUUID) are hidden, a hang-up never leaves "Ending call", and the
 *  1 s watchdog clears a call iOS never reports as ended. */
public final class GatedPhoneCallsTest {
    static int shown = -1, hangupResult = -1, disconnectReasons;
    static void check(boolean b, String m) { if (!b) throw new AssertionError(m); }

    static CallState call(String number, int status, int direction, String uuid) {
        return new CallState(number, "", status, direction, uuid);
    }

    public static void main(String[] args) throws Exception {
        CombiBAPServicePhone real = (CombiBAPServicePhone) Proxy.newProxyInstance(
            GatedPhoneCallsTest.class.getClassLoader(), new Class[]{CombiBAPServicePhone.class},
            new InvocationHandler() {
                public Object invoke(Object p, Method m, Object[] a) {
                    if (m.getName().equals("updateCallStates")) {
                        CombiBAPCallState[] s = (CombiBAPCallState[]) a[0];
                        int n = 0;
                        for (int i = 0; s != null && i < s.length; i++) if (s[i] != null) n++;
                        shown = n;
                    } else if (m.getName().equals("hangupCallResult")) {
                        hangupResult = ((Integer) a[0]).intValue();
                    } else if (m.getName().equals("updateDisconnectReason")) {
                        disconnectReasons++;
                    }
                    return null;
                }
            });
        GatedPhoneService g = new GatedPhoneService(real);
        GatedPhoneService.telSink = new GatedPhoneService.TelSink() {
            public boolean send(byte[] cmd) { return true; }
        };
        g.updateFsgSetup(FsgSetup.builder().setAppleLinkConnectionPossible(true)
            .setMobileConnectionType(5).build());               /* CarPlay: gate on */
        final int OUT = 1, DISC = 0, ACTIVE = 1, CONNECTING = 3;

        /* Outgoing call: dialling without a CallUUID, then answered with one. dio_manager keeps
         * both entries; only the keyed one is real. */
        GatedPhoneService.onCarPlayCalls(new CallState[]{call("+48600123456", CONNECTING, OUT, "")});
        check(shown == 1, "dialling call shown");
        GatedPhoneService.onCarPlayCalls(new CallState[]{
            call("+48600123456", CONNECTING, OUT, ""), call("+48600123456", ACTIVE, OUT, "U1")});
        check(shown == 1, "keyless orphan next to the keyed call hidden, shown=" + shown);

        /* iOS ends it: the keyed entry disconnects, the orphan would stay forever. */
        GatedPhoneService.onCarPlayCalls(new CallState[]{
            call("+48600123456", CONNECTING, OUT, ""), call("+48600123456", DISC, OUT, "U1")});
        check(shown == 0, "orphan hidden after the call ended, shown=" + shown);
        GatedPhoneService.onCarPlayCalls(new CallState[]{call("+48600123456", CONNECTING, OUT, "")});
        check(shown == 0, "orphan stays hidden in later updates");

        /* An empty update resets the filter: the next call to the same number shows. */
        GatedPhoneService.onCarPlayCalls(new CallState[0]);
        GatedPhoneService.onCarPlayCalls(new CallState[]{call("+48600123456", CONNECTING, OUT, "")});
        check(shown == 1, "new call after a reset shown");

        /* Hang-up from the VC: always OK. No update from iOS -> the watchdog clears the call. */
        hangupResult = -1;
        check(GatedPhoneService.onVcAction(GatedPhoneService.ACT_HANGUP, null), "hangup handled");
        check(hangupResult == 0, "hangup result OK, got " + hangupResult);
        Thread.sleep(GatedPhoneService.HANGUP_WATCHDOG_MS + 500L);
        check(shown == 0, "watchdog cleared the stale call, shown=" + shown);

        /* A CallStateUpdate before the watchdog fires cancels it. */
        GatedPhoneService.onCarPlayCalls(new CallState[0]);
        GatedPhoneService.onCarPlayCalls(new CallState[]{call("+48600999888", ACTIVE, OUT, "U2")});
        GatedPhoneService.onVcAction(GatedPhoneService.ACT_HANGUP, null);
        GatedPhoneService.onCarPlayCalls(new CallState[]{call("+48600999888", ACTIVE, OUT, "U2")});
        Thread.sleep(GatedPhoneService.HANGUP_WATCHDOG_MS + 500L);
        check(shown == 1, "update after the hang-up keeps the call, shown=" + shown);

        /* Hang-up with nothing live: still OK, no stuck "Ending call". */
        GatedPhoneService.onCarPlayCalls(new CallState[0]);
        hangupResult = -1;
        GatedPhoneService.onVcAction(GatedPhoneService.ACT_HANGUP, null);
        check(hangupResult == 0 && shown == 0, "idle hang-up OK");

        /* The stock HFP disconnect reason is not forwarded while the tab shows CarPlay calls. */
        g.updateDisconnectReason(3);
        check(disconnectReasons == 0, "disconnect reason forwarded while gated");
        System.out.println("GatedPhoneCallsTest: orphaned keyless/keyed entries hidden, reset, "
            + "hang-up always OK, watchdog clears and is cancelled by an update PASS");
    }
}
