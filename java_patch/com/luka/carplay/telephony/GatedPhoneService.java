/*
 * GatedPhoneService - wrapper around the stock CombiBAPServicePhone (HU PHONE 0x28)
 * that shows CarPlay calls on the cluster telephone tab.
 *
 * Gate = stock's own CarPlay verdict: FctID 14 arrives with mobileConnectionType 5
 * (BAPPropertyTelFSGSetup: TerminalModeDevice active && connection method 1).
 * While the gate is on, the VC sees an HFP phone instead (type 3, Tel_State 15 =
 * phone on without SIM), and the call functions 22/23/24 carry CarPlay's calls from
 * DSICarplay.updateCallState.  Stock's values for the gated functions are cached and
 * replayed when the gate goes off.  Every other method passes straight through.
 *
 * VC actions (accept, hangup, hold/resume/swap, dial, mic mute) come back through
 * GatedPhoneListener and go to hook/telephony (iAP2 AcceptCall, EndCall,
 * SwapCalls, Hold/MuteStatusUpdate) and to callNumber (dial).  Recents/favorites come from hook/telephony (iAP2
 * ListUpdate, EVT_TEL_LISTS), with the missed-call indication derived from them;
 * signal and carrier come from the stock CommunicationsUpdate (EVT_TEL_COMM), the iPhone's
 * battery from PowerUpdate (EVT_TEL_COMM battery/charging, hook/telephony).
 *
 * Android Auto (mobileConnectionType 6): the VC shows its "Android Auto" note for 6.
 * Stock keeps publishing the Bluetooth HFP calls of the Android Auto phone to the
 * cluster (BAPPropertyTelCallListUpdater; it only skips on G24 clusters), so FctID 14
 * is rewritten to 3 and everything else stays stock: calls, lists and the VC's actions
 * use the stock Bluetooth phone.  Opt-out: /mnt/app/carplay_aa_phone_tab.off.
 *
 * Copyright (c) 2026 LuKa (@LuKa_dev)
 */
package com.luka.carplay.telephony;

import com.luka.carplay.bus.CarplayBus;
import com.luka.carplay.framework.Log;
import com.luka.carplay.framework.HuTime;
import de.audi.atip.interapp.combi.bap.phone.CombiBAPServicePhone;
import de.audi.atip.interapp.combi.bap.phone.data.CallStartTime;
import de.audi.atip.interapp.combi.bap.phone.data.CombiBAPCallInfo;
import de.audi.atip.interapp.combi.bap.phone.data.CombiBAPCallStackEntry;
import de.audi.atip.interapp.combi.bap.phone.data.CombiBAPCallState;
import de.audi.atip.interapp.combi.bap.phone.data.CombiBAPFavoriteNumberEntry;
import de.audi.atip.interapp.combi.bap.phone.data.CombiBAPhoneMobileBatteryLevel;
import de.audi.atip.interapp.combi.bap.phone.data.FsgSetup;
import de.audi.atip.interapp.phone.TelAdbEntryStruct;
import java.util.Calendar;
import org.dsi.ifc.carplay.CallState;

public class GatedPhoneService implements CombiBAPServicePhone {
    private static final String TAG = "Telephony";

    private static final int MCT_HFP = 3, MCT_APPLE_LINK = 5, MCT_GOOGLE_LINK = 6;
    private static final String AA_PHONE_TAB_OFF = "/mnt/app/carplay_aa_phone_tab.off";
    private boolean aaAsHfp;
    private static final int TEL_STATE_PHONE_ON_NO_SIM = 15;
    private static final int SLOTS = 7;
    private static final int NO_DURATION = CALL_DURATION_NOT_AVAILABLE;

    /* DSICarplay CALLSTATE_* / CALLDIRECTION_* */
    private static final int DSI_DISCONNECTED = 0, DSI_ACTIVE = 1, DSI_HOLDING = 2, DSI_CONNECTING = 3;
    private static final int DSI_INCOMING = 0;

    /* BAP CallState values, as stock TelBAPCall.getCallState produces them. */
    private static final int BAP_RINGING = 1, BAP_ACTIVE = 2, BAP_DIALING = 3, BAP_HELD = 5;
    private static final int BAP_CALL_TYPE_VOICE = 1;

    /* VC actions handled for CarPlay (GatedPhoneListener). */
    static final int ACT_ACCEPT = 1, ACT_HANGUP = 2, ACT_HOLD = 3, ACT_RESUME = 4, ACT_SWAP = 5, ACT_DIAL = 6,
                     ACT_MUTE_ON = 7, ACT_MUTE_OFF = 8, ACT_RESET_MISSED = 9;
    /* hook/telephony CMD_TEL actions: MuteStatusUpdate, HoldStatusUpdate, AcceptCall, EndCall, SwapCalls,
     * InitiateCall by AddressBookID ([6][u8 service][id UTF-8]). */
    private static final int TEL_CMD_MUTE = 1, TEL_CMD_HOLD = 2, TEL_CMD_ACCEPT = 3, TEL_CMD_END = 4,
                             TEL_CMD_SWAP = 5, TEL_CMD_DIAL_CONTACT = 6;
    /* BAP callMode, as stock BAPPropertyTelCombinedNumbers.getCallMode maps DSI stacks. */
    private static final int CALLMODE_MISSED = 1, CALLMODE_RECEIVED = 2, CALLMODE_DIALED = 3;
    /* DSICarplay BUTTON_*: CarPlay's HID telephony keys, as stock MFLPhoneButtonsListener
     * posts them.  We send none of them: on the car they reached the phone neither
     * from the cluster nor from the wheel (2026-09-24), so every call action is iAP2. */
    private static final int BUTTON_ACCEPT_CALL = 13, BUTTON_END_CALL = 14, BUTTON_FLASH_CALL = 20;
    private static final int RESULT_OK = 0, RESULT_FAILED = 1;

