# GBA_Emulator Status

**Last verified:** 2026-08-22 (core verifiers); status reconciled 2026-09-29 against `docs/open-issues-status.md` (2026-09-12 record) — no test reruns performed for the reconciliation itself
**Status:** active development
**Confidence:** high (core verifiers); medium (accuracy — `misc-edge` regressed; post-#6 timer/timing state unknown, see below)

## Purpose

Portable Game Boy Advance (GBA) emulator core written in C++17 with mGBA test-suite verification, deterministic timing/scheduler, and an opaque C API for Android JNI integration.

## Current State

- **Core verifiers: 28/28 PASS.** Re-verified 2026-08-22 via `tools/run-core-tests.ps1` (clean `g++ -Werror` rebuild, all binaries run and pass). No compile warnings, no runtime failures in the headless suite.
- **mGBA public suites: 12/13 last measured GREEN, 1 REGRESSED (`misc-edge`).** `io-read` (51/130) was recorded RED on 2026-08-14 but **disproven on 2026-09-12**: a rerun of the pinned suite gives 130/130 and the contract is locked by `tests/io_read_open_bus_test.cpp` (see `docs/open-issues-status.md`). The remaining genuine regression is `misc-edge` (6/12 as of 2026-08-14); PR #6 ("misc-edge HBlank flag start-cycle", merged 2026-09-12) landed investigation + groundwork only, and **no post-#6 `misc-edge` rerun is on record** — treat its current state as UNKNOWN until a rerun exists.
- **Post-#6 timer/sio/timing state: UNKNOWN.** PR #6's scheduler/timer batching is merged to `main` (2026-09-12), and open PR #8 (`fix/timer-tick-regression`, head `e1e0f99`) reports it regressed `timers` (576/936), `timer-irq` (69/90), `sio-timing` (0/4) and `timing` (1946/2020) relative to the #6 parent. PR #8 is **not merged** and no rerun of these suites on current `main` is on record, so these four suites must not be counted GREEN from the pre-#6 measurements. The `GREEN` entries below are the last-measured states, not a certification of current `main`.
- **Video oracle aliases: 7/7 GREEN at last run (2026-06-05)** via the separate `tools/run-video-suite-all.ps1` runner (the upstream interactive `video` suite is intentionally excluded from the credibility matrix baseline and absent from the matrix by design — it does not emit `END pass=total`). No newer oracle run is on record; do not infer current-`main` greenness from the 2026-06-05 result.
- **Physical device soak: still failing/blocked.** The 2026-08-14 P1 soak evidence records audible-but-choppy audio (2,787 underruns @ frame 60; 710,778 after restart) and 1.4–2 fps pacing with a full present stall; thermal soak unmeasured. These FAIL records are preserved in `docs/evidence/` and are not superseded.
- **Commercial-ROM compatibility: no claim.** Only the legal locally built mGBA suite ROM is exercised; retail-game compatibility is explicitly untested.
- **Controlled external beta remains blocked** without target-device soak evidence.

**Engine audit (2026-08-22) — superseded for `io-read`:** the original audit traced the `io-read` regression to IO open-bus read modeling. That root cause was self-contradictory and was **disproven on 2026-09-12**: the pipeline-open-bus fallback *is* what produces the expected `0xDEAD` open-bus value, and re-running the pinned suite gives 130/130 on both `main` and the HBlank branch. The 2026-08-14 `51/130` figure was a stale/mis-built artifact. The contract is now locked natively by `tests/io_read_open_bus_test.cpp` (130/130). The `misc-edge` "H-blank bit start" sub-test remains timing-sensitive; PR #6 records the flag-cycle investigation.

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

- **Post-#6 timer/sio/timing conformance (unknown):** open PR #8 reports `timers`, `timer-irq`, `sio-timing` and `timing` regressions introduced by PR #6's scheduler/timer batching. No rerun on current `main` is on record; the state is UNKNOWN until PR #8 merges or a fresh matrix run is recorded.
- **`misc-edge` current state (unknown):** last measured 6/12 (2026-08-14). PR #6's fix landed as "investigation + groundwork" only; no post-merge rerun is on record.
- **Timing edge cases on unaligned memory access across specific Game Pak prefetch wait-states.**

## Verification

- Command: `.\tools\run-core-tests.ps1` and `.\tools\run-credibility-matrix.ps1` documented in `README.md`.

## Next Actions

1. Run local core tests (`tools/run-core-tests.ps1`).
2. Run credibility matrix harness (`tools/run-credibility-matrix.ps1`).
3. Complete target-device soak checklist (`docs/android-device-soak-checklist.md`).

## Evidence Sources

- [README.md](README.md)
- [QA_CHECKLIST.md](QA_CHECKLIST.md)
- [docs/open-issues-status.md](docs/open-issues-status.md)
- Device soak evidence: [docs/evidence/](docs/evidence/) (failed 2026-08-14 P1 records preserved)
