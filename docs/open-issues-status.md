# Open Issues Status

Status: Jun 2026 documentation rollup  
Date: 2026-06-03

This document summarizes what is **closed** (verified with reproducible evidence) versus
what **remains** for production and controlled beta. It does not replace per-suite logs in
`docs/mgba-suite-test-results.md` or the live matrix in
`build/test-results/credibility-matrix-latest.json`.

## Claims Policy

| Claim | Allowed? |
| --- | --- |
| Legal mGBA Game Boy Advance Test Suite (locally built ROM) | Yes, with artifacts |
| Thirteen upstream suite targets at `pass=total` | Yes, when matrix is GREEN |
| Seven deterministic video oracle probes | Yes, as bounded framebuffer evidence |
| Retail/commercial GBA game compatibility | **No** |
| BIOS bundled or full hardware fidelity | **No** |

## Closed (Jun 2026)

### Workstation core gates

- `tools/run-core-tests.ps1` — all local C++ verifiers pass.
- `tools/run-core-benchmarks.ps1` and `tools/check-core-performance-regression.ps1` — synthetic performance gate passes (Jun 2026 calibrated ceilings).
- `tools/check-release-readiness.ps1` — release governance wording gate passes.

### mGBA public suite regression baseline

Thirteen upstream targets are **GREEN** in `tools/mgba-suite-green-baseline.json`
(`END` with `pass=total`, zero parsed failures, zero unsupported instructions, zero fetch
failures). Latest matrix: `build/test-results/credibility-matrix-latest.json` (2026-06-01).

| Target | Pass/total (latest) |
| --- | --- |
| `memory` | 1552/1552 |
| `io-read` | 130/130 |
| `bios-math` | 615/615 |
| `dma` | 1256/1256 |
| `shifter` | 140/140 |
| `carry` | 93/93 |
| `multiply-long` | 72/72 |
| `timer-irq` | 90/90 |
| `timers` | 936/936 |
| `timing` | 2020/2020 |
| `sio-read` | 90/90 |
| `sio-timing` | 4/4 (four SKIPPED no-peer multiplayer cases) |
| `misc-edge` | 12/12 |

Harness aliases (not in the regression baseline file, but green in the credibility matrix):
`loadstore`, `ldmia`, `stmia`.

### Video oracle evidence

Seven **video_oracle_alias** rows reach `video_probe` with `video_oracle_comparison: match`
(actual vs expected frame hash). These do **not** mark the upstream interactive `video` suite
green:

| Alias | Probe |
| --- | --- |
| `video-basic-mode-3` | `basic-mode-3-actual` |
| `video-basic-mode-4` | `basic-mode-4-actual` |
| `video-degenerate-obj` | `degenerate-obj-actual` |
| `video-layer-toggle` | `layer-toggle-actual` |
| `video-layer-toggle-2` | `layer-toggle-2-actual` |
| `video-oam-update-delay` | `oam-update-delay-actual` |
| `video-window-offscreen-reset` | `window-offscreen-reset-actual` |

### Android dev shell (sibling `GbaEmulatorAndroid`)

- C bridge API and workstation tests (`android_core_bridge_test`, `android_runtime_test`).
- Compose shell, CMake `libgbaemulator.so`, package `com.gba.emulator.shell`.
- JNI: `GbaCoreBridge` (bridge) + `GbaRuntimeBridge` (`step_frame`, RGB565 framebuffer,
  `stop_reason`, audio batch counts).
- UI: bridge + runtime self-tests, SAF **Open ROM** (`ACTION_OPEN_DOCUMENT`), `EmulatorSession`,
  `GameScreen` frame loop, `TouchGameControls` overlay.
- Map: `docs/android-integration-plan.md`, `docs/android-rom-play-roadmap.md`,
  [`GbaEmulatorAndroid/PROJECT_CONTEXT.md`](../../GbaEmulatorAndroid/PROJECT_CONTEXT.md).

### Device evidence (P0 synthetic)

