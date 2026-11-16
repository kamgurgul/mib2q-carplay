/* Shared with the GEM scripts: the existing files are the source of truth.
 * Java 1.4 / Foundation 1.1. Never mount or write from the HMI event thread.
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.luka.carplay.connectivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;

public final class CarPlaySettings {
    public static final int MAP = 0, LAYOUT = 1, GUIDANCE = 2, CARDS = 3, PREVIEW = 4;
    public static final int TURN = 5, ETA = 6, SPEED = 7, COMPASS = 8, WIDE = 9;
    /* 1.5: the Android Auto GEM switches, and the opt-in pause of a hidden cluster map. */
    public static final int AA_MAP = 10, AA_DAYNIGHT = 11, PAUSE_HIDDEN = 12;
    /* Cluster video decoder and the cluster stream's frame rate, both phone types. */
    public static final int HW_DECODER = 13, FPS = 14;
    public static final int COUNT = 15;
    /* Frame rates offered: 30 (default, no file) or 60, the two Android Auto knows; the CarPlay
     * hook takes the same file. The cockpit itself captures the map at 30 fps. */
    private static final int[] FPS_VALUES = {30, 60};
    /* Hardware decode is off when any of these exists: the GEM switch writes the two under
     * root/, altscreen_render the /tmp ones after a failure (and its start records). */
    private static final String[] HW_OFF = {"altscreen_render.hwdecode.off", "altscreen_render.aa.hwdecode.off"};
    private static final String[] PATHS = {
        "mibr-carplay111.noadvertise", "root/mibr-carplay111-nav.maneuverLayout",
        "carplay_rgd.disabled", "root/mibr-carplay111-nav.hideDuringGuidance",
        "carplay_rgd.next_maneuver", "root/mibr-carplay111-nav.showManeuver",
        "root/mibr-carplay111-nav.showETA", "root/mibr-carplay111-nav.showSpeedLimit",
        "root/mibr-carplay111-nav.showCompass", "root/mibr-carplay111-nav.wideAll",
        "root/aa_cluster.off", "root/aa_cluster.daynight", "root/cluster_video.pause_hidden",
        "root/altscreen_render.hwdecode.off", "root/mibr-carplay111-fps"
    };
    private static final int[] DEFAULTS = {1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0};
    private static final String[] LAYOUTS = {"", "left", "top", "right"};
    private final File root, temporary;

    public CarPlaySettings(File root, File temporary) { this.root = root; this.temporary = temporary; }
    public static CarPlaySettings onUnit() { return new CarPlaySettings(new File("/mnt/app"), new File("/tmp")); }
    public static int defaultValue(int key) { return DEFAULTS[key]; }
    public static int choices(int key) {
        return key == LAYOUT ? 4 : key == FPS ? FPS_VALUES.length : (key == SPEED || key == COMPASS ? 3 : 2);
    }
    /** Frame rate of an FPS choice, for labels and tests. */
    public static int fpsValue(int choice) { return FPS_VALUES[choice]; }
    public static boolean valid(int key, int value) { return key >= 0 && key < COUNT && value >= 0 && value < choices(key); }

    /** Marker files: the file's presence alone is the setting. Off markers mean 0 when present. */
    private static boolean offMarker(int key) { return key == MAP || key == GUIDANCE || key == PREVIEW || key == AA_MAP; }
    private static boolean onMarker(int key) { return key == AA_DAYNIGHT || key == PAUSE_HIDDEN; }
    /** Settings that change the CarPlay cluster URL (the hook re-sends showUI for them). */
    public static boolean changesMapUrl(int key) { return key >= LAYOUT && key <= WIDE && key != GUIDANCE && key != PREVIEW; }

    public int get(int key) {
        if (key < 0 || key >= COUNT) throw new IllegalArgumentException("setting");
        File f = new File(root, PATHS[key]);
        if (key == HW_DECODER) {
            /* Also off when the renderer switched it off for this boot: the box then shows
             * what really runs, and ticking it asks for a fresh hardware try. */
            for (int i = 0; i < HW_OFF.length; i++)
                if (new File(root, "root/" + HW_OFF[i]).exists() || new File(temporary, HW_OFF[i]).exists()) return 0;
            return 1;
        }
        if (key == FPS) {
            String text = read(f);
            int fps;
            try { fps = text == null ? 0 : Integer.parseInt(text); }
            catch (NumberFormatException e) { fps = 0; }
            if (fps < 5 || fps > 60) return DEFAULTS[FPS];     /* the hook ignores it too */
            /* A value set by hand shows as the nearer choice. */
            return fps >= 45 ? 1 : 0;
        }
        if (offMarker(key)) {
            return f.exists() || (key == GUIDANCE && new File(temporary, "carplay_rgd.disabled").exists()) ? 0 : 1;
        }
        if (onMarker(key)) return f.exists() ? 1 : 0;
        String text = read(f);
        if (key == LAYOUT) {
            for (int i = 1; i < LAYOUTS.length; i++)
                if (LAYOUTS[i].equals(text) || (LAYOUTS[i] + "Aligned").equals(text)) return i;
            return 0;
        }
        if (text == null) return DEFAULTS[key];
        if (key == CARDS) return "yes".equals(text) || "1".equals(text) ? 0 : 1;
        if ((key == SPEED || key == COMPASS) && "user".equals(text)) return 2;
        return "yes".equals(text) || "1".equals(text) || "user".equals(text) ? 1 : 0;
    }

    /** Caller has made /mnt/app writable. Atomic value-file replacement; no shell interpolation. */
    public synchronized void set(int key, int value) throws IOException {
        if (!valid(key, value)) throw new IllegalArgumentException("setting value");
        File f = new File(root, PATHS[key]);
        String text = null;
        if (key == HW_DECODER) {
            /* Same files as GEM "HW decoder ON / OFF", CarPlay and Android Auto together. On
             * also clears this boot's failure markers and start records: a fresh try. */
            for (int i = 0; i < HW_OFF.length; i++) {
                File persistent = new File(root, "root/" + HW_OFF[i]);
                if (value == 0) { write(persistent, "\n"); continue; }
                remove(persistent);
                remove(new File(temporary, HW_OFF[i]));
                remove(new File(temporary, HW_OFF[i] + ".starts"));
            }
            return;
        }
        if (key == FPS) {
            if (value == DEFAULTS[FPS]) remove(f); else write(f, FPS_VALUES[value] + "\n");
            return;
        }
        if (value != DEFAULTS[key]) {
            if (offMarker(key) || onMarker(key)) text = "";
            else if (key == LAYOUT) text = LAYOUTS[value];
            else if (key == CARDS) text = "yes";
            else text = value == 2 ? "user" : (value == 1 ? "yes" : "no");
        }
        if (text == null) remove(f); else write(f, text + "\n");
        if (key == GUIDANCE) remove(new File(temporary, "carplay_rgd.disabled"));
    }

    public void requestMapRefresh() throws IOException {
        write(new File(temporary, "mibr-alt111-show-only"), "");
    }

    private static void remove(File f) throws IOException {
        if (f.exists() && !f.delete()) throw new IOException("Cannot remove " + f);
    }

    private static void write(File f, String text) throws IOException {
        File tmp = new File(f.getPath() + ".mmi-new");
        FileOutputStream out = null;
        try {
            out = new FileOutputStream(tmp);
            out.write(text.getBytes("UTF-8"));
            out.close(); out = null;
            if (!tmp.renameTo(f)) throw new IOException("Cannot replace " + f);
        } finally {
            if (out != null) try { out.close(); } catch (IOException e) { }
            tmp.delete();
        }
    }

    private static String read(File f) {
        FileInputStream in = null;
        try {
            in = new FileInputStream(f);
            byte[] b = new byte[64];
            int n = in.read(b);
            return n < 0 ? "" : new String(b, 0, n, "UTF-8").trim();
        } catch (IOException e) { return null; }
        finally { if (in != null) try { in.close(); } catch (IOException e) { } }
    }
}
