# Controlled Beta Readiness

Status: Phase 72 bounded local beta gate (device soak 2026-08-14: audio/pacing FAIL, matrix regression)
Date: 2026-06-03 (updated 2026-08-15)

The project is not ready for external beta yet. This document defines the controlled
beta gate, records the current evidence, and makes remaining blockers explicit.

## Release Candidate Checklist

| Gate | Requirement | Current Evidence | Status |
| --- | --- | --- | --- |
| CPU | ARM/Thumb verifier suite passes. | `tools/run-core-tests.ps1` passes. | PASS |
| Timing | WAITCNT, prefetch, scheduler, timer, and PPU timing verifier coverage exists. | `wait_state_control_test`, `core_scheduler_test`, `ppu_timing_test`. | PASS |
| PPU | Renderer synthetic tests cover current bounded modes/layers. | `ppu_renderer_test` passes. | PASS |
| APU | Direct Sound plus PSG/mixer seeds have deterministic tests. | `apu_test` passes. | PASS |
| Save | SRAM/Flash/EEPROM bounded behavior is tested. | `memory_bus_test` passes. | PASS |
| Save-State | Binary codec rejects malformed blobs and restores deterministic public subset. | `save_state_codec_test` passes. | PASS |
| Android Bridge | Opaque-handle bridge can load explicit bytes, run, reset, and hash. | `android_core_bridge_test` passes. | PASS |
| Android Runtime | Runtime can map input, render framebuffer, drain audio, and report underruns locally. | `android_runtime_test` passes. | PASS |
| Performance | Local synthetic benchmark and regression gates pass. | Integrator 2026-06-05: `check-core-performance-regression.ps1` PASS after timing-cache optimization + calibrated ceilings. | **PASS** |
| Device Evidence (P0 synthetic) | At least one target Android device or emulator run: bridge + runtime self-tests PASS, cold start, Home/resume; artifact on disk (no ROMs in git). | [`evidence/2026-06-03-android-device-soak-synthetic-pass.md`](evidence/2026-06-03-android-device-soak-synthetic-pass.md) — adb replay steps; re-run on hardware to attach model/API. | **PASS** (bounded) |
| Device Evidence (audio / thermal / pacing) | Audible playback, p95 frame pacing, 10+ min thermal notes on target hardware. | **FAIL** — soak 2026-08-14 (SM-S938U, Android 16, wireless adb, release build): audio audible but choppy/garbled (2,787 underruns @ frame 60; 710,778 after restart — restart leaves the Oboe stream broken); pacing ~1.4–2 fps degrading to a full presentation stall (0 presents) with `coreUnderruns=0` (core is not the bottleneck); thermal unmeasured. Artifact: [`evidence/2026-08-14-android-device-soak-p1-emerald-wireless.md`](evidence/2026-08-14-android-device-soak-p1-emerald-wireless.md). | **FAIL** |
| Store/Legal | Release governance checklist reviewed. | `docs/release-governance-legal-review.md`. | PASS |

## Regression Suite Gate

Before any beta candidate, run:

```powershell
.\tools\run-core-tests.ps1
.\tools\run-credibility-matrix.ps1 -FailOnRegression
.\tools\check-core-performance-regression.ps1
.\tools\check-release-readiness.ps1
```

Required evidence:

- Exact commands and cwd.
- Exit codes.
- Output excerpts.
- Timestamp.
- Whether output was truncated.

## Rollback And Recovery Plan

Broken save states:

- Reject unknown magic/version/corrupt payloads.
- Keep prior save-state blobs untouched when decode fails.
- Do not auto-migrate unknown versions.
- Add a migration table only when a future version is introduced.

Broken save files:

- Preserve original imported save bytes before writing a modified save.
- Prefer explicit user export/import over automatic filesystem writes until Android SAF
  persistence is implemented.
- If Flash/EEPROM behavior changes, document migration impact and provide a byte-for-byte
  backup path.

Broken beta build:

- Disable beta distribution.
- Revert to the last passing local evidence bundle.
- Retain crash/log artifacts for diagnosis.
- Do not ask users for copyrighted ROMs, BIOS files, screenshots, or save files unless a
  legal/private support process exists.

## Device Evidence Guidance

### P0 synthetic PASS (recorded 2026-06-03)