- Dated artifact: [`docs/evidence/2026-06-03-android-device-soak-synthetic-pass.md`](evidence/2026-06-03-android-device-soak-synthetic-pass.md)
  — bridge + runtime self-test PASS criteria and adb replay steps (no ROMs in git).
- **Controlled external beta** remains blocked for audio output, save-state UX on device, and
  long thermal soak until follow-up artifacts exist.

### Governance

- Fixture licensing registry and unsupported-claim scanning (local).
- Controlled-beta rollback/recovery plan documented in `docs/controlled-beta-readiness.md`.

## Remaining

### Controlled beta blockers

| Blocker | Owner hint | Doc |
| --- | --- | --- |
| P0 synthetic device soak | Android | **PASS** — [`evidence/2026-06-03-android-device-soak-synthetic-pass.md`](evidence/2026-06-03-android-device-soak-synthetic-pass.md) |
| Audible Oboe/AAudio on device | Android | **TEMPLATE** — [`evidence/2026-06-05-android-device-soak-av-template.md`](evidence/2026-06-05-android-device-soak-av-template.md); hardware replay required |
| Process lifecycle pause + save flush on stop | Android | BLOCKED — partial (Home/resume manual only) |
| Save import/export + save-state on device | Android | BLOCKED — workstation codec only |
| Frame-cycle pacing + 10+ min thermal soak | Android / core | BLOCKED — instruction-bounded `step_frame`; no thermal artifact |

**Controlled external beta stays BLOCKED** until audio, durable saves, and measured pacing/thermal
artifacts exist — not merely synthetic self-test PASS.

### Accuracy / engine

| Area | Status | Notes |
| --- | --- | --- |
| Upstream `video` suite | **GREEN (7/7 automation)** | `run-video-suite-all.ps1` + credibility matrix `video` row; upstream interactive suite still has no END pass/total |
| Additional video tests | Open | Beyond the seven oracle aliases |
| CPU pipeline / Thumb coverage | Partial | See `docs/production-engine-roadmap.md` |
| PPU/APU hardware completeness | Seed-level | Renderer/mixer seeds, not full hardware |
| Save-state on disk / migration UX | Bounded codec only | No production persistence promise |
| BIOS | No bundle | HLE enabled on Android/runtime `load_rom` (`configure_for_game_boot`); RegisterRamReset SWI 0x01 HLE; no retail BIOS file execution |

### Android product path

| Item | Status |
| --- | --- |
| JNI + `AndroidRuntime` frame loop / Compose RGB565 upload | **Implemented** (`GbaRuntimeBridge`, `GameScreen`) |
| SAF ROM import (`OpenDocument`) | **Implemented** (`GbaEmulatorScreen`); save export/import **not** implemented |
| Touch input overlay | **Implemented** (`TouchGameControls`) |
| `stop_reason` / audio batch counts via JNI | **Implemented** (counts only; no speaker output) |
| Oboe or AAudio playback | Not implemented |
| GLES presentation | Not implemented (Compose `Image` path) |
| Save-state + cartridge save SAF UX | Not implemented |
| Process-wide pause / thermal soak | Not recorded |

### Explicit non-goals (until evidence exists)

- Commercial ROM loading claims.
- App-store readiness or public superiority claims.
- Device thermal/audio/frame pacing without measured artifacts.

## Verification Commands

```powershell
# Full matrix (suites + video oracles + performance)
.\tools\run-credibility-matrix.ps1

# Regression-only (13 baseline suites + performance)
.\tools\run-credibility-matrix.ps1 -FailOnRegression

# Core verifiers
.\tools\run-core-tests.ps1
```

## Related Documents

- `docs/controlled-beta-readiness.md` — beta gate checklist
- `docs/mgba-suite-test-results.md` — historical suite run log
- `docs/android-integration-plan.md` — Android module map
- `docs/production-engine-roadmap.md` — engine phase plan
- `docs/benchmark-comparison-roadmap.md` — credibility matrix goals
