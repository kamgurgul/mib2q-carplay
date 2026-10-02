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
 * zooms the stock native map as usual, EXCEPT while the CarPlay cluster video
 * (AltScreen, plane 99) is on the VC: then the stock map is hidden, so the step
 * goes to the phone as CMD_ALT_ZOOM (hook -> changeMapZoomLevel) instead.
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

    /** Roller rotation (BAP MapScale steps). steps=0 still runs stock's MapScale Status
     * update, so the VC gets its acknowledgement without the hidden stock map moving. */
    public void setMapScale(int steps) {
        if (steps != 0
                && com.luka.carplay.core.ScreenModule.isConnected()
                && com.luka.carplay.core.ScreenModule.isAltScreenActive()) {
            int clamped = steps > 127 ? 127 : (steps < -128 ? -128 : steps);
            boolean sent = com.luka.carplay.bus.CarplayBus.getInstance().sendBinary(
                com.luka.carplay.bus.CarplayBus.CMD_ALT_ZOOM, new byte[]{(byte) clamped});
            com.luka.carplay.framework.Log.i("AltZoom", "roller steps=" + steps + " -> CarPlay"
                + (sent ? "" : " (bus send dropped)"));
            super.setMapScale(0);
            return;
        }
        super.setMapScale(steps);
    }

    public void setMapPresentation(boolean largeMapView, boolean leftMenu, boolean rightMenu) {
        com.luka.carplay.cluster.ClusterLayerController.onVcPresentation(largeMapView);
        super.setMapPresentation(largeMapView, leftMenu, rightMenu);
    }
}
