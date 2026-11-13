---
title: Maneuver mapping - iAP2 type -> BAP descriptor
tags: [rgd, maneuver, ios-re, verified]
status: verified-source
sources:
  - code: java_patch/com/luka/carplay/rgd/ManeuverMapper.java
  - code: java_patch/com/luka/carplay/rgd/RendererMapper.java
  - code: hook/routeguidance/rgd_tlv.h
  - code: hook/routeguidance/rgd_tlv.c
  - firmware: Maps 23G71 +[CarClusterUpdateManeuverInfo maneuverUpdateWithStep:component:]
  - firmware: CarPlay 23G71 +[CPManeuver _descriptionForTrafficSide:]
reconciles:
  - docs/reference/IOS266_MANEUVER_DECOMPILE.md
  - docs/archive/MIB3.md
---

# Maneuver mapping - iAP2 type -> BAP descriptor

Turns the per-maneuver `0x5202` fields into a BAP `ManeuverDescriptor` (icon + direction + side
streets). Ground truth = `ManeuverMapper.map()`, validated against Apple's Maps/CarPlay 26.6 binaries.

## 📋 Context

> [rgd-tlv](rgd-tlv.md) - 0x5202 sub-TLVs -> **maneuver-mapping** - type+angle -> icon -> [bap-fctids](bap-fctids.md) - FctID 23

```mermaid
flowchart LR
    accTitle: Maneuver mapping inputs and outputs
    accDescr: ManeuverMapper turns the 0x5202 maneuver type, exit angle and driving side into a BAP element and direction for FctID 23 and the RendererMapper path.

    tlv["0x5202 type(0x03)<br/>+ exitAngle(0x0B)<br/>+ drivingSide(0x08)"] --> mm["ManeuverMapper<br/>type->element, angle->direction"]:::here
    mm --> f23["FctID 23<br/>ManeuverDescriptor"]
    mm --> rend["RendererMapper -> maneuver_render"]
    classDef here fill:#fde68a,stroke:#b45309,color:#000;
```

## 📊 Inputs

- **Type** (`0x5202`/0x03) - `EManeuverType` 0-53, verified against MHI3 dio_manager
  `CDIONavigationMetadataTypeInfo::toString`.
- **JunctionElementExitAngle** (`0x5202`/0x0B) - signed BE16; `rgd_tlv.c` publishes it as both
  `turn_angle` and `exit_angle`. See [rgd-tlv](rgd-tlv.md).
- **drivingSide** (`0x5202`/0x08) - 0/1; side fallback when the angle is absent.

## 📊 Complete type -> BAP element table (0-53)

