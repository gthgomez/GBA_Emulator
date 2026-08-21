# mGBA Suite Test Results

Status: external-suite baseline advanced to BIOS IRQ dispatch frontier
Date: 2026-05-08

These results use the locally generated mGBA Game Boy Advance Test Suite ROM recorded
in `docs/external-test-suite-baseline.md`.

The ordered implementation/testing plan for finishing this suite is recorded in
`docs/mgba-suite-finish-roadmap.md`.

## Current Generated ROM

| Field | Value |
| --- | --- |
| Source suite | `external/test-suites/mgba-suite` |
| Source commit | `aac98dca785eaec3932af217aa658275737a8ed8` |
| Build script | `tools/build-mgba-suite.ps1` |
| ROM path | `build/test-suite-build/mgba-suite/suite.gba` |
| SHA-256 | `073AC37DB89B791A589EC93853074043B31D0C931F43F4A69AFA7319248EC8BB` |

## Initial Run

Command:

```powershell
.\tools\run-mgba-suite.ps1
```

Result file:

```text
build\test-results\mgba-suite-20260508-001454.txt
```

Observed output:

| Metric | Value |
| --- | --- |
| ROM loaded | true |
| Requested steps | 100,000 |
| Attempted steps | 4 |
| Executed steps | 3 |
| Unsupported steps | 1 |
| Fetch failures | 0 |
| Stop reason | `unsupported_instruction` |
| Final PC | `0x080000e4` |
| Scheduler cycles | 21 |
| State hash | `10365407367178653334` |

Disassembly at the frontier:

```asm
080000e0 <start_vector>:
 80000e0: e3a00301  mov r0, #67108864    @ 0x04000000
 80000e4: e5800208  str r0, [r0, #520]   @ 0x04000208
```

Interpretation:

The suite enters the cartridge start vector and reaches the first hardware-register
write to `0x04000208` (`IME`). The current `CoreSession` execution path can load and
begin executing the public suite ROM, but it stops because direct IO-register writes
through the memory bus are not yet routed through the modeled IO register facade.

## Initial Frontier

The first engine task was to expose a `CoreSession` execution path that can service
bus-visible IO register writes needed by cartridge startup code, beginning with IME at
`0x04000208`, without weakening the existing explicit IO facade tests. The run below
records that this frontier has been cleared.

## After CoreSession IO Routing

Command:

```powershell
.\tools\run-mgba-suite.ps1
```

Result file:

```text
build\test-results\mgba-suite-20260508-003251.txt
```

Observed output:

| Metric | Value |
| --- | --- |
| ROM loaded | true |
| Requested steps | 100,000 |
| Attempted steps | 100,000 |
| Executed steps | 13 |
| Skipped steps | 99,987 |
| Unsupported steps | 0 |
| Fetch failures | 0 |
| Stop reason | `max_steps` |
| Final PC | `0x067347fc` |
| Scheduler cycles | 77 |
| State hash | `1800027709572478096` |

Interpretation:

The engine now routes the startup `STR r0, [r0, #520]` write to `0x04000208`
through the `CoreSession` IO path and no longer stops with an unsupported instruction
at the IME write. The suite still does not produce a pass/fail signal: it reaches the
step limit with almost all later instructions condition-skipped, so the next frontier is
control-flow/boot-environment fidelity and suite-terminal reporting rather than the
initial IO-register write.

## After Thumb Startup And Debug Aperture Work

Command:

```powershell
.\tools\run-mgba-suite.ps1 -MaxSteps 1000000 -TraceSteps 0
```

Result file:

```text
build\test-results\mgba-suite-20260508-020859.txt
```

Observed output:

| Metric | Value |
| --- | --- |
| ROM loaded | true |
| Detected save type | `sram32k` |
| Save backing configured | true |
| Save bytes | 32,768 |
| Requested steps | 1,000,000 |
| Attempted steps | 292,521 |
| Executed steps | 290,466 |
| Skipped steps | 2,055 |
| Unsupported steps | 0 |
| Fetch failures | 1 |
| Stop reason | `fetch_failed` |
| Final PC | `0x00000018` |
| Scheduler cycles | 1,039,809 |
| State hash | `11504255727260936800` |

Interpretation:

The suite now clears the initial ARM/Thumb startup path, the long SRAM clear loop, and
the mGBA/no$gba debug-console probes without hitting an unsupported instruction. This
run configured the suite's detected `SRAM_V` save backing in the runner.

The current frontier is BIOS IRQ dispatch: a modeled interrupt vectors into ARM state
at `0x00000018`, then instruction fetch fails because no BIOS bytes or HLE IRQ vector
implementation is present. This matches the roadmap's known deferred BIOS IRQ dispatch
work; it should be handled as a BIOS/HLE policy implementation, not by bundling BIOS
assets.

## After BIOS IRQ/SWI HLE And Runner Capture

Command:

```powershell
.\tools\run-mgba-suite.ps1 -MaxSteps 1000000 -TraceSteps 0
```

Result file:

```text
build\test-results\mgba-suite-20260508-032318.txt
```

Observed output:

