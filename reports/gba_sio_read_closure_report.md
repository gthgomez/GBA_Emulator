# GBA SIO Read Closure Report

Date/time: 2026-05-12T17:45:34.7412597-05:00

## Git State

- Commit at start: `c69c065a42e6ab1e6e6a815c38b45a4f2532a05e`
- Starting status:
  - `main...origin/main`
  - Modified from prior `io-read` closure: `src/core/arm7tdmi.cpp`, `src/core/io_registers.cpp`, `src/core/memory_bus.cpp`, `src/core/ppu_timing.cpp`, `tests/io_registers_test.cpp`
  - Untracked: `reports/`
- Ending status:
  - `main...origin/main`
  - Modified: `src/core/arm7tdmi.cpp`, `src/core/io_registers.cpp`, `src/core/memory_bus.cpp`, `src/core/ppu_timing.cpp`, `tests/io_registers_test.cpp`
  - Untracked: `reports/`
- Files changed by this SIO pass:
  - `src/core/io_registers.cpp`
  - `tests/io_registers_test.cpp`
  - `reports/gba_sio_read_closure_report.md`

## Failure Classification

Initial `sio-read` result: RED, `25/90`, artifact `build\test-results\mgba-suite-20260512-173212.json`.

- `SIOMULTI/SIODATA32/SIODATA8`: real readable serial data latches. Current code exposed probe-write values such as `0xFFFF`; idle hardware-like reads need deterministic zero in the active mode's receive slots, with normal 32-bit/8-bit send aliases preserving `0xFFFF` where appropriate.
- `SIOCNT`: partially readable/masked control. Mode bits are real state; idle/reserved/status bits read as fixed masked values. Current code read back too many written bits.
- `RCNT`: partially readable/masked mode/GPIO control. General-purpose and JoyBus modes need deterministic mode-specific idle values; current code read back probe bits.
- `JOYCNT`, `JOY_RECV`, `JOY_TRANS`: real JoyBus registers with deterministic default reads. They were previously unimplemented and fell through to CPU instruction open bus (`0xDEAD`).
- `0x04000136`, `0x04000142`, `JOYSTAT`: already deterministic zero and passing.

## Implementation Summary

- Added centralized SIO register read helpers for:
  - `SIOCNT` mode/status read masks.
  - `RCNT` mode-dependent idle masks.
  - Serial data reads for normal 8-bit, normal 32-bit, multiplayer, UART, GPIO, and JoyBus-mode default states.
  - JoyBus register defaults: `JOYCNT = 0x0040`, `JOY_RECV/JOY_TRANS = 0`.
- Kept unsupported multiplayer/link protocol behavior honest: this implements deterministic register read semantics only, not full link-cable transfer/timing.
- Preserved the `io-read` CPU open-bus fix; no CPU open-bus path was reverted or bypassed.
- Added focused `io_registers_test` coverage for SIO idle data reads, SIOCNT/RCNT masks, and JoyBus defaults.

## Supported Now

- Deterministic halfword reads for the SIO register block covered by `sio-read`.
- Mode-aware read masks/defaults for `SIOCNT` and `RCNT`.
- Deterministic idle serial data reads for normal/multiplayer/UART/GPIO/JoyBus mode contexts.
- Deterministic JoyBus register read defaults.

## Still Not Supported

- Full link-cable multiplayer behavior.
- SIO protocol transfer timing.
- SIO interrupt progression beyond stable register defaults.
- IO byte-read callbacks are not generalized here; this closure targets the public halfword SIO read semantics exercised by `sio-read`.

## Commands Run

