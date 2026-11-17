package com.luka.carplay.core;

import com.luka.carplay.framework.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;

/** Driver's map choice, shared by CarPlay and Android Auto and retained across boots.
 * Only explicit selections write flash; session and video-liveness changes do not.
 * Java 1.4 / Foundation 1.1. */
class ClusterMapPreference {
    private static final String TAG = "ClusterMap";
    private final File file;
    private volatile boolean stockMap;
    /* What flash holds: 1 = Audi, 0 = phone (also for a missing file), -1 = unreadable/invalid. */
    private int committed;
    private int pending = -1;
    private Thread worker;

    ClusterMapPreference(File file) {
        this.file = file;
        committed = readCommitted();
        stockMap = committed == 1;
        Log.i(TAG, "loaded " + (stockMap ? "Audi" : "phone") + " map");
    }

    static ClusterMapPreference onUnit() {
        return new ClusterMapPreference(new File("/mnt/app/root/cluster_map_source")) {
            void write(boolean stock) throws Exception {
                // Same fixed commands as the MMI settings; never run on the HMI callback.
                run("unset LD_PRELOAD; PATH=/proc/boot:/bin:/usr/bin:/usr/sbin:/sbin; mount -uw /mnt/app");
                super.write(stock);
                // Flush the rename too, before the car can shut down.
                run("unset LD_PRELOAD; PATH=/proc/boot:/bin:/usr/bin; sync");
            }
        };
    }

    boolean isStockMap() { return stockMap; }

    /** Apply immediately, serialize disk writes and coalesce any intervening selections.
     *  Compared with what flash holds (not the in-memory choice), so a failed save or a
     *  writer that could not start is retried by the next selection that differs from disk. */
    synchronized void select(boolean stock) {
        stockMap = stock;
        int want = stock ? 1 : 0;
        if (worker == null && want == committed) { pending = -1; return; }
        pending = want;
        if (worker != null) return;
        Thread t = new Thread(new Runnable() { public void run() { savePending(); } }, "carplay-map-choice");
        t.setDaemon(true);
        try {
            t.start();
            worker = t;
        } catch (Throwable failure) {
            Log.w(TAG, "Could not start preference writer: " + failure);
        }
    }

    private void savePending() {
        for (;;) {
            int value;
            synchronized (this) {
                if (pending < 0) { worker = null; return; }
                value = pending;
                pending = -1;
            }
            try {
                write(value == 1);
                synchronized (this) { committed = value; }
                Log.i(TAG, "saved " + (value == 1 ? "Audi" : "phone") + " map");
            } catch (Throwable failure) {
                // A storage failure must not stop the current map switch or later saves.
                Log.w(TAG, "Could not save " + (value == 1 ? "Audi" : "phone")
                    + " map; the previous choice returns after a restart: " + failure);
            }
        }
    }

    /** Atomic replacement keeps the previous choice if writing the new value fails. */
    void write(boolean stock) throws Exception {
        File tmp = new File(file.getPath() + ".new");
        FileOutputStream out = null;
        try {
            out = new FileOutputStream(tmp);
            out.write((stock ? "audi\n" : "phone\n").getBytes("UTF-8"));
            out.getFD().sync();
            out.close(); out = null;
            if (!tmp.renameTo(file)) throw new IOException("Cannot replace " + file);
        } finally {
            if (out != null) try { out.close(); } catch (IOException e) { }
            tmp.delete();
        }
    }

    private int readCommitted() {
        FileInputStream in = null;
        try {
            if (!file.exists()) return 0;           // Existing installs still default to the phone.
            in = new FileInputStream(file);
            byte[] bytes = new byte[32];
            int count = in.read(bytes);
            String value = count < 0 ? "" : new String(bytes, 0, count, "UTF-8").trim();
            if ("audi".equals(value)) return 1;
            if ("phone".equals(value)) return 0;
            Log.w(TAG, "Invalid map choice in " + file + "; using phone");
        } catch (Exception failure) {
            Log.w(TAG, "Could not read map choice; using phone: " + failure);
        } finally {
            if (in != null) try { in.close(); } catch (IOException e) { }
        }
        return -1;
    }

    private static void run(String command) throws Exception {
        Process p = Runtime.getRuntime().exec(new String[] { "/bin/sh", "-c", command });
        try {
            if (p.waitFor() != 0) throw new IOException("Map preference command failed: " + command);
        } finally {
            p.getInputStream().close();
            p.getErrorStream().close();
            p.getOutputStream().close();
        }
    }
}
