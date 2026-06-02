# Controlled Beta Readiness

Status: Phase 72 bounded local beta gate (device soak still blocked)
Date: 2026-06-01

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
| Performance | Local synthetic benchmark and regression gates pass. | `run-core-benchmarks.ps1`, `check-core-performance-regression.ps1`. | PASS |
| Device Evidence | At least one target Android device run with lifecycle, frame pacing, audio, and thermal notes. | **Scaffold only:** [`../GbaEmulatorAndroid`](../GbaEmulatorAndroid) debug APK + bridge self-test UI; no recorded device run. Follow [`android-device-soak-checklist.md`](android-device-soak-checklist.md). | BLOCKED |
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

## Device Evidence Scaffold

Before flipping the Device Evidence row to PASS:

1. Build and install per [`android-device-soak-checklist.md`](android-device-soak-checklist.md).
2. Record model, ABI, timestamp, and self-test `PASS` artifact (no ROMs in git).
3. Reference the dated artifact in the Device Evidence row above.

Integration map: [`android-integration-plan.md`](android-integration-plan.md). Issue rollup:
[`open-issues-status.md`](open-issues-status.md).

## Known Issues

- Android dev shell exists at `Project_Android/GbaEmulatorAndroid` (Gradle, CMake, JNI bridge
  self-test only). No SAF import, OpenGL ES, Oboe, lifecycle instrumentation, or **recorded**
  device soak evidence yet.
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
| Android lifecycle/resource leak | Android owner | Future device/instrumentation tests required. | BLOCKED |
| Device thermal/audio/frame pacing unknown | Android owner | Future sustained target-device run required. | BLOCKED |

## Beta Readiness Result

Phase 72 is complete as a bounded local readiness gate: critical blockers are documented,
local regression gates exist, rollback/recovery is defined, and known issues are explicit.

Controlled external beta remains blocked until there is evidence on at least one target
Android device.
