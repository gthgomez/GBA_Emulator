# Benchmark Comparison Roadmap

Status: active implementation roadmap
Date: 2026-05-08

## Goal

Build a repeatable accuracy and performance comparison system that lets us make
evidence-backed claims about the engine against top open-source GBA emulators without
overstating compatibility.

The target is not a marketing benchmark. The target is an engineering matrix where every
performance number is paired with a correctness signal, a deterministic artifact, and a
named subsystem status.

## Purpose Of The Tests

The benchmark and suite work has three jobs:

- Prove correctness before speed claims. A fast run only counts if the same workload
  produces the expected suite result, checksum, state hash, or framebuffer hash.
- Localize regressions by subsystem. Failures should point at memory, DMA, BIOS, CPU
  instruction semantics, timing/IRQ, PPU, APU, cartridge/save, Android runtime, or
  benchmark integrity.
- Make comparisons fair. External comparisons should run on the same host, with the
  same workload, build mode, frame/cycle budget, and correctness gate.

## Green Definition

A target is green only when all of these are true:

- The command exits successfully.
- The result artifact is written under `build/test-results/`.
- The parsed status reaches the intended end condition.
- The pass count equals the total count, when the target has pass/total output.
- Parsed failure count is zero.
- Unsupported instructions and fetch failures are zero for mGBA-suite runs.
- Performance rows pass both checksum and median-threshold checks.

Anything else is red or yellow. A partial run with useful output is progress, but it is
not green.

## Current Verified Frontier

Latest verified green targets:

- Core verifier binaries: `tools/run-core-tests.ps1`
- mGBA `memory`: `1552/1552`
- mGBA `dma`: `1256/1256`
- mGBA `bios-math`: `615/615`
- mGBA `shifter`: `140/140`
- mGBA `carry`: `93/93`
- mGBA `multiply-long`: `72/72`
- Synthetic core performance report: PASS, including deterministic checksums

Latest matrix artifacts:

- Green frontier:
  `build/test-results/credibility-matrix-20260508-152541.json`
- Next-target frontier:
  `build/test-results/mgba-suite-20260508-151941.json`

Latest next-target status:

- `timing`: red, reaches real timing failures after the WAITCNT word-write hard stop was
  cleared; first failure is `Calibration ARM/ROM ...`.
- `timers`: red, now reaches timer test 22 before the default cap after HLE IRQ dispatch
  was changed to execute the real IWRAM IRQ handler path. The automatic IRQ path now
  models a short recognition latency and treats HLE `IntrWait`-observed IRQs as
  latency-ready, which fixes the first `0b, 0x0001` one-IRQ cases. Remaining failures
  are timer cycle/read alignment issues and multi-IRQ stop timing.
- `timer-irq`: red, improved from `0/90` to `4/90`; `FFFE` with 0-3 nops now passes.
  Remaining failures show two distinct gaps: timer reads are still sampled at coarse
  instruction boundaries, and the HLE IRQ path undercounts the BIOS/libgba prologue
  before the user handler stops the timer.

Current credibility statement:

Memory, DMA, BIOS math, and the contained ARM shifter/carry/multiply-long suites are
strong. Whole-emulator accuracy is not yet proven against the top open-source engines
because timing/IRQ, broader load-store/LDM/STM behavior, PPU, APU, save/cart edge cases,
and ROM-workload comparisons still need green evidence.

## Phase 1: Credibility Matrix Runner

Purpose:

Provide one command that runs the current public-suite frontier and performance gate,
then emits a single JSON and Markdown matrix.

Implemented target:

```powershell
.\tools\run-credibility-matrix.ps1
```

The matrix records:

- suite name
- green/red status
- pass/total
- first failure
- failure categories
- runner stop reason
- artifact paths
- performance gate status

Done when:

- The matrix can be run locally without hand-copying results.
- The already-green suites stay green.
- The next red suite is visible by subsystem rather than buried in raw logs.

## Phase 2: Timing, Timers, And IRQ

Purpose:

Prove that scheduler-visible time, timer reload/count-up behavior, and interrupt
delivery are accurate enough to survive public-suite pressure.

Targets:

