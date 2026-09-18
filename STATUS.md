# GBA_Emulator Status

**Last verified:** 2026-09-18
**Status:** active development
**Confidence:** high (core verifiers); medium (accuracy — `misc-edge` RED and `timing` short of `pass=total`, see below)

## Purpose

Portable Game Boy Advance (GBA) emulator core written in C++17 with mGBA test-suite verification, deterministic timing/scheduler, and an opaque C API for Android JNI integration.

## Current State

- **Core verifiers: 29/29 PASS.** Re-verified 2026-09-18 via `tools/run-core-tests.ps1` (clean `g++ -Werror` rebuild, all binaries run and pass). The 29th verifier (`intr_wait_serial_horizon_test`) guards the IntrWait serial completion edge added with this PR. No compile warnings, no runtime failures in the headless suite.
- **mGBA public suites: 11/13 GREEN, 2 below `pass=total`.** `io-read` is **GREEN 130/130** (re-verified 2026-09-12; the recorded `51/130` RED was disproven by PR #7). `misc-edge` (6/12) is RED and `timing` is 1956/2020 (64 failures pre-existing on `main`), so neither meets the `pass=total` bar. The other 11 (`memory`, `bios-math`, `dma`, `shifter`, `carry`, `multiply-long`, `timer-irq`, `timers`, `sio-read`, `sio-timing`, plus `io-read`) are GREEN.
- **Video oracle aliases: 7/7 GREEN** via the separate `tools/run-video-suite-all.ps1` runner (the upstream interactive `video` suite is intentionally excluded from the credibility matrix baseline).
- **Controlled external beta remains blocked** without target-device soak evidence.

**Engine audit (2026-09-18):** The recorded `io-read` regression was disproven. Re-running the pinned suite (`aac98dca`) gives **130/130** on both `main` and the HBlank branch, and a direct CPU probe confirms the expected open-bus values (write-only/INVALID registers → open bus `0xDEAD`; BG0CNT→`0xDFFF`, WININ/OUT→`0x3F3F`, BLDCNT→`0x3FFF`, BLDALPHA→`0x1F1F`); the pipeline-open-bus fallback is in fact what produces `0xDEAD`. The contract is now locked natively by `tests/io_read_open_bus_test.cpp`. The `misc-edge` "H-blank bit start" sub-test remains timing-sensitive and plausibly related to commit `81beba2` ("unify HBlank event+flag at hardware-calibrated cycle 1004").

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

- **HBlank flag ↔ Timer0 phase (`misc-edge`, RED 6/12):** the "H-blank bit start" sub-test is sensitive to exact HBlank-flag assertion timing relative to the CPU timer; review commit `81beba2`.
- **`timing` suite 1956/2020:** 64 failures remain and are pre-existing on `main`; triage is required before the suite can count toward `pass=total`.
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
