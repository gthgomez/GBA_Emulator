# GBA_Emulator Status

**Last verified:** 2026-08-01
**Status:** active development
**Confidence:** high

## Purpose

Portable Game Boy Advance (GBA) emulator core written in C++17 with mGBA test-suite verification, deterministic timing/scheduler, and an opaque C API for Android JNI integration.

## Current State

The core is in active development with 13 mGBA public test suites passing (`memory`, `io-read`, `bios-math`, `dma`, `shifter`, `carry`, `multiply-long`, `timer-irq`, `timers`, `timing`, `sio-read`, `sio-timing`, `misc-edge`) and 7 video oracle aliases passing against deterministic frame hashes. Controlled external beta remains blocked without target-device soak evidence.

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
