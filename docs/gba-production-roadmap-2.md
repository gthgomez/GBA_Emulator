# GBA Production Roadmap 2

Status: second roadmap, production-engine planning layer
Date: 2026-05-08

## Relationship To Roadmap 1

Roadmap 1 is `docs/mgba-suite-finish-roadmap.md`. It is the test-frontier roadmap:
make the local mGBA suite measurable, remove runner blind spots, and turn suite output
into pass/fail data.

This document is Roadmap 2. It assumes Roadmap 1 remains the immediate measurement
gate, then plans the broader production-grade emulator work needed to compete with
serious GBA emulators instead of merely booting test ROMs.

Roadmap 1 answers: "Can we measure correctness with the mGBA suite?"

Roadmap 2 answers: "What are we missing for a production-level GBA engine, and how do
we build toward best-in-class quality?"

## Research Snapshot

Sources checked on 2026-05-08:

- mGBA GitHub README: `https://github.com/mgba-emu/mgba`
- mGBA FAQ: `https://mgba.io/faq.html`
- mGBA accuracy article: `https://mgba.io/2017/04/30/emulation-accuracy/`
- GBATEK markdown fork hosted by mGBA: `https://mgba-emu.github.io/gbatek/`
- Tonc DMA reference: `https://gbadev.net/tonc/dma.html`
- Tonc timer reference: `https://gbadev.net/tonc/timers.html`
- gbadev audio register reference: `https://gbadev.net/gbadoc/audio/registers.html`
- NanoBoyAdvance project listing/news via gbadev: `https://www.gbadev.org/`

Research conclusion:

Production-grade GBA emulation is not a single feature. It is a correctness envelope:
ARM7TDMI behavior, memory/open-bus behavior, PPU timing, DMA bus arbitration, timers,
interrupts, audio FIFOs, BIOS services, cartridge backup media, peripherals, save
states, debugging, deterministic test automation, and platform UX must all agree well
enough that commercial games, homebrew, timing tests, and edge-case suites fail only
for known, categorized reasons.

mGBA's public positioning emphasizes accuracy plus speed and features such as save-type
detection, sensors, RTC, local link cable, built-in BIOS HLE, optional external BIOS,
debugging, rewind, patches, cheats, and archive loading. NanoBoyAdvance is a useful
competitive target because it prioritizes cycle-accuracy and lists RTC, solar sensor,
debug viewers, save states, remappable controls, archive loading, and HQ audio as core
features.

## Current Engine Position

Observed from Roadmap 1 and local runs:

- The mGBA suite ROM loads and SRAM backing is detected/configured.
- BIOS IRQ fetch failure at `0x00000018` is cleared by explicit BIOS HLE mode.
- The runner captures mGBA debug output and SRAM text.
- A 1,000,000-step run currently reaches `max_steps` with `0` unsupported instructions
  and `0` fetch failures.
- Step-indexed keypad scripting exists, but starting the default suite is not yet
  calibrated.

Current strategic blocker:

We still do not have suite-level pass/total data. Until Roadmap 1 produces named
failures, Roadmap 2 implementation must prioritize measurement, traceability, and
compatibility categories over broad speculative hardware work.

## What Is Missing For Production Level

### Accuracy Core

- Complete ARM and Thumb instruction semantics, including undefined/edge behavior,
  CPSR/SPSR transfers, banked registers, exception return paths, unaligned access, and
  data-dependent cycle costs.
- Memory bus behavior beyond simple mapped reads: open bus, BIOS protection,
  prefetch, WAITCNT, sequential/nonsequential timings, mirroring, invalid accesses, and
  region-specific quirks.
- Interrupt correctness: IE/IF/IME ordering, acknowledgement timing, nested IRQ
  masking, HALT wake behavior, and BIOS/libgba conventions.

### PPU And Video

- Full background modes 0-5, affine BGs, mosaic, blending, windows, OBJ priority,
  OBJ affine transforms, bitmap modes, forced blank, VBlank/HBlank/VCount timing, and
  line-based rendering.
- VRAM/OAM/palette access restrictions and contention timing during active drawing.
- Debug viewers for palettes, tiles, maps, sprites, affine matrices, layers, and
  per-scanline state.

