> Imported from `mhi2_altscreen_carplay` (docs/research/). Links to `../testing/…`
> matrices point into that repo and are not carried here.

# Audi B9 / AUG22 MU1438 compatibility lead — 2026-09-28

Status: **verified offline firmware comparison / no Audi runtime support**
Reference implementation remains: **Škoda MHI2 / MU1440 + AID10-class**

## Why this target is interesting

A contributor investigating an Audi B9 MHI2 / AUG22 MU1438 target reported strong structural similarity around the classic Harman CarPlay receiver. A supplied stock-configuration bundle adds useful independent evidence about the process and display topology.

This does **not** mean the current MU1440 binary should be loaded on MU1438. The existing exact-hash gate remains correct until ABI and vehicle routing are proven.

## Contributor-supplied stock configuration evidence

Bundle SHA-256:

`13533cff4a68dee5d39c097ea60c2012c0baef78a646287ab91fc12419d69a2e`

| File | SHA-256 |
| --- | --- |
| `dio_manager.json` | `99a11a14efa4a02bc5811b5d29d0b336695089c06774ca4568bb7844f4688528` |
| `smartphone_integrator.json` | `6e7968e0279a2548c50285923e5bbc66f8775ad490c2394e430318d570faeed9` |
| `displaymanager.json` | `c2263eb38ff03dbd23d1c95ddf47e04762df97e40da1825f8677801cec24d644` |

### CarPlay process model

`smartphone_integrator.json` launches CarPlay as:

```text
smartphone_integrator -> dio_manager
```

with `IPL_CONFIG_DIR_DIO_MANAGER=/etc/eso/production`, decoder use enabled and the stock CarPlay cleanup script.

### Screen receiver

`dio_manager.json` configures:

- maximum 30 fps;
- NVIDIA secondary-screen output selection;
- output device 0 = LVDS (default), 1 = HDMI;
- bad-frame watchdog threshold;
- the stock comment notes the dependency chain from the initial I-frame for subsequent frames.

### MOST video capability

`displaymanager.json` contains:

- MOST encoder device `/dev/mlb/isoTX2`;
- Audi-oriented queue size 8;
- encoder/channel-rate parameters;
- `video_over_most.force_routing = component_control`.

This is directly relevant to the MU1440 reference path, which also uses `/dev/mlb/isoTX2`.

### Second LVDS / HDMI capability

The same DisplayManager config also exposes an extended `2_lvds` mode:

- primary terminal: `Tegra:TFTLCD0`;
- secondary terminal: `Tegra:HDMI0`;
- secondary display: ID 4;
- HBAS test/production profiles force `kombi_type = lvds`.

Therefore the configuration should not be read as "both paths are equally plausible for the B9 Virtual Cockpit map". Audi OEM service-training material explicitly states that the B9 Virtual Cockpit receives the **large navigation map and detailed intersection maps over LVDS from J794 to J285**. MOST remains present for list menus/covers and J285 software updates. The supplied MU1438 configuration is consistent with that split.

## Main unknown: exact cluster and productive route

For the concrete Audi B9 vehicle, we still need to identify:

- exact Virtual Cockpit / instrument-cluster part number;
- HW/SW identification and generation;
- native resolution;
- stock navigation DisplayManager context/display IDs;
- the exact DisplayManager display/context and geometry that correspond to the B9 Virtual Cockpit LVDS map surface;
- how ownership/arbitration differs from the MU1440/AID10 reference target.

This LVDS display/context/geometry mapping is now one of the principal portability gates.

## Binary / ABI gate

The contributor also reported promising binary-level similarities around the classic Harman AirPlay/CarPlay implementation, including matching-looking hook-site prologues and relevant Screen/AES/SendCommand symbol families.

Those observations are useful for prioritizing the target, but they are **not** yet sufficient to declare ABI compatibility. The next safe step is an exact read-only comparison of:

1. stock `libairplay.so` identity/hash;
2. target functions and prologue bytes;
3. call signatures / relocation dependencies;
4. required DSI/DisplayManager interfaces;
5. process-local dependencies used by the MU1440 hook.

Only after that comparison should a target-specific build be considered.

## Contributor follow-up: exact B9 VC and static HMI topology

A follow-up report substantially tightens the target definition.

### Cluster identity

Cluster service information captured before a later cluster update:

