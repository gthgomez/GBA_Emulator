# mGBA Suite Finish Roadmap

Status: measurement roadmap implemented; accuracy frontiers continue in Roadmap 2
Date: 2026-05-08

This roadmap turns the current mGBA-suite frontier into ordered engineering work. The
goal is not only to run more instructions; the goal is to produce repeatable suite-level
pass/fail data from the legal, locally built mGBA Game Boy Advance Test Suite recorded
in `docs/external-test-suite-baseline.md`.

Current baseline:

- ROM: `build/test-suite-build/mgba-suite/suite.gba`
- SHA-256: `073AC37DB89B791A589EC93853074043B31D0C931F43F4A69AFA7319248EC8BB`
- Latest result: `build\test-results\mgba-suite-20260508-032318.txt`
- Current stop: no stop before the configured step budget. The runner reaches
  `1,000,000` attempted steps with `0` unsupported instructions and `0` fetch failures,
  while repeatedly passing through the HLE BIOS IRQ path.
- Current proof point: BIOS IRQ fetch at `0x00000018` is no longer a hard failure;
  mGBA debug output and SRAM text are captured into the result file.

Current Roadmap 1 implementation status:

- Individual suite automation is available with `tools/run-mgba-suite.ps1 -Suite <name>`.
- `-Suite all` now iterates every known suite in source-menu order and writes aggregate
  JSON/Markdown artifacts with a compatibility hash.
- `tools/run-credibility-matrix.ps1` records suite rows, failure categories, artifacts,
  and optional performance-gate status.
- `tools/run-mgba-suite.ps1` now applies suite-specific default step budgets when
  `-MaxSteps` is omitted, so long-running suites such as `timers`, `timing`, `dma`, and
  `video` are less likely to produce false red max-step frontiers.
- Suite JSON now names the active source test for timing/timers/timer-IRQ/BIOS
  math/DMA frontiers when the suite stops before `END:`.
- `tools/mgba-suite-green-baseline.json` records the currently verified green targets.
- `tools/run-credibility-matrix.ps1 -FailOnRegression` fails only when one of those
  verified-green targets regresses, while known-red targets remain visible.
- `-UpdateDocs` appends generated individual or all-suite summaries to
  `docs/mgba-suite-test-results.md`.

## Finish Criteria

The suite is considered locally measurable when all of these are true:

- The runner can start the ROM, configure detected SRAM backing, and reach the suite
  menu without fetch failures or unsupported instructions.
- The runner can drive deterministic keypad input or an equivalent local automation path
  to start one suite and, later, all suites.
- The runner captures mGBA/no$gba debug output and/or SRAM result text into a parseable
  result file.
- Each suite produces a machine-readable pass/total summary with command, ROM checksum,
  step budget, stop reason, final PC, state hash, and captured output.
- Any remaining failures are recorded as compatibility failures, not runner blind spots.

## Milestone 1: BIOS IRQ Dispatch HLE

Why this is next:

The current run enters ARM IRQ state at `0x00000018`. On real GBA hardware, BIOS code
handles the IRQ vector and dispatches to the user IRQ handler installed by libgba. The
project cannot bundle BIOS bytes, so this must be an explicit HLE path.

Implement:

- Add a no-BIOS/HLE IRQ-dispatch path that handles the GBA BIOS IRQ convention without
  requiring BIOS assets.
- Preserve the existing no-BIOS policy for ordinary BIOS reads; do not make BIOS memory
  silently contain invented instructions.
- Route IRQ dispatch through tested state transitions: IRQ mode entry, banked IRQ
  `SP`/`LR`, SPSR preservation, user IRQ handler dispatch, and return behavior.
- Add focused tests for VBlank IRQ dispatch from a synthetic libgba-style handler
  fixture.
- Keep caller-provided BIOS as a future mode; do not scan, download, or bundle BIOS.

Done when:

- `.\tools\run-mgba-suite.ps1 -MaxSteps 1000000 -TraceSteps 0` no longer stops at
  `final_pc=0x00000018`.
- Core tests include positive IRQ-HLE dispatch and negative no-BIOS read behavior.

