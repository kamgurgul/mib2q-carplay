/*
 * ClusterGalSink — VcMapViewGate's re-forward target.  Holds the live stock ClusterService
 * and applies the gate's current value to its CombiBAPListener on the Navigation dispatcher
 * (NavigationJobs), the thread that owns CombiBAPListener (see ScreenNavStatusGate).
 *
 * Adapted from wasimlhr/mib2q-android-auto-cluster (GPL-3.0-or-later).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package com.luka.carplay.aa;

import com.luka.carplay.framework.Log;

import de.audi.tghu.navi.app.Navigation;
import de.audi.tghu.navi.app.cluster.ClusterService;

public final class ClusterGalSink implements VcMapViewGate.Sink {
    private static ClusterGalSink last;

    private final ClusterService cs;

    private ClusterGalSink(ClusterService cs) {
        this.cs = cs;
    }

    /** One sink per ClusterService instance (stock replaces it only on a navigation restart). */
    public static synchronized ClusterGalSink of(ClusterService cs) {
        if (last == null || last.cs != cs) last = new ClusterGalSink(cs);
        return last;
    }

    public void forwardLater() {
        Runnable r = new Runnable() {
            public void run() {
                try {
                    cs.forwardGALStateRaw(VcMapViewGate.takeEffective());
                } catch (Throwable t) {
                    Log.w("AA", "vcmap: setGALState re-forward failed: " + t);
                }
            }
        };
        Navigation navigation = null;
        try {
            navigation = Navigation.getInstance();
        } catch (Throwable t) {
            /* fall through: run inline */
        }
        if (navigation != null && navigation.getDispatcher() != null) {
            navigation.getDispatcher().execute(r);
        } else {
            r.run();
        }
    }
}
