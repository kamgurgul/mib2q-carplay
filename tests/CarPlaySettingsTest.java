package com.luka.carplay.connectivity;

import java.io.File;
import java.io.FileOutputStream;
import java.util.Properties;
import java.io.InputStream;
import java.util.Iterator;

/** Round-trip the actual GEM files, including old manual overrides and failed writes. */
public final class CarPlaySettingsTest {
    static void check(boolean ok, String why) { if (!ok) throw new AssertionError(why); }
    static void write(File file, String text) throws Exception {
        FileOutputStream out = new FileOutputStream(file); out.write(text.getBytes("UTF-8")); out.close();
    }
    public static void main(String[] args) throws Exception {
        File root = new File(args[0], "app"), tmp = new File(args[0], "tmp");
        check(new File(root, "root").mkdirs() && tmp.mkdirs(), "test directories");
        CarPlaySettings s = new CarPlaySettings(root, tmp);
        for (int key = 0; key < CarPlaySettings.COUNT; key++) {
            check(s.get(key) == CarPlaySettings.defaultValue(key), "default " + key);
            for (int value = 0; value < CarPlaySettings.choices(key); value++) {
                s.set(key, value);
                check(new CarPlaySettings(root, tmp).get(key) == value, "persist " + key + ":" + value);
            }
            s.set(key, CarPlaySettings.defaultValue(key));
        }
        check(root.list().length == 1 && new File(root, "root").list().length == 0, "defaults remove overrides");
        File layout = new File(root, "root/mibr-carplay111-nav.maneuverLayout");
        write(layout, "rightAligned\n"); check(s.get(CarPlaySettings.LAYOUT) == 3, "GEM/manual right");
        write(layout, "topAligned\n"); check(s.get(CarPlaySettings.LAYOUT) == 2, "center maps to top");
        write(layout, "none\n"); check(s.get(CarPlaySettings.LAYOUT) == 0, "app default");
        write(new File(root, "root/mibr-carplay111-nav.showSpeedLimit"), "user\n");
        check(s.get(CarPlaySettings.SPEED) == 2, "retain phone-setting override");
        write(new File(root, "carplay_rgd.disabled"), ""); check(s.get(CarPlaySettings.GUIDANCE) == 0, "GEM guidance off");
        write(new File(tmp, "carplay_rgd.disabled"), "");
        s.set(CarPlaySettings.GUIDANCE, 1);
        check(s.get(CarPlaySettings.GUIDANCE) == 1, "clear both guidance disable files");
        s.requestMapRefresh(); check(new File(tmp, "mibr-alt111-show-only").exists(), "hook refresh");
        /* Hardware decoder: off for either phone type, persistent or for this boot, shows as
         * off; ON clears all four markers and the start records (GEM "HW decoder ON"). */
        File aaBoot = new File(tmp, "altscreen_render.aa.hwdecode.off");
        File starts = new File(tmp, "altscreen_render.hwdecode.off.starts");
        write(aaBoot, ""); write(starts, "3 1 0\n");
        check(s.get(CarPlaySettings.HW_DECODER) == 0, "renderer's boot marker shows as software");
        s.set(CarPlaySettings.HW_DECODER, 1);
        check(!aaBoot.exists() && !starts.exists() && s.get(CarPlaySettings.HW_DECODER) == 1, "ON = fresh hardware try");
        s.set(CarPlaySettings.HW_DECODER, 0);
        check(new File(root, "root/altscreen_render.hwdecode.off").exists()
            && new File(root, "root/altscreen_render.aa.hwdecode.off").exists(), "OFF for both phone types");
        s.set(CarPlaySettings.HW_DECODER, 1);
        /* Frame rate: 30 (no file) or 60; a hand-set value shows as the nearer one. */
        File fps = new File(root, "root/mibr-carplay111-fps");
        check(CarPlaySettings.choices(CarPlaySettings.FPS) == 2 && CarPlaySettings.fpsValue(0) == 30
            && CarPlaySettings.fpsValue(1) == 60, "30 or 60 only");
        s.set(CarPlaySettings.FPS, 1);
        check(new String(java.nio.file.Files.readAllBytes(fps.toPath()), "UTF-8").equals("60\n"), "60 written for the hooks");
        s.set(CarPlaySettings.FPS, 0); check(!fps.exists(), "30 = default, no file");
        write(fps, "24\n"); check(s.get(CarPlaySettings.FPS) == 0, "hand-set 24 shows as 30");
        write(fps, "50\n"); check(s.get(CarPlaySettings.FPS) == 1, "hand-set 50 shows as 60");
        write(fps, "junk\n"); check(s.get(CarPlaySettings.FPS) == 0, "garbled = default");
        check(fps.delete(), "cleanup");
        try { s.set(CarPlaySettings.LAYOUT, 4); throw new AssertionError("invalid choice accepted"); }
        catch (IllegalArgumentException expected) { }
        CarPlaySettings missing = new CarPlaySettings(new File(root, "missing"), tmp);
        try { missing.set(CarPlaySettings.ETA, 1); throw new AssertionError("failed write accepted"); }
        catch (java.io.IOException expected) { check(missing.get(CarPlaySettings.ETA) == 0, "failed write changed state"); }

        String[] langs = {"en","de","ru","uk","es","fr","it","nl","pl","pt","cs","tr","zh","zh_HK"};
        Properties english = load("en"); check(english.size() == 44, "all UI text keys present");
        for (int i = 0; i < langs.length; i++) {
            Properties p = load(langs[i]); check(p.keySet().equals(english.keySet()), "complete " + langs[i]);
            for (Iterator it = english.keySet().iterator(); it.hasNext();) {
                String key = (String) it.next();
                check(p.getProperty(key).length() > 0, "empty " + langs[i] + ":" + key);
                check(SettingsTexts.get(langs[i], key).equals(p.getProperty(key)), "lookup " + langs[i] + ":" + key);
            }
        }
        check(SettingsTexts.get("pl_PL", "title").equals(load("pl").getProperty("title")), "MMI Polish locale");
        check(SettingsTexts.get("zh_TW", "title").equals(load("zh_HK").getProperty("title")), "traditional Chinese");
        check(SettingsTexts.get("ja_JP", "title").equals(english.getProperty("title")), "unsupported locale fallback");
        System.out.println("CarPlaySettingsTest: GEM parity, persistence, failure handling and 14 translation bundles PASS");
    }
    static Properties load(String lang) throws Exception {
        InputStream in = SettingsTexts.class.getResourceAsStream("/com/luka/carplay/connectivity/"+lang+".properties");
        check(in != null, "packaged " + lang); Properties p = new Properties(); p.load(in); in.close(); return p;
    }
}
