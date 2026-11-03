/* Packaged MMI translations. No changes to Audi's lang_data.zip.
 * Copyright (c) 2026; SPDX-License-Identifier: GPL-3.0-or-later */
package com.luka.carplay.connectivity;

import java.io.InputStream;
import java.util.Hashtable;
import java.util.Properties;

public final class SettingsTexts {
    private static final Hashtable cache = new Hashtable();
    private SettingsTexts() { }

    public static synchronized String get(String locale, String key) {
        String language = language(locale);
        String text = table(language).getProperty(key);
        if (text == null) text = table("en").getProperty(key);
        return text == null ? key : text;
    }

    static String language(String locale) {
        if (locale == null) return "en";
        if (locale.startsWith("zh_HK") || locale.startsWith("zh_TW") || locale.startsWith("zh-Hant")) return "zh_HK";
        int end = locale.indexOf('_');
        if (end < 0) end = locale.indexOf('-');
        String language = end < 0 ? locale : locale.substring(0, end);
        // Restrict resource names, also providing an English fallback for other MMI languages.
        String supported = "|en|de|ru|uk|es|fr|it|nl|pl|pt|cs|tr|zh|";
        return supported.indexOf("|" + language + "|") >= 0 ? language : "en";
    }

    private static Properties table(String language) {
        Properties p = (Properties) cache.get(language);
        if (p != null) return p;
        p = new Properties();
        InputStream in = SettingsTexts.class.getResourceAsStream("/com/luka/carplay/connectivity/" + language + ".properties");
        try { if (in != null) p.load(in); }
        catch (java.io.IOException e) { /* fall back to English */ }
        finally { if (in != null) try { in.close(); } catch (java.io.IOException e) { } }
        cache.put(language, p);
        return p;
    }
}
