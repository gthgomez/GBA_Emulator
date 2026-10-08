# AGENTS.md — GBA_Emulator

This file is the sole instruction authority for engineering agents working in this
repository. Model- or vendor-specific instruction files (CLAUDE.md, GEMINI.md,
CODEX.md, and similar) are prohibited here; do not create or consult them. Nested
instruction files are also prohibited. Factual and architectural context lives in
`docs/PROJECT_CONTEXT.md` and is task data, not policy.

## What This Is

A portable Game Boy Advance emulator core written in C++17 (ARM7TDMI CPU, memory bus,
DMA, timers, PPU, APU, timing/scheduler framework). All verification is headless —
no Android, display, or JNI dependency in the core. Android integration lives in the
sibling `GbaEmulatorAndroid` dev shell. See `docs/PROJECT_CONTEXT.md` for the full
directory map and technical background.

## Hard Rules

- **NEVER commit BIOS, ROM, or save files.** They stay out-of-tree; `.gitignore`
  blocks `*.gba`, `*.gb`, `*.gbc`, `*.bin`, `*.bios`, and `*.sav`. BIOS is HLE'd at
  boot (entry PC `0x08000000`); no BIOS file is bundled.
- **Test before claiming anything works.** Run `tools/run-core-tests.ps1` and
  `tools/run-core-benchmarks.ps1` after any code change.
- **Timing is correctness-critical.** Changes to `core_scheduler.cpp`,
  `wait_state_control.cpp`, `dma_controller.cpp`, or `apu.cpp` must pass timing
  tests and benchmarks before correctness is claimed.
- **Determinism is required.** Benchmark baselines track run-to-run variance. If a
  change introduces non-determinism, fix it.
- **All verification is headless.** Tests compile via `g++` and run as native
  executables — no Android, display, or JNI required.
- **Legal boundary:** mGBA public test suite ROM only. No commercial ROM or BIOS
  compatibility claims.

## High-Risk Zones (pause and confirm)

- CPU instruction decoding/execution (`arm7tdmi.cpp`)
- Memory map or mirroring changes (`memory_bus.cpp`)
- Timing model changes (`core_scheduler.cpp`, `wait_state_control.cpp`)
- DMA controller logic (`dma_controller.cpp`)
- Save state format/codec changes (`save_state_codec.cpp`)
- Any change that could break deterministic behavior

## Accuracy Cautions

- Do not invent ARM7TDMI opcode encodings, cycle counts, or flag behavior; verify
  against the implementation and test suites.
- Do not confuse DMA timing modes (immediate / VBlank / HBlank / FIFO).
- Do not conflate C++17 with other language standards or hallucinate memory-map
  addresses.
- Do not treat ROM/BIOS files as committable assets.

## Reference Documentation

Deep documentation lives in `docs/`:
- `docs/android-integration-plan.md` — JNI bridge and Android integration architecture
- `docs/android-device-soak-checklist.md` — Device-level soak testing
- `docs/controlled-beta-readiness.md` — Beta release readiness gates
- `docs/design-decisions.md` — Architectural decisions and rationale
- `docs/open-issues-status.md` — Known issues and tracking

## Verification

Run from the repository root (PowerShell):

- `.\tools\run-core-tests.ps1` — Core test suite (compiles and runs all tests)
- `.\tools\run-core-benchmarks.ps1` — Performance benchmarks
- `.\tools\run-mgba-suite.ps1` — mGBA public test suite regression
- `.\tools\run-credibility-matrix.ps1` — Credibility matrix

On Linux, including Cloud Agents, the same core suite is `./tools/run-core-tests.sh`. Invoke the PowerShell verifiers with `pwsh -File ./tools/<script>.ps1`. The Cloud Agent environment installs `g++` (also selected as `c++`), CMake, Python 3, PowerShell 7, and SDL3 3.4.18 under `/usr/local`. Build the desktop host with `cmake -S . -B build/desktop -DCMAKE_BUILD_TYPE=Release` and `cmake --build build/desktop`, then run `./tools/run-desktop-smoke.sh`.

All commands compile from source via `g++`; no Gradle or CMake is involved at the
core level.
