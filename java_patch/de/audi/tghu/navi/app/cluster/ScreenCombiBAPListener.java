package de.audi.tghu.navi.app.cluster;

import de.audi.atip.log.LogChannel;
import de.audi.atip.mmicombi.IViewSizeManager;
import de.audi.tghu.command.ICommandListFactory;
import de.audi.tghu.navi.app.NavigationEnv;
import de.audi.tghu.navi.app.OperationManager;
import de.audi.tghu.navi.app.SpeechManager;
import de.audi.tghu.navi.app.audio.AudioStateMachine;
import de.audi.tghu.navi.app.map.MapManager;

/**
 * Stock BAP boundary for CarPlay KDK composition.
 *
 * VC's Fct44 (KDK visibility) and Fct54 (map presentation/stage) are forwarded to
 * the layer controller before stock acknowledges them.  The steering-wheel roller
 * zooms the stock native map as usual; while the CarPlay cluster video (AltScreen,
 * plane 99) is on the VC the step is ALSO sent to the phone as CMD_ALT_ZOOM
 * (hook -> changeMapZoomLevel), and the hidden stock map keeps driving the scale bar.
 */
public final class ScreenCombiBAPListener extends CombiBAPListener {
    public ScreenCombiBAPListener(
        ClusterService service,
        LogChannel logChannel,
        NavigationEnv env,
        SpeechManager speechManager,
        OperationManager operationManager,
        AudioStateMachine audioStateMachine,
        MapManager mapManager,
        ICommandListFactory commandListFactory,
        IViewSizeManager viewSizeManager
    ) {
        super(
            service,
            logChannel,
            env,
            speechManager,
            operationManager,
            audioStateMachine,
            mapManager,
            commandListFactory,
            viewSizeManager
        );
    }

    /** Apply the accepted stock state before its Status acknowledgement. This boundary
     * also covers internal supplementary visibility changes and initial Status replay,
     * which bypass the two-argument BAP request setter. */
    protected void updateMapVisibility() {
        com.luka.carplay.cluster.ClusterLayerController.onVcVisibility(this.supplementaryMapViewVisible);
        super.updateMapVisibility();
    }

    /** Roller rotation (BAP MapScale steps). While the CarPlay video is on the VC the step
     * also goes to the phone (CMD_ALT_ZOOM -> changeMapZoomLevel). Stock still gets the step:
     * its native map, hidden behind plane 99, zooms with it, so the VC's lower-bar scale
     * readout (FctID 45, e.g. "300ft") moves in step with the CarPlay zoom. iOS reports no
     * zoom level back, so following the stock scale is the only honest value for that bar. */
    public void setMapScale(int steps) {
        boolean connected = com.luka.carplay.core.ScreenModule.isConnected();
        boolean video = com.luka.carplay.core.ScreenModule.isAltScreenActive();
        if (steps != 0 && connected && video) {
            int clamped = steps > 127 ? 127 : (steps < -128 ? -128 : steps);
            boolean sent = com.luka.carplay.bus.CarplayBus.getInstance().sendBinary(
                com.luka.carplay.bus.CarplayBus.CMD_ALT_ZOOM, new byte[]{(byte) clamped});
            com.luka.carplay.framework.Log.i("AltZoom", "roller steps=" + steps + " -> CarPlay"
                + (sent ? "" : " (bus send dropped)") + " + stock scale bar");
        } else if (connected) {
            com.luka.carplay.framework.Log.i("AltZoom", "roller steps=" + steps
                + " stock only (cluster video " + (video ? "on" : "off") + ")");
        }
        super.setMapScale(steps);
    }

    /** Logged only: shows whether the roller arrives as a scale setting instead of steps. */
    public void setMapScaleSetting(int setting) {
        if (com.luka.carplay.core.ScreenModule.isConnected())
            com.luka.carplay.framework.Log.i("AltZoom", "setMapScaleSetting(" + setting + ")");
        super.setMapScaleSetting(setting);
    }

    public void setMapPresentation(boolean largeMapView, boolean leftMenu, boolean rightMenu) {
        com.luka.carplay.cluster.ClusterLayerController.onVcPresentation(largeMapView);
        super.setMapPresentation(largeMapView, leftMenu, rightMenu);
    }
}