## Milestone 2: BIOS SWI HLE For Suite Runtime

Why this follows IRQ dispatch:

The suite uses libgba runtime calls such as `VBlankIntrWait()`. The test sources also
include BIOS math tests and SWI fixtures, including Div, Sqrt, ArcTan, and CpuSet.

Implement:

- HLE `VBlankIntrWait` / `IntrWait` behavior sufficient for libgba's wait loop.
- HLE `Halt`/wake semantics only as needed by the wait services, with bounded tests.
- BIOS math SWIs used by the suite: Div, Sqrt, ArcTan/ArcTan2 as source-backed behavior
  is implemented.
- CpuSet/CpuFastSet HLE if the suite reaches those fixtures.
- Clear unsupported-service reporting for any BIOS SWI not yet implemented.

Done when:

- The suite can pass startup wait calls and remain in cartridge code instead of trapping
  into missing BIOS vectors.
- BIOS-HLE tests cover each implemented service and at least one unimplemented-service
  rejection.

## Milestone 3: Runner Automation And Result Capture

Why this is needed:

After startup, the suite shows a menu and waits for keypad input. It only runs a suite
when A is pressed. It also writes logs through mGBA/no$gba debug apertures and stores
text in SRAM.

Implement:

- Add runner input scripting: frame/step-indexed keypad masks, beginning with A press
  to start the selected suite.
- Add a deterministic "run selected suite" mode. First target should be the default
  selected suite, `Memory tests`.
- Capture writes to the debug string/flags apertures:
  - mGBA string region around `0x04FFF600`
  - mGBA flags at `0x04FFF700`
  - no$gba output pointer/char region around `0x04FFFA10`/`0x04FFFA1C`
- Export SRAM result text after the run, since `savprintf` appends report lines to
  SRAM.
- Parse `BEGIN`, `END`, pass counts, and suite names into a JSON or stable text summary.

Done when:

- A runner command can start at least one suite without manual emulator UI.
- The result file includes captured log text and an extracted pass/total summary.

## Milestone 4: First Suite Pass/Fail Baseline

Why this comes before broad accuracy work:

We need a small, repeatable measurement loop before trying to satisfy every suite.

Implement:

- Run the default `Memory tests` suite end-to-end with a generous step/frame budget.
- Record whether failures are emulator accuracy failures, runner automation failures,
  missing HLE services, or unsupported CPU/memory behavior.
- Add a document section per suite run with:
  - command
  - result file
  - pass/total
  - first failing test name if available
  - current blocker category

Done when:

- `docs/mgba-suite-test-results.md` has the first real suite-level pass/fail table.
- The runner can reproduce that result from a clean build artifact.

## Milestone 5: CPU And Memory Correctness Passes

Expected suite pressure:

The public suite includes memory, IO read, shifter, carry, multiply, multiply-long,
load/store, LDM/STM, and edge-case tests. Many of these probe behavior beyond the
current bounded local seeds.

Implement as frontiers appear:

- ARM block data transfer variants the suite depends on, including boundary behavior.
- ARM/Thumb load/store edge cases, including unaligned and cross-region behavior.
- Multiply and multiply-long cycle/result fidelity where suite failures identify gaps.
- Shifter/carry flag edge cases not covered by current local tests.
- BIOS/open-bus memory-read policy where the suite expects latched or protected-region
  behavior, documented separately from unsupported reads.

Done when:

- Memory, shifter/carry, multiply, and core CPU suites have recorded pass/total data.
- Remaining failures are listed by exact test name and feature gap.

## Milestone 6: Timer, IRQ, DMA, And Timing Suites

Expected suite pressure:

The suite includes timers, timer IRQ, timing, and DMA tests. These will require tighter
coordination between scheduler cycles, PPU events, DMA bus occupancy, and interrupt
delivery.

Implement:

- Timer reload/count-up/IRQ behavior needed by suite tests.
- Precise IF/IE/IME behavior around interrupt request, acknowledge, and dispatch.
- DMA immediate/HBlank/VBlank timing, source/destination update modes, repeat behavior,
  and BIOS/ROM/open-bus source effects as required by failures.