    /** CarPlay-side commands, installed by CarplayDSILifecycleController while DSI is up. */
    public interface Commands {
        /** Dial through CarPlay (requestUI2 "tel:<number>", stock callNumber). */
        void dial(String number);
    }

    private static volatile Commands commands;

    /* The steering-wheel Tel key reaches us twice: stock MFLPhoneButtonsListener
     * posts a HID key (FLASH 20, or HOOK 13 / HANGUP 14 on two-key wheels) and the
     * VC sends AcceptCall/HangupCall over BAP (App::handleKeyEvent, event 479).
     * During a call the BAP copy is answered over iAP2, so the stock HID copy is
     * dropped: it did nothing on the car, and on a unit where it works it would act
     * a second time (a FLASH right after an accept).  Without a call the VC sends no
     * BAP action (it opens the list itself), so stock's key is left alone. */
    public static boolean dropStockPhoneKey(int id) {
        if (id != BUTTON_ACCEPT_CALL && id != BUTTON_END_CALL && id != BUTTON_FLASH_CALL) return false;
        GatedPhoneService g;
        synchronized (GatedPhoneService.class) { g = instance; }
        return g != null && g.gatedWithCall();
    }

    private synchronized boolean gatedWithCall() { return gated && liveCalls() > 0; }

    public static void setCommands(Commands c) { commands = c; }

    private static GatedPhoneService instance;

    final CombiBAPServicePhone real;
    private boolean gated;

    /* Last stock values of the gated functions, replayed on gate-off. */
    private FsgSetup stockFsg;
    private boolean haveStockOp;
    private int stockTelState;
    private boolean stockPrivacy, stockEnhancedPrivacy;
    private boolean[] stockSupport;
    private CombiBAPCallState[] stockStates;
    private boolean stockEcallPending;
    private CombiBAPCallInfo[] stockInfos;
    private CallStartTime[] stockDurations;
    private CombiBAPCallStackEntry[] stockRecents;
    private CombiBAPFavoriteNumberEntry[] stockFavorites;
    private boolean haveStockMute, stockMute;
    private int stockSignal = -1, stockRegState, stockNetType, stockDataType;
    private boolean haveStockRegState, haveStockProvider, haveStockMissed;
    private int stockProvNameValid, stockProvIdValid, stockMissedCalls, stockMissedNumbers;
    private CombiBAPhoneMobileBatteryLevel stockBattery;
    private String stockProvName, stockProvId;

    /* CarPlay signal/carrier from the stock CommunicationsUpdate (hook EVT_TEL_COMM). */
    private int signalPct, regState;
    private String carrier = "";
    /* iPhone battery 0..100 from PowerUpdate, -1 = not reported. */
    private int batteryPct = -1, loggedBatteryPct = -2;
    /* Missed-call indication: unreturned missed calls newer than the last VC reset. */
    private long missedResetTs, newestRecentTs;   /* phone clock, s */
    private int missedCalls, missedNumbers;

    /* CarPlay recents/favorites from hook/telephony (iAP2 ListUpdate) and our mic mute. */
    private CombiBAPCallStackEntry[] recents = new CombiBAPCallStackEntry[0];
    private CombiBAPFavoriteNumberEntry[] favorites = new CombiBAPFavoriteNumberEntry[0];
    /* Parallel to favorites: AddressBookID and Service, for favorites iOS sends without a number. */
    private String[] favoriteIds = new String[0];
    private int[] favoriteServices = new int[0];
    private boolean muted;

    /* CarPlay calls: slot order follows DSICarplay's array.
     * dio_manager keys its call map by uniqueCallID, or by phoneNumber while that is empty,
     * and erases an entry only on DSI_DISCONNECTED. A call first reported without a CallUUID
     * and later with one (or the reverse) therefore leaves an orphan under the other key for
     * the rest of the session; ignoredCallKeys hides those (luka-dev MHI2Q-2026-10-29). */
    private CallState[] calls = new CallState[0];
    private final java.util.HashSet ignoredCallKeys = new java.util.HashSet();
    private int hangupSeq;
    private final String[] activeIds = new String[SLOTS];
    private final long[] activeSince = new long[SLOTS];

    public GatedPhoneService(CombiBAPServicePhone r) {
        this.real = r;
        synchronized (GatedPhoneService.class) { instance = this; }
        CarplayBus.getInstance().on(CarplayBus.EVT_TEL_LISTS, new CarplayBus.Listener() {
            public void onFrame(int type, int flags, byte[] payload, int len) {
                setLists(CarplayBus.parseText(payload, len));
            }
        });
        CarplayBus.getInstance().on(CarplayBus.EVT_TEL_COMM, new CarplayBus.Listener() {
            public void onFrame(int type, int flags, byte[] payload, int len) {
                setComm(CarplayBus.parseText(payload, len));
            }
        });
    }

    /** Wraps once: a second registration must not stack wrappers. */
    public static Object wrap(Object service) {
        if (service instanceof GatedPhoneService || !(service instanceof CombiBAPServicePhone)) return service;
        return new GatedPhoneService((CombiBAPServicePhone)service);
    }

    /** From the patched CarplayDSILifecycleController (DSICarplay.updateCallState). */
    public static void onCarPlayCalls(CallState[] cs) {
        GatedPhoneService g;
        synchronized (GatedPhoneService.class) { g = instance; }
        if (g != null) g.setCalls(cs);
    }

