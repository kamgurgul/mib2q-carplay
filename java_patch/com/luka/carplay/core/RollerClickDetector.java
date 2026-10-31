package com.luka.carplay.core;

/**
 * Classifies short presses of the left steering-wheel roller without delaying
 * the DSI callback thread.  The owner serializes calls and publishes SINGLE
 * only after the double-click window has expired.
 */
public final class RollerClickDetector {
    public static final int NONE = 0;
    public static final int SINGLE = 1;
    public static final int DOUBLE = 2;

    private final long windowMs;
    private boolean pendingSingle;
    private long singleDueMs;
    private boolean secondPress;
    private int readySingles;

    public RollerClickDetector(long windowMs) {
        if (windowMs <= 0L) throw new IllegalArgumentException("windowMs");
        this.windowMs = windowMs;
    }

    /** Called on a map-tab PRESSED edge. */
    public void onPressed(long nowMs) {
        secondPress = pendingSingle && nowMs <= singleDueMs;
        if (secondPress) {
            pendingSingle = false;
        } else if (pendingSingle && nowMs > singleDueMs) {
            /* Preserve the first click even if this input edge races the worker. */
            pendingSingle = false;
            ++readySingles;
        }
    }

    /** Called on RELEASED. shortEligible is false after a long press or lost press. */
    public int onReleased(long nowMs, boolean shortEligible) {
        if (!shortEligible) {
            secondPress = false;
            return NONE;
        }
        if (secondPress) {
            secondPress = false;
            return DOUBLE;
        }
        pendingSingle = true;
        singleDueMs = nowMs + windowMs;
        return NONE;
    }

    /** A long press owns the gesture; discard a possible double-click candidate. */
    public void onLongPress() {
        secondPress = false;
    }

    /** Called by the lightweight worker. */
    public int poll(long nowMs) {
        if (readySingles > 0) {
            --readySingles;
            return SINGLE;
        }
        if (pendingSingle && nowMs >= singleDueMs) {
            pendingSingle = false;
            return SINGLE;
        }
        return NONE;
    }

    /** 0 means there is no pending single click. */
    public long waitMillis(long nowMs) {
        if (readySingles > 0) return 1L;
        if (!pendingSingle) return 0L;
        long wait = singleDueMs - nowMs;
        return wait > 0L ? wait : 1L;
    }

    public void cancel() {
        pendingSingle = false;
        secondPress = false;
        singleDueMs = 0L;
        readySingles = 0;
    }
}