| # | EManeuverType | BAP mainElement | direction |
|---:|---|---|---|
| 0 | NO_TURN | FOLLOW_STREET | STRAIGHT |
| 1 | LEFT_TURN | TURN | LEFT |
| 2 | RIGHT_TURN | TURN | RIGHT |
| 3 | STRAIGHT_AHEAD | TURN | STRAIGHT |
| 4 | U_TURN | UTURN | signed (angle<0->L else R; side fallback) |
| 5 | FOLLOW_ROAD | FOLLOW_STREET | STRAIGHT |
| 6 | ENTER_ROUNDABOUT | TURN | L/R by driving side |
| 7 | EXIT_ROUNDABOUT | EXIT_ROUNDABOUT_TRS_L/R | L/R by driving side |
| 8 | OFF_RAMP | TURN | SLIGHT_L/R (side from angle sign, else driving side) |
| 9 | ON_RAMP | TURN | SLIGHT_L/R (same rule) |
| 10 | ARRIVE_END_OF_NAVIGATION | ARRIVED | STRAIGHT |
| 11 | START_ROUTE | TURN | `directionFromTurnAngle` |
| 12 | ARRIVE_AT_DESTINATION | ARRIVED | STRAIGHT |
| 13 | KEEP_LEFT | TURN | SLIGHT_LEFT |
| 14 | KEEP_RIGHT | TURN | SLIGHT_RIGHT |
| 15 | ENTER_FERRY | FOLLOW_STREET | STRAIGHT |
| 16 | EXIT_FERRY | TURN | `directionFromTurnAngle` |
| 17 | CHANGE_FERRY | FOLLOW_STREET | STRAIGHT |
| 18 | START_ROUTE_WITH_U_TURN | UTURN | signed |
| 19 | U_TURN_AT_ROUNDABOUT | ROUNDABOUT_TRS_L/R | UTURN |
| 20 | LEFT_TURN_AT_END | TURN | `dirFromEndOfRoadAngleLeft` |
| 21 | RIGHT_TURN_AT_END | TURN | `dirFromEndOfRoadAngleRight` |
| 22 | HIGHWAY_OFF_RAMP_LEFT | TURN | SLIGHT_LEFT (type wins over angle) |
| 23 | HIGHWAY_OFF_RAMP_RIGHT | TURN | SLIGHT_RIGHT (type wins over angle) |
| 24 | ARRIVE_DESTINATION_LEFT | ARRIVED | LEFT |
| 25 | ARRIVE_DESTINATION_RIGHT | ARRIVED | RIGHT |
| 26 | U_TURN_WHEN_POSSIBLE | UTURN | signed |
| 27 | ARRIVE_END_OF_DIRECTIONS | ARRIVED | STRAIGHT |
| 28-46 | ROUNDABOUT_EXIT_1 ... _19 | ROUNDABOUT_TRS_L/R | `directionFromAngle16(turnAngle)` |
| 47 | SHARP_LEFT_TURN | TURN | SHARP_LEFT |
| 48 | SHARP_RIGHT_TURN | TURN | SHARP_RIGHT |
| 49 | SLIGHT_LEFT_TURN | TURN | SLIGHT_LEFT |
| 50 | SLIGHT_RIGHT_TURN | TURN | SLIGHT_RIGHT |
| 51 | CHANGE_HIGHWAY | TURN | `directionFromTurnAngle` |
| 52 | CHANGE_HIGHWAY_LEFT | CHANGE_LANE | LEFT |
| 53 | CHANGE_HIGHWAY_RIGHT | CHANGE_LANE | RIGHT |

The `-1` `MT_NOT_SET` sentinel (the `RouteGuidance.State` default) -> `NO_SYMBOL / STRAIGHT`. The `255`
wire sentinel (`MAN_TYPE_NOT_SET` in `rgd_tlv.h`) and any type outside `0-53` are rejected by
`ManeuverMapper.isValidType()` in `BAPBridge` **before** `map()` is ever called. A valid in-range type
that falls through the switch -> `NO_INFO / STRAIGHT`.

## 📌 Signed exit angle is the primary direction rule

Apple normalizes the junction angle sign per traffic side, in **both** maneuver builders
(`maneuverUpdateWithStep:` and `maneuverUpdateWithGuidanceEvent:`), ending in an `FNEG`:

| `drivingSide` | traffic side (`_descriptionForTrafficSide:`) | ordinary U-turn exit angle |
|---:|---|---|
| 0 | right | negative -> **left** U-turn |
| 1 | left | positive -> **right** U-turn |

So the receiver honours a **signed** angle for accNav types **4, 18, 26**; `drivingSide` is the
fallback when no role-2 angle is present. The `0/+/-1000 = absent` sentinel is our own convention
(Apple Maps 26.6 never synthesizes it). Apple collapses GEO 86/88 -> accNav 4; GEO 25/35 keep their
own signed angle. **Type 19 stays a roundabout, not a U-turn.**

## 💡 Ramps are one slight turn

`OFF_RAMP` / `ON_RAMP` / `HIGHWAY_OFF_RAMP_LEFT|RIGHT` all map to BAP `TURN` + `SLIGHT_L/R`. BAP
`EXIT_LEFT/RIGHT` is no longer used: even the base EXIT glyph adds a second bend back to straight,
geometry CarPlay's single angle does not describe. For types 8/9 the angle **sign** only picks the side
(`rampGoesLeft`; 0 and +/-1000 fall back to `drivingSide`); for 22/23 the side in the type wins even if
the angle conflicts. The renderer draws the same single bend and, for off-ramps, adds the continuing
main road (`RendererMapper.withForwardRoad`) - see [maneuver-renderer](../cluster/maneuver-renderer.md).