    /** True while at least one CarPlay call is ringing, dialing, active, or held. */
    public static boolean hasActiveCalls() {
        GatedPhoneService g;
        synchronized (GatedPhoneService.class) { g = instance; }
        return g != null && g.hasLiveCallsSync();
    }

    private synchronized boolean hasLiveCallsSync() {
        return gated && liveCalls() > 0;
    }

    /** A VC telephone action. Returns false when the gate is off, so stock handles it. */
    static boolean onVcAction(int action, String number) {
        GatedPhoneService g;
        synchronized (GatedPhoneService.class) { g = instance; }
        return g != null && g.handle(action, number, null);
    }

    /** A VC dial: the entry's number and the name the VC shows for it. */
    static boolean onVcDial(String number, String name) {
        GatedPhoneService g;
        synchronized (GatedPhoneService.class) { g = instance; }
        return g != null && g.handle(ACT_DIAL, number, name);
    }

    private synchronized boolean handle(int action, String number, String name) {
        if (!gated) return false;
        Commands c = commands;
        int result = RESULT_FAILED;
        switch (action) {
            case ACT_ACCEPT:
                /* iAP2 AcceptCall, action 0, no UUID: iOS answers the incoming call. */
                if (has(BAP_RINGING) && tel(TEL_CMD_ACCEPT, false)) result = RESULT_OK;
                real.acceptCallResult(result);
                break;
            case ACT_HANGUP:
                /* iAP2 EndCall, action 0, no UUID: iOS ends the frontmost call,
                 * dialing included. Always OK, so the VC never stays on "Ending call";
                 * a 1 s watchdog drops an orphaned entry if no CallStateUpdate follows. */
                if (liveCalls() > 0) {
                    tel(TEL_CMD_END, false);
                    scheduleHangupWatchdog();
                }
                result = RESULT_OK;
                real.hangupCallResult(result);
                if (liveCalls() == 0) publishCalls();
                break;
            case ACT_HOLD:
            case ACT_RESUME:
                /* Explicit iAP2 HoldStatusUpdate, not the HID Flash toggle. */
                if (liveCalls() > 0 && tel(TEL_CMD_HOLD, action == ACT_HOLD)) result = RESULT_OK;
                if (action == ACT_HOLD) real.callHoldResult(result);
                else real.resumeCallResult(result);
                break;
            case ACT_SWAP:
                /* iAP2 SwapCalls (no params): iOS swaps the active and the held call. */
                if (liveCalls() > 1 && tel(TEL_CMD_SWAP, false)) result = RESULT_OK;
                real.swapCallsResult(result);
                break;
            case ACT_MUTE_ON:
            case ACT_MUTE_OFF: {
                boolean on = action == ACT_MUTE_ON;
                if (liveCalls() > 0 && tel(TEL_CMD_MUTE, on)) {
                    /* ponytail: optimistic; iOS mute changes made on the phone are not
                     * read back (CommunicationsUpdate is stock's subscription). */
                    muted = on;
                }
                real.updateMicMuteState(muted);
                result = muted == on ? RESULT_OK : RESULT_FAILED;
                break;
            }
            case ACT_RESET_MISSED:
                missedResetTs = newestRecentTs;   /* phone clock: HU and phone clocks may differ */
                missedCalls = missedNumbers = 0;
                real.updateMissedCallIndication(0, 0);
                result = RESULT_OK;
                break;
            case ACT_DIAL: {
                String n = dialable(number);
                if (c != null && n != null) {
                    if (!n.equals(number)) Log.i(TAG, "dial: number formatting removed ("
                        + number.length() + " -> " + n.length() + " chars)");
                    c.dial(n);
                    result = RESULT_OK;
                } else if (n == null && dialFavoriteContact(name)) {
                    result = RESULT_OK;          /* iOS calls the contact itself (InitiateCall) */
                } else if (n == null) {
                    Log.w(TAG, "dial: entry has no phone number and no matching favorite ("
                        + (number == null ? 0 : number.length()) + " chars)");
                }
                real.dialNumberResult(result);
                break;
            }
            default:
                return false;
        }
        Log.i(TAG, "VC action " + action + " -> " + (result == RESULT_OK ? "CarPlay" : "failed"));
        return true;
    }

    /** The number as a tel: URL body, or null when it has no digit.  A favorite's RemoteID is the
     *  number as stored in the contact ("+48 600 123 456", "(555) 123-4567", NBSP and bidi marks),
     *  while a recent's is plain digits; iOS ignores a tel: URL with those characters, so only
     *  digits, a leading '+', '*', '#' (as %23) and the ',' / ';' pause characters are kept. */
    static String dialable(String number) {
        if (number == null) return null;
        StringBuffer b = new StringBuffer(number.length());
        boolean digit = false;
        for (int i = 0; i < number.length(); i++) {
            char ch = number.charAt(i);
            if (ch >= '0' && ch <= '9') { b.append(ch); digit = true; }
            else if (ch >= '０' && ch <= '９') { b.append((char)(ch - '０' + '0')); digit = true; }
            else if ((ch == '+' || ch == '＋') && b.length() == 0) b.append('+');
            else if (ch == '*' || ch == ',' || ch == ';') b.append(ch);
            else if (ch == '#') b.append("%23");
        }
        return digit ? b.toString() : null;
    }

