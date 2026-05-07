# GBA Emulator Production Engine Roadmap

Status: active reference roadmap after Phase 58
Date: 2026-05-07
Scope anchor: `Project_Android/GBA_Emulator`
State anchor: `docs/ralph-state/gba-emulator-plan-2026-05-05.md`

## North Star

Build a portable C++17 Game Boy Advance core that can run legal homebrew and test ROMs
deterministically at full speed on midrange Android hardware, with verified CPU timing,
stable frame/audio pacing, save persistence, save states, and benchmarked regressions.

This document is an implementation roadmap, not a compatibility or production-readiness
claim. The current engine is a strong scaffold with many focused verifiers; it is not
yet a production emulator and should not be publicly compared as superior to mature GBA
emulators until accuracy, compatibility, device, thermal, and legal evidence exists.

## Current Baseline

The current Ralph state records Phase 58 complete. The core has:

- A portable C++17 core layout.
- Focused ARM/Thumb, memory, timer, IRQ, DMA, PPU timing, renderer seed, APU Direct
  Sound seed, save-backing, and `CoreSession` verifiers.
- Explicit in-memory Game Pak ROM loading for caller-provided legal bytes.
- Thumb memory/stack/SWI program-coverage seeds and ARM PSR/SWI/SWP/RRX/condition
  completeness seeds.
- Mode-banked register, modeled exception-return, and scheduler HALT wake seeds.
- An explicit no-BIOS/caller-provided-BIOS/HLE-requested SWI policy boundary.
- Memory-bus correctness hardening for unaligned word-read rotation, explicit unmodeled
  read policies, video access-window hooks, and hard/soft reset semantics.
- Pipeline/prefetch timing seed for selected PC-visible reads, WAITCNT-tied Game Pak
  prefetch metadata, 128 KiB boundary timing, and a prefetch/cart-loop benchmark row.
- A legal-program harness for explicit caller-provided in-memory byte blobs, expected
  state assertions, clean stop-reason reporting, and future fixture-admission rules.
- Local synthetic benchmarks, output-shape gate, and regression thresholds.
- No Android app, JNI, Gradle, CMake, ROM scanner, downloader, BIOS image, bundled ROM,
  app-store work, or public compatibility claim.

## Standing Rules For Every Phase

Every phase must preserve these rules unless Jonathan explicitly re-aims the work:

- Work inside `Project_Android/GBA_Emulator/**` and the Ralph state file only.
- Preserve all prior tests; do not weaken verifiers to make a phase pass.
- No BIOS images, copyrighted ROMs, copyrighted assets, downloaders, storage scanners,
  app-store work, deploys, public legal claims, third-party emulator code, or external
  fixtures unless explicitly authorized.
- Run `tools/run-core-tests.ps1` after every code phase.
- Run `tools/run-core-benchmarks.ps1` and `tools/check-core-performance-regression.ps1`
  when CPU, memory, scheduler, PPU, APU, DMA, timing, save-state, or benchmark paths
  change.
- Report exact command, cwd, exit code, timestamp, output excerpt, and truncation status.
- Update the Ralph state with completed phase status, evidence, and next unlock criteria.

## Phase Roadmap

## Ordering Rationale

The roadmap hardens the engine in dependency order:

- CPU correctness and exception behavior come before compatibility claims.
- BIOS/HLE policy comes before any real-program harness so `SWI` behavior is explicit
  instead of accidental.
- Memory, IO, and fetch timing come before broader renderer, DMA, audio, and save
  behavior because those systems all depend on bus semantics.
- Legal program harnesses come before optimization so performance work is guarded by
  correctness evidence.
- Android integration stays late so platform code does not hide core defects.

### Phase 52: Thumb Program Coverage Hardening

Goal: make Thumb execution capable of running a small legal memory-backed program beyond
ALU and branch seeds.

Implement:

