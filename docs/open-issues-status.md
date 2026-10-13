# Open Issues Status

Status: 2026-10-11 reconciliation — `misc-edge` re-measured 4/12, `timers` GREEN 936/936, `timing` 1956/2020, post-#6 PR #8 merge confirmed; dimension statuses separated  
Date: 2026-06-03 (updated 2026-10-11; prior updates 2026-09-29, 2026-08-15, 2026-09-12)

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
> `misc-edge` remains the only genuinely measured regression. The last committed
> figure is **6/12 (2026-08-14)**; it is **re-measured at 4/12 on 2026-10-11**
> (`main` `6eb935a`) with a locally built public suite ROM (source `aac98dca`, ROM
> sha256 `9693913e8b9a41050122cf768c8157eea092d192a435c12d274173a810a38b1b`). PR #6
> ("misc-edge HBlank flag start-cycle", merged 2026-09-12 as `9755c99`) moved
> `kHblankFlagCycles` 1006→1004, against its own recorded warning that flipping the
> constant without reworking the DISPSTAT sampler compensation rules regresses other
> measurements, and no post-merge rerun was recorded. #6 is therefore a plausible
> cause of the gap versus the 6/12 record, but the extra loss is not proven here. The
> 2026-10-11 run also fails `DMA Prefetch` (`Break` two words long, `Read` reads 0
> instead of `0xDEAD0000`).
>
> **Post-#6 timer/timing state RESOLVED by measurement (2026-10-11).** The "open PR #8"
> note below is superseded: PR #8 (`fix/timer-tick-regression`) was **merged** to `main`
> as `51890c4` ("recover 9fa5067 regressions (timers/timer-irq/sio-timing + Thumb MUL
> timing)"). Re-running on `main` `6eb935a`: `timers` is **GREEN 936/936**; `timing` is
> **1956/2020** (64 failures, all `Trivial DMA` / `Short DMA` timing rows, pre-existing on
> `main`). `timer-irq` and `sio-timing` were **not** re-run on 2026-10-11 and have no
> current measurement.

| Target | Pass/total (2026-06-01 baseline) | Last measured |
| --- | --- | --- |
| `memory` | 1552/1552 | GREEN |
| `io-read` | 130/130 | **GREEN 130/130** (re-verified 2026-09-12; 2026-08-14 "RED 51/130" disproven) |
| `bios-math` | 615/615 | GREEN |
| `dma` | 1256/1256 | GREEN |
| `shifter` | 140/140 | GREEN |
| `carry` | 93/93 | GREEN |
| `multiply-long` | 72/72 | GREEN |
| `timer-irq` | 90/90 | Not re-run 2026-10-11 (no current measurement); PR #8 merged as `51890c4`; last measured GREEN |
| `timers` | 936/936 | **GREEN 936/936** (re-measured 2026-10-11, `main` `6eb935a`) |
| `timing` | 2020/2020 | **1956/2020** (re-measured 2026-10-11, `main` `6eb935a`; 64 `Trivial/Short DMA` failures, pre-existing) |
| `sio-read` | 90/90 | GREEN |
| `sio-timing` | 4/4 (four SKIPPED no-peer multiplayer cases) | Not re-run 2026-10-11 (no current measurement); PR #8 merged as `51890c4`; last measured GREEN |
| `misc-edge` | 12/12 | **RED 4/12** (re-measured 2026-10-11, `main` `6eb935a`; `H-blank bit start` + `DMA Prefetch`); last committed figure was 6/12 on 2026-08-14 |

Harness aliases (not in the regression baseline file, but green in the credibility matrix):
`loadstore`, `ldmia`, `stmia`.

The 2026-10-11 re-measurement commands, ROM provenance, and full per-subtest results
are recorded in [`docs/evidence/2026-10-11-conformance-remeasure.md`](evidence/2026-10-11-conformance-remeasure.md).

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
  `GbaEmulatorAndroid/PROJECT_CONTEXT.md` (sibling checkout `../GbaEmulatorAndroid`; not linked because the path resolves only on a side-by-side local clone).

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
| Branch-to-self (`B #-8`, `0xEAFFFFFE`) | **CLOSED — fixed in PR #20** | A taken branch whose target equals its own address now remains at that address on both ARM and Thumb paths. `tests/branch_to_self_test.cpp` covers ARM `0xEAFFFFFE` and Thumb `0xE7FE`; the verifier passes on the current upstream tree. |
| Save-type detection with multiple markers | **OPEN — pre-existing** | `MemoryBus::detect_game_pak_save_type()` returns `nullopt` when a ROM contains more than one of `SRAM_V`/`FLASH*_V`/`EEPROM_V` (`memory_bus.cpp`), so the desktop host treats such a title as having no cartridge save and neither loads nor writes its `.sav`. |
| Upstream `video` suite | Absent from matrix **by design** (reconciled 2026-09-12) | the default `-Suites` list in `tools/run-credibility-matrix.ps1` omits it (interactive video emits no `END pass=total`); the 7 oracle aliases are verified separately via `tools/run-video-suite-all.ps1` (last run 2026-06-05; no newer rerun on record) |
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
