/* Native Connection Manager settings, adapted from luka-dev/mib2q-carplay-rgi
 * MHI2Q-2026-10-29 (df73e061). Screen/drawer integration Copyright (c) 2026 LuKa
 * (@LuKa_dev). File-backed controls and packaged translations Copyright (c) 2026.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.luka.carplay.connectivity;

import com.luka.carplay.framework.Log;
import com.luka.carplay.core.CarPlayApp;
import de.audi.atip.base.IFrameworkAccess;
import de.audi.atip.hmi.IDrawerContextConnectivity;
import de.audi.atip.hmi.model.ChoiceModel;
import de.audi.atip.hmi.model.HMIModel;
import de.audi.atip.hmi.model.listener.DefaultChoiceListener;
import de.audi.atip.hmi.view.HMIView;
import de.audi.atip.hmi.view.IDrawerController;
import de.audi.atip.hmi.view.Screen;
import de.audi.atip.model.ConnectivityModelBank;
import de.audi.tghu.connectivity.hmi.evohigh.ConnectivityScreenFactory;
import de.audi.tghu.connectivity.hmi.evohigh.FontFactory;
import de.esolutions.hmi.widgets.audi.base.AbstractScreenWidget;
import de.esolutions.hmi.widgets.audi.base.AbstractWidget;
import de.esolutions.hmi.widgets.audi.base.widgets.AbstractWidgetController;
import de.esolutions.hmi.widgets.audi.evo.PropertyObject;
import de.esolutions.hmi.widgets.audi.evo.high.widgets.AbstractRendererHigh;
import de.esolutions.hmi.widgets.audi.evo.high.widgets.CompositeRendererHigh;
import de.esolutions.hmi.widgets.audi.base.StringUtility;
import de.esolutions.hmi.widgets.audi.evo.widgets.DrawerController;
import de.esolutions.hmi.widgets.audi.evo.widgets.LabelController;
import de.esolutions.hmi.widgets.audi.evo.widgets.menu.InfolineController;
import de.esolutions.hmi.widgets.audi.evo.widgets.menu.InfolineTimerController;
import de.esolutions.hmi.widgets.audi.evo.widgets.menu.MenuController;
import de.esolutions.hmi.widgets.audi.evo.widgets.menu.MenuItemController;
import de.esolutions.hmi.widgets.audi.evo.widgets.menu.MultiLineMenuItemController;
import java.util.List;


public final class CarPlaySettingsUi {
    private static final String TAG = "CarPlaySettings";
    private static final int MODEL_FIRST = 2500500;
    private static final int TEXT_TITLE = 2501500;
    private static final int DRAWER_CM_OPT = 2500001;
    private static final int SCREEN_ABOUT_CARPLAY = 2500140;
    private static final int EVENT_ABOUT_CARPLAY = 2500228;
    private static final int EVENT_WLAN_SETTINGS = 2500073;
    private static final int INTERNAL_ID_OPT_CARPLAY = 2500271;
    private static final int[] SUBMENU_BITMAPS = new int[]{39, 39, 39, 39};
    private static final int[] COMBO_BITMAPS = createComboBitmaps();
    private static final CarPlaySettings settings = CarPlaySettings.onUnit();
    private static final ChoiceModel[] models = new ChoiceModel[CarPlaySettings.COUNT];
    private static final String[] LABELS = {"map", "layout", "guidance", "cards", "preview",
        "turn", "eta", "speed", "compass", "wide"};
    /* text offsets: title=0, label/hint pairs=1..20, choice labels=21..31. */
    private static final String[] TEXT_KEYS = {
        "title", "map", "map.hint", "layout", "layout.hint", "guidance", "guidance.hint",
        "cards", "cards.hint", "preview", "preview.hint", "turn", "turn.hint", "eta", "eta.hint",
        "speed", "speed.hint", "compass", "compass.hint", "wide", "wide.hint",
        "default", "left", "center", "right", "hidden", "shown", "next", "follow", "off", "on", "phone"
    };
    private static final int[][] CHOICES = {
        null, {21,22,23,24}, null, {25,26}, {27,28}, null, null, {29,30,31}, {29,30,31}, null
    };
    private static final Object pendingLock = new Object();
    private static final int[] pending = new int[CarPlaySettings.COUNT];
    private static Thread worker;
    static { for (int i = 0; i < pending.length; i++) pending[i] = -1; }
    private CarPlaySettingsUi() { }

    public static ConnectivityModelBank newBank() { return new Bank(); }
    public static ConnectivityScreenFactory newFactory(IFrameworkAccess fw) { return new Factory(fw); }

    static void refresh() {
        for (int i = 0; i < models.length; i++) {
            synchronized (pendingLock) {
                if (models[i] != null && pending[i] < 0) models[i].setValue(settings.get(i));
            }
        }
    }

    /** Coalesce rapid choices; filesystem/mount/sync work never blocks the HMI thread. */
    static void select(int key, int value) {
        if (!CarPlaySettings.valid(key, value)) return;
        synchronized (pendingLock) {
            pending[key] = value;
            if (worker == null) {
                worker = new Thread(new Runnable() { public void run() { applyPending(); } }, "carplay-settings");
                worker.setDaemon(true);
                worker.start();
            }
        }
    }

    private static void applyPending() {
        for (;;) {
            int key = -1, value = -1;
            synchronized (pendingLock) {
                for (int i = 0; i < pending.length; i++) if (pending[i] >= 0) { key = i; value = pending[i]; break; }
                if (key < 0) { worker = null; return; }
            }
            try {
                // Fixed argv only. GEM uses the same mount before editing these files.
                Process mount = Runtime.getRuntime().exec(new String[]{"/bin/sh", "-c", "unset LD_PRELOAD; PATH=/proc/boot:/bin:/usr/bin:/usr/sbin:/sbin; mount -uw /mnt/app"});
                mount.waitFor();
                settings.set(key, value);
                if (key != CarPlaySettings.MAP && key != CarPlaySettings.GUIDANCE && key != CarPlaySettings.PREVIEW)
                    settings.requestMapRefresh();
                if (key == CarPlaySettings.GUIDANCE) CarPlayApp.onRouteGuidanceSettingsChanged();
                if (key == CarPlaySettings.PREVIEW)
                    com.luka.carplay.rgd.RouteGuidance.onPreviewSettingsChanged();
                Runtime.getRuntime().exec(new String[]{"/bin/sh", "-c", "unset LD_PRELOAD; PATH=/proc/boot:/bin:/usr/bin; sync"}).waitFor();
                Log.i(TAG, LABELS[key] + "=" + value);
            } catch (Throwable t) {
                Log.w(TAG, "Could not save " + LABELS[key] + ": " + t);
            } finally {
                synchronized (pendingLock) {
                    if (pending[key] == value) pending[key] = -1;
                    if (models[key] != null && pending[key] < 0) models[key].setValue(settings.get(key));
                }
            }
        }
    }

    private static int[] createComboBitmaps() {
        int[] b = new int[44];
        for (int i = 0; i < 42; i++) b[i] = 127;
        b[42] = 129; b[43] = 332;
        return b;
    }

    static final class Bank extends ConnectivityModelBank {
        Bank() {
            int need = MODEL_FIRST % 100000 + CarPlaySettings.COUNT;
            if (models.length < need) {
                HMIModel[] grown = new HMIModel[need];
                System.arraycopy(models, 0, grown, 0, models.length);
                models = grown;
            }
            int n = modelIDs.length;
            int[] ids = new int[n + CarPlaySettings.COUNT];
            System.arraycopy(modelIDs, 0, ids, 0, n);
            for (int k = 0; k < CarPlaySettings.COUNT; k++) ids[n + k] = MODEL_FIRST + k;
            modelIDs = ids;
        }
        protected synchronized void createModel(int index) {
            final int key = index - MODEL_FIRST % 100000;
            if (key < 0 || key >= CarPlaySettings.COUNT) { super.createModel(index); return; }
            if (models[index] != null) return;
            final ChoiceModel m = new ChoiceModel(MODEL_FIRST + key);
            m.setValue(settings.get(key));
            m.setChoiceListener(new DefaultChoiceListener() {
                public void itemSelected(int id, int value, int unused, int terminal) {
                    if (!CarPlaySettings.valid(key, value)) return;
                    try { m.setValue(value); select(key, value); }
                    catch (Throwable t) { Log.w(TAG, "Selection failed: " + t); m.setValue(settings.get(key)); }
                }
            });
            models[index] = m;
            CarPlaySettingsUi.models[key] = m;
        }
    }

    static final class Factory extends ConnectivityScreenFactory {
        Factory(IFrameworkAccess fw) { super(fw); }

        public IDrawerController[] getOptionDrawers(int terminal) {
            IDrawerController[] drawers = super.getOptionDrawers(terminal);
            if (drawers != null) {
                for (int i = 0; i < drawers.length; i++) {
                    if (drawers[i] instanceof DrawerController
                            && ((DrawerController) drawers[i]).getID() == DRAWER_CM_OPT) {
                        try { patchOptionDrawer((DrawerController) drawers[i], terminal); }
                        catch (Throwable t) { Log.w(TAG, "Drawer: " + t); }
                    }
                }
            }
            return drawers;
        }

        protected Screen createScreen(int id, int terminal) {
            Screen s = super.createScreen(id, terminal);
            if (id == SCREEN_ABOUT_CARPLAY) {
                try { buildCarPlaySettingsScreen(s, terminal); }
                catch (Throwable t) { Log.w(TAG, "Screen: " + t); }
            }
            return s;
        }

        public Screen getScreen(int id, int terminal) {
            if (id == SCREEN_ABOUT_CARPLAY) refresh();
            return super.getScreen(id, terminal);
        }

        public String getText(int id) {
            if (id >= TEXT_TITLE && id < TEXT_TITLE + TEXT_KEYS.length) {
                String language = "en";
                try {
                    de.audi.atip.i18n.Language l = getFramework().getLanguageMgr().getCurrentLanguage("LANG_COMPONENT_HMI");
                    if (l != null && l.getLocale() != null) language = l.getLocale().toString();
                } catch (Throwable t) { /* English before language service starts */ }
                return SettingsTexts.get(language, TEXT_KEYS[id - TEXT_TITLE]);
            }
            return super.getText(id);
        }

        public int getTextStatus(int id) {
            return id >= TEXT_TITLE && id < TEXT_TITLE + TEXT_KEYS.length ? 8 : super.getTextStatus(id);
        }

        private void patchOptionDrawer(DrawerController drawer, int terminal) {
            Object main = drawer.getDrawerMain();
            if (!(main instanceof AbstractWidgetController)) return;
            AbstractWidgetController mainCtrl = (AbstractWidgetController) main;
            if (mainCtrl.getChildrenSize() == 0) return;
            AbstractWidget first = mainCtrl.getChild(0);
            if (!(first instanceof MenuController)) return;
            MenuController menu = (MenuController) first;

            MenuItemController oldCarPlayItem = null;
            MenuItemController refSubmenuItem = null;
            List children = menu.getChildren();
            for (int i = 0; i < children.size(); i++) {
                Object child = children.get(i);
                if (child instanceof MenuItemController) {
                    MenuItemController mi = (MenuItemController) child;
                    if (mi.getEvent() == EVENT_ABOUT_CARPLAY) {
                        oldCarPlayItem = mi;
                    } else if (mi.getEvent() == EVENT_WLAN_SETTINGS) {
                        refSubmenuItem = mi;
                    }
                }
            }

            MenuItemController item = new MenuItemController();
            CompositeRendererHigh r = new CompositeRendererHigh(item);
            item.setRenderer(r);
            item.setBitmaps(refSubmenuItem != null && refSubmenuItem.getBitmapIndices() != null
                    ? refSubmenuItem.getBitmapIndices() : SUBMENU_BITMAPS);
            r.setFonts(refSubmenuItem != null && refSubmenuItem.getRenderer() instanceof AbstractRendererHigh
                    && ((AbstractRendererHigh) refSubmenuItem.getRenderer()).getFonts() != null
                    ? ((AbstractRendererHigh) refSubmenuItem.getRenderer()).getFonts()
                    : FontFactory.getFonts(0, terminal, this.getFramework()));
            item.setEvent(EVENT_ABOUT_CARPLAY);
            item.setLabelId(TEXT_TITLE);
            item.setGlassplateInsetsBottom(6);
            item.setGlassplateInsetsTop(7);
            item.setInternalID(INTERNAL_ID_OPT_CARPLAY);
            item.setType(MenuItemController.TYPE_SUBMENU);
            item.setWidgetID(1);

            if (refSubmenuItem != null) {
                int idx = menu.getChildren().indexOf(refSubmenuItem);
                menu.addWithPosition(item, idx + 1);
            } else {
                menu.add(item);
            }
            if (oldCarPlayItem != null) {
                menu.remove(oldCarPlayItem);
            }

            PropertyObject[] props = drawer.getPropertyObjects();
            if (props != null) {
                PropertyObject newProp = new PropertyObject(
                    item,
                    1,
                    -2,
                    -2,
                    new int[]{IDrawerContextConnectivity.CONTEXT_CONNECTIVITY_CONNECTIVITY},
                    null,
                    null,
                    null,
                    null
                );
                boolean replaced = false;
                PropertyObject[] updated = new PropertyObject[props.length];
                for (int i = 0; i < props.length; i++) {
                    if (oldCarPlayItem != null && props[i] != null && props[i].getWidget() == oldCarPlayItem) {
                        updated[i] = newProp;
                        replaced = true;
                    } else {
                        updated[i] = props[i];
                    }
                }
                if (!replaced) {
                    PropertyObject[] grown = new PropertyObject[props.length + 1];
                    System.arraycopy(updated, 0, grown, 0, updated.length);
                    grown[grown.length - 1] = newProp;
                    updated = grown;
                }
                drawer.setPropertyObjects(updated);
            }
        }

        private MenuItemController createCheckboxItem(int terminal, int modelId, int labelId,
                                                      int hintId, int internalId) {
            MenuItemController item = new MenuItemController();
            CompositeRendererHigh r = new CompositeRendererHigh(item);
            item.setRenderer(r);
            r.setFonts(FontFactory.getFonts(0, terminal, this.getFramework()));
            item.setModelID(modelId);
            item.setMapping(new int[][]{{0}, {1}});
            item.setLabelId(labelId);
            item.setInfolineTextIdEnabled(hintId);
            item.setHasInfolineDisclaimer(false);
            item.setGlassplateInsetsBottom(6);
            item.setGlassplateInsetsTop(7);
            item.setInternalID(internalId);
            item.setType(MenuItemController.TYPE_CHECKBOX);
            item.setWidgetID(1);
            return item;
        }

        private MenuItemController createComboItem(int terminal, int modelId, int labelId,
                                                   int[] choiceTextIds, int hintId, int internalId) {
            MenuItemController item = new MenuItemController();
            CompositeRendererHigh r = new CompositeRendererHigh(item);
            item.setRenderer(r);
            item.setBitmaps(COMBO_BITMAPS);
            r.setFonts(FontFactory.getFonts(3, terminal, this.getFramework()));
            item.setModelID(modelId);
            item.setLabelId(labelId);
            item.setTextIds(choiceTextIds);
            item.setInfolineTextIdEnabled(hintId);
            item.setHasInfolineDisclaimer(false);
            item.setGlassplateInsetsBottom(6);
            item.setGlassplateInsetsTop(7);
            item.setInternalID(internalId);
            item.setType(MenuItemController.TYPE_COMBO_BOX);
            item.setWidgetID(1);
            return item;
        }

        private void buildCarPlaySettingsScreen(Screen s, int terminal) {
            AbstractScreenWidget screen = (AbstractScreenWidget) s;
            updateScreenTitle(screen, TEXT_TITLE);

            Object mainArea = screen.getMainArea();
            if (!(mainArea instanceof AbstractWidgetController)) return;
            AbstractWidgetController mainCtrl = (AbstractWidgetController) mainArea;
            MenuController menu = null;
            for (int i = 0; i < mainCtrl.getChildrenSize(); i++) {
                if (mainCtrl.getChild(i) instanceof MenuController) {
                    menu = (MenuController) mainCtrl.getChild(i);
                    break;
                }
            }
            if (menu == null) return;

            // Remove stock static "About CarPlay" multiline text item
            List children = menu.getChildren();
            for (int i = children.size() - 1; i >= 0; i--) {
                Object c = children.get(i);
                if (c instanceof MultiLineMenuItemController || c instanceof MenuItemController) {
                    menu.remove((AbstractWidget) c);
                }
            }

            InfolineTimerController timer = menu.getInfolineTimer();
            if (timer != null) {
                timer.setStartTimerAutomatically(true);
            }
            InfolineController infoline = menu.getInfoline();
            if (infoline != null && infoline.getInfolineRenderer() != null) {
                infoline.getInfolineRenderer().setAutoWrap(StringUtility.WRAP_MODE_STANDARD);
            }

            for (int key = 0; key < CarPlaySettings.COUNT; key++) {
                int label = TEXT_TITLE + 1 + key * 2;
                int model = MODEL_FIRST + key;
                MenuItemController item;
                if (CHOICES[key] == null) {
                    item = createCheckboxItem(terminal, model, label, label + 1, 2509910 + key);
                } else {
                    int[] texts = new int[CHOICES[key].length];
                    for (int i = 0; i < texts.length; i++) texts[i] = TEXT_TITLE + CHOICES[key][i];
                    item = createComboItem(terminal, model, label, texts, label + 1, 2509910 + key);
                }
                registerScreenView(screen, model, item);
                menu.addWithPosition(item, key);
            }
        }

        private static void updateScreenTitle(AbstractScreenWidget screen, int titleTextId) {
            for (int i = 0; i < screen.getChildrenSize(); i++) {
                AbstractWidget child = screen.getChild(i);
                if (child.getRole() == 2 && child instanceof AbstractWidgetController) {
                    AbstractWidgetController titleArea = (AbstractWidgetController) child;
                    if (titleArea.getChildrenSize() > 0
                            && titleArea.getChild(0) instanceof AbstractWidgetController) {
                        AbstractWidgetController titleBar = (AbstractWidgetController) titleArea.getChild(0);
                        for (int j = 0; j < titleBar.getChildrenSize(); j++) {
                            AbstractWidget w = titleBar.getChild(j);
                            if (w instanceof LabelController && w.getRole() == 0) {
                                ((LabelController) w).setTextIds(new int[]{titleTextId});
                                return;
                            }
                        }
                    }
                }
            }
        }

        private static void registerScreenView(AbstractScreenWidget screen, int modelId, HMIView item) {
            int[] ids = screen.getViewIDs();
            if (ids == null) ids = new int[0];
            HMIView[][] views = new HMIView[ids.length + 1][];
            int[] nids = new int[ids.length + 1];
            int o = 0;
            boolean placed = false;
            for (int k = 0; k < ids.length; k++) {
                if (!placed && ids[k] > modelId) {
                    nids[o] = modelId;
                    views[o++] = new HMIView[]{item};
                    placed = true;
                }
                nids[o] = ids[k];
                views[o++] = screen.findViews(ids[k]);
            }
            if (!placed) {
                nids[o] = modelId;
                views[o] = new HMIView[]{item};
            }
            screen.setViews(nids, views);
        }
    }
}
