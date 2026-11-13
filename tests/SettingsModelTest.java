package com.luka.carplay.connectivity;

import de.audi.atip.model.ConnectivityModelBank;
import de.audi.atip.hmi.model.ChoiceModel;

/** Use Audi's actual model bank; retain stock IDs and register all added controls. */
public final class SettingsModelTest {
    public static void main(String[] args) throws Exception {
        java.lang.reflect.Constructor ctor = Class.forName("com.luka.carplay.rgd.BAPBridge$SilentLogChannel").getDeclaredConstructor();
        ctor.setAccessible(true);
        final Object log = ctor.newInstance();
        de.audi.atip.hmi.model.AbstractModelBank.initLogging((de.audi.atip.log.LogChannelFactory)
            java.lang.reflect.Proxy.newProxyInstance(SettingsModelTest.class.getClassLoader(),
                new Class[]{de.audi.atip.log.LogChannelFactory.class}, new java.lang.reflect.InvocationHandler() {
                    public Object invoke(Object p, java.lang.reflect.Method m, Object[] args) { return log; }
                }));
        ConnectivityModelBank stock = new ConnectivityModelBank();
        ConnectivityModelBank bank = CarPlaySettingsUi.newBank();
        int[] oldIds = stock.getAllModelIds(), ids = bank.getAllModelIds();
        if (ids.length != oldIds.length + CarPlaySettings.COUNT) throw new AssertionError("model count");
        for (int i = 0; i < oldIds.length; i++) {
            if (ids[i] != oldIds[i]) throw new AssertionError("stock model moved");
            if (oldIds[i] >= 2500500 && oldIds[i] < 2500500 + CarPlaySettings.COUNT)
                throw new AssertionError("model ID collision");
        }
        for (int key = 0; key < CarPlaySettings.COUNT; key++) {
            ChoiceModel m = (ChoiceModel) bank.getModel(2500500 + key);
            if (m.getValue() != CarPlaySettings.defaultValue(key)) throw new AssertionError("default " + key);
            if (bank.getModel(2500500 + key) != m) throw new AssertionError("model recreated");
        }
        System.out.println("SettingsModelTest: stock IDs preserved, all " + CarPlaySettings.COUNT + " choice models registered PASS");
    }
}
