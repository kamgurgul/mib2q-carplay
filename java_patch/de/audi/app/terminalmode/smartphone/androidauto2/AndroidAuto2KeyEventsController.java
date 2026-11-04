/*
 * Patched de.audi.app.terminalmode.smartphone.androidauto2.AndroidAuto2KeyEventsController:
 * identical to stock (written from the MU1329 class file) except updateKey, which drops the
 * DDS_SELECT that the stock keyboard stack collapses out of the left steering-wheel roller
 * press (raw MFW key 40). That press toggles the route-info line on the VC
 * (SteeringWheelInputModule), as with CarPlay; without this it also reached the phone as a
 * Select. The centre-console knob (raw key 16) is still Select.
 */
package de.audi.app.terminalmode.smartphone.androidauto2;

import com.luka.carplay.core.SteeringWheelInputModule;
import de.audi.app.terminalmode.TerminalModeUtils;
import de.audi.app.terminalmode.keyevents.ITerminalModeDSIKeyEventsController;
import de.audi.app.terminalmode.keyevents.Key;
import de.audi.app.terminalmode.keyevents.KeyState;
import de.audi.atip.log.LogChannel;
import de.esolutions.fw.util.commons.Buffer;
import org.dsi.ifc.androidauto2.DSIAndroidAuto2;
import org.dsi.ifc.androidauto2.TouchEvent;

public final class AndroidAuto2KeyEventsController implements ITerminalModeDSIKeyEventsController {
    private static final String LOGCLASS = "TerminalModeDSIKeyEventsController";
    private volatile Key lastJoystickkey;
    private final boolean[] downState = new boolean[3];
    private final DSIAndroidAuto2 dsi;
    private final LogChannel lc;

    public AndroidAuto2KeyEventsController(DSIAndroidAuto2 dsiandroidauto2, LogChannel logchannel) {
        this.dsi = dsiandroidauto2;
        this.lc = logchannel;
    }

    public void updateKey(Key key, KeyState keystate) {
        if (Key.DDS_SELECT.is(key) && SteeringWheelInputModule.consumeCollapsedSelect(keystate)) {
            this.lc.log(1000000, "[%1.updateKey] suppress MFW DDS_SELECT for Android Auto", LOGCLASS);
            return;
        }
        if (TerminalModeUtils.isJoystickMiddleposition(key)) {
            if (null == this.lastJoystickkey) return;
            this.lc.log(1000000, "[%1.updateKey] joystick middle position, release button %2",
                LOGCLASS, this.lastJoystickkey);
            this.dsi.postButtonEvent(this.getKeyId(this.lastJoystickkey), 1);
            this.lastJoystickkey = null;
            return;
        }
        int i = this.getKeyId(key);
        if (0 == i) {
            this.lc.log(1000000, "[%1.updateKey] unknown key id", LOGCLASS);
            return;
        }
        this.lc.log(1000000, "[%1.updateKey] %2", LOGCLASS, (long) i);
        if (TerminalModeUtils.isJoystick(key)) this.lastJoystickkey = key;
        this.dsi.postButtonEvent(i, this.getKeyState(keystate));
    }

    public void updateTouchEvent(int i, int j, int k, int l, int i1, int j1, int k1) {
        this.lc.log(1000000, "[%1.updateTouchEvent]", LOGCLASS);
        TouchEvent[] atouchevent = new TouchEvent[j];
        if (j >= 1) atouchevent[0] = new TouchEvent(l, i1, 0);
        if (j >= 2) {
            int[] corr = this.calcSecondCorr(l, i1, j1, k1);
            atouchevent[1] = new TouchEvent(corr[0], corr[1], 1);
        }
        if (this.lc.isDebug()) {
            if (j >= 1) {
                this.lc.log(1000000, "[%1.updateTouchEvent] %2/%3", LOGCLASS,
                    (long) atouchevent[0].getX(), (long) atouchevent[0].getY());
            } else if (j >= 2) {
                Buffer buffer = new Buffer(50);
                buffer.append(atouchevent[0].getX());
                buffer.append('/');
                buffer.append(atouchevent[0].getY());
                buffer.append(' ');
                buffer.append(atouchevent[1].getX());
                buffer.append('/');
                buffer.append(atouchevent[1].getY());
                this.lc.log(1000000, "[%1.updateTouchEvent] %2", LOGCLASS, buffer.toString());
            }
        }
        int state;
        if (!this.downState[i]) {
            this.downState[i] = true;
            state = 0;
        } else if (j == 0) {
            this.downState[i] = false;
            state = 1;
        } else {
            state = 2;
        }
        this.dsi.postTouchEvent(this.getDSITouchInputId(i), atouchevent, state, 0);
    }

    public void updateTouchEvents(de.audi.app.terminalmode.keyevents.TouchEvent[] atouchevent) {
        this.lc.log(1000000, "[%1.updateTouchEvents] %2 %3", LOGCLASS,
            Integer.toString(atouchevent.length), atouchevent[0]);
        TouchEvent[] events = new TouchEvent[atouchevent.length];
        int state = 0;
        for (int i = 0; i < atouchevent.length; i++) {
            de.audi.app.terminalmode.keyevents.TouchEvent t = atouchevent[i];
            events[i] = new TouchEvent(t.getCurrentX(), t.getCurrentY(), i);
        }
        switch (atouchevent[0].getTouchState()) {
            case 0: state = 0; break;
            case 1: state = 1; break;
            default: state = 2; break;
        }
        this.dsi.postTouchEvent(atouchevent[0].isTouchScreen() ? 1 : 0, events, state, 0);
    }

    private int[] calcSecondCorr(int x, int y, int unused, int r) {
        if (r > 100) r -= 100;
        int x2 = (int) ((double) x + Math.acos((double) (r / 100)) * (double) r);
        int y2 = (int) ((double) y + Math.asin((double) (r / 100)) * (double) r);
        return new int[] { x2, y2 };
    }

    public void updateRotary(int i) {
        this.dsi.postRotaryEvent(i);
    }

    private int getKeyState(KeyState keystate) {
        if (keystate.is(KeyState.PRESSED)) return 0;
        if (keystate.is(KeyState.RELEASED)) return 1;
        this.lc.log(1000000, "[%1.getKeyState] Unsupported key state %2", LOGCLASS, keystate);
        return -1;
    }

    private int getKeyId(Key key) {
        if (key.is(Key.BACK)) return 4;
        if (key.is(Key.DDS_SELECT)) return 23;
        if (key.isOneOf(Key.JS_NORTH, Key.SOFTKEY_SOUTHWEST)) return 19;
        if (key.isOneOf(Key.JS_EAST, Key.SOFTKEY_NORTHEAST)) return 22;
        if (key.isOneOf(Key.JS_SOUTH, Key.SOFTKEY_SOUTHEAST)) return 20;
        if (key.isOneOf(Key.JS_WEST, Key.SOFTKEY_NORTHWEST)) return 21;
        if (key.is(Key.SOFTKEY_EAST)) return 2;
        if (key.is(Key.SOFTKEY_WEST)) return 1;
        this.lc.log(1000000, "[%1.getKeyId] Unsupport %2", LOGCLASS, key);
        return 0;
    }

    private int getDSITouchInputId(int i) {
        switch (i) {
            case 2: return 1;
            default: return 0;
        }
    }

    public void updateCharacterEvent(String[] astring, int[] aint) {
        this.lc.log(1000000, "[%1.updateCharacterEvent]", LOGCLASS);
    }
}
