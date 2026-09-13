# Open Issues Status

Status: 2026-08-15 update — device soak audio/pacing FAILs + credibility matrix regression recorded  
Date: 2026-06-03 (updated 2026-08-15)

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

**2026-08-14 REGRESSION — `misc-edge` only.** The 2026-06-01 matrix (below) was GREEN, but
the 2026-08-14 run (`build/test-results/credibility-matrix-20260814-210843-10140.json`,
first valid pwsh-7 run since June) reported `io-read` and `misc-edge` as RED and the
`video` target as **missing**.

> **Doc corrections (2026-09-12):**
> - `io-read` is **GREEN 130/130**. Re-running the pinned suite (`aac98dca`) gives
>   130/130 on both `main` and the HBlank branch, and a direct CPU probe confirms the
>   values (write-only/INVALID registers → open bus `0xDEAD`; BG0CNT→`0xDFFF`,
>   WININ/OUT→`0x3F3F`, BLDCNT→`0x3FFF`, BLDALPHA→`0x1F1F`). The previously recorded
>   root cause was self-contradictory (the pipeline-open-bus fallback *is* what yields
>   `0xDEAD`). The contract is now locked natively by
>   `tests/io_read_open_bus_test.cpp` (130/130). The 2026-08-14 `io-read` figure is
>   treated as a stale/mis-built artifact.
> - The `video` target is **absent from the matrix by design**
>   (the default `-Suites` list in `tools/run-credibility-matrix.ps1:5-7` omits it;
>   note `tools/mgba-suite-green-baseline.json` lists `"video"` in `green_suites` even
>   though its notes treat it as outside the baseline); the 7 oracle probes
>   are verified separately via `tools/run-video-suite-all.ps1` and are GREEN.
>
> `misc-edge` remains the only genuine regression. Needs triage before any beta candidate.

| Target | Pass/total (2026-06-01 baseline) | Last measured |
| --- | --- | --- |
| `memory` | 1552/1552 | GREEN |
| `io-read` | 130/130 | **GREEN 130/130** (re-verified 2026-09-12; 2026-08-14 "RED 51/130" disproven) |
| `bios-math` | 615/615 | GREEN |
| `dma` | 1256/1256 | GREEN |
| `shifter` | 140/140 | GREEN |
| `carry` | 93/93 | GREEN |
| `multiply-long` | 72/72 | GREEN |
| `timer-irq` | 90/90 | GREEN |
| `timers` | 936/936 | GREEN |
| `timing` | 2020/2020 | GREEN |
| `sio-read` | 90/90 | GREEN |
| `sio-timing` | 4/4 (four SKIPPED no-peer multiplayer cases) | GREEN |
| `misc-edge` | 12/12 | **RED 6/12** (H-blank bit start Hblank) |

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
| Audible Oboe/AAudio on device | Android | **FAIL** — [`evidence/2026-08-14-android-device-soak-p1-emerald-wireless.md`](evidence/2026-08-14-android-device-soak-p1-emerald-wireless.md): audible but choppy; 2,787 underruns @ frame 60, 710,778 after restart |
| Process lifecycle pause + save flush on stop | Android | PARTIAL — force-stop + relaunch clean (2026-08-14); lock/unlock does not recover a stalled presentation loop |
| Save import/export + save-state on device | Android | BLOCKED — workstation codec only; in-game save not reached on device (1.4 fps play) |
| Frame-cycle pacing + 10+ min thermal soak | Android / core | **FAIL** — 1.4–2 fps → full stall (0 presents), `coreUnderruns=0`; thermal unmeasured |

**Controlled external beta stays BLOCKED** until audio, durable saves, and measured pacing/thermal
artifacts exist — not merely synthetic self-test PASS.

### Accuracy / engine

| Area | Status | Notes |
| --- | --- | --- |
| Upstream `video` suite | **MISSING (2026-08-14)** | baseline expects the `video` alias group but the current matrix produces no `video` row — re-verify `run-video-suite-all.ps1` and the matrix config; seven oracle aliases were green as of 2026-06-01 |
| Additional video tests | Open | Beyond the seven oracle aliases |
| CPU pipeline / Thumb coverage | Partial | See `docs/production-engine-roadmap.md` |
| PPU/APU hardware completeness | Seed-level | Renderer/mixer seeds, not full hardware |
| Save-state on disk / migration UX | Bounded codec only | No production persistence promise |
| BIOS | No bundle | HLE enabled on Android/runtime `load_rom` (`configure_for_game_boot`); RegisterRamReset SWI 0x01 HLE; no retail BIOS file execution |

### Android product path

| Item | Status |
| --- | --- |
| JNI + `AndroidRuntime` frame loop / `SurfaceView` RGB565 present | **Implemented** (`GbaRuntimeBridge`, `GameScreen`, `GameViewportSurfaceController`) |
| SAF ROM import (`OpenDocument`) | **Implemented** (`GbaEmulatorScreen`); cartridge save + save-state export/import via SAF — see rows below |
| Touch input overlay | **Implemented** (`TouchGameControls`) |
| `stop_reason` / audio batch counts via JNI | **Implemented** (counts + Oboe playback path) |
| Oboe playback (`ApuAudioEngine`) | **Implemented** — hardware output verified 2026-08-14; quality currently **FAIL** (underrun storm: 2,787 @ frame 60, 710,778 after restart) |
| Presentation | **Implemented** (`SurfaceView` + `Canvas.drawBitmap`); GLES not implemented |
| Cartridge save + save-state SAF UX | **Implemented** (`SaveRepository` + `CreateDocument`/`OpenDocument` flows); on-device round-trip verification still required |
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