    /** A favorite iOS listed without a number (RemoteID): dial its contact by AddressBookID.
     *  The VC echoes the entry's name, which is how the entry is found again. */
    private boolean dialFavoriteContact(String name) {
        if (name == null || name.length() == 0) return false;
        for (int i = 0; i < favorites.length && i < favoriteIds.length; i++) {
            if (!name.equals(favorites[i].getName()) || favoriteIds[i].length() == 0) continue;
            byte[] id;
            try { id = favoriteIds[i].getBytes("UTF-8"); } catch (java.io.UnsupportedEncodingException e) { return false; }
            byte[] cmd = new byte[2 + id.length];
            cmd[0] = (byte) TEL_CMD_DIAL_CONTACT;
            cmd[1] = (byte) favoriteServices[i];
            System.arraycopy(id, 0, cmd, 2, id.length);
            boolean sent = telSink.send(cmd);
            Log.i(TAG, "dial: favorite " + i + " without a number -> InitiateCall " + (sent ? "sent" : "not sent"));
            return sent;
        }
        return false;
    }

    private static boolean tel(int action, boolean on) {
        return telSink.send(new byte[] { (byte)action, (byte)(on ? 1 : 0) });
    }

    /** CMD_TEL to hook/telephony; replaced by tests. */
    interface TelSink { boolean send(byte[] cmd); }
    static TelSink telSink = new TelSink() {
        public boolean send(byte[] cmd) { return CarplayBus.getInstance().sendBinary(CarplayBus.CMD_TEL, cmd); }
    };

    private boolean has(int bapState) {
        for (int i = 0; i < calls.length; i++)
            if (calls[i] != null && bapState(calls[i]) == bapState) return true;
        return false;
    }

    private int liveCalls() {
        int n = 0;
        for (int i = 0; i < calls.length; i++) if (calls[i] != null && bapState(calls[i]) != 0) n++;
        return n;
    }

    private synchronized void setCalls(CallState[] cs) {
        hangupSeq++;
        if (cs == null || cs.length == 0) {
            ignoredCallKeys.clear();
            calls = new CallState[0];
            if (gated) publishCalls();
            return;
        }
        /* 1. A disconnecting entry, or a keyed entry next to a keyless one for the same
         *    call, marks the copies dio_manager left under the other key. */
        boolean anyDisconnected = false;
        for (int i = 0; i < cs.length; i++) {
            CallState a = cs[i];
            if (a == null) continue;
            if (a.status == DSI_DISCONNECTED) {
                anyDisconnected = true;
                ignoredCallKeys.remove(callKey(a));
                for (int j = 0; j < cs.length; j++) {
                    CallState b = cs[j];
                    if (b == null || b == a || b.status == DSI_DISCONNECTED) continue;
                    if (sameOrOrphanedCall(b, a)) ignoredCallKeys.add(callKey(b));
                }
            } else if (str(a.uniqueCallID).length() > 0) {
                for (int j = 0; j < cs.length; j++) {
                    CallState b = cs[j];
                    if (b == null || b == a || b.status == DSI_DISCONNECTED) continue;
                    if (str(b.uniqueCallID).length() == 0 && sameOrOrphanedCall(b, a))
                        ignoredCallKeys.add(callKey(b));
                }
            }
        }
        /* 2. Without a disconnect in this update, a keyed call that becomes active again
         *    is real: show it. */
        java.util.ArrayList kept = new java.util.ArrayList(cs.length);
        for (int i = 0; i < cs.length; i++) {
            CallState c = cs[i];
            if (c == null || c.status == DSI_DISCONNECTED) continue;
            String key = callKey(c);
            if (!anyDisconnected && c.status == DSI_ACTIVE && str(c.uniqueCallID).length() > 0)
                ignoredCallKeys.remove(key);
            if (!ignoredCallKeys.contains(key)) kept.add(c);
        }
        calls = (CallState[]) kept.toArray(new CallState[kept.size()]);
        if (gated) publishCalls();
    }

    /* EndCall sent: if no CallStateUpdate arrives within 1 s, the calls that were live are
     * orphans of dio_manager's map; hide them. Any update in between cancels this. */
    private void scheduleHangupWatchdog() {
        final int seq = ++hangupSeq;
        final java.util.ArrayList snapshot = new java.util.ArrayList(calls.length);
        for (int i = 0; i < calls.length; i++)
            if (calls[i] != null && bapState(calls[i]) != 0) snapshot.add(callKey(calls[i]));
        if (snapshot.isEmpty()) return;
        Thread t = new Thread(new Runnable() {
            public void run() {
                try { Thread.sleep(HANGUP_WATCHDOG_MS); } catch (InterruptedException e) { return; }
                hangupWatchdog(seq, snapshot);
            }
        }, "CPHangupWatchdog");
        t.setDaemon(true);
        t.start();
    }

    static final long HANGUP_WATCHDOG_MS = 1000L;

    synchronized void hangupWatchdog(int seq, java.util.List snapshot) {
        if (!gated || hangupSeq != seq) return;
        java.util.ArrayList remaining = new java.util.ArrayList(calls.length);
        boolean changed = false;
        for (int i = 0; i < calls.length; i++) {
            CallState c = calls[i];
            if (c == null) continue;
            String k = callKey(c);
            if (snapshot.contains(k)) { ignoredCallKeys.add(k); changed = true; }
            else remaining.add(c);
        }
        if (!changed) return;
        calls = (CallState[]) remaining.toArray(new CallState[remaining.size()]);
        Log.i(TAG, "VC hangup watchdog cleared stale call entry");
        publishCalls();
    }

    private static String callKey(CallState c) {
        String id = str(c.uniqueCallID);
        if (id.length() > 0) return "id:" + id;
        return "num:" + str(c.phoneNumber) + ":" + c.direction;
    }

