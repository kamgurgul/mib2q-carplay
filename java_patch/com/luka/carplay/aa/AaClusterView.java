/*
 * AaClusterView — tells the Android Auto gal hook which Virtual Cockpit layout shows the
 * cluster video, so it can send the phone the matching safe area (AAP 0x8009, aa_uiconfig.c):
 *
 *   full    the large map view (VC FctID 54 stage = popup)
 *   classic the small window between the two dials (LayoutMIB2HighB9)
 *   sport   the small window left of the centre dial (LayoutMIB2HighB9Sport)
 *
 * /tmp/aa_cluster_view is one fixed 8-byte record rewritten in place ("full   \n",
 * "classic\n", "sport  \n"): /tmp is /dev/shmem, which has no truncate-safe rename, and a
 * fixed length means a concurrent reader never sees a short record.  Written only on change.
 * Same zone decision as the CarPlay AltScreen CMD_ALT_ZONE (ClusterLayerController).
 *
 * Java 1.4.  SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.luka.carplay.aa;

import com.luka.carplay.framework.Log;

import java.io.RandomAccessFile;

public final class AaClusterView {
    public static final String PATH = "/tmp/aa_cluster_view";
    public static final int FULL = 0;
    public static final int CLASSIC = 1;
    public static final int SPORT = 2;

    private static int last = -1;

    private AaClusterView() {}

    /** Pure mapping: VC stage + stock layout class name -> view. */
    public static int viewFor(boolean largeMapView, String layoutName) {
        if (largeMapView) return FULL;
        return layoutName != null && layoutName.endsWith("Sport") ? SPORT : CLASSIC;
    }

    public static String record(int view) {
        switch (view) {
            case SPORT:   return "sport  \n";
            case CLASSIC: return "classic\n";
            default:      return "full   \n";
        }
    }

    /** ClusterLayerController.applyNow (serialized by its apply lock). Never throws. */
    public static synchronized void publish(boolean largeMapView, String layoutName) {
        int v = viewFor(largeMapView, layoutName);
        if (v == last) return;
        RandomAccessFile f = null;
        try {
            f = new RandomAccessFile(PATH, "rw");
            f.seek(0);
            f.write(record(v).getBytes());
            last = v;
            Log.i("AA", "cluster view -> " + record(v).trim());
        } catch (Throwable t) {
            /* retried on the next change of the layer state */
        } finally {
            if (f != null) try { f.close(); } catch (Throwable t) { /* ignore */ }
        }
    }
}
