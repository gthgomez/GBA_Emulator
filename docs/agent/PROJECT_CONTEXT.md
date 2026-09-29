# PROJECT_CONTEXT.md — GBA_Emulator

## What This Is

A portable Game Boy Advance emulator core written in C++17. Implements ARM7TDMI CPU emulation, memory bus, DMA, timers, PPU, APU, and a timing/scheduler framework. All assertions are verified headlessly in-memory — no Android or display dependency in the core.

## Tech Stack

- **Language:** C++17
- **Build:** Direct `g++` compilation via PowerShell scripts (no CMake for core — CMake used only for Android JNI bridge in sibling project)
- **Testing:** Headless in-memory test framework with deterministic assertions
- **Benchmarking:** Custom benchmark harness for timing-critical paths
- **Target:** Portable core with opaque C++ API; JNI/Android integration deferred to sibling `GbaEmulatorAndroid`

## Startup Sequence

1. Read this project's `CLAUDE.md`
2. Read this file (`PROJECT_CONTEXT.md`)
3. Read root `PROJECT_CONTEXT.md` for workspace context
4. Read root `CLAUDE.md` for behavioral rules
5. Review `tasks/lessons.md` if it exists

## Directory Map

```
GBA_Emulator/
├── include/                     — C++ headers
├── src/core/                    — Core emulator source
│   ├── arm7tdmi.cpp             — CPU emulation (ARM/Thumb)
│   ├── memory_bus.cpp           — Memory mapping and bus
│   ├── core_scheduler.cpp       — Global event scheduler
│   ├── core_session.cpp         — Session lifecycle
│   ├── dma_controller.cpp       — DMA engine
│   ├── timers.cpp               — Timer hardware
│   ├── ppu_*.cpp                — PPU (renderer, background, sprites, timing)
│   ├── apu.cpp                  — Audio processing unit
│   ├── io_registers.cpp         — MMIO register routing
│   ├── interrupt_controller.cpp — Interrupt handling
│   ├── keypad.cpp               — Keypad input
│   ├── wait_state_control.cpp   — WAITCNT timing
│   ├── bios.cpp                 — BIOS HLE (no bundled BIOS file)
│   ├── android_core_bridge.cpp  — Narrow JNI-facing C API bridge
│   ├── android_runtime.cpp      — Android runtime (video/input/audio flow)
│   ├── save_state_codec.cpp     — Save state serialization
│   └── instruction_cache.cpp    — Predecoded instruction cache
├── tests/                       — Headless in-memory test files
├── benchmarks/                  — Benchmark harness and tests
├── tools/                       — Build and verification scripts
│   ├── run-core-tests.ps1       — Core test suite
│   ├── run-core-benchmarks.ps1  — Benchmark suite
│   ├── run-credibility-matrix.ps1
│   ├── run-mgba-suite.ps1       — mGBA public test suite runner
│   ├── run-rom-smoke.ps1        — ROM smoke tests (out-of-tree ROMs)
│   └── mgba-suite-green-baseline.json
├── docs/                        — Detailed documentation
│   ├── android-integration-plan.md
│   ├── android-device-soak-checklist.md
│   ├── controlled-beta-readiness.md
│   ├── open-issues-status.md
│   ├── design-decisions.md
│   └── production-engine-roadmap.md
├── external/                    — External test suites (mGBA)
└── build/                       — Build outputs
```

## High-Risk Zones

- **Timing accuracy:** Sequential fetches, DMA timing, WAITCNT configuration, prefetch pipeline timing. Timing errors break game compatibility.
- **CPU instruction accuracy:** All ARM and Thumb opcodes must decode and execute correctly with proper flag updates.
- **Memory mapping:** Mirroring, unaligned access behavior, open-bus reads, and bus-owner arbitration.
- **Direct Sound audio mixing:** APU timing and FIFO DMA refill must be sample-accurate.
- **PPU scanline timing:** Background rendering, sprite rendering, and window/blend effects.
- **Save states:** Versioned codec with deterministic serialization — breaking changes must be versioned.

## Invariants

- **NEVER commit BIOS or ROM files** — they remain out-of-tree. The `.gitignore` blocks `*.gba`, `*.gb`, `*.gbc`, `*.bin`, `*.bios`, and `*.sav`.
- **Opaque C++ API handle** with deferred JNI/Android integration. The core has no Android dependency.
- **All assertions are verified headlessly in-memory** — tests compile and run as native Windows/Linux executables.
- **Timing-critical code must be deterministic** — run-to-run variance is tracked via benchmark baselines.
- **BIOS HLE** is used for game boot — no bundled BIOS file. Entry PC is `0x08000000`.
- **Legal boundary:** mGBA public test suite ROM only. No commercial ROM or BIOS claims.

## Verification

- Core tests: `.\tools\run-core-tests.ps1`
- Core benchmarks: `.\tools\run-core-benchmarks.ps1`
- Credibility matrix: `.\tools\run-credibility-matrix.ps1`
- mGBA public suite: `.\tools\run-mgba-suite.ps1`
- All commands compile from source via `g++` — no Gradle or CMake involved at the core level
