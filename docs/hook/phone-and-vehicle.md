---
title: Phone tab, vehicle data and Siri - ported from mib2q-carplay-rgi
tags: [hook, telephony, vehicle, siri, port]
status: ported-car-test-pending
sources:
  - code: hook/telephony/tel_hook.c
  - code: hook/vehicle/vehicle_hook.c
  - code: java_patch/com/luka/carplay/telephony/
  - code: java_patch/com/luka/carplay/core/VehicleDataModule.java
  - code: java_patch/de/audi/app/combi/bap/app/phone/ServiceManagerPhone.java
  - code: java_patch/de/audi/app/terminalmode/smartphone/carplay/CarPlaySpeechRequestHandler.java
  - upstream: luka-dev/mib2q-carplay-rgi f2141d0 (docs/cluster-and-rgi/CARPLAY_TELEPHONY.md, docs/connect-and-usb/VEHICLE_DATA_TO_PHONE.md)
---

# Phone tab, vehicle data and Siri

Three CarPlay features ported unchanged from
[luka-dev/mib2q-carplay-rgi](https://github.com/luka-dev/mib2q-carplay-rgi) (`f2141d0`, GPL-3.0).
Upstream verified them on the car (MU1316); here they are built against our MU1329 HMI and are
**car test pending**. The full design notes live upstream:
`docs/cluster-and-rgi/CARPLAY_TELEPHONY.md`, `VC_TELEPHONE_TAB.md`, `IAP2_COMMUNICATIONS.md` and
`docs/connect-and-usb/VEHICLE_DATA_TO_PHONE.md`.

## 📞 CarPlay calls on the cluster phone tab

While CarPlay owns the iPhone the VC telephone tab works as with a Bluetooth phone: caller,
call state and timer, accept / hang up / hold / swap / mute (tab and steering wheel), dialling,
recents, favorites, missed calls, signal and carrier. The HU phone app skips the "Your iPhone
is in Apple CarPlay mode" popup and goes straight to CarPlay. No caller photo (iAP2 has none).
The skip fires when the popup's phone state (`telCarPlayDisclaimer_compound`) is entered by any
route, including the phone app's deep history bringing it back on MENU -> Telephone or TEL
(issue #69). The entry guards that pick it are hooked as well, and a one-second dedupe keeps one
entry to one press.

| Piece | Role |
|---|---|
| `hook/telephony` | StartListUpdates 0x4170 / ListUpdate 0x4171, CommunicationsUpdate 0x4158, PowerUpdate battery; sends AcceptCall / EndCall / SwapCalls / Hold / Mute on `CMD_TEL` |
| bus | `EVT_TEL_LISTS 0x0040`, `EVT_TEL_COMM 0x0041` (sticky), `CMD_TEL 0x0117` |
| `GatedPhoneService` / `GatedPhoneListener` | wrap the stock PHONE 0x28 BAP service; on only while stock reports `mobileConnectionType == 5` (CarPlay), else everything passes through |
| `ServiceManagerPhone`, `phone/sm/Activator`, `system/sm/Activator` | patched stock classes that install the wrappers and the popup skip (`CarPlayPhoneSMM`, `CarPlaySystemSMM`) |
| `CarplayDSILifecycleController` | forwards `updateCallState`, dials with `tel:` URLs, drops the duplicate stock Tel key |

Dialling from the tab uses the stock `tel:<number>` URL, with the contact formatting (spaces,
dashes, brackets, NBSP, bidi marks) stripped first: a favorite's RemoteID is the number as stored
in the contact, a recent's is plain digits, and iOS ignores a `tel:` URL with formatting in it. A
favorite iOS lists without a RemoteID is found again by the name the VC echoes and dialled with
iAP2 InitiateCall 0x415A (Type 0, Service, AddressBookID; `CMD_TEL` action 6). The hook logs once
per list change how many favorites came with a number and with an AddressBookID (counts only).
*Car test pending* (`tests/GatedPhoneDialTest.java`, `tests/tel_hook_test.c`).

Bluetooth phones are untouched: the gate stays off.

### Android Auto (our addition, not upstream)

Stock sends `mobileConnectionType` 6 while Android Auto is active, and the VC shows its
"Android Auto" note for 5, 6 and 7 (VC binding rule, upstream `VC_TELEPHONE_TAB.md`). The
calls themselves still reach the cluster: Android Auto calls run over Bluetooth HFP, and stock
`BAPPropertyTelCallListUpdater` keeps publishing them (it only drops the incoming call on G24
clusters while Android Auto has video focus). So `GatedPhoneService` rewrites 6 to 3 and passes
everything else through: call view, lists and accept / hang-up are the stock Bluetooth phone's.
No hook is involved. Opt-out: `/mnt/app/carplay_aa_phone_tab.off`. Test:
`tests/GatedPhoneAndroidAutoTest.java`. *Car test pending.*

## 🚗 Vehicle data to the iPhone

`VehicleDataModule` (CarPlay sessions only) reads DSI wheel speed, reverse gear, standstill,
range, outside temperature and the fuel warning five times a second and sends them as
`CMD_VEHICLE 0x0118`. The hook turns them into `$PASCD` sentences (wheel speed, once the phone
asks) and VehicleStatusUpdate 0xA101 (range, temperature, low-range warning, at most 1 Hz).
iOS accepts the VehicleStatusComponent (Identify param 21) only when the Identify also declares
StartVehicleStatusUpdates 0xA100, VehicleStatusUpdate 0xA101 and StopVehicleStatusUpdates 0xA102;
otherwise it rejects it. So the hook appends the vehicle components only when Cinemo's Identify
lists all three, that is, when the installer's `dio_manager.json` change is active (see Install), and
logs it otherwise. There is no rejection fallback: SI starts a new `dio_manager` for every connect,
so a per-process fallback re-sent the rejected component every time (upstream c86bc3d, issue #53).

`carplay_startup.sh` switches: `IAP2_VEHICLE_STATUS`, `IAP2_VEHICLE_SPEED`, `IAP2_PHONE_BATTERY`
(all `on`) and `IAP2_VEHICLE_ENGINE` (`gasoline`; one word `gasoline` / `diesel` / `electric` /
`cng` in `/mnt/app/root/carplay_vehicle_engine` overrides it).

## 🎙️ Siri on a short press

**Opt-in** (our change; upstream has it always on). With the marker `/mnt/app/carplay_siri_short.on`
(GEM *Siri on short voice-key press ON / OFF*) a short press of the steering-wheel voice key opens
Siri instead of the car's voice control while CarPlay is connected (patched
`CarPlaySpeechRequestHandler`, `TerminalModeSDSHandler`, switch `SiriShortPress`, read on every
press). Without it both classes answer exactly as stock: the short press starts the car's voice
control, the long press Siri. Test: `tests/SiriShortPressTest.java`.

## ⚙️ Install

The installer adds the message IDs to `dio_manager.json` (`patch_dio`): for telephony and vehicle data,
sent `0x4154 0x4157 0x415A 0x415B 0x415C 0x415D 0x415F 0x4160 0x4170 0xA101 0xFFFB`, received
`0x4155 0x4158 0x4171 0xA100 0xA102 0xFFFA 0xFFFC`, and it sets the PASCD bit 0x08 in `"nmea"`. iOS rejects a
feature group unless every required message of it is declared, so the stock IDs of each group are
re-added too when something else trimmed them, and an ID found in the wrong list is moved (upstream
61897b5, 1a7bc73). A unit installed with 1.1 needs the installer run again; it only adds what is missing.

## 🧪 Tests

`tests/tel_hook_test.c`, `tests/vehicle_hook_test.c` (`scripts/run_tests.sh`),
`tests/VehicleDataEncodingTest.java` (`scripts/test_java_transports.sh`),
`scripts/test_install_dio.sh`.

## 🔍 Stock-class check

The five stock classes taken whole from upstream (`ServiceManagerPhone`, `TerminalModeSDSHandler`,
`CarPlaySpeechRequestHandler`, `phone/sm/Activator`, `system/sm/Activator`) were compared with our
P5152 `base.jar`: four are byte-identical to upstream's MU1329 jar, `ServiceManagerPhone` differs
only in constant-pool order (same bytecode). `CarplayDSILifecycleController` already carries our
own changes, so only upstream's four telephony hunks were merged into it.
