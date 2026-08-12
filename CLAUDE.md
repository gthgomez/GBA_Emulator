# CLAUDE.md — GBA_Emulator

## Model & Trust Configuration

- **Primary model:** Sonnet 4.6
- **Trust level:** High autonomy for tests and benchmarks; pause for core CPU/memory timing changes

## Context Stack

1. Read this file (`GBA_Emulator/CLAUDE.md`)
2. Read `GBA_Emulator/PROJECT_CONTEXT.md` for directory map and invariants
3. Read root `PROJECT_CONTEXT.md` for workspace context
4. Read root `CLAUDE.md` (`Project_Android/CLAUDE.md`) for behavioral rules
5. Review `tasks/lessons.md` if it exists

## C++ Specific Rules

- **NEVER commit BIOS, ROM, or save files.** `.gitignore` blocks them. The core is verified headlessly.
- **Timing is correctness-critical.** Changes to `core_scheduler.cpp`, `wait_state_control.cpp`, `dma_controller.cpp`, or `apu.cpp` must pass timing tests and benchmarks before claiming correctness.
- **All verification is headless.** Tests compile via `g++` and run as native executables. No Android, no display, no JNI required.
- **Test before claiming anything works.** Run `tools/run-core-tests.ps1` and `tools/run-core-benchmarks.ps1` after any code change.
- **Determinism is required.** Benchmark baselines track run-to-run variance. If a change introduces non-determinism, fix it.

## Reference Documentation

See `docs/` for deep documentation:
- `docs/android-integration-plan.md` — JNI bridge and Android integration architecture
- `docs/android-device-soak-checklist.md` — Device-level soak testing
- `docs/controlled-beta-readiness.md` — Beta release readiness gates
- `docs/design-decisions.md` — Architectural decisions and rationale
- `docs/open-issues-status.md` — Known issues and tracking

## High-Risk Zones (Pause and Confirm)

- Changes to CPU instruction decoding/execution (`arm7tdmi.cpp`)
- Memory map or mirroring changes (`memory_bus.cpp`)
- Timing model changes (`core_scheduler.cpp`, `wait_state_control.cpp`)
- DMA controller logic (`dma_controller.cpp`)
- Save state format/codec changes (`save_state_codec.cpp`)
- Any change that could break deterministic behavior

## Verification

- `.\tools\run-core-tests.ps1` — Core test suite (compiles and runs all tests)
- `.\tools\run-core-benchmarks.ps1` — Performance benchmarks
- `.\tools\run-mgba-suite.ps1` — mGBA public test suite regression
- `.\tools\run-credibility-matrix.ps1` — Credibility matrix
