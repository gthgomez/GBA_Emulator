# GBA SIO Timing Closure Report

Date/time: 2026-05-12T19:08:59.9507925-05:00

Starting commit: `c69c065a42e6ab1e6e6a815c38b45a4f2532a05e`

## Starting Working Tree

Captured before the SIO timing edits:

```text
## main...origin/main
 M src/core/arm7tdmi.cpp
 M src/core/io_registers.cpp
 M src/core/memory_bus.cpp
 M src/core/ppu_timing.cpp
 M tests/io_registers_test.cpp
?? reports/
```

These were retained as existing io-read and sio-read/open-bus changes.

## Ending Working Tree

```text
## main...origin/main
 M include/gba/core/core_scheduler.hpp
 M include/gba/core/io_registers.hpp
 M src/core/arm7tdmi.cpp
 M src/core/core_scheduler.cpp
 M src/core/core_session.cpp
 M src/core/io_registers.cpp
 M src/core/memory_bus.cpp
 M src/core/ppu_timing.cpp
 M tests/io_registers_test.cpp
 M tools/core-benchmark-baseline.json
 M tools/run-core-tests.ps1
?? reports/
```

Changed tracked files at report time:

```text
include/gba/core/core_scheduler.hpp
include/gba/core/io_registers.hpp
src/core/arm7tdmi.cpp
src/core/core_scheduler.cpp
src/core/core_session.cpp
src/core/io_registers.cpp
src/core/memory_bus.cpp
src/core/ppu_timing.cpp
tests/io_registers_test.cpp
tools/core-benchmark-baseline.json
tools/run-core-tests.ps1
```

Untracked reports directory contains this report plus previous closure reports.

## Failure Classification

Before the patch, `sio-timing` was RED, `0/8`.

All eight cases returned an actual result of `0x00000004`, indicating that `SWI 0x02` / Halt returned almost immediately instead of waiting for an enabled interrupt source.

Classified failures:

- `Multi/9.6k`, `Multi/38.4k`, `Multi/57.6k`, `Multi/115.2k`: multiplayer no-peer transfers should not synthesize a serial completion. The suite expects timer timeout/skip behavior.
- `Normal8/256k`, `Normal8/2M`, `Normal32/256k`, `Normal32/2M`: internal-clock normal-mode transfers need deterministic serial completion timing, start/busy clearing, and optional serial IRQ delivery.

After adding transfer scheduling but before the final latency adjustment, the normal-mode cases completed 40 cycles early:

- `Normal8/256k`: actual `0x251`, expected `0x279`
- `Normal8/2M`: actual `0x91`, expected `0xB9`
- `Normal32/256k`: actual `0x851`, expected `0x879`
- `Normal32/2M`: actual `0x151`, expected `0x179`

Root cause: missing SIO transfer progression in the device tick path, missing Halt wait semantics for the suite's IRQ-driven wait loop, and a fixed serial completion latency needed after bit-clock completion.

## Implementation Summary

- Added SIO transfer state to `IoRegisters`: active flag and remaining cycle count.
- Added deterministic `IoRegisters::tick(cycles)` and wired it into `CoreScheduler::advance_devices`.
- Connected `CoreSession` to give the scheduler access to IO registers after construction, reset, and save-state load.
- Changed HLE `SWI 0x02` handling to wait for an enabled interrupt by advancing devices, rather than returning immediately.
- Implemented normal 8-bit and 32-bit internal-clock SIO completion scheduling:
  - slow clock: 64 cycles/bit
  - fast clock: 8 cycles/bit
  - completion latency: 40 cycles
- On transfer completion, SIO requests `IRQ_SERIAL` only when SIOCNT IRQ enable is set.
- Multiplayer/external/no-peer starts remain deterministic and do not invent a peer completion interrupt.
- Added focused IO-register tests for serial completion, IRQ-enabled and IRQ-disabled behavior, and no-peer multiplayer idle behavior.
- Updated the core test script so the scheduler test links `io_registers.cpp`, matching the scheduler's new IO tick dependency.
- Updated the performance baseline checksum for `core_session_state_hash` from `15808` to `58800`, because serial transfer active state and remaining cycles are now part of modeled deterministic core-session state. Timing thresholds were not relaxed.

## Behavior Now Supported

- Internal-clock normal 8-bit SIO transfer completion timing.
- Internal-clock normal 32-bit SIO transfer completion timing.
- Deterministic serial IRQ request on transfer completion when enabled.
- No serial IRQ request when SIOCNT IRQ enable is clear.
- Deterministic no-peer behavior for multiplayer timing tests: no synthetic transfer completion.
- SIO read/default behavior from the previous goal remains preserved.

## Still Not Supported