- Thumb load/store immediate forms.
- Thumb load/store register-offset forms.
- Thumb load/store byte and halfword forms.
- Thumb SP-relative load/store seed.
- Thumb `PUSH`/`POP` seed, including safe PC/LR handling.
- Thumb `SWI` decode that enters the existing exception path without BIOS execution.

Done when:

- Synthetic Thumb programs can load, store, push, pop, branch, and stop via a modeled
  software interrupt.
- Unsupported Thumb forms still reject cleanly.
- Existing benchmark rows remain valid.

### Phase 53: ARM CPU Completeness Pass

Goal: remove high-risk gaps in the ARM interpreter before larger timing and cartridge
work depend on it.

Implement:

- `MRS` and `MSR` CPSR/SPSR coverage.
- `SWI` instruction handling through the existing exception-entry path.
- `SWP`/`SWPB` seed if memory semantics can be kept deterministic.
- `RRX` shifter support.
- More complete condition-code coverage in tests.
- Explicit undefined-instruction behavior for unsupported encodings.

Done when:

- CPU tests cover status-register transfer, software interrupt entry, `RRX`, and
  rejected undefined paths without mutating unrelated state.

Status: complete for a bounded local seed. Remaining work includes privileged CPSR
write restrictions, real banked registers, full undefined-instruction exception entry,
BIOS/HLE policy, pipeline-visible PC behavior, and true locked-bus arbitration for swap.

### Phase 54: Register Banking And Exception Realism

Goal: make IRQ/SVC transitions credible enough for legal test programs.

Implement:

- Banked `SP`/`LR` for SVC, IRQ, FIQ, Abort, Undefined, and System/User behavior.
- FIQ banked registers where needed.
- IRQ mask behavior around timer-triggered IRQ entry.
- `HALT`/interrupt wake seed through an explicit core API or IO route.
- Negative tests for illegal CPSR mode transitions and missing BIOS vector execution.

Done when:

- A synthetic timer IRQ can enter IRQ mode, use banked registers, and return through a
  modeled path without BIOS bytes.

Status: complete for a bounded local seed. User/system sharing, SVC/IRQ/Abort/Undefined
SP/LR banks, FIQ R8-R14 banks, exception LR placement, SPSR-based return, scheduler IRQ
banking, and explicit HALT wake are covered. BIOS vector contents, BIOS IRQ dispatch,
full undefined exception entry, data-abort restoration, and exact low-power HALT/STOP
semantics remain deferred.

### Phase 55: BIOS/HLE Policy And SWI Boundary

Goal: make BIOS-dependent behavior explicit before the core tries to run real legal
programs.

Implement:

- A documented BIOS policy with supported modes: no BIOS, caller-provided BIOS later,
  and optional HLE candidates only when source-backed and tested.
- `SWI` handling rules for ARM and Thumb that do not require bundled BIOS bytes.
- A clear failure mode for unimplemented BIOS services.
- Hooks for future user-provided BIOS injection without storing or bundling BIOS data.
- Negative tests proving BIOS bytes are not required, invented, scanned, downloaded, or
  silently assumed.

Done when:

- `SWI` behavior is deterministic, unsupported BIOS service calls fail cleanly, and the
  core has a documented path for future legal BIOS handling without adding BIOS assets.

Status: complete for a bounded local policy/API seed. ARM and Thumb SWI service-number
decoding, no-BIOS vector-trap policy, caller-provided BIOS metadata, HLE
unimplemented-service failure, and no-bundled-BIOS tests are covered. BIOS image
loading/execution, HLE service implementations, BIOS IRQ dispatch, and legal advice
remain deferred.

### Phase 56: Memory Bus And IO Correctness Hardening

Goal: make memory and IO behavior less scaffold-like before timing, DMA, renderer, and
cartridge systems rely on it.

Implement:

- GBA-style unaligned halfword/word read behavior where source-backed and tested.
- Open-bus or unmapped-region policy, documented and tested.
- Integration plan or first implementation for modeled IO registers through the memory
  bus path, reducing the gap between `MemoryBus` and `IoRegisters`.