    /* Last 7 digits: the same number with and without the country prefix. */
    private static String normDigits(String s) {
        if (s == null || s.length() == 0) return "";
        StringBuffer sb = new StringBuffer(s.length());
        for (int i = 0; i < s.length(); i++) {
            char ch = s.charAt(i);
            if (ch >= '0' && ch <= '9') sb.append(ch);
        }
        String d = sb.toString();
        return d.length() > 7 ? d.substring(d.length() - 7) : d;
    }

    private static boolean sameOrOrphanedCall(CallState candidate, CallState ref) {
        String d1 = normDigits(candidate.phoneNumber), d2 = normDigits(ref.phoneNumber);
        if (d1.length() > 0 && d1.equals(d2)) return true;
        boolean id1Empty = str(candidate.uniqueCallID).length() == 0;
        boolean id2Empty = str(ref.uniqueCallID).length() == 0;
        if (id1Empty != id2Empty && candidate.direction == ref.direction) return true;
        return candidate.status == DSI_CONNECTING && candidate.direction == ref.direction
            && (d1.length() == 0 || d2.length() == 0);
    }

    /* ---------------- gated functions ---------------- */

    public synchronized void updateFsgSetup(FsgSetup f) {
        stockFsg = f;
        boolean carplay = f != null && f.getMobileConnectionType() == MCT_APPLE_LINK;
        if (carplay != gated) {
            gated = carplay;
            Log.i(TAG, "cluster phone tab " + (gated ? "-> CarPlay calls" : "-> stock"));
            GatedAddressBook.setCarPlay(gated);                /* Directory: no phonebook */
            if (gated) publishCarPlay();
            else { ignoredCallKeys.clear(); calls = new CallState[0]; replayStock(); }  /* no stale call on the next session */
            return;
        }
        real.updateFsgSetup(gated ? asHfp(f) : forStockCluster(f));
    }

    public synchronized void updateFSGOperationState(int telState, boolean privacy, boolean enhanced) {
        haveStockOp = true;
        stockTelState = telState; stockPrivacy = privacy; stockEnhancedPrivacy = enhanced;
        if (!gated) real.updateFSGOperationState(telState, privacy, enhanced);
    }

    public synchronized void updateMobileServiceSupport(boolean[] b) {
        stockSupport = b;
        if (!gated) real.updateMobileServiceSupport(b);
    }

    public synchronized void updateCallStates(CombiBAPCallState[] s, boolean ecallPending) {
        stockStates = s; stockEcallPending = ecallPending;
        if (!gated) real.updateCallStates(s, ecallPending);
    }

    public synchronized void updateCallInfo(CombiBAPCallInfo[] i) {
        stockInfos = i;
        if (!gated) real.updateCallInfo(i);
    }

    public synchronized void updateCallDurations(CallStartTime[] d) {
        stockDurations = d;
        if (!gated) real.updateCallDurations(d);
    }

    public synchronized void updateMicMuteState(boolean m) {
        haveStockMute = true; stockMute = m;
        if (!gated) real.updateMicMuteState(m);
    }

    public synchronized void updateSignalQuality(int q) {
        stockSignal = q;
        if (!gated) real.updateSignalQuality(q);
    }

    public synchronized void updateRegisterState(int reg, int net, int data) {
        haveStockRegState = true; stockRegState = reg; stockNetType = net; stockDataType = data;
        if (!gated) real.updateRegisterState(reg, net, data);
    }

    public synchronized void updateNetworkProvider(int nameValid, String name, int idValid, String id) {
        haveStockProvider = true;
        stockProvNameValid = nameValid; stockProvName = name; stockProvIdValid = idValid; stockProvId = id;
        if (!gated) real.updateNetworkProvider(nameValid, name, idValid, id);
    }

    public synchronized void updateMissedCallIndication(int calls, int numbers) {
        haveStockMissed = true; stockMissedCalls = calls; stockMissedNumbers = numbers;
        if (!gated) real.updateMissedCallIndication(calls, numbers);
    }

    public synchronized void updateCombinedNumbers(CombiBAPCallStackEntry[] e) {
        stockRecents = e;
        if (!gated) real.updateCombinedNumbers(e);
    }

    public synchronized void updateFavoriteList(CombiBAPFavoriteNumberEntry[] e) {
        stockFavorites = e;
        if (!gated) real.updateFavoriteList(e);
    }

    /* ---------------- CarPlay lists ---------------- */

    private synchronized void setLists(CarplayBus.Data d) {
        int rn = Math.max(0, d.num("r.n", 0)), fn = Math.max(0, d.num("f.n", 0));
        CombiBAPCallStackEntry[] r = new CombiBAPCallStackEntry[rn];
        /* Missed calls at the top of the list (newest first), stopping at the first
         * answered or dialed call or at the last VC reset: the unreturned ones. */
        int mCalls = 0, mNumbers = 0;
        boolean countingMissed = true;
        for (int i = 0; i < rn; i++) {
            if (!countingMissed) break;
            long ts = d.num64("r." + i + ".ts", 0);
            if (d.num("r." + i + ".type", -1) != 3 || (ts > 0 && ts <= missedResetTs)) {
                countingMissed = false;
            } else {
                mNumbers++;
                mCalls += Math.max(1, d.num("r." + i + ".occ", 1));
            }
        }
        missedCalls = mCalls;
        missedNumbers = mNumbers;
        newestRecentTs = rn > 0 ? d.num64("r.0.ts", 0) : 0;
        for (int i = 0; i < rn; i++) {
            /* iAP2 recents and favorites: param 1 = RemoteID (number), 2 = DisplayName,
             * which is the formatted number when there is no contact (accessoryd
             * CoreAccessories_18; car capture). */
            String num = d.str("r." + i + ".a", ""), name = d.str("r." + i + ".b", "");
            r[i] = recent(i, isNumber(name) ? "" : name, num,
                          callMode(d.num("r." + i + ".type", -1)), d.num64("r." + i + ".ts", 0));
        }
        CombiBAPFavoriteNumberEntry[] f = new CombiBAPFavoriteNumberEntry[fn];
        String[] fid = new String[fn];
        int[] fsvc = new int[fn];
        for (int i = 0; i < fn; i++) {
            String num = d.str("f." + i + ".a", ""), name = d.str("f." + i + ".b", "");
            f[i] = new CombiBAPFavoriteNumberEntry(i, isNumber(name) ? "" : name, num);
            fid[i] = d.str("f." + i + ".id", "");
            fsvc[i] = d.num("f." + i + ".svc", 0);
        }
        recents = r;
        favorites = f;
        favoriteIds = fid;
        favoriteServices = fsvc;
        Log.i(TAG, "CarPlay lists: recents=" + rn + " favorites=" + fn);
        if (gated) publishLists();
    }

