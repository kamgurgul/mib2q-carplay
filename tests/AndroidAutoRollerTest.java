import de.audi.app.terminalmode.keyevents.Key;
import de.audi.app.terminalmode.keyevents.KeyState;
import de.audi.app.terminalmode.smartphone.androidauto2.AndroidAuto2KeyEventsController;
import de.audi.atip.log.LogChannel;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import org.dsi.ifc.androidauto2.DSIAndroidAuto2;

/** Left steering-wheel roller press with Android Auto: the route-info toggle module runs for
 *  Android Auto sessions, and the patched key controller drops the DDS_SELECT copy of that
 *  press (raw key 40) while the centre-console knob (raw key 16) stays Select (keycode 23). */
public final class AndroidAutoRollerTest {
    static final ArrayList<String> posted = new ArrayList<String>();
    static void check(boolean b, String m) { if (!b) throw new AssertionError(m); }

    static void raw(String method, int state) throws Exception {
        Method m = Class.forName("com.luka.carplay.core.SteeringWheelInputModule")
            .getDeclaredMethod(method, new Class[]{int.class});
        m.setAccessible(true);
        m.invoke(null, new Object[]{Integer.valueOf(state)});
    }

    public static void main(String[] args) throws Exception {
        DSIAndroidAuto2 dsi = (DSIAndroidAuto2) Proxy.newProxyInstance(
            AndroidAutoRollerTest.class.getClassLoader(), new Class[]{DSIAndroidAuto2.class},
            new InvocationHandler() {
                public Object invoke(Object p, Method m, Object[] a) {
                    if (m.getName().equals("postButtonEvent")) posted.add(a[0] + "/" + a[1]);
                    return null;
                }
            });
        Constructor c = Class.forName("com.luka.carplay.rgd.BAPBridge$SilentLogChannel").getDeclaredConstructor();
        c.setAccessible(true);
        AndroidAuto2KeyEventsController k = new AndroidAuto2KeyEventsController(dsi, (LogChannel) c.newInstance());

        /* No wheel press: DDS_SELECT is Android Auto Select (23), press 0 / release 1. */
        k.updateKey(Key.DDS_SELECT, KeyState.PRESSED);
        k.updateKey(Key.DDS_SELECT, KeyState.RELEASED);
        check(posted.size() == 2 && "23/0".equals(posted.get(0)) && "23/1".equals(posted.get(1)),
            "knob Select not forwarded: " + posted);

        /* Wheel roller press (raw key 40 PRESSED/RELEASED): both collapsed copies dropped. */
        posted.clear();
        raw("armCollapsedSelect", 1);
        k.updateKey(Key.DDS_SELECT, KeyState.PRESSED);
        raw("armCollapsedSelect", 0);
        k.updateKey(Key.DDS_SELECT, KeyState.RELEASED);
        check(posted.isEmpty(), "wheel press reached the phone: " + posted);

        /* A real centre-console press right after clears the marker: Select again. */
        raw("armCollapsedSelect", 1);
        raw("clearCollapsedSelect", 1);
        k.updateKey(Key.DDS_SELECT, KeyState.PRESSED);
        check(posted.size() == 1 && "23/0".equals(posted.get(0)), "knob after wheel lost: " + posted);
        k.updateKey(Key.DDS_SELECT, KeyState.RELEASED);

        /* Other keys untouched, even while a wheel marker is armed. */
        posted.clear();
        raw("armCollapsedSelect", 1);
        k.updateKey(Key.BACK, KeyState.PRESSED);
        check(posted.size() == 1 && "4/0".equals(posted.get(0)), "BACK changed: " + posted);
        raw("armCollapsedSelect", 0);
        k.updateKey(Key.DDS_SELECT, KeyState.PRESSED);   /* consume the stale press marker */
        k.updateKey(Key.DDS_SELECT, KeyState.RELEASED);

        /* The roller module now runs in Android Auto sessions too. */
        Class app = Class.forName("com.luka.carplay.core.CarPlayApp");
        Field mods = app.getDeclaredField("MODULES"), owners = app.getDeclaredField("MODULE_OWNERS");
        mods.setAccessible(true); owners.setAccessible(true);
        Object[] m = (Object[]) mods.get(null);
        int[] o = (int[]) owners.get(null);
        boolean found = false;
        for (int i = 0; i < m.length; i++) {
            if (m[i].getClass().getName().endsWith("SteeringWheelInputModule")) {
                found = true;
                check((o[i] & 2) != 0, "SteeringWheelInputModule not started for Android Auto");
            }
        }
        check(found, "SteeringWheelInputModule missing");
        System.out.println("AndroidAutoRollerTest: knob Select forwarded, wheel press dropped, marker cleared "
            + "by the knob, other keys untouched, roller module runs for Android Auto PASS");
    }
}