- WAITCNT/prefetch refinements based on timing-suite deltas.
- Stable cycle-accounting diagnostics in the runner for first failing timing tests.

Done when:

- Timer, timer IRQ, DMA, and timing suites produce pass/total summaries.
- Any non-passing timing cases are categorized as known cycle-fidelity gaps.

Latest phase status:

- `timer-irq` is green: `90/90` in
  `build/test-results/mgba-suite-20260509-173711.json`.
- `timers` is green at `936/936` in
  `build/test-results/mgba-suite-20260511-231709.json`, with no unsupported
  instructions or fetch failures.
- `timing` is green at `2020/2020` in
  `build/test-results/mgba-suite-20260512-052423.json`, with no unsupported
  instructions or fetch failures.

## Milestone 7: IO, SIO, And Video Suite Support

Expected suite pressure:

The suite includes IO-read, SIO-read, SIO-timing, and video tests. Some of this is not
required for basic ROM execution but is required for suite completion.

Implement:

- IO readback semantics for registers the suite probes.
- Serial/SIO register behavior sufficient for read and timing tests, or explicit
  documented unsupported results if source-backed behavior is deferred.
- PPU timing and video state needed by the video suite.
- Debuggable first-failure capture for IO/SIO/video tests.

Done when:

- IO-read, SIO-read, SIO-timing, and video suites have suite-level pass/total data.
- Remaining gaps map to named IO/SIO/video hardware behaviors rather than unknown
  runner state.

## Milestone 8: All-Suite Automation Gate

Implement:

- A runner mode that iterates every suite in `src/main.c` order:
  - Memory tests
  - IO read tests
  - Timing tests
  - Timers
  - Timer IRQ
  - Shifter tests
  - Carry tests
  - Multiply long tests
  - BIOS math tests
  - DMA tests
  - SIO read tests
  - SIO timing tests
  - Misc edge tests
  - Video tests
- A stable summary artifact under `build/test-results/`.
- A docs update command or script that appends the latest summary to
  `docs/mgba-suite-test-results.md`.
- A regression gate option that can fail CI/local checks only when a previously passing
  suite regresses, not merely because known unimplemented suites still fail.

Done when:

- One command produces an all-suite summary with pass/total per suite and a final
  compatibility hash.
- `tools/run-core-tests.ps1`, `tools/run-core-benchmarks.ps1`,
  `tools/check-core-performance-regression.ps1`, `tools/check-release-readiness.ps1`,
  and the mGBA suite runner all have documented expected outcomes.

Implemented commands:

```powershell
.\tools\run-mgba-suite.ps1 -Suite all -MaxSteps 20000000 -TraceSteps 0
.\tools\run-mgba-suite.ps1 -Suite all -MaxSteps 20000000 -TraceSteps 0 -UpdateDocs
.\tools\run-credibility-matrix.ps1 -FailOnRegression
```

Expected outcome:

- `run-mgba-suite.ps1 -Suite all` may report `RED` overall while hardware suites are
  still under implementation, but it must produce an aggregate JSON artifact, Markdown
  artifact, per-suite rows, and a compatibility hash.
- `run-credibility-matrix.ps1 -FailOnRegression` should pass when currently verified
  green suites remain green, even if known-red suites are included in the matrix.
- `run-credibility-matrix.ps1 -FailOnRed` remains the stricter gate for an explicitly
  requested all-green target list.
- Default matrix `-Suites` and `tools/mgba-suite-green-baseline.json` list upstream suites
  plus embedded aliases (`loadstore`, `ldmia`, `stmia`). Video oracle aliases are run
  separately; a video-only matrix with `-FailOnRegression` reports false `REGRESSION`
  because baseline targets are missing from that run, not because video evidence regressed.

## Recommended Immediate Work

Roadmap 1 is now a measurement system rather than the main blocker. Continue Roadmap 2
from the next known red subsystem groups: load/store and LDM/STM evidence, then
IO/SIO/video hardware behavior. Keep Roadmap 1 commands as the regression harness while
hardware behavior is fixed.
