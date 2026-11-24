package com.luka.carplay.telephony;

import de.audi.atip.base.IFrameworkAccess;
import de.audi.atip.hmi.HMIService;
import de.audi.atip.hmi.IHMIServiceApp;
import de.audi.atip.hmi.model.AbstractModelBank;
import de.audi.atip.hmi.model.ChoiceModel;
import de.audi.atip.log.LogChannel;
import de.audi.atip.log.LogChannelFactory;
import de.audi.atip.statemachine.AbstractSMM;
import de.audi.atip.statemachine.SMServices;

import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;

/**
 * Issue #69: the "Your iPhone is in Apple CarPlay mode" popup is skipped whenever its phone
 * state is entered, also when the phone app's deep history brings it back with no guard.
 * Runs the real stock PhoneSMM tables (stock/base.jar) under CarPlayPhoneSMM.
 */
public final class CarPlayDisclaimerSkipTest {
    private static final int DISCLAIMER_P5152 = 300553;
    private static final int OTHER_STATE = 300003;          /* telCenter child, no stock enter action */
    private static final int GUARDED_ENTRY = 300002;        /* telCenter ENTER, branch 0 = popup */
    private static int failures;

    /* The live value of SHOW_CAR_PLAY_DISCLAIMER_CHOICE (301049) the stock guard reads. */
    private static ChoiceModel showDisclaimer;
    /* SWITCH_TO_TERMINAL_MODE_CHOICE (4095): every press flips it. */
    private static ChoiceModel switchToCarPlay;
    private static int drawerPushes;

    public static void main(String[] args) throws Exception {
        final LogChannel silent = silentLog();
        /* Stock models log through the model bank; give it a silent channel first. */
        AbstractModelBank.initLogging((LogChannelFactory) Proxy.newProxyInstance(
            LogChannelFactory.class.getClassLoader(), new Class[] { LogChannelFactory.class },
            new InvocationHandler() {
                public Object invoke(Object p, Method m, Object[] a) { return silent; }
            }));
        showDisclaimer = new ChoiceModel(CarPlayPhoneSMM.SHOW_CAR_PLAY_DISCLAIMER_CHOICE);
        switchToCarPlay = new ChoiceModel(4095);

        stateForLabel();

        CarPlayPhoneSMM smm = new CarPlayPhoneSMM(framework(), 0, "main");
        setField(AbstractSMM.class, smm, "hmiService", hmiService());
        SMServices services = services();

        check(getInt(smm, "disclaimerState") == DISCLAIMER_P5152,
              "popup state resolved from the stock label table: " + getInt(smm, "disclaimerState"));
        check(smm.getState(DISCLAIMER_P5152).hasEnterAction(), "popup state has an enter action");

        /* MENU -> Telephone after a skip: history re-enters the popup, no guard runs. */
        resetPresses();
        showDisclaimer.setValue(1);
        int before = switchToCarPlay.getValue();
        int pushes = drawerPushes;
        smm.execEnterAction(services, DISCLAIMER_P5152);
        check(switchToCarPlay.getValue() != before, "history entry presses Switch to Apple CarPlay");
        check(drawerPushes == pushes + 1, "stock enter action (popup drawers) still runs");

        /* The same entry seen twice within a second flips the toggle once, not back. */
        int afterFirst = switchToCarPlay.getValue();
        smm.execEnterAction(services, DISCLAIMER_P5152);
        check(switchToCarPlay.getValue() == afterFirst, "repeat within a second is dropped");

        /* CarPlay deactivated or gone: the popup's own buttons stay. */
        resetPresses();
        showDisclaimer.setValue(0);
        before = switchToCarPlay.getValue();
        smm.execEnterAction(services, DISCLAIMER_P5152);
        check(switchToCarPlay.getValue() == before, "no press while choice 301049 is 0");

        /* Any other phone state: nothing. */
        resetPresses();
        showDisclaimer.setValue(1);
        before = switchToCarPlay.getValue();
        smm.execEnterAction(services, OTHER_STATE);
        check(switchToCarPlay.getValue() == before, "other states are not touched");

        /* A guarded entry (TEL into telCenter): the guard presses, the state entry that follows
         * is the same user action and must not flip the toggle back. */
        resetPresses();
        before = switchToCarPlay.getValue();
        check(smm.checkGuard(GUARDED_ENTRY, 0), "stock guard 300002 branch 0 is taken while 301049 == 1");
        int afterGuard = switchToCarPlay.getValue();
        check(afterGuard != before, "guarded entry presses once");
        smm.execEnterAction(services, DISCLAIMER_P5152);
        check(switchToCarPlay.getValue() == afterGuard, "entry right after the guard press is not a second flip");

        if (failures > 0) {
            System.out.println("CarPlayDisclaimerSkipTest: " + failures + " failure(s)");
            System.exit(1);
        }
        System.out.println("CarPlayDisclaimerSkipTest: all passed");
    }