    private void publishLists() {
        real.updateCombinedNumbers(recents);
        real.updateFavoriteList(favorites);
        real.updateMissedCallIndication(missedCalls, missedNumbers);
    }

    /* ---------------- CarPlay signal / carrier ---------------- */

    private synchronized void setComm(CarplayBus.Data d) {
        int sig = d.num("signal", -1), reg = d.num("registration", -1);
        /* iAP2 SignalStrength 0..5 -> BAP percent (stock DSI scale, 20 per bar). */
        signalPct = sig < 0 ? 0 : Math.min(sig, 5) * 20;
        regState = bapRegisterState(reg, d.bool("airplane", false));
        carrier = d.str("carrier", "");
        batteryPct = d.num("battery", -1);
        if (gated) publishComm();
    }

    /* BAP charge level buckets, as stock BAPPropertyTelMobileBatteryLevel maps HFP bars;
     * the lowest bucket (below 20 %) is critical there too. */
    public static CombiBAPhoneMobileBatteryLevel batteryLevel(int pct) {
        int level;
        if (pct < 0) level = CombiBAPhoneMobileBatteryLevel.BATTERY_LEVEL_UNKNOWN_LEVEL_NOT_RECEIVED;
        else if (pct == 0) level = CombiBAPhoneMobileBatteryLevel.BATTERY_LEVEL_CHARGE_LEVEL_0;
        else if (pct < 20) level = CombiBAPhoneMobileBatteryLevel.BATTERY_LEVEL_CHARGE_LEVEL_0_19;
        else if (pct < 40) level = CombiBAPhoneMobileBatteryLevel.BATTERY_LEVEL_CHARGE_LEVEL_20_39;
        else if (pct < 60) level = CombiBAPhoneMobileBatteryLevel.BATTERY_LEVEL_CHARGE_LEVEL_40_59;
        else if (pct < 80) level = CombiBAPhoneMobileBatteryLevel.BATTERY_LEVEL_CHARGE_LEVEL_60_79;
        else level = CombiBAPhoneMobileBatteryLevel.BATTERY_LEVEL_CHARGE_LEVEL_80_100;
        return CombiBAPhoneMobileBatteryLevel.createOnlyMobileChargeLevel1(
            new CombiBAPhoneMobileBatteryLevel.ChargeLevel(level, pct >= 0 && pct < 20));
    }

    private void publishComm() {
        real.updateSignalQuality(signalPct);
        real.updateRegisterState(regState, 0, 0);
        int valid = carrier.length() > 0 ? 1 : 0;
        real.updateNetworkProvider(valid, carrier, valid, carrier);
        real.updateMobileBatteryLevel(batteryLevel(batteryPct));
        if (batteryPct != loggedBatteryPct) {
            loggedBatteryPct = batteryPct;
            Log.i(TAG, "iPhone battery " + batteryPct + "% -> VC level "
                + batteryLevel(batteryPct).getMobileChargeLevel1().getChargeLevelPercent());
        }
    }

    /* iAP2 RegistrationStatus (0 unknown, 1 not registered, 2 searching, 3 denied,
     * 4 home, 5 roaming, 6 emergency only) -> BAP RegisterState. */
    private static int bapRegisterState(int iap2, boolean airplane) {
        if (airplane) return 0;
        switch (iap2) {
            case 2: return 2;
            case 3: return 3;
            case 4: return 1;
            case 5: return 4;
            default: return 0;
        }
    }

    private static boolean isNumber(String s) {
        if (s == null || s.length() == 0) return false;
        for (int i = 0; i < s.length(); i++) {
            char ch = s.charAt(i);
            if ("0123456789+*#-(). /\u00a0\u202a\u202b\u202c\u202d\u202e\u2066\u2067\u2068\u2069".indexOf(ch) < 0)
                return false;
        }
        return true;
    }

    /* iAP2 recents Type 1 incoming / 2 outgoing / 3 missed, 0 other (iOS
     * ACCCommunicationsFeaturePlugin currentRecentsListWithCoalescing; car capture 2). */
    private static int callMode(int iap2Type) {
        switch (iap2Type) {
            case 1: return CALLMODE_RECEIVED;
            case 2: return CALLMODE_DIALED;
            case 3: return CALLMODE_MISSED;
            default: return 0;
        }
    }

    private static CombiBAPCallStackEntry recent(int pos, String name, String num, int mode, long ts) {
        if (ts <= 0) return new CombiBAPCallStackEntry(pos, name, num, 0, mode);
        Calendar c = Calendar.getInstance();
        c.setTime(new java.util.Date(HuTime.convertUtcToLocalMs(ts > 100000000000L ? ts : ts * 1000L)));  /* s or ms */
        return new CombiBAPCallStackEntry(pos, name, num, 0, mode,
            (short)c.get(Calendar.DAY_OF_MONTH), (short)(c.get(Calendar.MONTH) + 1),
            (short)c.get(Calendar.YEAR), (short)c.get(Calendar.HOUR_OF_DAY),
            (short)c.get(Calendar.MINUTE), (short)c.get(Calendar.SECOND),
            new TelAdbEntryStruct());
    }

