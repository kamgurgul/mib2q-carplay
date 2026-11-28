package com.luka.carplay.telephony;

import com.luka.carplay.bus.CarplayBus;
import de.audi.atip.interapp.combi.bap.phone.CombiBAPServicePhone;
import de.audi.atip.interapp.combi.bap.phone.data.FsgSetup;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;

/** VC register state from hook/telephony's EVT_TEL_COMM, through the real GatedPhoneService:
 *  each iAP2 RegistrationStatus maps as stock maps DSI, airplane mode is not registered, and a
 *  status not reported since the hook's reset (registration -1) is home, not "not registered". */
public final class GatedPhoneCommTest {
    static int lastReg = -2;
    static void check(boolean b, String m) { if (!b) throw new AssertionError(m); }

    static void comm(GatedPhoneService g, String text, int want) throws Exception {
        byte[] raw = text.getBytes("UTF-8");
        Method set = GatedPhoneService.class.getDeclaredMethod("setComm", new Class[]{CarplayBus.Data.class});
        set.setAccessible(true);
        lastReg = -2;
        set.invoke(g, new Object[]{CarplayBus.parseText(raw, raw.length)});
        check(lastReg == want, "\"" + text.replace('\n', ' ') + "\" -> RegisterState " + lastReg + ", want " + want);
    }

    static String frame(int signal, int registration, boolean airplane) {
        return "signal:i:" + signal + "\nregistration:i:" + registration + "\nairplane:b:" + airplane
            + "\ncarrier:s:\nbattery:i:-1\ncharging:i:-1\n";
    }

    public static void main(String[] args) throws Exception {
        CombiBAPServicePhone real = (CombiBAPServicePhone) Proxy.newProxyInstance(
            GatedPhoneCommTest.class.getClassLoader(), new Class[]{CombiBAPServicePhone.class},
            new InvocationHandler() {
                public Object invoke(Object p, Method m, Object[] a) {
                    if (m.getName().equals("updateRegisterState")) lastReg = ((Integer) a[0]).intValue();
                    return null;
                }
            });
        GatedPhoneService g = new GatedPhoneService(real);
        g.updateFsgSetup(FsgSetup.builder().setAppleLinkConnectionPossible(true)
            .setMobileConnectionType(5).build());               /* CarPlay: gate on */

        int[][] map = { {0, 0}, {1, 0}, {2, 2}, {3, 3}, {4, 1}, {5, 4}, {6, 0} };
        for (int i = 0; i < map.length; i++) comm(g, frame(3, map[i][0], false), map[i][1]);
        comm(g, frame(3, 4, true), 0);                           /* airplane mode */
        comm(g, frame(3, -1, false), 1);                         /* signal only after a reset */
        comm(g, frame(-1, -1, false), 1);                        /* nothing reported yet */
        comm(g, frame(-1, -1, true), 0);
        System.out.println("GatedPhoneCommTest: iAP2 registration mapped as stock, airplane not registered, "
            + "unreported status shown as home PASS");
    }
}