1. Build and install per [`android-device-soak-checklist.md`](android-device-soak-checklist.md).
2. On a physical device **or** API 26+ emulator (`arm64-v8a` preferred), run **both** self-tests:
   **Run bridge self-test** and **Run runtime self-test** → UI shows PASS; runtime shows 240×160 preview.
3. Cold start, force-stop + relaunch, and Home → resume (minimal lifecycle) per checklist.
4. Copy device model, Android version, and ABI from `adb shell getprop` into the dated artifact under
   `docs/evidence/` (template: [`evidence/2026-06-03-android-device-soak-synthetic-pass.md`](evidence/2026-06-03-android-device-soak-synthetic-pass.md)).
5. Do **not** commit ROMs, BIOS, saves, or logcat blobs containing user paths to copyrighted files.

If no hardware is available during a doc-only pass, the artifact still lists **adb replay commands**;
the integrator or release owner must execute them and fill the Device row before claiming hardware-specific PASS.

### Still required before external beta

- **Audio:** Oboe/AAudio **clean** output — 2026-08-14 soak measured an underrun storm (2,787 @ frame 60; 710,778 after restart): audible-but-choppy is a FAIL.
- **Lifecycle:** Process-wide pause on background (`onStop` / `ProcessLifecycleOwner`) without native leak; force-stop + relaunch is clean, but lock/unlock during a stalled loop does not recover.
- **Pacing / thermal:** Frame pacing at the 16.7 ms target — measured 1.4–2 fps with full stalls; 10+ minute thermal notes still missing.
- **Saves:** Cartridge save and save-state round-trip on device (workstation codec already gated).

Integration map: [`android-integration-plan.md`](android-integration-plan.md). Issue rollup:
[`open-issues-status.md`](open-issues-status.md).

## Known Issues

- Android dev shell at `Project_Android/GbaEmulatorAndroid`: JNI runtime, SAF ROM picker with header
  validation, game loop, touch overlay, Oboe playback, save SAF, lifecycle pause flush, debug ROM smoke
  path, and **P0 synthetic** soak artifact exist. 2026-08-14 device soak (release build, wireless adb):
  the game boots and plays to the title screen and Start-advance on hardware with byte-identical
  framebuffer CRCs to the workstation — but audio is choppy (underrun storm) and the presentation loop
  runs ~1.4–2 fps and degrades to a full stall. GLES, thermal artifact, and clean pacing do **not**
  exist.
- Public mGBA suite regression baseline is green on workstation (`tools/mgba-suite-green-baseline.json`);
  seven video **oracle** aliases are green; the upstream interactive `video` suite is not.
- No BIOS image is bundled or executed; BIOS/HLE behavior is intentionally bounded.
- No commercial ROM or retail-game compatibility claim exists.
- PPU/APU/DMA/save-state behavior remains seed-level and not hardware-complete.
- Save-state codec serializes a public restorable subset, not every modeled internal
  device register.
- Local performance metrics are workstation synthetic metrics, not device thermal or
  battery evidence.

## High Risks

| Risk | Owner | Mitigation | Status |
| --- | --- | --- | --- |
| Accidental copyrighted ROM/BIOS fixture admission | Jonathan/release owner | Fixture registry and release readiness check. | MITIGATED LOCALLY |
| Unsupported compatibility or superiority claims | Jonathan/release owner | README/legal wording gate and release governance review. | MITIGATED LOCALLY |
| Save-state incompatibility after future format changes | Core owner | Versioned codec, reject unknown versions, require migration table. | PARTIAL |
| Android lifecycle/resource leak | Android owner | GameScreen pause + restart; 2026-08-14: force-stop + relaunch clean on release; lock/unlock during a stalled loop does not recover it. | PARTIAL |
| Device thermal/audio/frame pacing unknown | Android owner | 2026-08-14 soak: audio FAIL (underrun storm), pacing FAIL (1.4–2 fps → stall); thermal unmeasured. | BLOCKED |

## Beta Readiness Result

Phase 72 is complete as a bounded local readiness gate: critical blockers are documented,
local regression gates exist, rollback/recovery is defined, and known issues are explicit.

Controlled external beta remains blocked: the 2026-08-14 device soak delivered the missing
audio/pacing evidence and it is a **FAIL** (underrun storm + 1.4–2 fps presentation stall on a
release build), and the workstation credibility matrix is RED (`io-read`, `misc-edge`
regressions; `video` target missing). Saves on device and 10+ min thermal remain unmeasured.