    /* ---------------- publishing ---------------- */

    /** Stock FctID 14 outside the CarPlay gate: Android Auto's 6 becomes HFP (3) so the VC
     *  shows the stock Bluetooth call view instead of its "Android Auto" note. */
    private FsgSetup forStockCluster(FsgSetup f) {
        boolean aa = f != null && f.getMobileConnectionType() == MCT_GOOGLE_LINK;
        if (aa) {
            try { aa = !new java.io.File(AA_PHONE_TAB_OFF).exists(); } catch (Throwable t) { }
        }
        if (aa != aaAsHfp) {
            aaAsHfp = aa;
            Log.i(TAG, "cluster phone tab " + (aa ? "-> Android Auto phone as Bluetooth HFP" : "-> stock"));
        }
        return aa ? asHfp(f) : f;
    }

    private static FsgSetup asHfp(FsgSetup f) {
        return FsgSetup.builder()
            .setInternalSimCardReaderPresent(f.isInternalSimCardReaderPresent())
            .setCableConnectionPossible(f.isCableConnectionPossible())
            .setHfpConnectionPossible(true)
            .setRsapConnectionPossible(f.isRsapConnectionPossible())
            .setAppleLinkConnectionPossible(f.isAppleLinkConnectionPossible())
            .setGoogleLinkConnectionPossible(f.isGoogleLinkConnectionPossible())
            .setMobileConnectionType(MCT_HFP)
            .build();
    }

    private void publishCarPlay() {
        real.updateFsgSetup(asHfp(stockFsg));
        real.updateFSGOperationState(TEL_STATE_PHONE_ON_NO_SIM, false, true);
        boolean[] support = new boolean[MOBILE_SERVICE_SUPPORT_MAX];
        support[MOBILE_SERVICE_SUPPORT_CALL_STATE] = true;
        support[MOBILE_SERVICE_SUPPORT_CALL_INFO] = true;
        support[MOBILE_SERVICE_SUPPORT_CALL_DURATION_SYNC] = true;
        support[MOBILE_SERVICE_SUPPORT_DIAL_NUMBER] = true;
        support[MOBILE_SERVICE_SUPPORT_HANGUP_CALL] = true;
        support[MOBILE_SERVICE_SUPPORT_ACCEPT_CALL] = true;
        support[MOBILE_SERVICE_SUPPORT_CALL_HOLD] = true;
        support[MOBILE_SERVICE_SUPPORT_RESUME_CALL] = true;
        support[MOBILE_SERVICE_SUPPORT_MICRO_MUTE_ON_OFF] = true;
        support[MOBILE_SERVICE_SUPPORT_COMBINED_NUMBERS] = true;
        support[MOBILE_SERVICE_SUPPORT_FAVORITE_LIST] = true;
        support[MOBILE_SERVICE_SUPPORT_MISSED_CALL_INDICATION] = true;
        support[MOBILE_SERVICE_SUPPORT_REGISTER_STATE] = true;
        support[MOBILE_SERVICE_SUPPORT_NETWORK_PROVIDER] = true;
        support[MOBILE_SERVICE_SUPPORT_SIGNAL_QUALITY] = true;
        support[MOBILE_SERVICE_SUPPORT_MOBILE_BATTERY_LEVEL] = true;
        real.updateMobileServiceSupport(support);
        muted = false;
        real.updateMicMuteState(false);
        publishCalls();
        publishLists();
        publishComm();
    }

    private void replayStock() {
        if (stockFsg != null) real.updateFsgSetup(forStockCluster(stockFsg));
        if (haveStockOp) real.updateFSGOperationState(stockTelState, stockPrivacy, stockEnhancedPrivacy);
        if (stockSupport != null) real.updateMobileServiceSupport(stockSupport);
        real.updateCallStates(stockStates, stockEcallPending);  /* null = stock's EMPTY_CALL_STATES */
        real.updateCallInfo(stockInfos);
        real.updateCallDurations(stockDurations != null ? stockDurations : noDurations());
        real.updateMicMuteState(haveStockMute && stockMute);
        real.updateCombinedNumbers(stockRecents != null ? stockRecents : new CombiBAPCallStackEntry[0]);
        real.updateFavoriteList(stockFavorites != null ? stockFavorites : new CombiBAPFavoriteNumberEntry[0]);
        real.updateSignalQuality(stockSignal >= 0 ? stockSignal : 0);
        if (haveStockRegState) real.updateRegisterState(stockRegState, stockNetType, stockDataType);
        else real.updateRegisterState(0, 0, 0);
        if (haveStockProvider) real.updateNetworkProvider(stockProvNameValid, stockProvName, stockProvIdValid, stockProvId);
        else real.updateNetworkProvider(0, "", 0, "");
        if (haveStockMissed) real.updateMissedCallIndication(stockMissedCalls, stockMissedNumbers);
        else real.updateMissedCallIndication(0, 0);
        real.updateMobileBatteryLevel(stockBattery != null ? stockBattery
            : CombiBAPhoneMobileBatteryLevel.createOnlyMobileChargeLevel1(new CombiBAPhoneMobileBatteryLevel.ChargeLevel(
                CombiBAPhoneMobileBatteryLevel.BATTERY_LEVEL_UNKNOWN_DEVICE_NOT_CONNECTED, false)));
    }