| Metric | Value |
| --- | --- |
| ROM loaded | true |
| Detected save type | `sram32k` |
| Save backing configured | true |
| BIOS mode | `hle` |
| Requested steps | 1,000,000 |
| Attempted steps | 1,000,000 |
| Executed steps | 997,557 |
| Skipped steps | 2,443 |
| Unsupported steps | 0 |
| Fetch failures | 0 |
| Stop reason | `max_steps` |
| Final PC | `0x00000018` |
| Scheduler cycles | 98,725,590,998 |
| State hash | `1015351838219990654` |
| Debug bytes captured | 64 |
| SRAM text bytes captured | 32 |

Captured output:

```text
Game Boy Advance Test Suite
===
```

Interpretation:

The previous BIOS IRQ fetch failure is cleared. The runner now uses explicit BIOS HLE
mode, records mGBA debug output, exports SRAM text, and emits a stable JSON summary.
The current run remains measurable instead of stopping at the BIOS vector: it reaches
the 1,000,000-step budget with no unsupported instructions and no fetch failures.

Remaining runner gap:

Step-indexed keypad scripting is implemented and accepted by the runner, but the
recorded A-button scripts tried so far did not yet start the default `Memory tests`
suite. Until that input timing is calibrated or replaced with an equivalent automation
hook, suite-level pass/total counts remain unavailable.

## Generated Run 20260508-100144

| Field | Value |
| --- | --- |
| Suite request | `memory` |
| Target suite | `memory` |
| Status | `started_incomplete` |
| Pass/total | `Unavailable` |
| First failure | `FAIL: ROM load DMA0 16` |
| Stop reason | `fetch_failed` |
| Final PC | `0x16` |
| State hash | `11378496949970687853` |
| Text result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-100144.txt` |
| JSON result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-100144.json` |

Progression in this implementation pass:

- `Memory tests` now starts deterministically with `-Suite memory`.
- The result wrapper emits parsed JSON under `build/test-results/` and updates this doc
  on demand.
- The first unsupported frontier moved from `ROM load / U16 (unaligned)` to real
  suite-reported failures, then through `ROM store`.
- Current first suite-reported failure is `ROM load DMA0 16`; current hard stop is a
  BIOS/protected-region fetch at `0x16` while running later memory tests.

## Generated Run 20260508-102246

| Field | Value |
| --- | --- |
| Suite request | `memory` |
| Target suite | `memory` |
| Status | `started_incomplete` |
| Pass/total | `Unavailable` |
| First failure | `FAIL: ROM load swi B 32 (unaligned 1)` |
| Stop reason | `fetch_failed` |
| Final PC | `0x2` |
| State hash | `17783199741103767183` |
| Text result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-102246.txt` |
| JSON result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-102246.json` |


## Generated Run 20260508-102544

| Field | Value |
| --- | --- |
| Suite request | `memory` |
| Target suite | `memory` |
| Status | `started_incomplete` |
| Pass/total | `Unavailable` |
| First failure | `Stopped before END in Memory tests at ROM store / subtest_id=65535 (stop_reason=fetch_failed)` |
| Stop reason | `fetch_failed` |
| Final PC | `0x0` |
| State hash | `14003899380767385744` |
| Text result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-102544.txt` |
| JSON result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-102544.json` |


## Generated Run 20260508-102807

| Field | Value |
| --- | --- |
| Suite request | `memory` |
| Target suite | `memory` |
| Status | `started_incomplete` |
| Pass/total | `Unavailable` |
| First failure | `Stopped before END in Memory tests at ROM store / subtest_id=65535 (stop_reason=fetch_failed)` |
| Stop reason | `fetch_failed` |
| Final PC | `0x0` |
| State hash | `14003899380767385744` |
| Text result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-102807.txt` |
| JSON result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-102807.json` |


## Generated Run 20260508-102909

| Field | Value |
| --- | --- |
| Suite request | `memory` |
| Target suite | `memory` |
| Status | `started_incomplete` |
| Pass/total | `Unavailable` |
| First failure | `Stopped before END in Memory tests at ROM store / subtest_id=65535 (stop_reason=fetch_failed)` |
| Stop reason | `fetch_failed` |
| Final PC | `0x0` |
| State hash | `14003899380767385744` |
| Text result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-102909.txt` |
| JSON result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-102909.json` |


## Generated Run 20260508-103446

| Field | Value |
| --- | --- |
| Suite request | `memory` |
| Target suite | `memory` |
| Status | `started_incomplete` |
| Pass/total | `Unavailable` |
| First failure | `Stopped before END in Memory tests at ROM store / subtest_id=65535 (stop_reason=fetch_failed)` |
| Stop reason | `fetch_failed` |
| Final PC | `0x0` |
| State hash | `14003899380767385744` |
| Text result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-103446.txt` |
| JSON result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-103446.json` |


## Generated Run 20260508-104022