```text
Part number / HW: 8W5920790C / H15
SW:               0299
SW GSS:           C1_AU491_0297_0930_prod
SW KSS:           --- / 0931
SW ZPM:           190
ZPM DB GSS/KSS:   28 / 28
Display supervisor / vision state: 0 / 1
Text DB:           0.60F(19800)
```

A later cluster update package was applied:

```text
AU_C1_AU491_0313_0991_prod_8S0906961AG_A4-A5-Q5_2017
```

The cluster service screen has **not** yet been reread afterwards, so the current cluster SW values must not be inferred from the package name. The head unit remained `MHI2_ER_AUG22_K3346 / MU1438`, so the head-unit hashes and static analysis in this research note remain current.

### Resolution evidence

The exact native raster has not yet been measured on this vehicle.

Strong indirect evidence points to the early B9 Virtual Cockpit **1440x540** class:

- the stock HMI uses `LayoutMIB2HighB9` / `LayoutMIB2HighB9Sport`;
- observed KDK geometry is:
  - small: position `1055,207`, crop `210x153`;
  - large: position `1091,110`, crop `328x180`;
  - sport-small: position `984,139`;
- a commercial A4/A5/Q5/Q7 MHI2Q comparator advertises a `1440x540` cluster AltScreen and reproduces the same KDK geometry.

Treat `1440x540` as **strongly supported but not yet vehicle-measured** for this exact cluster.

### Static DisplayManager topology

The target's stock `lsd.jxe` statically confirms:

```text
Terminal 1 = cluster
DISPLAYID_CLUSTER = 4
```

When `getSysConst(541) == 2`, the dual-KDK context set is:

| Context | Displayables |
| ---: | --- |
| 72 | `{33}` |
| 73 | `{20, 102, 101}` |
| 74 | `{20, 102, 101, 33}` |
| 75 | `{}` |
| 76 | `{58}` |
| 77 | `{20, 102, 101, 58}` |
| 78 | main display: `{16, 59}` |

Known displayables:

```text
33  DISPLAYABLE_KOMBI_MAP_VIEW
58  DISPLAYABLE_GOOGLE_EARTH_KOMBI_MAP_VIEW
20  DISPLAYABLE_MAP_ROUTE_GUIDANCE
59  DISPLAYABLE_EXTERNAL_SMARTPHONE
101/102 HMI-created KDK backing images
```

For `getKombiType() == 4`, the HMI adds contexts `i + 79` with displayable 20 placed in front.

This means the **static composition contract is no longer unknown**. What remains open is the runtime mapping of real cluster states to these contexts on the vehicle.

### Runtime tooling limitation

On this firmware, `dmdt` is not currently usable for runtime capture:

- without `IPL_CONFIG_DIR`, it aborts looking for `./config/framework.json`;
- with `IPL_CONFIG_DIR=/etc/eso/production`, commands such as `gs/gc/gd` exit without useful output.

The running DisplayManager mode is reported as:

```text
displaymanager 2_lvds
```

and the process list also contains `videoovermost` plus four `devp-iso-mmx-mib2` instances for `isoRX1`, `isoRX2`, `isoTX1` and `isoTX2`.

This is consistent with the OEM topology: presence of MOST processes/devices does **not** imply that the B9 VC large map uses MOST.

### libairplay identity and hook-site evidence

Target stock `libairplay.so`:

```text
SHA-256 34503a9f18799420005d7bd40c3cbf84634c8c9030bb0dcba79d519f0227e949
```

The first two ARM instructions of all three functions used by the current MU1440 direct inline hook are:

```text
AirPlayReceiverSessionSetup     e92d4ff0 ed2d8b02
AirPlayReceiverSessionStart     e92d4ff0 ed2d8b02
AirPlayReceiverSessionTearDown  e92d4ff0 ed2d8b02
```

These are **exactly the same two instructions required by the current MU1440 fail-closed hook implementation**.

Additional reported entry points:

```text
PlatformControl        e92d4ff0 ed2d8b04 ...
SessionControl         e92d4ff0 e1a06002 ...
AirPlayCopyServerInfo  e92d4ff0 e24dde4d ...
SetSecurityInfo        e92d41f0 e28050b4 ...
AES_CBCFrame_Init      e92d41f0 e1a05003 ...
```

This is strong evidence for common Harman AirPlay lineage, but **not an ABI pass by itself**.

### Potential PLT-interposition opportunity

The contributor reports that internal call sites found so far reach these functions through PLT stubs rather than direct `bl` instructions.