- VRAM, palette, and OAM access-window policy hooks for PPU timing phases.
- Separate hard-reset and soft-reset semantics so ROM/save backing is not accidentally
  discarded by a normal reset.
- Negative tests for partial writes, invalid widths, read-only regions, and unmapped
  regions.

Done when:

- Memory and IO access have explicit behavior for aligned, unaligned, mirrored,
  unmapped, read-only, unsupported, hard-reset, and soft-reset cases.

Status: complete for a bounded local hardening seed. Unaligned word reads rotate from
the aligned backing word, odd halfword accesses and unaligned word writes reject
cleanly, BIOS/IO/external/unmapped read policy is explicit, palette/VRAM/OAM expose a
future contention hook, and soft reset preserves explicit ROM/save backing while hard
reset drops it. True open-bus latching, integrated IO-bank routing, PPU contention
timing, and broad instruction-family unaligned behavior remain deferred.

### Phase 57: Pipeline And Prefetch Timing

Goal: improve deterministic timing for instruction fetch and Game Pak execution before
save protocols, DMA timing, renderer timing, and performance optimization build on
scheduler assumptions.

Implement:

- Pipeline-visible PC behavior for ARM and Thumb.
- Game Pak prefetch buffer seed tied to WAITCNT.
- Sequential/non-sequential transition rules across branches and address boundaries.
- Region-dependent timing for executed instruction streams.
- Benchmark rows for prefetch and cartridge fetch loops.

Done when:

- The same instruction stream reports different elapsed cycles for IWRAM, EWRAM, and
  Game Pak ROM under different WAITCNT settings.

Status: complete for a bounded local timing seed. Selected ARM/Thumb PC-visible reads,
WAITCNT-tied prefetch metadata/hits, 128 KiB Game Pak boundary non-sequential timing,
prefetch state hashing, and a `scheduler_prefetch_cart_loop` benchmark row are covered.
Full hardware prefetch fill/drain behavior, the prefetch-disable bug, DMA bus stealing,
complete PC-visible instruction semantics, and Android/device timing remain deferred.

### Phase 58: Legal Program Harness

Goal: create a repeatable compatibility harness for tiny legal programs while keeping
fixtures explicit and auditable.

Implement:

- A minimal in-memory program runner around `CoreSession`.
- Expected-state assertions for caller-provided legal byte blobs.
- Fixture registry rules for any future checked-in homebrew/test ROMs.
- Negative tests for unloaded ROM, invalid header, unsupported instruction, and clean
  stop reasons.

Done when:

- The core can run tiny legal test programs through one harness with deterministic final
  state hashes.

Status: complete for a bounded local harness seed. `run_legal_program` and
`run_loaded_legal_program` wrap `CoreSession`, accept explicit in-memory bytes only,
check expected stop/final PC/register/state-hash values, optionally enforce the
cartridge fixed-value header byte, and report clean empty/unloaded/invalid/mismatch
states. Checked-in ROM fixtures, filesystem loading, scanners, downloaders, Android SAF,
compatibility reporting, and public legal claims remain deferred.

### Phase 59: Save Protocols

Goal: move from raw save buffers to real cartridge save behavior.

Implement:

- Flash command-state machine seed.
- Flash chip ID, erase, program, and bank-switch behavior.
- EEPROM serial protocol seed.
- Explicit save-type metadata override and conservative detection hooks.
- Persistence API still platform-neutral; no Android filesystem assumptions.

Done when:

- SRAM, Flash, and EEPROM behavior can be tested through bus-visible operations and
  exported/imported through core-owned byte buffers.

### Phase 60: DMA Trigger And Bus Stealing Timing

Goal: make DMA interact with the scheduler instead of acting as an immediate helper
only.

Implement:

- HBlank, VBlank, FIFO, and special trigger modes.
- DMA repeat behavior under scheduled triggers.
- DMA cycle charging and CPU stall/bus-stealing metadata.
- FIFO refill path into Direct Sound.
- Negative tests for disabled, invalid, and overlapping DMA conditions.

