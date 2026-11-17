package com.luka.carplay.core;

import com.luka.carplay.framework.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Real files: restart, interrupted replacement, failed saves, and overlapping selections. */
public final class ClusterMapPreferenceTest {
    static void check(boolean ok, String why) { if (!ok) throw new AssertionError(why); }

    static void saved(ClusterMapPreference preference) throws Exception {
        Field f = ClusterMapPreference.class.getDeclaredField("worker");
        f.setAccessible(true);
        Thread worker;
        synchronized (preference) { worker = (Thread) f.get(preference); }
        if (worker != null) {
            worker.join(5000);
            check(!worker.isAlive(), "preference writer did not finish");
        }
    }

    static void text(File file, String value) throws Exception {
        FileOutputStream out = new FileOutputStream(file);
        try { out.write(value.getBytes("UTF-8")); }
        finally { out.close(); }
    }

    static final class BlockingPreference extends ClusterMapPreference {
        final CountDownLatch entered = new CountDownLatch(1);
        final CountDownLatch release = new CountDownLatch(1);
        int writes;
        BlockingPreference(File file) { super(file); }
        void write(boolean stock) throws Exception {
            if (++writes == 1) {
                entered.countDown();
                check(release.await(5, TimeUnit.SECONDS), "blocked save was not released");
            }
            super.write(stock);
        }
    }

    public static void main(String[] args) throws Exception {
        Log.setLevel(-1);
        File root = new File(args[0]);
        root.mkdirs();
        check(root.isDirectory() && root.list().length == 0, "empty test directory");
        File file = new File(root, "cluster_map_source");
        ClusterMapPreference p = new ClusterMapPreference(file);
        check(!p.isStockMap() && !file.exists(), "missing preference must default to phone without writing");
        p.select(true); saved(p);
        check(new ClusterMapPreference(file).isStockMap(), "Audi choice lost on restart");
        p.select(false); saved(p);
        check(!new ClusterMapPreference(file).isStockMap(), "phone choice lost on restart");
        p.select(true); saved(p);

        File tmp = new File(file.getPath() + ".new");
        text(tmp, "pho"); // An interrupted save must not replace the committed choice.
        check(new ClusterMapPreference(file).isStockMap(), "partial temporary file replaced Audi choice");
        check(tmp.delete() && tmp.mkdir(), "create write failure");
        text(new File(tmp, "block"), "");
        p.select(false); saved(p);
        check(!p.isStockMap(), "failed save prevented the immediate map switch");
        check(new ClusterMapPreference(file).isStockMap(), "failed save destroyed previous choice");
        check(new File(tmp, "block").delete() && tmp.delete(), "clear write failure");
        p.select(true); saved(p);
        p.select(false); saved(p);
        check(!new ClusterMapPreference(file).isStockMap(), "writer did not recover after failure");
        check(!tmp.exists(), "temporary file left after successful save");

        for (String invalid : new String[] { "", "junk", "audi-extra", "phone-extra" }) {
            text(file, invalid);
            check(!new ClusterMapPreference(file).isStockMap(), "invalid value did not default to phone");
        }
        check(file.delete() && file.mkdir(), "create unreadable preference");
        check(!new ClusterMapPreference(file).isStockMap(), "unreadable preference did not default to phone");
        check(file.delete(), "clear unreadable preference");

        BlockingPreference blocked = new BlockingPreference(file);
        blocked.select(true);
        check(blocked.entered.await(5, TimeUnit.SECONDS), "save did not start");
        blocked.select(false); blocked.select(true); blocked.select(false);
        check(!blocked.isStockMap(), "selection blocked behind disk work");
        blocked.release.countDown(); saved(blocked);
        check(blocked.writes == 2, "intervening choices were not coalesced");
        check(!new ClusterMapPreference(file).isStockMap(), "older save overwrote latest selection");
        blocked.select(false); saved(blocked);
        check(blocked.writes == 2, "unchanged preference wrote flash");

        // Invalid file: flash does not hold "phone" yet, so choosing it must write it.
        text(file, "junk");
        CountingPreference c = new CountingPreference(file);
        c.select(false); saved(c);
        check(c.writes == 1 && "phone".equals(raw(file)), "invalid file not repaired by phone choice");

        // Failed save: the next differing choice compares with flash, not with memory.
        c.fail = true; c.select(true); saved(c);
        check(c.writes == 2 && c.isStockMap() && "phone".equals(raw(file)), "failed save state");
        c.fail = false; c.select(false); saved(c);
        check(c.writes == 2, "choice already on flash was written again");
        c.select(true); saved(c);
        check(c.writes == 3 && "audi".equals(raw(file)), "retry after failed save did not write");

        // Back to the committed choice while a different value is being written: rewrite it.
        BlockingPreference back = new BlockingPreference(file);   // flash holds audi
        back.select(false);
        check(back.entered.await(5, TimeUnit.SECONDS), "save did not start");
        back.select(true);
        back.release.countDown(); saved(back);
        check(back.writes == 2 && "audi".equals(raw(file)), "return to committed choice lost");
        System.out.println("ClusterMapPreferenceTest: both restart choices, defaults, interrupted/failed saves, "
            + "recovery, flash-tracked retries and nonblocking ordered writes PASS");
    }

    static String raw(File file) throws Exception {
        java.io.FileInputStream in = new java.io.FileInputStream(file);
        try {
            byte[] b = new byte[32];
            int n = in.read(b);
            return n < 0 ? "" : new String(b, 0, n, "UTF-8").trim();
        } finally { in.close(); }
    }

    static final class CountingPreference extends ClusterMapPreference {
        volatile boolean fail;
        int writes;
        CountingPreference(File file) { super(file); }
        void write(boolean stock) throws Exception {
            writes++;
            if (fail) throw new java.io.IOException("injected failure");
            super.write(stock);
        }
    }
}
