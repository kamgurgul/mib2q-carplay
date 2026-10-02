> Imported from `mhi2_altscreen_carplay` (docs/research/). Links to `../testing/…`
> matrices point into that repo and are not carried here.

# Exact Audi MU1438 vs Skoda MU1440 offline comparison

Date: 2026-09-28. Status: **static evidence, not Audi runtime support**.

This audit compares Audi `MHI2_ER_AUG22_K3346_MU1438` with Skoda
`MHI2_ER_SKG13_P4526_MU1440`. It rechecks the concrete compatibility lead rather
than extending the current MU1440 admission gate to another brand.

## Source and method

| Input | SHA-256 | Scope |
| --- | --- | --- |
| Audi update archive | `d859eca607a492625e71f40a90503f2011a7828be853b5e86d147912203dbfa2` | exact AUG22 K3346 MU1438 A4/A5/Q5/Q7 package |
| Skoda update archive | `b23225d68999f0613f2652c898ce36c74d6c1d63a36f29ff5d4faf500c60e5a5` | exact SKG13 P4526 MU1440 package |
| Audi stock libairplay | `34503a9f18799420005d7bd40c3cbf84634c8c9030bb0dcba79d519f0227e949` | app50 |
| Skoda stock libairplay | `193a4fd9101ec2aa05e7159cfa307b96500810d379ca74a194f172adc13a46b5` | app50 reference hash |
| Audi LSD JXE | `e43d80e7eeb803d6a7db29908562b9545e7b17138d4660ce3a6efafa974b99ce` | update stage2/50 equals earlier backup input |
| Skoda LSD JXE | `a55d9cfb69c5756f8202b7f7aa4079d4d5b637ae4c2fd0fe723f1d6816cbeea8` | update stage2/50 equals earlier unit-export input |

Native comparison covers **13 component pairs / 26 exact ELF inputs**, app50
and selected stage2/50. Ghidra 12.1.3 exported all 26 inputs with zero failed
function reconstructions; this is not a proof of recovered type correctness.
pyelftools/Capstone independently checked complete section/function bytes,
dynamic names, dependencies and ARM PLT routes. There are 2,097 common named
function pairs and 16 focused AirPlay targets. The call-site scan is bounded by
available symbols and is not an exhaustive indirect/callback audit.

Full update archives and reconstructed bulk output are local-only. Exact small
native/class/config fixtures and 34 selected AirPlay pseudocode bodies are
retained in private Research for independent review. **No OEM binaries,
decompiled OEM source, unit identifiers, keys or local filesystem paths are
published here.** Public evidence is the identity/delta matrices and findings.

App70 has a filesystem comparison only; no app70 ABI admission is implied.
Audi production configs are backup/contributor evidence, not asserted here to
equal every shipped update profile. Skoda configs are update EFS-system/50.
Not all inner Audi filesystems were decoded; stage2/50 was explicitly added to
close the earlier app-only DisplayManager/JXE gap.

## Native difference matrix

