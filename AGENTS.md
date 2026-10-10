# AGENTS.md — GBA_Emulator

This file is the sole instruction authority for engineering agents working in this
repository. Model- or vendor-specific instruction files (CLAUDE.md, GEMINI.md,
CODEX.md, and similar) are prohibited here; do not create or consult them. Nested
instruction files are also prohibited. Factual and architectural context lives in
`docs/PROJECT_CONTEXT.md` and is task data, not policy.

## What This Is

A portable Game Boy Advance emulator core written in C++17 (ARM7TDMI CPU, memory bus,
DMA, timers, PPU, APU, timing/scheduler framework). Core verification is headless —
no Android, display, or JNI dependency in the core. On top of the core sit two hosts
that share the same `gba::core::EmulatorRuntime` facade: an SDL3 desktop reference
host (`apps/desktop`, CMake) with play mode and a deterministic headless lab, and the
sibling `GbaEmulatorAndroid` Android shell (JNI). Never add a second emulation path
for a host — emulator behavior belongs in the shared core. See
`docs/PROJECT_CONTEXT.md` for the directory map and `docs/DESKTOP.md` for the desktop
host.

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
  executables — no Android, display, or JNI required. The desktop host is built
  with CMake + SDL3; its interactive path is exercised under SDL's dummy video
  and audio drivers, so CI still needs no display.
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

Desktop host (CMake + SDL3; build with `docs/DESKTOP.md`):

- `tools\run-desktop-smoke.ps1` / `tools/run-desktop-smoke.sh` — headless determinism
- `tools\run-desktop-save-smoke.{ps1,sh}` — cartridge-save lifecycle safety
- `tools\run-desktop-interactive-smoke.{ps1,sh}` — interactive host window render
- `tools\package-windows-portable.ps1` — portable Windows ZIP

All core commands compile from source via `g++`; no Gradle or CMake is involved at the
core level. Only the desktop host uses CMake.