- `timing`
- `timers`
- `timer-irq`

Likely implementation areas:

- timer overflow reload timing
- IF/IE/IME acknowledge ordering
- IRQ entry timing and HLE return behavior
- scheduler cycle accounting
- DMA/APU side effects triggered by timer events

Green gate:

```powershell
.\tools\run-credibility-matrix.ps1 -Suites timing,timers,timer-irq -SkipPerformance -FailOnRed
```

## Phase 3: ARM Instruction Correctness

Purpose:

Close the CPU semantic gaps that top open-source engines already handle routinely.

Targets:

- `shifter`
- `carry`
- `multiply-long`
- load/store and LDM/STM coverage from available suite frontiers or local fixtures

Likely implementation areas:

- shifter carry-out edge cases: green for current mGBA suite
- flag-preserving and flag-writing ALU behavior: green for current carry suite
- multiply/multiply-long signedness and accumulation cases: green for current
  multiply-long suite, including ARM7TDMI carry-flag quirk for `MULLS`
- unaligned load/store behavior
- block-transfer writeback and base-register-in-list behavior

Green gate:

```powershell
.\tools\run-credibility-matrix.ps1 -Suites shifter,carry,multiply-long -SkipPerformance -FailOnRed
```

## Phase 4: Broader Hardware Suites

Purpose:

Expand from CPU/bus credibility into emulator-wide hardware behavior.

Targets:

- `io-read`
- `sio-read`
- `sio-timing`
- `misc-edge`
- `video`

Likely implementation areas:

- IO register readback semantics
- serial/SIO register behavior
- PPU timing and video state
- video memory access quirks under active rendering
- edge-case open-bus and protection behavior

Green gate:

```powershell
.\tools\run-credibility-matrix.ps1 -Suites io-read,sio-read,sio-timing,misc-edge,video -SkipPerformance -FailOnRed
```

## Phase 5: External Performance Comparison

Purpose:

Compare against top open-source engines fairly after accuracy gates exist.

Required before claiming comparison:

- fixed ROM/workload set with legal redistribution status documented
- identical host machine and build mode
- fixed cycle/frame budgets
- final state hash or framebuffer/audio checksum
- speed metrics paired with correctness metrics

Metrics:

- frames per second or frames per host second
- ns per emulated instruction/cycle where available
- p50/p95 frame time
- host CPU time
- allocation count or peak memory when measurable
- final state hash, framebuffer hash, and audio checksum

Comparison engines to study locally:

- mGBA
- NanoBoyAdvance
- SkyEmu
- SameBoy is Game Boy/Game Boy Color rather than GBA, so use it only as a tooling
  reference, not a direct GBA performance target.

## Phase 6: Regression Gate Policy

Purpose:

Keep development honest without blocking known-unimplemented areas.

Rules:

- Previously green targets must not regress.
- Red targets may remain red while actively being implemented, but the first failure and
  failure category must be tracked.
- Performance baseline updates require a checksum explanation, not only faster/slower
  timing.
- External comparison claims require an accompanying matrix artifact.

Recommended local gates:

```powershell
.\tools\run-core-tests.ps1
.\tools\run-credibility-matrix.ps1 -Suites memory,bios-math,dma -PerformanceRuns 3 -FailOnRed
.\tools\run-credibility-matrix.ps1 -Suites timing,timers,timer-irq,shifter,carry,multiply-long -SkipPerformance
```

Note: the matrix default step cap is `20,000,000` so slower deterministic menu input
does not create false red results for large suites such as DMA.

## How We Get It All Green

Work in this loop:

1. Run the matrix for the next red group.
2. Pick the first failing suite category, not the noisiest downstream failure.
3. Add or update a focused local core test that reproduces the hardware rule.
4. Fix the smallest core behavior needed for that rule.
5. Run the local core tests.
6. Rerun the affected mGBA suite.
7. Rerun the green-frontier matrix to prove no regression.
8. Only then update docs or baselines.

The next implementation group remains timing/timers/IRQ. The CPU shifter/carry/
multiply-long group is green; after timer timing is credible, move to load-store,
LDM/STM, IO/SIO/video, and all-suite automation.