### DMA, Timers, And Scheduling

- DMA start modes: immediate, VBlank, HBlank, special/audio FIFO.
- DMA priority, CPU halt during DMA, repeat behavior, source/destination update modes,
  word-count edge cases, ROM/RAM/open-bus sources, and FIFO refill behavior.
- Timers: reload timing, cascading/count-up, IRQ behavior, sound timer coupling, and
  exact 280,896-cycle frame cadence.
- Scheduler diagnostics that can explain the first timing divergence, not only the
  final crash.

### Audio

- DMG PSG channels: square 1/2, wave, noise, envelopes, sweeps, length counters.
- Direct Sound A/B FIFOs, timer-driven sampling, DMA refill, bias, mixing, clipping,
  resampling, latency, and stable output buffers.
- A quality mode that improves output without changing emulated timing.

### BIOS And System Services

- Legal BIOS HLE for required services: IRQ dispatch, SWI wait services, math, CpuSet,
  decompression, affine helpers, reset, checksum, and unimplemented-service reporting.
- Optional external BIOS mode with checksum/custody, never bundled.
- Clear separation between HLE behavior and protected BIOS-memory read policy.

### Cartridge And Peripheral Compatibility

- Save media: SRAM/FRAM, Flash 64K/128K, EEPROM 512B/8KB serial protocol, import/export,
  auto-detection, and game database override.
- Cartridge hardware: RTC, rumble, tilt/gyro, solar sensor, e-Reader if chosen later.
- ROM loading: `.gba`, ZIP/7z, patch formats IPS/UPS/BPS, metadata, checksum catalog,
  and legal dumping workflow notes.
- Link cable/local multiplayer only after single-core determinism is solid.

### Production UX And Tooling

- Save states with versioning, compatibility checks, screenshots/thumbnails, and
  migration tests.
- Deterministic replay: input log, frame hash, state hash, trace hash.
- Debugger: CPU registers, memory viewer, disassembler, breakpoints/watchpoints,
  IO/PPU/DMA/timer inspectors, trace export.
- Compatibility database: per-ROM status, required save type, known issue, last tested
  engine commit, result artifacts.
- Android runtime: low-latency audio, frame pacing, controller mapping, touch overlay,
  scoped-storage import/export, battery/performance modes, crash-safe saves.

## Competitive Strategy: How To Get Above The Rest

Do not try to beat mature emulators by claiming generic accuracy. Beat them in
evidence, developer workflow, and explainability.

Differentiators to build:

- Evidence-first compatibility: every compatibility claim links to a command, ROM hash,
  state hash, frame hash, trace hash, and captured output.
- First-failure diagnostics: a failed suite/game should produce "first divergence:
  DMA3 HBlank timing at scanline 42" rather than "game broken."
- Dual-lane engine modes: Accurate mode for tests and hard games; Fast mode for normal
  play, with documented compatibility tradeoffs.
- Legal BIOS discipline: robust HLE plus optional user BIOS, never bundled.
- Android-native polish: instant import, reliable saves, controller/touch profiles,
  audio latency controls, thermal-aware performance gates.
- Developer-grade debug tools before flashy filters.

## Implementation Plan

### Phase 0: Measurement Lock

Goal:
Finish Roadmap 1 enough that every later phase is driven by named failures.

Implement:

- Calibrate keypad automation or add a deterministic runner hook that starts the
  selected suite without manual UI.
- Add `--suite memory`, `--suite all`, and `--until-output END` runner modes.
- Parse debug/SRAM text into `{ suite, pass, total, first_failure, stop_reason }`.
- Emit JSON result artifacts under `build/test-results/`.
- Add a doc updater that appends the latest suite table to
  `docs/mgba-suite-test-results.md`.

Exit gate:

- One command starts `Memory tests` and produces a pass/total or first named failure.
- Known runner limitations are separated from emulator failures.

### Phase 1: BIOS HLE Completion

Goal:
Remove BIOS as a blind spot without bundling BIOS assets.

Implement:

- Complete SWIs reached by the suite: IntrWait, VBlankIntrWait, Halt, Div, DivArm,
  Sqrt, ArcTan, ArcTan2, CpuSet, CpuFastSet.
