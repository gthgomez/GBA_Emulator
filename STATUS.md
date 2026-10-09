# GBA_Emulator Status

**Last verified:** 2026-10-08 (core verifiers 32/32 PASS on native Windows via `run-core-tests.ps1`; both runners register the same 32 verifiers, Linux parity verified by CI — desktop MVP data-safety fixes verified locally on native Windows, see PR #18)
**Status:** active development
**Confidence:** high (core verifiers + desktop automation + manual Windows qualification); medium (accuracy — `misc-edge` RED and `timing` short of `pass=total`, see below)

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
- **Newly found engine defect (branch-to-self):** a bare `B #-8` (branch to its
  own address) runs away instead of looping, while a branch-to-previous-
  instruction loop works. Found while building the interactive video fixture;
  recorded in `docs/open-issues-status.md`. Not a desktop-MVP blocker (the
  fixtures and ordinary game idle loops use the working idiom), but a real
  correctness issue to fix in the core.

- **Core verifiers: 32/32 PASS.** Re-verified 2026-10-08 on native Windows via
  `tools/run-core-tests.ps1` (clean `g++ -Werror` rebuild, all 32 binaries run
  and pass). The Windows-only stack overflow in `desktop_save_state_guard_test`
  (0xC00000FD: six ~405 KB `CoreSession` objects plus codec snapshots against
  the executable's 2 MiB stack reservation) was fixed by heap-allocating the
  snapshot objects in `save_state_codec.cpp` and the guard test. The Linux
  runner now registers `intr_wait_serial_horizon_test` too, so both runners
  carry the same 32 verifiers (previously 31 on Linux).
- **mGBA public suites: 11/13 GREEN, 2 below `pass=total`.** `io-read` is **GREEN 130/130** (re-verified 2026-09-12; the recorded `51/130` RED was disproven by PR #7). `misc-edge` (6/12) is RED and `timing` is 1956/2020 (64 failures pre-existing on `main`), so neither meets the `pass=total` bar. The other 11 (`memory`, `bios-math`, `dma`, `shifter`, `carry`, `multiply-long`, `timer-irq`, `timers`, `sio-read`, `sio-timing`, plus `io-read`) are GREEN.
- **Video oracle aliases: 7/7 GREEN** via the separate `tools/run-video-suite-all.ps1` runner (the upstream interactive `video` suite is intentionally excluded from the credibility matrix baseline and absent from the matrix by design — it does not emit `END pass=total`).
- **Physical device soak: still failing/blocked.** The 2026-08-14 P1 soak evidence records audible-but-choppy audio (2,787 underruns @ frame 60; 710,778 after restart) and 1.4–2 fps pacing with a full present stall; thermal soak unmeasured. These FAIL records are preserved in `docs/evidence/` and are not superseded.
- **Commercial-ROM compatibility: no claim.** Only the legal locally built mGBA suite ROM is exercised; retail-game compatibility is explicitly untested.
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