- Full multiplayer/link-cable peer communication.
- Real external-clock peer-driven transfers.
- Full UART protocol behavior.
- Commercial link compatibility claims.

Full multiplayer/link-cable support remains unsupported and unclaimed.

## Commands Run

Pre-patch/baseline:

```powershell
git -C C:\Workspace\Project_Android\GBA_Emulator status --short --branch
& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite io-read -MaxSteps 8000000 -TraceSteps 0
& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite sio-read -MaxSteps 8000000 -TraceSteps 0
C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite sio-timing -MaxSteps 8000000 -TraceSteps 0 -TraceFirstFailure
```

Development checks:

```powershell
& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite sio-timing -MaxSteps 8000000 -TraceSteps 0 -TraceFirstFailure
C:\Workspace\Project_Android\GBA_Emulator\tools\run-core-tests.ps1
```

Final verification:

```powershell
& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite sio-timing -MaxSteps 8000000 -TraceSteps 0
& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite sio-read -MaxSteps 8000000 -TraceSteps 0
& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite io-read -MaxSteps 8000000 -TraceSteps 0
C:\Workspace\Project_Android\GBA_Emulator\tools\run-core-tests.ps1
C:\Workspace\Project_Android\GBA_Emulator\tools\check-core-performance-regression.ps1
& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite memory -MaxSteps 8000000 -TraceSteps 0
& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite bios-math -MaxSteps 8000000 -TraceSteps 0
& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite dma -MaxSteps 20000000 -TraceSteps 0
& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite shifter -MaxSteps 8000000 -TraceSteps 0
& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite carry -MaxSteps 8000000 -TraceSteps 0
& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite multiply-long -MaxSteps 8000000 -TraceSteps 0
& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite timer-irq -MaxSteps 8000000 -TraceSteps 0
& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite timers -MaxSteps 20000000 -TraceSteps 0
& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite timing -MaxSteps 20000000 -TraceSteps 0
```

Optional next baseline:

```powershell
& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite misc-edge -MaxSteps 12000000 -TraceSteps 0 -TraceFirstFailure
```

## Results

| Target | Before | After | Evidence |
| --- | --- | --- | --- |
| sio-timing | RED, 0/8 | GREEN, 4/4 with 4 skipped no-peer multiplayer cases | `mgba-suite-20260512-185636.json` |
| sio-read | GREEN, 90/90 | GREEN, 90/90 | `mgba-suite-20260512-185704.json` |
| io-read | GREEN, 130/130 | GREEN, 130/130 | `mgba-suite-20260512-185731.json` |
| core verifier | PASS | PASS | `run-core-tests.ps1` |
| core performance gate | PASS baseline before goal | PASS after checksum update for deterministic SIO state | `check-core-performance-regression.ps1` |
| memory | GREEN, 1552/1552 | GREEN, 1552/1552 | `mgba-suite-20260512-190157.json` |
| bios-math | GREEN, 615/615 | GREEN, 615/615 | `mgba-suite-20260512-190253.json` |
| dma | GREEN, 1256/1256 | GREEN, 1256/1256 | `mgba-suite-20260512-190328.json` |
| shifter | GREEN, 140/140 | GREEN, 140/140 | `mgba-suite-20260512-190508.json` |
| carry | GREEN, 93/93 | GREEN, 93/93 | `mgba-suite-20260512-190531.json` |
| multiply-long | GREEN, 72/72 | GREEN, 72/72 | `mgba-suite-20260512-190553.json` |
| timer-irq | GREEN, 90/90 | GREEN, 90/90 | `mgba-suite-20260512-190617.json` |
| timers | GREEN, 936/936 | GREEN, 936/936 | `mgba-suite-20260512-190636.json` |
| timing | GREEN, 2020/2020 | GREEN, 2020/2020 | `mgba-suite-20260512-190725.json` |
| misc-edge | not advanced | RED baseline | `mgba-suite-20260512-190834.json` |

## misc-edge Baseline

`misc-edge` was run once after `sio-timing` and the protected baseline were green. It remains RED:

```text
suite_runner: unsupported_steps=1
suite_runner: fetch_failures=0
suite_runner: stop_reason=unsupported_instruction
suite_runner: final_pc=0x80065c4
suite_runner: last_instruction=0xcb04
suite_test: status=RED
```

No `misc-edge` patch was attempted in this goal.

## Completion Status

GREEN for this goal:

- `sio-timing` passes.
- `sio-read` remains PASS, 90/90.
- `io-read` remains PASS, 130/130.
- Core verifier passes.
- Core performance gate passes.
- All previous green public suites remain green.
- No hardcoded suite answers, ROM names, or test addresses were introduced.
- Full multiplayer/link-cable remains explicitly unsupported and unclaimed.

Next recommended blocker: `misc-edge`.