- Add decompression and affine SWIs when commercial compatibility demands them.
- Add per-service tests and negative unimplemented-service reporting.
- Add optional external BIOS mode with checksum logging.

Exit gate:

- BIOS math suite produces pass/total data.
- Startup and wait loops no longer rely on synthetic timing shortcuts without labels.

### Phase 2: CPU And Memory Correctness

Goal:
Pass or categorize all CPU/memory suite failures.

Implement:

- ARM/Thumb block transfer edge cases.
- Unaligned load/store and cross-region behavior.
- Multiply/multiply-long timing and flags.
- Shifter/carry corner cases.
- Open-bus and protected-region read policy.
- WAITCNT and prefetch model refinements.

Exit gate:

- Memory, shifter, carry, multiply, multiply-long, load/store, LDM/STM suites have
  pass/total data and named residual failures.

### Phase 3: Scheduler, Timers, IRQ, DMA

Goal:
Make timing failures explainable and reducible.

Implement:

- Exact timer reload/count-up/IRQ semantics.
- DMA priority/arbitration and CPU bus halt.
- HBlank/VBlank/special DMA timing.
- Sound FIFO DMA refill.
- IRQ acknowledge/order diagnostics.
- Frame/scanline event trace.

Exit gate:

- Timer, timer IRQ, DMA, and timing suites produce stable pass/total summaries.
- First timing failure includes event trace and cycle delta.

### Phase 4: PPU Accuracy

Goal:
Render commercial games and video tests with correct ordering, timing, and effects.

Implement:

- Mode 0-5 renderer completion.
- Windows, blending, mosaic, OBJ priority, affine BG/OBJ.
- Per-scanline register latching.
- VRAM/OAM/palette contention.
- Frame hash capture for regression tests.

Exit gate:

- Video suite has pass/total data.
- A curated visual corpus renders with stable frame hashes.

### Phase 5: Audio Production

Goal:
Correct audio timing plus good user-perceived output.

Implement:

- Complete PSG channel units.
- Direct Sound FIFOs and timer/DMA coupling.
- Mixer, resampler, clipping, buffering, latency controls.
- Audio hash/golden tests for synthetic fixtures.

Exit gate:

- Audio fixtures pass deterministic sample hashes.
- Android playback runs without underruns at target devices/settings.

### Phase 6: Cartridge And Peripheral Layer

Goal:
Support real game save/peripheral requirements.

Implement:

- EEPROM serial protocol and detection.
- Flash/SRAM/FRAM import/export hardening.
- RTC, rumble, solar, tilt/gyro abstractions.
- ROM patch/archive loading.
- Compatibility database overrides.

Exit gate:

- Known RTC/save/peripheral games are categorized with tested save behavior.
- Saves survive app restarts, imports, exports, and save-state transitions.

### Phase 7: Production Runtime

Goal:
Turn the engine into a reliable Android product.

Implement:

- Frame pacing and audio synchronization.
- Touch/controller mapping profiles.
- Battery/thermal performance modes.
- Save-state UI and crash-safe persistence.
- ROM library metadata, legal import workflow, and user-owned content language.
- Crash reports stripped of private ROM/save data.

Exit gate:

- Android beta checklist passes on target devices.
- No save corruption in repeated power-cycle/import/export tests.

### Phase 8: Above-The-Rest Tooling

Goal:
Build the features that make the engine unusually inspectable.

Implement:

- Deterministic replay files.
- Trace diff against prior builds.
- IO/PPU/DMA/timer visual inspectors.
- Per-game compatibility reports generated from local evidence.
- "Why did this fail?" diagnostics for every suite/game stop.

Exit gate:

- A failed ROM/test produces a compact artifact bundle that a developer can use to
  reproduce and localize the issue without guessing.

## Ordering Rule

Do not broaden into flashy product features until Roadmap 1 produces suite-level
measurements. The engine gets above the rest by being more measurable first, then more
accurate, then more polished.

Immediate next action:

Finish Phase 0 by making `Memory tests` start automatically and produce a parsed
result artifact. After that, every implementation sprint should begin from the first
named failing test.
