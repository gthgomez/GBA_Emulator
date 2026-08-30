# GBA_Emulator Status

**Last verified:** 2026-08-22
**Status:** active development
**Confidence:** high (core verifiers); medium (accuracy — 2 mGBA suites regressed, see below)

## Purpose

Portable Game Boy Advance (GBA) emulator core written in C++17 with mGBA test-suite verification, deterministic timing/scheduler, and an opaque C API for Android JNI integration.

## Current State

- **Core verifiers: 28/28 PASS.** Re-verified 2026-08-22 via `tools/run-core-tests.ps1` (clean `g++ -Werror` rebuild, all binaries run and pass). No compile warnings, no runtime failures in the headless suite.
- **mGBA public suites: 11/13 GREEN, 2 REGRESSED.** Of the 13 upstream suites, `io-read` (51/130) and `misc-edge` (6/12) regressed as of **2026-08-14** (see `docs/open-issues-status.md`). The other 11 (`memory`, `bios-math`, `dma`, `shifter`, `carry`, `multiply-long`, `timer-irq`, `timers`, `timing`, `sio-read`, `sio-timing`) remain GREEN.
- **Video oracle aliases: 7/7 GREEN** via the separate `tools/run-video-suite-all.ps1` runner (the upstream interactive `video` suite is intentionally excluded from the credibility matrix baseline).
- **Controlled external beta remains blocked** without target-device soak evidence.

**Engine audit (2026-08-22):** The `io-read` regression is traced to IO open-bus read modeling. `IoRegisters::read16` returns `std::nullopt` for write-only/INVALID registers (correct per gbatek), but `MemoryBus::read16` → `read16_or_thumb_pipeline_open_bus` substitutes the pipeline instruction word on `nullopt` instead of the expected open-bus value (`0xDEAD`), so 79 of 130 registers mismatch. The readable-register masks (BGxCNT, WININ/OUT, BLDCNT, BLDALPHA, sound regs) are confirmed correct against `io-read.c`. The `misc-edge` "H-blank bit start" sub-test is timing-sensitive and plausibly related to commit `81beba2` ("unify HBlank event+flag at hardware-calibrated cycle 1004").

## Verified Capabilities

- C++17 ARM7TDMI CPU core (ARM/Thumb instruction sets, WAITCNT timing, cycle accounting).
- PPU scanline timing, background/sprite OAM rendering, APU Direct Sound audio mixing.
- Headless in-memory test runner (`tools/run-core-tests.ps1`) and credibility matrix (`tools/run-credibility-matrix.ps1`).
- Opaque C API (`android_core_bridge`) for JNI integration without global state leaks.

## Recent Evidence

- `README.md` documents mGBA public test suite baseline (`tools/mgba-suite-green-baseline.json`).
- Open issues and test coverage documented in `docs/open-issues-status.md`.

## In Progress

- Refinement of Thumb instruction coverage and PPU mode rendering edge cases.
- Controlled beta readiness preparation (`docs/controlled-beta-readiness.md`).

## Blockers

- Controlled beta distribution blocked until target-device soak testing is completed per `docs/controlled-beta-readiness.md`.

## Risks and Unknowns

- **IO open-bus read modeling (open defect, root cause of `io-read` regression):** unhandled/ `std::nullopt` IO reads fall back to the pipeline instruction word rather than a latched open-bus value, so write-only/INVALID registers return the wrong value under the mGBA `io-read` harness. Fix requires a per-region open-bus latch.
- **HBlank flag ↔ Timer0 phase (`misc-edge` regression):** the "H-blank bit start" sub-test is sensitive to exact HBlank-flag assertion timing relative to the CPU timer; review commit `81beba2`.
- Timing edge cases on unaligned memory access across specific Game Pak prefetch wait-states.

## Verification

- Command: `.\tools\run-core-tests.ps1` and `.\tools\run-credibility-matrix.ps1` documented in `README.md`.

## Next Actions

1. Run local core tests (`tools/run-core-tests.ps1`).
2. Run credibility matrix harness (`tools/run-credibility-matrix.ps1`).
3. Complete target-device soak checklist (`docs/android-device-soak-checklist.md`).

## Evidence Sources

- [README.md](file:///C:/Workspace/Project_Android/GBA_Emulator/README.md)
- [QA_CHECKLIST.md](file:///C:/Workspace/Project_Android/GBA_Emulator/QA_CHECKLIST.md)
- [docs/open-issues-status.md](file:///C:/Workspace/Project_Android/GBA_Emulator/docs/open-issues-status.md)