    private void publishCalls() {
        CombiBAPCallState[] states = new CombiBAPCallState[SLOTS];
        CombiBAPCallInfo[] infos = new CombiBAPCallInfo[SLOTS];
        CallStartTime[] durations = noDurations();
        long now = System.currentTimeMillis();
        int n = 0;
        for (int i = 0; i < calls.length && n < SLOTS; i++) {
            CallState c = calls[i];
            int st = c != null ? bapState(c) : 0;
            if (st == 0) continue;
            states[n] = new CombiBAPCallState(st, BAP_CALL_TYPE_VOICE, 0);
            states[n].setCallOptionAcceptCallEnabled(st == BAP_RINGING);
            states[n].setCallOptionCallHoldEnabled(st == BAP_ACTIVE);
            states[n].setCallOptionResumeCallEnabled(st == BAP_HELD);
            infos[n] = new CombiBAPCallInfo(name(c), str(c.phoneNumber), BAP_CALL_TYPE_VOICE, 0);
            if (st == BAP_ACTIVE || st == BAP_HELD) {
                String id = str(c.uniqueCallID);
                if (!id.equals(activeIds[n])) { activeIds[n] = id; activeSince[n] = now; }
                durations[n] = elapsed(now - activeSince[n]);
            } else {
                activeIds[n] = null;
            }
            n++;
        }
        for (int i = n; i < SLOTS; i++) activeIds[i] = null;
        real.updateCallStates(states, false);
        real.updateCallInfo(infos);
        real.updateCallDurations(durations);
        if (n == 0 && muted) { muted = false; real.updateMicMuteState(false); }
    }

    private static int bapState(CallState c) {
        switch (c.status) {
            case DSI_ACTIVE: return BAP_ACTIVE;
            case DSI_HOLDING: return BAP_HELD;
            case DSI_CONNECTING: return c.direction == DSI_INCOMING ? BAP_RINGING : BAP_DIALING;
            case DSI_DISCONNECTED:
            default: return 0;
        }
    }

    /* Stock shows an empty name when it equals the number (TelBAPCall.getDisplayName). */
    private static String name(CallState c) {
        String n = str(c.callerName);
        return n.equals(str(c.phoneNumber)) ? "" : n;
    }

    private static String str(String s) { return s != null ? s : ""; }

    private static CallStartTime elapsed(long ms) {
        int s = (int)(ms / 1000);
        return new CallStartTime(s / 3600, (s / 60) % 60, s % 60);
    }

    private static CallStartTime[] noDurations() {
        CallStartTime[] d = new CallStartTime[SLOTS];
        for (int i = 0; i < SLOTS; i++) d[i] = new CallStartTime(NO_DURATION);
        return d;
    }

    /* ---------------- pass-through ---------------- */

    public void updateLockState(int i) { real.updateLockState(i); }
    /* With CarPlay calls the stock HFP disconnect reason belongs to no call shown on the VC. */
    public synchronized void updateDisconnectReason(int i) { if (!gated) real.updateDisconnectReason(i); }
    public void dialNumberResult(int i) { real.dialNumberResult(i); }
    public void dialNumberFromAdbEntryResult(int i) { real.dialNumberFromAdbEntryResult(i); }
    public void dialServiceResult(int i) { real.dialServiceResult(i); }
    public void confirmEmergencyCallResult(int i) { real.confirmEmergencyCallResult(i); }
    public void hangupCallResult(int i) { real.hangupCallResult(i); }
    public void acceptCallResult(int i) { real.acceptCallResult(i); }
    public void callHoldResult(int i) { real.callHoldResult(i); }
    public void resumeCallResult(int i) { real.resumeCallResult(i); }
    public void releaseActiveCallAcceptWaitingCallResult(int i) { real.releaseActiveCallAcceptWaitingCallResult(i); }
    public void swapCallsResult(int i) { real.swapCallsResult(i); }
    public void callHoldAcceptWaitingCallResult(int i) { real.callHoldAcceptWaitingCallResult(i); }
    public void releaseAllCallsAcceptWaitingCallResult(int i) { real.releaseAllCallsAcceptWaitingCallResult(i); }
    public void setWaitingCallOnHoldResult(int i) { real.setWaitingCallOnHoldResult(i); }
    public void joinCallsResult(int i) { real.joinCallsResult(i); }
    public void splitCallResult(int i) { real.splitCallResult(i); }
    public synchronized void updateMobileBatteryLevel(CombiBAPhoneMobileBatteryLevel b) {
        stockBattery = b;
        if (!gated) real.updateMobileBatteryLevel(b);
    }
    public void updateMissedCalls(CombiBAPCallStackEntry[] acombibapcallstackentry) { real.updateMissedCalls(acombibapcallstackentry); }
    public void updateReceivedCalls(CombiBAPCallStackEntry[] acombibapcallstackentry) { real.updateReceivedCalls(acombibapcallstackentry); }
    public void updateDialedNumbers(CombiBAPCallStackEntry[] acombibapcallstackentry) { real.updateDialedNumbers(acombibapcallstackentry); }
    public void updateRingToneMuteState(boolean flag) { real.updateRingToneMuteState(flag); }
    public void updateAutomaticRedialActive(boolean flag) { real.updateAutomaticRedialActive(flag); }
    public void updateAutomaticRedialExtendedInfo(int i, String s, String s1, int j) { real.updateAutomaticRedialExtendedInfo(i, s, s1, j); }
    public void updateSupportedServiceNumbers(boolean flag, boolean flag1, boolean flag2, boolean flag3) { real.updateSupportedServiceNumbers(flag, flag1, flag2, flag3); }
}