## ⚙️ Generic angle -> direction

`directionFromTurnAngle` (START_ROUTE, EXIT_FERRY, CHANGE_HIGHWAY) buckets at 45 deg: `<=22` straight,
`<=67` slight, `<=112` normal, else sharp; `|angle| > 180` keeps the old signed LEFT/RIGHT fallback.
`map(..., anglePresent)` treats a received `-1` as a real angle; only the legacy unfilled State slot
(`-1` without presence) is normalized to the `1000` "absent" sentinel. `LEFT/RIGHT_TURN_AT_END` keep
their end-of-road direction (not coarsened by the DSI override).

## 🔐 No junction gate

The icon is chosen from `maneuverType`, exit angle and driving side only, as MIBSI does. MHI3's
`tbt_renderer` also gates on `junctionType` (non-roundabout types -> `NO_INFO` unless it is `0`), but
third-party apps (e.g. Sygic) may omit the JunctionType field; `State` then keeps `-1` and every ordinary
turn lost its arrow ([#9](https://github.com/kamgurgul/mib2q-carplay/issues/9)). `junctionType` still
selects the side-street algorithm (`SideStreets`: `0` intersection, `1` roundabout, else none).

## 🧭 Per-app compensation (source stacks)

Ported from luka-dev `52fbc2b`, `5508460`, `a334f1b`. `0x5200`/0x13 SourceName (the app's localized
display name, not a bundle ID) selects a stack in `NavigationStack.forSource`; the stack's modules
adjust a fresh `ManeuverSemantics` value before `ManeuverMapper`, and HUD and renderer use the same
result. The received `RouteGuidance.State` is never changed.

| Stack | Names (exact, case/NBSP-insensitive) | What it does |
| --- | --- | --- |
| `default` | anything else: Apple Maps, Google Maps, Waze, empty, unknown | nothing, mapping as above |
| `baidu` | `百度地图` | infers a missing roundabout junction; CP 28..32 are Baidu *directions*, not exit numbers: coarse hints +90, 0, -45, -90, -135 degrees (never snapped to a road) |
| `amap` | `Amap`, `高德地图`, `高德地圖`, `AMapiPhone` | AMap sends junction 0 for roundabouts: CP 28 = left circle, 29 = generic departure, 30 = right circle |
| `yandex` | Navigator/Maps names (`Навигатор`, `Yandex Navi`, `Яндекс Карты`, ...) | CP 28..46 carry `exitNumber` 1..19 with no angle; exits 1..14 get the illustrative schematic below |

**Roundabout schematic (Yandex only):** with an exit number, no received angle and no real road
geometry, `RoundaboutSchematic` spreads the entry, the exits up to the selected one and one exit after
it over the HUD's 22.5-degree grid. FctID 23 gets the selected direction and the other roads as side
streets; `maneuver_render` gets the same angles with road snapping off. It is drawn, not measured: the
real exit direction is still unknown (exit 2 is always drawn straight on). Exits 15..19 and CP 7 keep
the old fallback. A received angle (also 0 or -1), partial geometry or an app change replaces it.

**Driving side:** none of the analysed apps sets `trafficSide`, so iOS omits `0x5202`/0x08
(`mDrivingSide = -1`). The circulation side then comes from the car's RHD coding
(`CarPlayApp.isVehicleRightHandDrive()`, sysConst 464 == 1 -> left-hand traffic); an explicit 0/1
from the phone still wins. A car driven abroad across the traffic side uses its home side.

**App changes:** the native hook keeps the source name across soft reroutes and writes it into every
cached snapshot (empty = unknown). A different named app flushes the native slot cache, and Java
clears the old app's route content, so maneuvers are never relabelled. A first late name only enriches.

Android Auto does not use this path's stacks: `AaRgState` sends no `source_name` (always `default`),
and AA roundabouts are mapped in `AaManeuverMap` (exit number without angle -> estimated angle,
issue #17). Waze on CarPlay sends `source_supports_rg = 0`, so it has no route guidance at all.

## ⚠️ Product gap

`MAN_TLV_DESCRIPTION` (0x5202/0x02 InstructionText) is parsed but not surfaced on the cluster.