The verified follow-up now confirms lifecycle PLT/JUMP_SLOT routes on **both**
MU1438 and MU1440. MU1440's need for inline hooks is runtime-derived; it cannot
be explained simply by absence of PLT entries. No Audi-only preload advantage
has been established. See the [offline pair report](MU1438_MU1440_OFFLINE_COMPARISON_2026-09-28.md).

Before choosing the hook strategy, perform an offline relocation/binding audit:

1. inspect dynamic symbol binding/visibility for Setup/Start/TearDown;
2. inspect `JUMP_SLOT` / relocation entries;
3. check dynamic flags for `DF_SYMBOLIC` / equivalent local-binding behavior;
4. enumerate all call sites and callback/function-pointer references;
5. prove whether an `LD_PRELOAD` replacement actually wins symbol resolution.

Do **not** remove the inline-hook option: its existing two-instruction prologue gate already matches these three MU1438 functions exactly.

### Stock AirPlay screen model

Additional static findings:

- stock `AirPlayReceiverSessionSetup` uses a single session screen object and does not read `displayUUID`;
- `AirPlayReceiverSessionPlatformCopyProperty("displays")` returns one display entry through `AirPlayReceiverSessionScreen_CopyDisplaysInfo -> ScreenCopyMain()`;
- `NvssVideoImpl::start` reads `NVSS_VIDEO_OUTPUT_DEVICE` at start and opens NvSS with layer ID **59**.

This makes the output-device/profile configuration a first-class porting seam for the Audi target.

## Verified local firmware/companion comparison

The exact `MHI2_ER_AUG22_K3346_MU1438` update was located and compared against
`MHI2_ER_SKG13_P4526_MU1440`: **13 native pairs, 26 Ghidra inputs, zero failed
function exports**, 16 focused AirPlay target comparisons and the selected Java
display-owner classes. Both backup/unit JXE hashes match the update stage2/50
JXEs exactly. This closes source-identity gaps without claiming a runtime ABI pass.

The detailed [pair report](MU1438_MU1440_OFFLINE_COMPARISON_2026-09-28.md) and
[compatibility/component matrix](../testing/COMPATIBILITY_MATRIX.md#exact-mu1438-versus-mu1440-component-matrix)
separate stable code, implementation changes and required adaptations.

Important refinements:

- Five app `.text` pairs are identical; the MOST driver differs only in two
  physical-address header bytes. Low-level similarity does not make MOST the
  productive Audi VC map transport.
- The security context/key/IV fields shift +8 bytes, but the existing AES
  observer consumes function arguments. The stock call returns at +0x38 on
  both builds, inside the existing +0x100 caller gate. Runtime validation remains open.
- Audi NvSS startup has no Skoda-style missing-environment guard and uses a
  different C++ wrapper handle offset. Both use layer 59; that is not cluster context 59.
- `HMIKombiMapControlActivator` selects terminal0/LVDS for KombiType4,
  terminal1/LVDS for other targets with SysConst541==2, otherwise terminal1/H264-MOST
  on the remaining activated path. Static context 72–78 applies to SysConst541==2;
  this is not a blanket statement about every Audi profile.
- DMDT command/core code is unchanged in the compared pair. The reported runtime
  no-output condition needs service/config/ownership diagnosis; its cause is not
  resolved by this offline comparison.

## Admission criteria

Audi B9 / AUG22 MU1438 can move from **compatibility lead** to a vehicle-test candidate only after:

- [x] exact firmware + 13 stock component-pair hashes captured (app50/stage2-50 scope);
- [x] focused offline native/Java comparison completed and limitations documented;
- [x] pre-update cluster identity captured (`8W5920790C`, H15, SW 0299); **post-update/current SW still needs confirmation**;
- [x] OEM transport architecture identified as LVDS for the B9 Virtual Cockpit large map; runtime activity/context validation still pending;
- [ ] hook/ABI comparison passes;
- [ ] target-specific build is produced rather than bypassing the MU1440 gate;
- [ ] reversible STOCK recovery is defined;
- [ ] first vehicle experiment is bounded and logged.

Until then, the correct status is **interesting and structurally promising, but unvalidated**.

## Audi OEM source

Audi service-training networking material documents the B9/J794 -> J285 image-transfer split: Virtual Cockpit large navigation map and detailed intersection maps over LVDS; list menus/covers and cluster software update over MOST. See the cross-brand [MIB2 cluster/display transport knowledge base](MIB2_CLUSTER_DISPLAY_TRANSPORT_KNOWLEDGE_BASE.md) for the source set and comparison with MQB MOST clusters.
