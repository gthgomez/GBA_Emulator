# GBA_Emulator Status

**Last verified:** 2026-11-19 (Linux C++ verifiers and public fixture rerun)
**Status:** active development

[Current conformance results and reproduction](docs/open-issues-status.md#2026-11-19-rerun).

## Purpose

Portable Game Boy Advance (GBA) emulator core written in C++17 with mGBA test-suite verification, deterministic timing/scheduler, and an opaque C API for Android JNI integration.

## Current State

- **Desktop host shipped (PRs #12/#13), hardened for the MVP:** `gba-desktop` play
  mode (SDL3 window/input/audio, cartridge saves, save states, pause/reset) and a
  deterministic headless lab (JSON artifacts, scripted input, hashes,
  screenshots) on the same `EmulatorRuntime`. The MVP pass added save-data safety
  (atomic writes, ROM-switch flush + rollback, reset preserves cartridge save,
  rejected saves preserved, no spurious empty `.sav`), independent keyboard/pad
  input aggregation, stale-audio clearing, wrong-ROM save-state rejection, a
  no-argument launch file dialog, and a portable Windows ZIP workflow. CI now
  builds the host and runs headless-determinism, save-lifecycle, and
  interactive-host smokes on Linux + Windows. See `docs/DESKTOP.md`.
- **Real-game video regression (issue #15): FIXED.** Pokemon Emerald rendered
  uniform black from ~frame 30 since core commit `6785fcc`; bisected in PR #14 and
  fixed in PR #17 (PPU windowing bypass when no window is enabled), merged on
  `main` at `ed73298`. This is historical, not current behavior.
- **Branch to self is fixed by PR #20.** The verifier checks ARM and Thumb self-branches
  and an untaken conditional ARM branch.
- **Core verifiers: 36/36 PASS.** The Linux C++ build and both script inventories include
  the desktop save guards, branch-to-self, and the current HLE and timing regressions.
- **Public conformance: 11/13 numeric suites pass.** The current source build of the pinned
  fixture reproduced `io-read` 51/130 on baseline `e92389fbe8c1` and reached 130/130 with
  the Thumb open-bus fix. `misc-edge` is 4/12 and `timing` is 1956/2020. The seven
  named video oracles pass separately. [Reproduction and remaining failures](docs/open-issues-status.md#2026-11-19-rerun).
- **Physical device soak: still failing/blocked.** The 2026-08-14 P1 soak evidence records audible-but-choppy audio (2,787 underruns @ frame 60; 710,778 after restart) and 1.4–2 fps pacing with a full present stall; thermal soak unmeasured. These FAIL records are preserved in `docs/evidence/` and are not superseded.
- **Commercial-ROM boot smoke: local only (2026-11-18).** Stock Emerald (`BPEE`) boots through title and intro on the rebuilt desktop/headless host. The public test matrix still uses only the legal mGBA suite ROM; no general retail compatibility is claimed. See `docs/evidence/2026-11-18-emerald-boot.md`.
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

- **Branch-to-self (`B #-8`) runs away** instead of looping (found 2026-10-08). A
  real ARM instruction games use for idle loops; the interactive fixture avoids
  it, but it should be fixed in the core CPU/scheduler path.
- **HBlank flag ↔ Timer0 phase (`misc-edge`, RED 6/12):** the "H-blank bit start" sub-test is sensitive to exact HBlank-flag assertion timing relative to the CPU timer; review commit `81beba2`.
- **`timing` suite 1956/2020:** 64 failures remain and are pre-existing on `main`; triage is required before the suite can count toward `pass=total`.
- Timing edge cases on unaligned memory access across specific Game Pak prefetch wait-states.
- **Desktop physical playtest (manual, 2026-10-08): performed from the portable
  ZIP on a real Windows 11 display/audio session** — windowed rendering ~60 fps
  from a clean-directory extraction, save/restart, reset preservation, ROM
  switching (including refusal when the save flush fails), wrong-ROM state
  rejection, and a 20-minute 72,000-frame sustained run with zero audio
  underruns. CI still exercises the host under dummy drivers only. Physical
  gamepad was not available (none connected); audible audio and hands-on
  keyboard play remain user-verified items.

## Verification

- Core verifiers: `.\tools\run-core-tests.ps1` / `./tools/run-core-tests.sh` (32/32).
- Desktop host: `./tools/run-desktop-smoke.sh`, `./tools/run-desktop-save-smoke.sh`,
  `./tools/run-desktop-interactive-smoke.sh` (PowerShell equivalents on Windows).
- Portable package: `.\tools\package-windows-portable.ps1` -> `dist\gba-desktop-*-windows-x64.zip`
  (verified by clean-directory extraction and launch on 2026-10-08; CI also
  uploads it as an artifact).
- Credibility matrix: `.\tools\run-credibility-matrix.ps1`.

## Next Actions

1. Fix the branch-to-self (`B #-8`) core defect and add a regression test
   (reproduced on 2026-10-08 on both ARM and Thumb paths; fix prepared as a
   separate core PR).
2. Merge PR #18 (desktop MVP hardening) after CI green.
3. Complete target-device soak checklist (`docs/android-device-soak-checklist.md`).
4. Run credibility matrix harness (`.\tools\run-credibility-matrix.ps1`).

## Evidence Sources

- [README.md](README.md)
- [QA_CHECKLIST.md](QA_CHECKLIST.md)
- [docs/open-issues-status.md](docs/open-issues-status.md)
- Device soak evidence: [docs/evidence/](docs/evidence/) (failed 2026-08-14 P1 records preserved)
