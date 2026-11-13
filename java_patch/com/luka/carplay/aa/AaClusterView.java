/*
 * AaClusterView — tells the Android Auto gal hook which Virtual Cockpit layout shows the
 * cluster video, so it can send the phone the matching safe area (AAP 0x8009, aa_uiconfig.c):
 *
 *   full    the large map view (VC FctID 54 stage = popup)
 *   classic the small window between the two dials (LayoutMIB2HighB9)
 *   sport   the small window left of the centre dial (LayoutMIB2HighB9Sport)
 *
 * /tmp/aa_cluster_view is one fixed 22-byte record rewritten in place: the view name padded to
 * 8 ("full    ", "classic ", "sport   "), then the stock layout's Sport small-stage shift and
 * its map origin row, each right-aligned in 6, e.g. "sport     -460     18\n".  /tmp is
 * /dev/shmem, which has no truncate-safe rename, and a fixed length means a concurrent reader
 * never sees a short record.  Written only on change.
 * Same zone decision as the CarPlay AltScreen CMD_ALT_ZONE (ClusterLayerController).
 *
 * The two numbers say where the cockpit shows the phone's picture (plane 99): from that panel
 * row, and in the Sport small stage shifted by that many pixels.  They differ per car
 * (B9Sport -476 / 26, Q7Sport -460 / 18), and the hook's safe areas are B9 numbers that follow
 * them (issue #53).  A hook before 1.5 reads only the first 8 bytes.
 *
 * Java 1.4.  SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.luka.carplay.aa;

import com.luka.carplay.framework.Log;

import java.io.RandomAccessFile;

public final class AaClusterView {
    public static final String PATH = "/tmp/aa_cluster_view";
    /* "1\n" while the cockpit shows no phone map, "0\n" otherwise (aa_focus.c, opt-in pause). */
    public static final String HIDDEN_PATH = "/tmp/aa_cluster_hidden";
    public static final int FULL = 0;
    public static final int CLASSIC = 1;
    public static final int SPORT = 2;

    /* Clamp, so the record keeps its length whatever a layout returns; the hook ignores numbers
     * no layout has and keeps its B9 values. */
    private static final int FIELD_MIN = -9999, FIELD_MAX = 9999;

    private static String last;

    private AaClusterView() {}

    /** Pure mapping: VC stage + stock layout class name -> view. */
    public static int viewFor(boolean largeMapView, String layoutName) {
        if (largeMapView) return FULL;
        return layoutName != null && layoutName.endsWith("Sport") ? SPORT : CLASSIC;
    }

    public static String name(int view) {
        switch (view) {
            case SPORT:   return "sport";
            case CLASSIC: return "classic";
            default:      return "full";
        }
    }

    /** Fixed 22 bytes: name padded to 8, small-stage shift and map origin row in 6 each. */
    public static String record(int view, int stageDx, int mapY) {
        StringBuffer b = new StringBuffer(22);
        b.append(name(view));
        while (b.length() < 8) b.append(' ');
        field(b, stageDx);
        b.append(' ');
        field(b, mapY);
        return b.append('\n').toString();
    }

    private static void field(StringBuffer b, int v) {
        String s = Integer.toString(v < FIELD_MIN ? FIELD_MIN : v > FIELD_MAX ? FIELD_MAX : v);
        for (int i = s.length(); i < 6; i++) b.append(' ');
        b.append(s);
    }

    /** ScreenModule (under its lock), on change. Fixed 2-byte record rewritten in place, like
     *  the view record. Never throws; false = not written (retried on the next change). */
    public static boolean publishHidden(boolean hidden) {
        RandomAccessFile f = null;
        try {
            f = new RandomAccessFile(HIDDEN_PATH, "rw");
            f.seek(0);
            f.write((hidden ? "1\n" : "0\n").getBytes());
            Log.i("AA", "cluster map " + (hidden ? "hidden" : "shown") + " on the cockpit");
            return true;
        } catch (Throwable t) {
            return false;
        } finally {
            if (f != null) try { f.close(); } catch (Throwable t) { /* ignore */ }
        }
    }

    /** ClusterLayerController.applyNow (serialized by its apply lock). Never throws. */
    public static synchronized void publish(boolean largeMapView, String layoutName,
                                            int stageDx, int mapY) {
        String r = record(viewFor(largeMapView, layoutName), stageDx, mapY);
        if (r.equals(last)) return;
        RandomAccessFile f = null;
        try {
            f = new RandomAccessFile(PATH, "rw");
            f.seek(0);
            f.write(r.getBytes());
            last = r;
            Log.i("AA", "cluster view -> " + r.trim() + " (view, small-stage shift, map row; "
                + layoutName + ")");
        } catch (Throwable t) {
            /* retried on the next change of the layer state */
        } finally {
            if (f != null) try { f.close(); } catch (Throwable t) { /* ignore */ }
        }
    }
}
