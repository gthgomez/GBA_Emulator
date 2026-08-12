# GBA_Emulator

Portable Game Boy Advance emulator core (C++17) with public mGBA test-suite evidence and a
sibling Android dev shell. Controlled external beta remains blocked without device evidence.

## Current Status (Jun 2026)

| Area | State | Notes |
| --- | --- | --- |
| Local core verifiers | PASS | `.\tools\run-core-tests.ps1` |
| mGBA public suites (13) | GREEN | `memory`, `io-read`, `bios-math`, `dma`, `shifter`, `carry`, `multiply-long`, `timer-irq`, `timers`, `timing`, `sio-read`, `sio-timing`, `misc-edge` — regression baseline in `tools/mgba-suite-green-baseline.json` |
| Video oracle aliases (7) | GREEN | Deterministic `video_probe` with matching actual/expected frame hashes; upstream interactive `video` suite is **not** green |
| Credibility matrix | GREEN | `.\tools\run-credibility-matrix.ps1` (latest: `build/test-results/credibility-matrix-latest.json`) |
| Android shell | Scaffold only | [`../GbaEmulatorAndroid`](../GbaEmulatorAndroid) — JNI bridge self-test; see `docs/android-integration-plan.md` |
| Controlled beta | BLOCKED | No target-device soak row yet — `docs/controlled-beta-readiness.md` |

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