Done when:

- Scheduler traces show DMA occupying bus time and affecting CPU/device timing.

### Phase 61: PPU Mode 0-2 Renderer Core

Goal: make tiled rendering credible before Android texture upload exists.

Implement:

- Multi-BG priority composition for modes 0-2.
- BG scroll register routing.
- OBJ priority integration against all BG layers.
- Window mask seed.
- Mosaic seed if it can be kept bounded.
- No heap allocation during scanline/frame render.

Done when:

- Synthetic frames cover BG priority, OBJ priority, transparency, scroll, and window
  behavior into a fixed framebuffer.

### Phase 62: PPU Bitmap/Affine Renderer Core

Goal: cover the remaining major video modes in the core framebuffer.

Implement:

- Modes 3, 4, and 5 bitmap rendering.
- Affine BG seed for modes 1 and 2.
- Palette/page behavior for mode 4.
- Forced blank behavior.
- Blending targets and alpha/brightness seed.

Done when:

- Synthetic renderer tests cover all major display modes without Android/OpenGL code.

### Phase 63: APU PSG And Mixer Fidelity

Goal: move audio from Direct Sound seed to a useful deterministic GBA mixer.

Implement:

- Square channel sweep, length, duty, and envelope behavior.
- Wave channel playback.
- Noise LFSR behavior.
- SOUNDBIAS behavior where source-backed.
- Fixed-point resampler and deterministic audio-buffer drain.

Done when:

- Synthetic APU tests can produce stable hashes for PSG, Direct Sound, and mixed output
  across frame boundaries.

### Phase 64: Core Keypad And Input IO

Goal: model GBA input in the core before Android maps touch or gamepad controls.

Implement:

- Core keypad state for A, B, Select, Start, D-pad, L, and R.
- KEYINPUT and KEYCNT IO behavior through the modeled IO path.
- Key interrupt request behavior where source-backed and bounded.
- Deterministic input snapshots for scheduler/program tests.
- Negative tests for impossible button masks and read-only key-state mutation.

Done when:

- Legal program harnesses can drive input through core APIs and IO reads without Android
  dependencies.

### Phase 65: Compatibility Test Corpus

Goal: measure real behavior against legal tests instead of relying only on synthetic
unit tests.

Implement:

- Legal homebrew/test-ROM fixture policy.
- Fixture license registry enforcement.
- Expected-output/state-hash runner.
- Negative fixture tests for invalid headers and unsupported mapper/save behavior.
- Compatibility report format that avoids public overclaiming.

Done when:

- A curated legal fixture set runs in the local harness with reproducible state,
  framebuffer, audio, or input-sensitive hashes.

### Phase 66: Deterministic Save-State Format

Goal: turn the Phase 49 save-state seed into a production-grade core contract.

Implement:

- Versioned binary save-state format.
- Component section metadata and compatibility checks.
- Serialization for renderer framebuffer and all newly modeled device state.
- Corruption and version-mismatch rejection.
- Deterministic restore tests across multi-frame runs.

Done when:

- Run N frames, save, restore, run N frames produces identical state hashes and rejects
  malformed state blobs cleanly.

### Phase 67: Performance Architecture Pass

Goal: optimize only after correctness and timing surfaces are measurable.

Implement:

- Predecoded instruction structs for hot fetch loops.
- Dispatch-table or threaded-interpreter experiment behind a compile-time boundary.
- Flat hot memory paths without virtual calls.
- Scheduler batching until the next device event.
- No-allocation frame/audio loop assertions.

Done when:

- Benchmarks improve or hold steady without weakening correctness tests, and each
  optimization has a before/after measurement artifact.

### Phase 68: Android Core Bridge

Goal: expose the verified core to Android with a narrow, safe boundary.

Implement only with explicit authorization:

- JNI opaque `CoreSession` handle.
- Thread-safe create/destroy/step/reset/load APIs.
- SAF import for explicit user-selected files only.
- No scanner, downloader, bundled BIOS, bundled ROM, or broad storage permission.
- Native crash and lifecycle tests.