| Field | Value |
| --- | --- |
| Suite request | `memory` |
| Target suite | `memory` |
| Status | `started_incomplete` |
| Pass/total | `Unavailable` |
| First failure | `Stopped before END in Memory tests at ROM store / subtest_id=65535 (stop_reason=fetch_failed)` |
| Stop reason | `fetch_failed` |
| Final PC | `0x0` |
| State hash | `14003899380767385744` |
| Text result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-104022.txt` |
| JSON result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-104022.json` |


## Generated Run 20260508-104526

| Field | Value |
| --- | --- |
| Suite request | `memory` |
| Target suite | `memory` |
| Status | `started_incomplete` |
| Pass/total | `Unavailable` |
| First failure | `FAIL: ROM out-of-bounds load U8` |
| Stop reason | `unsupported_instruction` |
| Final PC | `0x8003288` |
| State hash | `5192665856967880725` |
| Text result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-104526.txt` |
| JSON result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-104526.json` |


## Generated Run 20260508-104842

| Field | Value |
| --- | --- |
| Suite request | `memory` |
| Target suite | `memory` |
| Status | `started_incomplete` |
| Pass/total | `Unavailable` |
| First failure | `FAIL: ROM out-of-bounds load U8` |
| Stop reason | `unsupported_instruction` |
| Final PC | `0x80032b8` |
| State hash | `14147882220119834095` |
| Text result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-104842.txt` |
| JSON result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-104842.json` |


## Generated Run 20260508-105202

| Field | Value |
| --- | --- |
| Suite request | `memory` |
| Target suite | `memory` |
| Status | `started_incomplete` |
| Pass/total | `Unavailable` |
| First failure | `FAIL: ROM out-of-bounds load U8` |
| Stop reason | `max_steps` |
| Final PC | `0x802ab26` |
| State hash | `3668384537738085752` |
| Text result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-105202.txt` |
| JSON result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-105202.json` |


## Generated Run 20260508-105248

| Field | Value |
| --- | --- |
| Suite request | `memory` |
| Target suite | `memory` |
| Status | `started_incomplete` |
| Pass/total | `Unavailable` |
| First failure | `FAIL: ROM out-of-bounds load U8` |
| Stop reason | `unsupported_instruction` |
| Final PC | `0x8002adc` |
| State hash | `9943322903445327547` |
| Text result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-105248.txt` |
| JSON result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-105248.json` |


## Generated Run 20260508-105901

| Field | Value |
| --- | --- |
| Suite request | `memory` |
| Target suite | `memory` |
| Status | `started_incomplete` |
| Pass/total | `Unavailable` |
| First failure | `FAIL: ROM out-of-bounds load U8` |
| Stop reason | `unsupported_instruction` |
| Final PC | `0x8002adc` |
| State hash | `9943322903445327547` |
| Text result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-105901.txt` |
| JSON result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-105901.json` |


## Generated Run 20260508-110304

| Field | Value |
| --- | --- |
| Suite request | `memory` |
| Target suite | `memory` |
| Status | `started_incomplete` |
| Pass/total | `Unavailable` |
| First failure | `FAIL: ROM out-of-bounds load U8` |
| Stop reason | `unsupported_instruction` |
| Final PC | `0x8002adc` |
| State hash | `9943322903445327547` |
| Text result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-110304.txt` |
| JSON result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-110304.json` |


## Generated Run 20260508-110730

| Field | Value |
| --- | --- |
| Suite request | `memory` |
| Target suite | `memory` |
| Status | `started_incomplete` |
| Pass/total | `Unavailable` |
| First failure | `FAIL: ROM out-of-bounds load U8` |
| Stop reason | `unsupported_instruction` |
| Final PC | `0x8002adc` |
| State hash | `12607592897233671954` |
| Text result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-110730.txt` |
| JSON result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-110730.json` |


## Generated Run 20260508-111540

| Field | Value |
| --- | --- |
| Suite request | `memory` |
| Target suite | `memory` |
| Status | `started_incomplete` |
| Pass/total | `Unavailable` |
| First failure | `FAIL: ROM out-of-bounds load U8` |
| Stop reason | `unsupported_instruction` |
| Final PC | `0x80222c0` |
| State hash | `14344146234020658692` |
| Text result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-111540.txt` |
| JSON result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-111540.json` |


## Generated Run 20260508-111846

| Field | Value |
| --- | --- |
| Suite request | `memory` |
| Target suite | `memory` |
| Status | `started_incomplete` |
| Pass/total | `Unavailable` |
| First failure | `FAIL: ROM out-of-bounds load U8` |
| Stop reason | `unsupported_instruction` |
| Final PC | `0x802ba3c` |
| State hash | `14659035261027695781` |
| Text result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-111846.txt` |
| JSON result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-111846.json` |


## Generated Run 20260508-112204

| Field | Value |
| --- | --- |
| Suite request | `memory` |
| Target suite | `memory` |
| Status | `complete` |
| Pass/total | `1326/1552` |
| First failure | `FAIL: ROM out-of-bounds load U8` |
| Stop reason | `until_output` |
| Final PC | `0x8006328` |
| State hash | `15135001198483552090` |
| Text result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-112204.txt` |
| JSON result | `C:\Workspace\Project_Android\GBA_Emulator\build\test-results\mgba-suite-20260508-112204.json` |

