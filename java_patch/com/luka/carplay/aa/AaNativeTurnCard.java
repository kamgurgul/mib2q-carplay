/* Accepted VC Fct44 visibility for the Android Auto hook. Java 1.4 / Foundation 1.1.
 * SPDX-License-Identifier: GPL-3.0-or-later */
package com.luka.carplay.aa;

import java.io.RandomAccessFile;

public final class AaNativeTurnCard {
    public static final String PATH = "/tmp/aa_cluster_native_card";
    private static int last = -1;

    private AaNativeTurnCard() {}

    public static synchronized void publish(boolean visible) {
        int state = visible ? 1 : 0;
        if (state == last) return;
        RandomAccessFile f = null;
        try {
            f = new RandomAccessFile(PATH, "rw");
            f.seek(0);
            f.writeByte(visible ? '1' : '0');
            f.writeByte('\n');
            last = state;
        } catch (Throwable t) {
            /* Retry on the next Fct44 update; never interrupt the stock listener. */
        } finally {
            if (f != null) try { f.close(); } catch (Throwable t) { /* ignore */ }
        }
    }
}