See the direct [compatibility matrix](../testing/COMPATIBILITY_MATRIX.md#exact-mu1438-versus-mu1440-component-matrix),
[full component CSV](../testing/MU1438_MU1440_COMPONENT_MATRIX.csv),
[hook CSV](../testing/MU1438_MU1440_HOOK_MATRIX.csv) and
[routing CSV](../testing/MU1438_MU1440_ROUTING_MATRIX.csv).
The [function-difference summary](../testing/MU1438_MU1440_FUNCTION_DIFF_SUMMARY.csv)
adds per-component counts: e.g. AirPlay has 1229 common bounded names, 232
byte-identical, 606 with only mnemonic-shape equality, 391 with different shapes;
the IRC adapter has 300/15/160/125. These are intersections of nonzero-sized
ELF function symbols, including aliases, **not fractions of complete library
coverage or estimates of required rewrite effort**. DMDT's zero in that summary
means no such common bounded names, not no recovered functions (Ghidra exports
55 per build); its whole `.text` still matches.

The important split is:

- `libiap2client.so.1`, `libnvss_video.so` and `mm-ipod`: identical `.text` and
  RX load payloads; file identity differs only in QNX/debug information.
- `dmdt`: identical `.text`; the only rodata-string change is version
  `2.11.26` → `2.11.27`. `libdmdt_core.so` also has identical `.text`; its nine
  changed rodata strings are build paths/build information.
- `devp-iso-mmx-mib2`: exactly two differing file bytes, `0xa1` and `0xa2`, in
  the first PT_LOAD `p_paddr` field (`0x404000` → `0x443000`). All other bytes
  match. The RX hash includes that ELF header, so a differing RX hash here must
  not be misreported as changed driver instructions.
- `libairplay`, `dio_manager` and `smartphone_integrator`: code/interface
  deltas. AirPlay adds `libsocket.so.3`/`libm.so.2` dependencies on Skoda;
  smartphone integration needs `libusbdi.so.2` only on Audi.
- `libdsicarplayproxy`, `videoovermost` and `libirc_mmx_adapter`: same dynamic
  name/needed sets, different code. That is not a callback/object ABI pass.
- `displaymanager`: different RX payload (566548 vs 569060 bytes), with section
  names stripped. It lives in stage2 on both trains, not in QNX6 app.img.

Dynamic counts in the CSV include named data as well as functions: smartphone
integration has 864/726 defined and 365/355 undefined names. Earlier narrower
tool counters are not interchangeable with this scope.

## Lifecycle and security: what actually aligns

Addresses below are **unrebased ELF virtual addresses**, not file offsets.
Ghidra added `0x10000` to these DYN imports; do not copy its displayed addresses
unchanged into a target profile.

| Function | Audi VA / bytes | Skoda VA / bytes | Static result |
| --- | --- | --- | --- |
| Setup | `0x22fe0` / 5032 | `0x24058` / 4520 | different body; same hook-gate prologue |
| Start | `0x21368` / 2992 | `0x21f80` / 3148 | different body; same hook-gate prologue |
| TearDown | `0x22b24` / 1004 | `0x23868` / 1036 | different body; same hook-gate prologue |
| PlatformControl | `0x1b718` / 2392 | `0x1be98` / 2436 | different body; expected entry pattern matches |
| SessionControl | `0x24d90` / 1660 | `0x25c80` / 1660 | same mnemonic shape, not an ABI proof |
| SetSecurityInfo | `0x24c64` / 108 | `0x25b54` / 108 | same flow, shifted session fields |
| AES_CBCFrame_Init | `0x2d1bc` / 88 | `0x2e84c` / 88 | stock arguments/caller gate align statically |
| DeriveAESKeySHA512ForScreen | `0x28de0` / 252 | `0x2a39c` / 296 | Skoda adds allocation/length guards |

Setup/Start/TearDown start with `e92d4ff0 ed2d8b02` on both builds.
PlatformControl matches `e92d4ff0 ed2d8b04`, SessionControl
`e92d4ff0 e1a06002`. Thus the existing entry checks positively match; they are
not a complete CF/argument/response-descriptor ABI validation. Only
`AES_CTR_Final` is byte-identical among the 16 focused targets.

`SetSecurityInfo` uses the following structure offsets:

| Field | Audi | Skoda |
| --- | --- | --- |
| AES context storage | `0xb4` | `0xbc` |
| context pointer | `0x1bc` | `0x1c4` |
| copied 16-byte key | `0x1c0` | `0x1c8` |
| copied 16-byte IV | `0x1d0` | `0x1d8` |

Both finalize, clear, call stock `AES_CBCFrame_Init(ctx,key,iv,0)` and store on
success. The call returns at entry +`0x38` on both (`0x24c9c` / `0x25b8c`),
inside the existing symbol-resolved +`0x100` gate. Current GEN2 observes the
**function arguments**, not these structure offsets. Therefore the +8-byte
shift is a direct-object-access concern, not automatic failure of the AES
observer or stock-preserving lifecycle trampoline. Loader/coverage and actual
stream derivation still need runtime proof. No session keys were gathered.

The per-screen SHA-512 helper follows the same named derivation/temporary wipe
pattern, but MU1440 guards formatted buffer pointers/lengths where MU1438 does
not. This is an error-path difference, not proof of every salt or wire profile.

### PLT correction

Both stock libraries expose GLOBAL/DEFAULT lifecycle definitions and
`R_ARM_JUMP_SLOT`/PLT routes; neither inspected object declares symbolic binding.
Audi's named HTTP request paths use lifecycle PLT stubs, and required
security/control/property calls use PLT on both builds.

**This is not an Audi-only preload opportunity.** MU1440's inline requirement
is vehicle-derived evidence; the static pair does not prove QNX loader
precedence, local resolution or every indirect callback path. Keep the inline
strategy available and test target-specific runtime coverage separately.

The first-pass PLT decoder had treated a rotated immediate operand as an already
resolved value. The corrected audit decodes ARM imm8/rotation, and regression
checks the exact AES route. Setup is now hashed over its full extent instead
of an initial 4096-byte prefix. A mnemonic-shape match still ignores operands.

## Presentation and HMI: the principal architecture seam

Both AirPlay NvSS startup wrappers read `NVSS_VIDEO_OUTPUT_DEVICE`, convert it
to a zero/nonzero device selection and open layer **59**. Audi lacks the
Skoda NULL-environment check; Skoda also rejects an already-open handle. The
wrapper handle is at +`0x50` on Audi, +`4` on Skoda. The stable low-level
NvSS library does not make these C++ wrappers interchangeable.

Layer59 is not display ID59 or cluster context59. The stock display-property
path uses `ScreenCopyMain` and one stock screen object; an extra UUID alone is
not proof of a second independently routed output.

Audi's `HMIKombiMapControlActivator` branches as follows:

| Condition | Controller terminal | Display type |
| --- | ---: | --- |
| KombiType == 4 | 0 | LVDS (1) |
| otherwise SysConst541 == 2 | 1 | LVDS (1) |
| remaining activated path | 1 | H264/MOST (2) |

Activation requires KombiType4 or SysConst541 in {1,2}; the screen registry uses
HMI terminal1 separately. Its SysConst541==2 context 72–78 composition is
documented in the [Audi lead](AUDI_B9_MU1438_COMPATIBILITY_LEAD_2026-09-28.md).
Type4 adds contexts i+79. Live values were not captured by this offline run.

Audi uses `DisplayManagerMIB2High`/`MapControllerEvoHigh` and model-specific
B9/B9Sport layouts. Skoda uses MOST stream-sink `DisplayManagementAdapter` and
`NavigationMapAdapter` service/event ownership. Shared display ID4 and
displayables33/58/59 are semantic anchors, not portable class implementations.

Both production configs expose TFTLCD0/id0 and HDMI0/id4 in `2_lvds`; Audi marks
the secondary display annotated. MOST defaults differ: queue8/18,
bytes/frame20000/40960, force routing component_control/unset. Camera target
dimensions in those configs must not be treated as cluster panel/AltScreen
viewport measurements.

The reported Audi DMDT no-output behavior is **not explained** by a changed
command implementation: DMDT/client-core code matches. Framework service
availability, active mode and config/ownership remain diagnostic candidates.

## Required adaptation order and remaining gates

1. Separate exact-hash Audi profile; audit signatures, CF object/value types,
   stream111 descriptors, lifecycle/control entry checks and crypto errors.
2. Preserve Audi DIO/USB/process-launch configuration and stock delegation;
   do not substitute companion apps/libraries based on filename/name sets.
3. Define Audi-specific **LVDS/NvSS presentation and ownership**. The proven
   MU1440 MOST Direct-TS writer is not by itself an Audi LVDS presenter.
4. Measure productive KombiType/SysConst/context, annotation, native output
   geometry and STOCK restore on the exact cluster/software revision.
5. Only then produce a bounded target-specific build/test candidate and prove
   STOCK → project → STOCK. Keep the existing MU1440 hash gate unchanged.

This report changes documentation/matrices only. It does not provide an Audi
installer, modify runtime binaries, claim current cluster software from an
update package name, or admit other MHI2/MHI2Q variants.