Done when:

- Android can create a core, load explicit legal bytes, step deterministically, and
  release resources without leaks or races.

### Phase 69: Android Video, Input, And Audio Integration

Goal: make the mobile app usable while keeping core behavior platform-neutral.

Implement only after Phase 68:

- OpenGL ES texture upload from the core framebuffer.
- Touch and gamepad input mapping into core input state.
- Audio backend integration, likely Oboe, after APU determinism is credible.
- Pause/resume lifecycle behavior.
- Device rotation and background behavior tests.

Done when:

- A legal test program can render frames, accept input, produce audio, pause, resume,
  and shut down cleanly on Android.

### Phase 70: Android Performance And Power Gate

Goal: make performance-per-watt measurable instead of aspirational.

Implement:

- Device benchmark command or instrumentation test.
- Frame pacing metrics: average, p95, and missed-frame counts.
- Audio underrun metrics.
- Thermal and sustained-run observations.
- Baseline profiles or native optimization changes only after evidence.

Done when:

- Midrange Android hardware can run the selected legal fixture set at target pacing for
  sustained sessions, with recorded metrics and reproducible commands.

### Phase 71: Release Governance And Legal Review

Goal: prepare the project for external beta without accidental legal or store-policy
exposure.

Implement:

- Legal review checklist for BIOS, ROM, trademarks, screenshots, compatibility claims,
  and fixture licenses.
- Privacy and data-safety review for SAF import and save persistence.
- Crash-reporting and telemetry decision record, if any telemetry is introduced.
- Public README language that avoids unsupported compatibility claims.

Done when:

- Release-facing wording and fixture handling are reviewed and evidence-backed.

### Phase 72: Controlled Beta Readiness

Goal: prove the engine and Android shell are ready for a controlled beta, not a broad
public release.

Implement:

- Release candidate checklist.
- Regression suite gate for CPU, timing, PPU, APU, save, save-state, Android lifecycle,
  and device performance.
- Rollback/recovery plan for broken save states or save files.
- Known-issues document with unsupported hardware behavior.

Done when:

- No unresolved critical blockers remain, high risks are documented with owner and
  mitigation, and beta evidence exists on at least one target Android device.

## Measurement Matrix

The benchmark suite should grow with the engine. Add or keep rows for:

- CPU step throughput.
- ARM and Thumb fetch-loop throughput.
- Memory bus read/write cost by region.
- WAITCNT timing lookup and elapsed-cycle estimate cost.
- Prefetch/cartridge execution loops.
- DMA copy and scheduled-trigger cost.
- PPU BG, OBJ, scanline, and full-frame cost.
- APU tick, FIFO, PSG, mixer, and resampler cost.
- Keypad/input IO polling and interrupt cost.
- Core-session state hash and save-state serialize/restore cost.
- Android frame pacing, audio underruns, native CPU time, and sustained thermal behavior
  once Android integration is authorized.

## Production Readiness Definition

The engine should not be treated as production-ready until all of these are true:

- Legal fixture policy is enforced and no copyrighted ROM/BIOS assets are bundled.
- BIOS/HLE behavior is explicit, tested, and does not silently depend on bundled assets.
- CPU, memory, timing, DMA, PPU, APU, save, and save-state behavior have positive and
  negative tests.
- Core keypad/input behavior is tested before Android maps touch or gamepad controls.
- Performance regression gates exist for local core paths and Android device paths.
- Save persistence and save states have corruption, migration, and rollback handling.
- Android lifecycle, input, rendering, and audio have device evidence.
- Public docs avoid unsupported compatibility, legality, or superiority claims.

## Recommended Immediate Next Phase

Phase 59 should be next: save protocols. The core now has an explicit in-memory program
harness, so the next correctness blocker is making save behavior bus-realistic: Flash
command state, chip ID, erase/program, bank switching, and EEPROM serial protocol while
keeping persistence platform-neutral.
