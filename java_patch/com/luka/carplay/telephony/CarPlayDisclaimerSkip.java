/*
 * CarPlayDisclaimerSkip - presses the HU's "Switch to Apple CarPlay" for the user.
 *
 * With CarPlay active the HU puts up "Your iPhone is in Apple CarPlay mode" (phone state
 * telCarPlayDisclaimer_compound) whenever the phone app is entered.  Its button is
 * SWITCH_TO_CARPLAY_BUTTON, which stock CoMaCarplayHandler turns into a flip of
 * SWITCH_TO_TERMINAL_MODE_CHOICE (4095); system mediator 76 maps that change to event
 * 2000 SWITCH_TO_TERMINAL_MODE, which the system root state takes to CarPlay (state 253).
 *
 * Callers: CarPlayPhoneSMM when the popup state is entered by any route (including the
 * phone app's deep history bringing a remembered popup back, issue #69) and when one of
 * the phone SM's guarded entries picks it; CarPlaySystemSMM for the system SM's own entry.
 * One entry can reach more than one of them, so a second press within a second is dropped
 * (and logged): two flips of the toggle would cancel out.
 *
 * Copyright (c) 2026 LuKa (@LuKa_dev)
 */
package com.luka.carplay.telephony;

import com.luka.carplay.framework.Log;
import de.audi.atip.base.IFrameworkAccess;
import de.audi.atip.hmi.modelaccess.ChoiceModelApp;

public final class CarPlayDisclaimerSkip {
    private static final String TAG = "Telephony";
    private static final int SWITCH_TO_TERMINAL_MODE_CHOICE = 4095;
    private static final long REPRESS_GUARD_MS = 1000;
    private static long lastPress;

    private CarPlayDisclaimerSkip() {}

    /** The CarPlay popup is about to be entered by {@code source}: go to CarPlay instead. */
    public static void press(IFrameworkAccess framework, String source) {
        long now = System.currentTimeMillis();
        synchronized (CarPlayDisclaimerSkip.class) {
            if (now >= lastPress && now - lastPress < REPRESS_GUARD_MS) {
                Log.i(TAG, "CarPlay popup skip already pressed " + (now - lastPress)
                        + " ms ago (" + source + ")");
                return;
            }
            lastPress = now;
        }
        try {
            ChoiceModelApp m = framework.getHmiServiceApp().getChoiceModel(SWITCH_TO_TERMINAL_MODE_CHOICE);
            m.setValue(m.getValue() == 0 ? 1 : 0);
            Log.i(TAG, "CarPlay popup skipped (" + source + ") -> Apple CarPlay");
        } catch (Throwable t) {
            Log.w(TAG, "CarPlay popup skip failed: " + t);   /* stock popup stays */
        }
    }

    /**
     * The state ID a state machine exports under {@code label} (its parallel
     * extStateLabelList / extStateIDList), or 0 when it has none.  State IDs are
     * module-based (phone 300000+), so 0 never names a real state.
     */
    static int stateForLabel(String[] labels, int[] ids, String label) {
        if (labels == null || ids == null || label == null) return 0;
        int n = labels.length < ids.length ? labels.length : ids.length;
        for (int i = 0; i < n; i++) {
            if (label.equals(labels[i])) return ids[i] > 0 ? ids[i] : 0;
        }
        return 0;
    }
}
