# Third-party notices - mib2q-carplay

| Component | Where | License / status |
| --- | --- | --- |
| mib2q-carplay (this project) | all files unless listed below | GPL-3.0-or-later, see `LICENSE` |
| [luka-dev/mib2q-carplay-rgi](https://github.com/luka-dev/mib2q-carplay-rgi) | files marked `Copyright (c) LuKa (@LuKa_dev)` in the source; built into `libcarplay_hook.so`, `maneuver_render`, `carplay_hook.jar`, the `carplay_*.sh` scripts | published **without a license**; the author's terms apply |
| [harman-f/mhi2_altscreen_carplay](https://github.com/harman-f/mhi2_altscreen_carplay) | GEN2 cluster-video hook, built into `libaltscreen111_mhi2q.so` | GPL-3.0-or-later |
| [wasimlhr/mib2q-android-auto-cluster](https://github.com/wasimlhr/mib2q-android-auto-cluster) | Android Auto: `aa_hook/` (built into `libaa_cluster_hook.so`), `com.luka.carplay.aa` and the rebuilt `androidauto2` classes in `carplay_hook.jar` | GPL-3.0-or-later |
| [chopinwong01/mhi2-android-auto-video-vc](https://github.com/chopinwong01/mhi2-android-auto-video-vc) | receiver protocol injection the Android Auto hook adapts | GPL-3.0 |
| [FFmpeg](https://ffmpeg.org) 6.1.5 | statically linked H.264 decoder in `altscreen_render` | LGPL-2.1-or-later, see `licenses/FFmpeg-COPYING.LGPLv2.1` |
| [stb_image](https://github.com/nothings/stb) | cover-art decoding in `libcarplay_hook.so` | public domain / MIT |
| Audi / e.solutions HMI classes | modified `de.audi.*` and `de.esolutions.*` classes inside `carplay_hook.jar` | **proprietary**, derived from the vehicle's own firmware; not covered by the GPL; provided for use on your own vehicle only |

## FFmpeg (LGPL)

`altscreen_render` links a minimal static FFmpeg 6.1.5 (H.264 decoder and parser only, no
GPL parts). Source: <https://ffmpeg.org/releases/ffmpeg-6.1.5.tar.xz>. The exact
configuration and the build that links it are in this project's source,
`scripts/build_altscreen_render.sh`, so `altscreen_render` can be rebuilt against a modified
FFmpeg.

## Source code

The complete source for the GPL parts, including build scripts, is in the project
repository. `carplay_hook.jar` can be rebuilt from `java_patch/` against your own unit's
`lsd.jxe` (see the project README, *Build*).
