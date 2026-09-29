# GBA_Emulator

Portable Game Boy Advance emulator core (C++17) with public mGBA test-suite evidence and a
sibling Android dev shell. Controlled external beta remains blocked without device evidence.

## Current Status (updated 2026-09-18)

| Area | State | Notes |
| --- | --- | --- |
| Local core verifiers | PASS (29/29) | `.\tools\run-core-tests.ps1` — clean `g++ -Werror` rebuild, all binaries pass (29th: the IntrWait serial-horizon verifier added with this PR) |
| mGBA public suites (13) | 11 GREEN / 2 NOT FULL | `io-read` is GREEN 130/130 (re-verified 2026-09-12; the recorded `51/130` RED was disproven by PR #7). `misc-edge` (6/12) and `timing` (1956/2020, 64 failures pre-existing on `main`) remain below `pass=total` — see `docs/open-issues-status.md`. Baseline in `tools/mgba-suite-green-baseline.json` |
| Video oracle aliases (7) | GREEN | Deterministic `video_probe` via `.\tools\run-video-suite-all.ps1`; upstream interactive `video` suite is intentionally **not** in the credibility matrix baseline |
| Credibility matrix | RED | `.\tools\run-credibility-matrix.ps1` stays RED while `misc-edge` (6/12) and `timing` (1956/2020) are below `pass=total`; `io-read` is no longer a regression (latest: `build/test-results/credibility-matrix-latest.json`) |
| Android shell | Scaffold only | [`../GbaEmulatorAndroid`](../GbaEmulatorAndroid) — JNI bridge self-test; see `docs/android-integration-plan.md` |
| Controlled beta | BLOCKED | No target-device soak row yet — `docs/controlled-beta-readiness.md` |

> **Note:** Sanitized builds (`run-core-tests.ps1 -Sanitize`) cannot run in this environment — the bundled MinGW `g++` (Rev1, 12.2.0) ships no `libasan`/`libubsan`, so ASan/UBSan link fails. This is a toolchain limitation, not an engine defect.

**Claims policy:** Legal mGBA Game Boy Advance Test Suite ROM only (locally built). No
commercial ROM, BIOS bundle, or retail-game compatibility claims. Open/closed issue
tracking: `docs/open-issues-status.md`. Active status: [STATUS.md](STATUS.md) | [QA_CHECKLIST.md](QA_CHECKLIST.md).

## Roadmap

The production engine phase plan lives in
`docs/production-engine-roadmap.md`. It starts from the Phase 51 baseline and splits
the remaining work into CPU hardening, bus/timing accuracy, PPU/APU completeness,
save-state productionization, performance architecture, legal fixture compatibility,
Android integration, device measurement, and controlled beta readiness phases.

The accuracy/performance comparison roadmap lives in
`docs/benchmark-comparison-roadmap.md`. It defines the goal, purpose, green criteria,
and phased benchmark matrix for comparing this engine against top open-source GBA
emulators without separating speed claims from correctness evidence.

## Verify

From this repository:

```powershell
.\tools\run-core-tests.ps1
```

The script compiles and runs all local core verifier binaries with C++17 using `g++`.

## Docs

- [Current Core Scope](docs/CORE_SCOPE.md) — architecture and CPU core scope
- [Performance & Measurement](docs/PERFORMANCE.md) — benchmark commands, regression gates, and performance notes

## License

Licensed under the [Apache License 2.0](LICENSE). Copyright 2026 Jonathan Gomez Aguilar.
See [NOTICE](NOTICE) for attribution.

This repository distributes no ROMs, BIOS images, or copyrighted game assets. The core
operates only on caller-provided, legally obtained ROM data. See
[docs/fixture-license-registry.md](docs/fixture-license-registry.md) and
[docs/release-governance-legal-review.md](docs/release-governance-legal-review.md) for the
fixture and release-governance rules.