| Order | Command | Result |
| --- | --- | --- |
| 1 | `git -C C:\Workspace\Project_Android\GBA_Emulator status --short --branch` | PASS: dirty baseline recorded |
| 2 | `git -C C:\Workspace\Project_Android\GBA_Emulator diff --stat` | PASS: prior dirty patch identified |
| 3 | `git -C C:\Workspace\Project_Android\GBA_Emulator rev-parse HEAD` | PASS: `c69c065a42e6ab1e6e6a815c38b45a4f2532a05e` |
| 4 | `& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite io-read -MaxSteps 8000000 -TraceSteps 0 \| Select-String ...` | PASS before SIO edits: `130/130`, GREEN, artifact `build\test-results\mgba-suite-20260512-173035.json` |
| 5 | `C:\Workspace\Project_Android\GBA_Emulator\tools\run-core-tests.ps1` | PASS before SIO edits |
| 6 | `C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite sio-read -MaxSteps 8000000 -TraceSteps 0 -TraceFirstFailure` | FAIL before SIO edits: `25/90`, RED, artifact `build\test-results\mgba-suite-20260512-173212.json` |
| 7 | `& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite sio-read -MaxSteps 8000000 -TraceSteps 0 -TraceFirstFailure \| Select-String ...` | PASS after implementation: `90/90`, GREEN, artifact `build\test-results\mgba-suite-20260512-173407.json` |
| 8 | `& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite sio-read -MaxSteps 8000000 -TraceSteps 0 \| Select-String ...` | PASS final: `90/90`, GREEN, artifact `build\test-results\mgba-suite-20260512-173454.json` |
| 9 | `& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite io-read -MaxSteps 8000000 -TraceSteps 0 \| Select-String ...` | PASS: `130/130`, GREEN, artifact `build\test-results\mgba-suite-20260512-173521.json` |
| 10 | `C:\Workspace\Project_Android\GBA_Emulator\tools\run-core-tests.ps1` | FAIL once on stale `RCNT readback routes` unit expectation |
| 11 | `C:\Workspace\Project_Android\GBA_Emulator\tools\run-core-tests.ps1` | PASS after focused unit update |
| 12 | `C:\Workspace\Project_Android\GBA_Emulator\tools\check-core-performance-regression.ps1` | PASS: `core_performance_regression: PASS` |
| 13 | `& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite memory -MaxSteps 8000000 -TraceSteps 0 \| Select-String ...` | PASS: `1552/1552`, GREEN, artifact `build\test-results\mgba-suite-20260512-173818.json` |
| 14 | `& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite bios-math -MaxSteps 8000000 -TraceSteps 0 \| Select-String ...` | PASS: `615/615`, GREEN, artifact `build\test-results\mgba-suite-20260512-173905.json` |
| 15 | `& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite dma -MaxSteps 20000000 -TraceSteps 0 \| Select-String ...` | PASS: `1256/1256`, GREEN, artifact `build\test-results\mgba-suite-20260512-173938.json` |
| 16 | `& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite shifter -MaxSteps 8000000 -TraceSteps 0 \| Select-String ...` | PASS: `140/140`, GREEN, artifact `build\test-results\mgba-suite-20260512-174121.json` |
| 17 | `& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite carry -MaxSteps 8000000 -TraceSteps 0 \| Select-String ...` | PASS: `93/93`, GREEN, artifact `build\test-results\mgba-suite-20260512-174146.json` |
| 18 | `& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite multiply-long -MaxSteps 8000000 -TraceSteps 0 \| Select-String ...` | PASS: `72/72`, GREEN, artifact `build\test-results\mgba-suite-20260512-174208.json` |
| 19 | `& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite timer-irq -MaxSteps 8000000 -TraceSteps 0 \| Select-String ...` | PASS: `90/90`, GREEN, artifact `build\test-results\mgba-suite-20260512-174232.json` |
| 20 | `& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite timers -MaxSteps 20000000 -TraceSteps 0 \| Select-String ...` | PASS: `936/936`, GREEN, artifact `build\test-results\mgba-suite-20260512-174254.json` |
| 21 | `& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite timing -MaxSteps 20000000 -TraceSteps 0 \| Select-String ...` | PASS: `2020/2020`, GREEN, artifact `build\test-results\mgba-suite-20260512-174345.json` |
| 22 | `& C:\Workspace\Project_Android\GBA_Emulator\tools\run-mgba-suite.ps1 -Suite sio-timing -MaxSteps 12000000 -TraceSteps 0 -TraceFirstFailure \| Select-String ...` | FAIL next-blocker baseline: `0/8`, RED, artifact `build\test-results\mgba-suite-20260512-174452.json` |

## Before/After Status

| Area | Before | After |
| --- | --- | --- |
| `sio-read` | RED: `25/90` | GREEN: `90/90` |
| `io-read` | GREEN: `130/130` | GREEN: `130/130` |
| Core verifier | PASS baseline | PASS |
| Core performance gate | PASS baseline | PASS |
| `memory` | GREEN: `1552/1552` | GREEN: `1552/1552` |
| `bios-math` | GREEN: `615/615` | GREEN: `615/615` |
| `dma` | GREEN: `1256/1256` | GREEN: `1256/1256` |
| `shifter` | GREEN: `140/140` | GREEN: `140/140` |
| `carry` | GREEN: `93/93` | GREEN: `93/93` |
| `multiply-long` | GREEN: `72/72` | GREEN: `72/72` |
| `timer-irq` | GREEN: `90/90` | GREEN: `90/90` |
| `timers` | GREEN: `936/936` | GREEN: `936/936` |
| `timing` | GREEN: `2020/2020` | GREEN: `2020/2020` |

## SIO Timing

`sio-timing` was run once after `sio-read` and the protected baseline were green. It is RED at `0/8`; failing cases are:

- `Multi/9.6k`
- `Multi/38.4k`
- `Multi/57.6k`
- `Multi/115.2k`
- `Normal8/256k`
- `Normal8/2M`
- `Normal32/256k`
- `Normal32/2M`

Next recommended blocker: `sio-timing`.

## Result

GREEN for the `sio-read` blocker: `sio-read` passes, `io-read` remains green, all previously green suites remain green, core verifier and performance gate pass, and unsupported SIO/link behavior is documented without claiming full link-cable support.