    private static void stateForLabel() {
        String[] labels = { "telIntellicall", CarPlayPhoneSMM.DISCLAIMER_LABEL, "telCenter" };
        int[] ids = { 300067, 300553, 300004 };
        check(CarPlayDisclaimerSkip.stateForLabel(labels, ids, CarPlayPhoneSMM.DISCLAIMER_LABEL) == 300553,
              "label lookup finds the popup");
        check(CarPlayDisclaimerSkip.stateForLabel(labels, ids, "missing") == 0, "unknown label -> 0");
        check(CarPlayDisclaimerSkip.stateForLabel(labels, new int[] { 1 }, CarPlayPhoneSMM.DISCLAIMER_LABEL) == 0,
              "short ID list -> 0, no exception");
        check(CarPlayDisclaimerSkip.stateForLabel(null, ids, CarPlayPhoneSMM.DISCLAIMER_LABEL) == 0, "null labels -> 0");
    }

    private static LogChannel silentLog() throws Exception {
        Constructor c = Class.forName("com.luka.carplay.rgd.BAPBridge$SilentLogChannel").getDeclaredConstructor();
        c.setAccessible(true);
        return (LogChannel) c.newInstance();
    }

    private static IFrameworkAccess framework() throws Exception {
        final LogChannel silent = silentLog();
        final IHMIServiceApp app = (IHMIServiceApp) Proxy.newProxyInstance(
            IHMIServiceApp.class.getClassLoader(), new Class[] { IHMIServiceApp.class },
            new InvocationHandler() {
                public Object invoke(Object p, Method m, Object[] a) {
                    if (m.getName().equals("getChoiceModel") && ((Integer) a[0]).intValue() == 4095)
                        return switchToCarPlay;
                    return defaultValue(m);
                }
            });
        return (IFrameworkAccess) Proxy.newProxyInstance(
            IFrameworkAccess.class.getClassLoader(), new Class[] { IFrameworkAccess.class },
            new InvocationHandler() {
                public Object invoke(Object p, Method m, Object[] a) {
                    if (m.getName().equals("getLogChannel")) return silent;
                    if (m.getName().equals("getHmiServiceApp")) return app;
                    return defaultValue(m);
                }
            });
    }

    private static HMIService hmiService() {
        return (HMIService) Proxy.newProxyInstance(
            HMIService.class.getClassLoader(), new Class[] { HMIService.class },
            new InvocationHandler() {
                public Object invoke(Object p, Method m, Object[] a) {
                    if (m.getName().equals("getModel")) {
                        int id = ((Integer) a[a.length - 1]).intValue();
                        if (id == CarPlayPhoneSMM.SHOW_CAR_PLAY_DISCLAIMER_CHOICE) return showDisclaimer;
                    }
                    return defaultValue(m);
                }
            });
    }

    private static SMServices services() {
        return (SMServices) Proxy.newProxyInstance(
            SMServices.class.getClassLoader(), new Class[] { SMServices.class },
            new InvocationHandler() {
                public Object invoke(Object p, Method m, Object[] a) {
                    if (m.getName().equals("pushDrawerIDs")) drawerPushes++;
                    return defaultValue(m);
                }
            });
    }

    private static Object defaultValue(Method m) {
        Class r = m.getReturnType();
        if (r == Boolean.TYPE) return Boolean.FALSE;
        if (r == Integer.TYPE) return new Integer(0);
        if (r == Long.TYPE) return new Long(0L);
        if (r == Short.TYPE) return new Short((short) 0);
        if (r == Byte.TYPE) return new Byte((byte) 0);
        if (r == Character.TYPE) return new Character('\0');
        if (r == Float.TYPE) return new Float(0f);
        if (r == Double.TYPE) return new Double(0d);
        return null;
    }

    /* Each case starts outside CarPlayDisclaimerSkip's one-second repeat window. */
    private static void resetPresses() throws Exception {
        Field f = CarPlayDisclaimerSkip.class.getDeclaredField("lastPress");
        f.setAccessible(true);
        f.setLong(null, 0L);
    }

    private static int getInt(Object o, String name) throws Exception {
        Field f = CarPlayPhoneSMM.class.getDeclaredField(name);
        f.setAccessible(true);
        return f.getInt(o);
    }

    private static void setField(Class owner, Object o, String name, Object v) throws Exception {
        Field f = owner.getDeclaredField(name);
        f.setAccessible(true);
        f.set(o, v);
    }

    private static void check(boolean ok, String what) {
        System.out.println((ok ? "PASS " : "FAIL ") + what);
        if (!ok) failures++;
    }
}
