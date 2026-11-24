/*
 * CarPlayPhoneSMM - the stock HU phone state machine, minus the CarPlay popup.
 *
 * The popup is phone state telCarPlayDisclaimer_compound (300553 on P5152).  It is skipped
 * in two places, both pressing "Switch to Apple CarPlay" for the user (CarPlayDisclaimerSkip):
 *
 *  - When the state is entered, by any route (execEnterAction).  This is the one that
 *    matters for issue #69.  After a skip the phone SM is left on the popup state while the
 *    screen goes to CarPlay, and telCenter (300004) and phoneDesktop (300001) keep deep
 *    history.  So the next MENU -> Telephone or TEL restores the popup straight from history
 *    and no guard runs.  301446 (TEL_CAR_PLAY_DEACTIVATION_FINISHED) is another entry
 *    without a guard.  The state is looked up by its exported label, not a firmware-specific
 *    number.  Its enter flag is made sure of in init(); stock already sets it, because the
 *    popup's enter action pushes its drawers.  A history restore re-enters the state, so the
 *    enter action runs then too.
 *  - When one of five guarded entry choices picks it: transitions 300002 / 301320 / 301327
 *    / 301389 / 301447, branch 0, guarded by SHOW_CAR_PLAY_DISCLAIMER_CHOICE (301049) == 1
 *    (checkGuard).  This is the original, car-tested hook.  It fires just before the state
 *    is entered, so the enter hook's press lands in CarPlayDisclaimerSkip's one-second
 *    repeat window and is dropped there.  Either way, one press per entry.
 *
 * The enter hook presses only while the stock choice still says CarPlay holds the phone.
 * When CarPlay was deactivated, or it disconnected while the popup sat in history, the
 * popup's own buttons stay.
 *
 * Copyright (c) 2026 LuKa (@LuKa_dev)
 */
package com.luka.carplay.telephony;

import com.luka.carplay.framework.Log;
import de.audi.atip.base.IFrameworkAccess;
import de.audi.atip.hmi.model.ChoiceModel;
import de.audi.atip.statemachine.SMServices;
import de.audi.atip.statemachine.State;
import de.audi.tghu.phone.sm.PhoneSMM;

public class CarPlayPhoneSMM extends PhoneSMM {
    private static final String TAG = "Telephony";
    static final String DISCLAIMER_LABEL = "telCarPlayDisclaimer_compound";
    static final int SHOW_CAR_PLAY_DISCLAIMER_CHOICE = 301049;

    /* Set in init(), which the stock AbstractSMM constructor calls before this class's own
     * field initializers would run. So no initializer here (it would overwrite the value);
     * 0 = not found, and the enter hook stays off. */
    private int disclaimerState;

    public CarPlayPhoneSMM(IFrameworkAccess framework, int n, String name) {
        super(framework, n, name);
    }

    protected void init() {
        super.init();
        int state = CarPlayDisclaimerSkip.stateForLabel(extStateLabelList, extStateIDList, DISCLAIMER_LABEL);
        if (state == 0) {
            Log.w(TAG, "CarPlay popup state '" + DISCLAIMER_LABEL + "' not exported; only guarded entries are skipped");
            return;
        }
        try {
            int idx = id2ArrayIdx(state);
            if ((stateFlagList[idx] & State.ENTER_ACTION_FLAG) == 0) {
                stateFlagList[idx] |= State.ENTER_ACTION_FLAG;
                Log.i(TAG, "CarPlay popup state " + state + ": enter action flag added");
            }
            disclaimerState = state;
        } catch (Throwable t) {
            Log.w(TAG, "CarPlay popup state " + state + " not hooked: " + t);
        }
    }

    public boolean checkGuard(int transition, int branch) {
        boolean taken = super.checkGuard(transition, branch);
        if (taken && branch == 0 && isDisclaimerEntry(transition))
            CarPlayDisclaimerSkip.press(getFramework(), "phone transition " + transition);
        return taken;
    }

    public void execEnterAction(SMServices services, int state) {
        super.execEnterAction(services, state);
        if (state == disclaimerState && state != 0 && carPlayHoldsPhone())
            CarPlayDisclaimerSkip.press(getFramework(), "phone state " + state + " entered");
    }

    /* The stock guard's own condition: the popup is meant for CarPlay mode only. */
    private boolean carPlayHoldsPhone() {
        try {
            return ((ChoiceModel) getModel(SHOW_CAR_PLAY_DISCLAIMER_CHOICE)).getValue() == 1;
        } catch (Throwable t) {
            Log.w(TAG, "CarPlay popup choice " + SHOW_CAR_PLAY_DISCLAIMER_CHOICE + " unreadable: " + t);
            return false;
        }
    }

    private static boolean isDisclaimerEntry(int t) {
        return t == 300002 || t == 301320 || t == 301327 || t == 301389 || t == 301447;
    }
}
