/*
 * CarPlaySystemSMM - the stock system state machine, minus its own CarPlay popup entry.
 *
 * System transition 833 (ENTER of system state 484) is the system SM's only direct route
 * to the phone's telCarPlayDisclaimer_compound.  Its branch 0 (SHOW_CAR_PLAY_DISCLAIMER_CHOICE
 * 301049 == 1) targets the popup without passing any PhoneSMM guard.  When it is taken we
 * press "Switch to Apple CarPlay" for the user (CarPlayDisclaimerSkip).  In the P5152 tables
 * state 484 is reached on REMOTE_HMI_SWITCHTOSERVICE, not on the TEL key.  TEL goes system
 * 295 -> 36 -> phone slot 13 -> phoneDesktop, and the phone SM's history or guards pick the
 * popup from there (CarPlayPhoneSMM).  Issue #69's TEL log never showed 833.  The popup
 * state's enter hook in CarPlayPhoneSMM also sees this route, and the press dedupe keeps it
 * to one flip.  Same transition and guard on MU1316 US, MU1329 EU and MU1003 CN; this only
 * subclasses, so each firmware keeps its own SystemSMM.
 *
 * Copyright (c) 2026 LuKa (@LuKa_dev)
 */
package com.luka.carplay.telephony;

import de.audi.atip.base.IFrameworkAccess;
import de.audi.tghu.system.sm.SystemSMM;

public class CarPlaySystemSMM extends SystemSMM {
    private static final int TEL_ENTRY_TRANSITION = 833;

    public CarPlaySystemSMM(IFrameworkAccess framework, int n, String name) {
        super(framework, n, name);
    }

    public boolean checkGuard(int transition, int branch) {
        boolean taken = super.checkGuard(transition, branch);
        if (taken && branch == 0 && transition == TEL_ENTRY_TRANSITION)
            CarPlayDisclaimerSkip.press(getFramework(), "system transition " + transition);
        return taken;
    }
}
