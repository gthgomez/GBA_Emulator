# GBA_Emulator

Portable Game Boy Advance emulator core scaffold with Android integration intentionally deferred.

## Current Core Scope

This repository currently contains a minimal C++17 memory-bus scaffold, ARM7TDMI
data-processing immediate/register-shifter/logical/arithmetic-carry opcode, branch/PC,
load/store transfer, first Thumb decoder, status-register, and exception-entry seed
verifiers, first timer/IRQ verifier, first DMA verifier, first PPU scanline timing
verifier, first background tile fetch verifier, first sprite/OAM verifier, first APU
timing/register verifier, first APU deterministic Direct Sound audio-buffer verifier,
first cartridge/save-bus metadata verifier, first explicit in-memory save backing
verifier, first core session save-state/determinism verifier, first core integration
session boundary, first state-hash benchmark row, first
memory-mapped IO register routing verifier for already-modeled devices, first global
core scheduler/frame-step verifier, first scheduler-integrated synthetic instruction
fetch/decode loop verifier, first ARM/Thumb dispatch-mode fetch verifier, first
benchmark output gate, first WAITCNT metadata verifier, first WAITCNT IO register
routing verifier, first WAITCNT-aware cartridge/save timing metadata verifier, first
scheduler-facing WAITCNT elapsed-cycle estimate verifier, a local synthetic benchmark
harness, first Thumb branch/high-register/BX verifier, first explicit Game Pak ROM blob
boundary verifier, first WAITCNT-applied cartridge fetch/data-load timing seed, first
PPU renderer scanline framebuffer seed, first Thumb program-coverage hardening verifier,
first ARM CPU completeness verifier for PSR transfer, software interrupt, swap, RRX,
and full condition-code seed behavior, first register-banking/exception-return/HALT wake
verifier, first explicit BIOS/SWI policy-boundary verifier, first memory-bus correctness
hardening verifier for unaligned word-read rotation, explicit unmodeled read policies,
video access-window hooks, and hard/soft reset semantics, first pipeline/prefetch timing
seed for selected PC-visible reads, Game Pak prefetch metadata, 128 KiB boundary timing,
and a prefetch benchmark row, first explicit in-memory legal-program harness verifier,
first Flash/EEPROM save-protocol seed, first scheduled DMA trigger and Direct Sound FIFO
DMA refill seed, first multi-layer/bitmap/window/blend renderer seed, first deterministic
PSG square/wave/noise mixer seed, first keypad/input IO seed, first legal fixture corpus
runner seed, first versioned save-state codec seed, first predecoded-instruction cache
seed, first narrow Android-facing core bridge seed, and a local test runner.
It now also includes a first Android-facing runtime seed for video/input/audio flow and
a local Android performance/power gate seed for frame pacing and underrun metrics.
Release governance and controlled-beta readiness gates now document legal/privacy
controls, rollback, known issues, and blocked production-readiness requirements.

In scope for this scaffold:

- Portable core layout.
- First memory behavior verifier.
- First memory mirroring verifier for implemented internal regions.
- First memory-region timing metadata verifier for implemented internal regions.
- First CPU decode/execute verifier for a tiny ARM data-processing immediate subset.
- First branch/PC verifier for `B` and `BL`.
- First load/store verifier for aligned word `STR` and `LDR` immediate offsets.
- First byte/halfword transfer verifier for `STRB`, `LDRB`, `STRH`, `LDRH`, `LDRSB`, and `LDRSH`.
- First register-offset transfer verifier for word/byte `STR`, `LDR`, `STRB`, and `LDRB`.
- First halfword register-offset transfer verifier for `STRH`, `LDRH`, `LDRSB`, and `LDRSH`.
- First post-index/writeback transfer verifier for narrow word and halfword load/store paths.
- First block-transfer verifier for narrow `STM`/`LDM` register-list paths.
- First cycle-accounting metadata verifier for supported ARM instruction families.
- First elapsed-cycle composition helper for supported ARM/memory timing paths.
- First scheduler-step verifier for executed-instruction elapsed-cycle accumulation.
- First Thumb decoder/execution seed for narrow `MOV`, `CMP`, `ADD`, and `SUB` forms.
- First CPSR/SPSR and CPU-mode state seed for supervisor, IRQ, and system foundations.
- First exception/interrupt-entry seed for vector metadata and CPSR/SPSR transitions
  without BIOS execution or vector contents.
- First timer/IRQ verifier for four 16-bit timers, overflow requests, IE/IF/IME gating,
  and CPU IRQ exception entry.
- First DMA verifier for immediate memory-to-memory transfers, address-control modes,
  word-count normalization, repeat destination reload, priority order, and completion
  IRQ requests.
- First PPU scanline timing verifier for DISPSTAT/VCOUNT-style status, frame/line
  cycle constants, VBlank/HBlank/VCount flags, and LCD interrupt requests.
- First background tile fetch verifier for regular text BG control decode, charblock
  and screenblock addressing, 4bpp/8bpp tile pixels, palette lookup, flip bits, map
  wrapping, and transparency.
- First sprite/OAM verifier for 128 OBJ attribute slots, shape/size decode, signed X
  wrapping, regular OBJ flip bits, 4bpp/8bpp OBJ tile pixels, OBJ palette lookup, and
  transparency.
- First APU timing/register verifier for SOUNDCNT/SOUNDBIAS state, master sound enable,
  512 Hz frame sequencer ticks, Direct Sound FIFO A/B storage, timer-selected sample
  consumption, and FIFO refill request metadata.
- First APU deterministic audio-buffer verifier for timer-driven Direct Sound sample
  latching, fixed 32,768 Hz core sample cadence, stereo route mixing, fixed-capacity
  sample buffering, and scheduler-routed timer overflow consumption.
- First cartridge/save-bus metadata verifier for Game Pak ROM wait-state windows,
  SRAM/Flash save aperture metadata, and large-ROM EEPROM candidate metadata without
  loading ROM data or backing save storage.
- First explicit in-memory save backing verifier for SRAM32K, Flash64K, Flash128K,
  EEPROM512, and EEPROM8K raw save buffers, including import/export through the core API
  and byte-aperture SRAM/Flash reads/writes without Android filesystem assumptions.
- First core session save-state/determinism verifier for owned CPU, memory, interrupts,
  timers, DMA, PPU timing, APU, WAITCNT, and scheduler state, including save, restore,
  deterministic rerun, and state-hash checks.
- First core integration session boundary for future clients: `CoreSession` owns the
  modeled core devices and exposes explicit step/run, save/load, and hash APIs without
  JNI, Gradle, Android UI, SAF, OpenGL, Oboe, or filesystem assumptions.
- First memory-mapped IO register routing verifier for already-modeled DISPSTAT/VCOUNT,
  timer, DMA, interrupt, SOUNDCNT/SOUNDBIAS, and Direct Sound FIFO surfaces.
- First global core scheduler/frame-step verifier for coordinating CPU elapsed cycles,
  timer ticks, PPU timing ticks, APU frame timing, immediate DMA, and pending IRQ
  service.
- First scheduler-integrated synthetic instruction fetch/decode loop verifier for
  memory-backed ARM instruction fetch from the current PC, bounded local runs, and
  explicit fetch-failure/unsupported-instruction stop reasons.
- First ARM/Thumb dispatch-mode fetch verifier for choosing 32-bit ARM fetch or 16-bit
  Thumb fetch from CPSR T state.
- First benchmark output gate for checking required synthetic benchmark rows, positive
  timing fields, and nonzero checksums.
- First WAITCNT metadata verifier for SRAM wait control, Game Pak wait-state 0/1/2
  first/subsequent access metadata, prefetch enable state, and PHI terminal output.
- First WAITCNT IO register routing verifier for 16-bit `0x04000204` reads/writes
  through the existing `IoRegisters` facade.
- First WAITCNT-aware cartridge/save timing metadata verifier for Game Pak ROM windows,
  EEPROM-candidate addresses, and SRAM/Flash save aperture timing without making those
  ranges readable or writable.
- First scheduler-facing WAITCNT elapsed-cycle estimate verifier for composing ARM
  memory cycle classes with Game Pak ROM/save timing metadata.
- First Thumb branch/high-register/BX verifier for unconditional branch, EQ/NE
  conditional branch, high-register MOV/ADD/CMP, and BX interworking state changes.
- First Thumb program-coverage hardening verifier for memory-backed Thumb load/store,
  byte/halfword, signed register-offset load, SP-relative load/store, stack `PUSH`/`POP`,
  and `SWI` exception entry.
- First ARM CPU completeness verifier for `MRS`/`MSR` CPSR/SPSR transfers, ARM `SWI`
  exception entry, `SWP`/`SWPB` through the modeled memory bus, `RRX` shifter behavior,
  all non-reserved ARM condition codes, and reserved-condition rejection.
- First register-banking and exception-realism verifier for user/system shared registers,
  SVC/IRQ/Abort/Undefined banked SP/LR, FIQ banked R8-R14, exception LR placement in
  the target mode bank, modeled exception return through SPSR, IRQ-bank scheduler
  service, and an explicit scheduler HALT wake latch.
- First explicit BIOS/SWI policy-boundary verifier for ARM/Thumb SWI service-number
  decoding, no-BIOS vector-trap policy, future caller-provided BIOS mode metadata, and
  clean unimplemented-service failure for requested HLE without bundling BIOS bytes.
- First memory-bus correctness hardening verifier for unaligned word-read rotation, odd
  halfword rejection, explicit BIOS/IO/external/unmapped read policy, future video
  access-window hooks for palette/VRAM/OAM, and separate hard-reset/soft-reset
  semantics.
- First pipeline/prefetch timing seed for selected ARM/Thumb pipeline-visible PC reads,
  WAITCNT-tied Game Pak prefetch metadata and hit behavior, 128 KiB Game Pak
  non-sequential boundary timing, scheduler state hashing of prefetch state, and a
  prefetch/cart-loop benchmark row.
- First legal-program harness verifier for explicit caller-provided in-memory byte
  blobs, expected-state assertions, clean unloaded/invalid/unsupported stop reporting,
  and future fixture-admission rules without checked-in ROM/BIOS fixtures.
- First Flash/EEPROM save-protocol seed for Flash ID, program, sector erase, chip erase,
  bank switching, explicit EEPROM block read/write, and conservative save marker
  detection without filesystem persistence.
- First scheduled DMA trigger seed for VBlank/HBlank/special trigger dispatch,
  bus-cycle occupancy metadata, and Direct Sound FIFO refill routing into the APU.
- First multi-layer/bitmap renderer seed for modes 0-5, multi-BG priority, simple WIN0
  masking, mosaic snap, bitmap page selection, forced blank, and brightness blending
  into the fixed framebuffer.
- First deterministic PSG mixer seed for square, wave, and noise generators mixed with
  Direct Sound and included in APU/core-session state hashing.
- First keypad/input IO seed for active-low KEYINPUT reads, KEYCNT IRQ selection,
  OR/AND keypad interrupt behavior, and core-session state hashing.
- First legal fixture corpus runner seed for explicit in-memory ROM bytes, license/
  redistributability gating, harness expectations, and combined deterministic hashes.
- First versioned save-state codec seed for a binary envelope over the current public,
  restorable core-session subset with magic/version/corruption rejection.
- First predecoded-instruction cache seed for bounded ARM/Thumb operation classification
  and hit/miss accounting before dispatch-table experiments.
- First narrow Android-facing core bridge seed with an opaque thread-safe handle,
  explicit ROM byte loading, bounded run, reset, and state hash APIs.
- First Android-facing runtime seed for explicit ROM loading, keypad mapping,
  framebuffer rendering, APU audio draining, frame stepping, and underrun reporting.
- First Android performance/power gate seed for average/p95 frame duration,
  missed-frame count, audio underruns, final hash, and conservative thermal observation.
- First release governance and controlled-beta readiness gate for BIOS/ROM wording,
  fixture licensing, unsupported-claim scanning, rollback planning, known issues, and
  roadmap requirement auditing.
- First explicit Game Pak ROM blob boundary verifier for caller-provided in-memory ROM
  bytes, read-only cartridge reads through Game Pak ROM windows, header metadata
  parsing, and scheduler fetch from `0x08000000`.
- First WAITCNT-applied cartridge timing seed for opt-in scheduler fetch timing,
  adjacent sequential Game Pak fetch tracking, and ARM data loads from explicit ROM
  bytes using WAITCNT-aware elapsed-cycle composition.
- First PPU renderer scanline framebuffer seed for fixed-size 240x160 output,
  backdrop fill, BG0 regular text rendering, OBJ overlay priority, and transparent
  pixel handling.
- First synthetic benchmark harness for current memory, CPU, IO, DMA, PPU fetch, timer,
  PPU timing, APU timing, scheduler step, scheduler fetch-loop, Thumb dispatch, and
  WAITCNT timing-lookup and elapsed-estimate surfaces.
- First register-shifter verifier for ARM data-processing operand2 immediate and register-specified shifts.
- First multiply verifier for basic ARM `MUL` and `MLA`.
- First multiply-long verifier for `UMULL`, `UMLAL`, `SMULL`, and `SMLAL`.
- First logical data-processing verifier for `AND`, `EOR`, `TST`, `TEQ`, `ORR`, `BIC`, and `MVN`.
- First arithmetic/carry data-processing verifier for `RSB`, `ADC`, `SBC`, `RSC`, and `CMN`.
- Local-only build output under `build/`.

Out of scope for this scaffold:

- Android app code.
- Gradle.
- CMake.
- JNI.
- ROMs.
- BIOS images.
- Filesystem cartridge loading and save-file persistence.
- Downloaders or storage scanners.
- App-store, deployment, or public legal/compatibility claims.

## Roadmap

The production engine phase plan lives in
`docs/production-engine-roadmap.md`. It starts from the Phase 51 baseline and splits
the remaining work into CPU hardening, bus/timing accuracy, PPU/APU completeness,
save-state productionization, performance architecture, legal fixture compatibility,
Android integration, device measurement, and controlled beta readiness phases.

The accuracy/performance comparison roadmap lives in
`docs/benchmark-comparison-roadmap.md`. It defines the goal, purpose, green criteria,
and phased benchmark matrix for comparing this engine against top open-source GBA
emulators without separating speed claims from correctness evidence.

## Verify

From this repository:

```powershell
.\tools\run-core-tests.ps1
```

The script compiles and runs all local core verifier binaries with C++17 using `g++`.

## Measure

From this repository:

```powershell
.\tools\run-core-benchmarks.ps1
```

The benchmark script compiles an optimized local executable with `g++ -O2` and prints
CSV rows:

```text
benchmark,operations,elapsed_ms,ops_per_second,ns_per_operation,checksum
```

The benchmarks are synthetic no-ROM/no-BIOS workloads. They are useful for tracking
relative local performance changes in the current core surfaces; they are not hardware
accuracy, compatibility, battery, Android, or production readiness claims.

For a local performance-regression gate, run:

```powershell
.\tools\check-core-performance-regression.ps1
```

That script reruns the benchmark three times by default, compares median ns/op and
deterministic checksums against `tools/core-benchmark-baseline.json`, and writes
`build/core_benchmark_regression_latest.json`. To test the gate itself without timing
noise, run:

```powershell
.\tools\test-core-performance-regression-gate.ps1
```

To collect a longer local calibration run and generate a proposed threshold update
without editing the checked-in baseline, run:

```powershell
.\tools\calibrate-core-performance-thresholds.ps1
```

To collect the current accuracy/performance credibility matrix, run:

```powershell
.\tools\run-credibility-matrix.ps1
```

For the currently green public-suite frontier plus the synthetic performance gate:

```powershell
.\tools\run-credibility-matrix.ps1 -Suites memory,bios-math,dma -PerformanceRuns 3 -FailOnRed
```

For the Roadmap 1 all-suite measurement artifact:

```powershell
.\tools\run-mgba-suite.ps1 -Suite all -MaxSteps 20000000 -TraceSteps 0
```

For the Roadmap 1 regression-only gate, which fails only if a previously verified
green public-suite target regresses:

```powershell
.\tools\run-credibility-matrix.ps1 -FailOnRegression
```

## Performance Notes

The first performance target is correctness-shaped: implement instruction families in the
order they most affect the eventual scheduler. `LDM`/`STM` are now present because block
transfers map multiple register accesses onto sequential memory words, which later makes
cycle accounting and memory wait-state modeling visible without adding Android, ROM, or
BIOS scope.

The current elapsed-cycle helpers compose instruction cycle classes with internal-memory
timing metadata for supported memory instructions and expose a WAITCNT-aware estimator
for Game Pak ROM/save timing metadata. Cartridge execution, BIOS, IO, prefetch
buffering, and platform integration remain locked for later phases.

The first scheduler step executes one supported ARM instruction and accumulates elapsed
cycles when the instruction executes. Skipped-condition and unsupported paths preserve
the current cycle total in this scaffold.

The memory bus now normalizes implemented internal-region mirrors for EWRAM, IWRAM,
palette RAM, VRAM, and OAM. It also exposes metadata for Game Pak ROM wait-state
windows, SRAM/Flash save apertures, and large-ROM EEPROM candidate addresses while
continuing to reject cartridge reads/writes until explicit ROM and save backing storage
exist.

The first memory-bus correctness hardening pass now rotates unaligned word reads from
the aligned backing word, rejects odd halfword accesses and unaligned word writes
without partial mutation, exposes explicit unmodeled read policies for BIOS, IO,
external, and unmapped regions, exposes future video access-window hooks for
palette/VRAM/OAM, and separates soft reset from hard reset so caller-provided ROM/save
backing is not accidentally discarded by a normal core reset.

The first Thumb seed began with narrow ALU forms only. Later phases added branches,
high-register operations, BX interworking, dispatch-mode fetch, and Phase 52
memory-backed load/store forms, stack `PUSH`/`POP`, and `SWI` exception entry. Thumb
long branch/link, literal-pool loads, multiple load/store, full pipeline-visible PC
behavior, and BIOS service execution remain future phases.

The first status-register seed exposes CPSR flags, interrupt masks, Thumb-state bit, and
CPU mode bits, with separate SPSR storage for exception modes. The first exception-entry
seed records fixed vector metadata and updates mode, SPSR, LR, masks, ARM state, and PC
without reading BIOS/vector contents. Phase 53 adds ARM `MRS`/`MSR`, ARM `SWI`,
`SWP`/`SWPB`, `RRX`, and full non-reserved ARM condition-code seed coverage. Register
banking now covers user/system sharing, SVC/IRQ/Abort/Undefined SP/LR banks, FIQ
R8-R14 banks, and a modeled SPSR restore path for exception return. Privileged/user-mode
restrictions, handler execution, BIOS/vector-content fetch behavior, abort restoration,
and true locked-bus swap arbitration remain future phases.

The first BIOS/SWI policy boundary decodes ARM and Thumb SWI comments into GBA service
numbers and makes the current no-BIOS behavior explicit: SWI traps to the existing
vector path, HLE requests fail as unimplemented, and future caller-provided BIOS mode is
metadata only. No BIOS data, BIOS dump, downloader, scanner, or HLE service
implementation is present.

The first timer/IRQ seed models four local 16-bit timers, reload-on-start/overflow,
prescalers, count-up cascading, timer overflow IF requests, IE/IF/IME gating, a
CPU-facing IRQ service hook, and IO register routes for the modeled timer/interrupt
registers. It does not model BIOS IRQ dispatch, DMA/audio side effects, or a global
device scheduler yet.

The first DMA seed models four local DMA channels for immediate memory-to-memory copies
through the current `MemoryBus`, including 16-bit/32-bit transfer size, address-control
modes, word-count normalization, repeat destination reload, channel priority order, and
completion IRQ requests through `InterruptController`, and IO register routes for DMA
source/destination/count/control. It does not model H-Blank/V-Blank/special/FIFO/video-
capture triggers, DMA bus stealing, cartridge restrictions, or cycle timing yet.

The first PPU timing seed models scanline/frame timing only: 960 visible cycles, 272
HBlank cycles, 1,232 cycles per scanline, 228 scanlines, and 280,896 cycles per frame.
It exposes DISPSTAT/VCOUNT-style status and requests VBlank, visible-line HBlank, and
VCount-match interrupt flags through `InterruptController`, with IO register routes for
DISPSTAT/VCOUNT. It does not render scanlines, evaluate priority, model VRAM/OAM access
windows, or coordinate a global scheduler yet.

The first background and sprite seeds decode the smallest useful rendering data paths:
regular text background map/tile/palette lookup and regular non-affine OBJ
attribute/tile/palette lookup. They use synthetic verifier data written directly into
the current `MemoryBus`; there is still no renderer, LCD priority mixer, affine
backgrounds, bitmap modes, blending, mosaic, windowing, sprite overflow behavior, or
scanline compositor.

The first APU seed now models register/timing surfaces plus a deterministic Direct
Sound audio-buffer path: SOUNDCNT_L/H/X, SOUNDBIAS, wave RAM storage, a 512 Hz frame
sequencer, Direct Sound FIFO A/B timer consumption/refill metadata, latched Direct
Sound samples, fixed 32,768 Hz stereo sample generation, fixed-capacity buffering, and
scheduler-routed timer overflow consumption. It does not emulate DMG square/wave/noise
channel waveforms, connect FIFO refill requests to DMA, apply SOUNDBIAS ramp behavior,
or integrate an audio backend yet.

The first renderer seed renders one scanline at a time into a fixed-size 240x160
framebuffer. It supports backdrop fill, mode-0-style BG0 regular text pixels through
the existing background fetcher, and regular non-affine OBJ overlay through the
existing sprite fetcher. It does not yet implement affine backgrounds, bitmap modes,
multiple BG priority composition, windows, blending, mosaic, forced blank, access
contention, sprite overflow behavior, or Android/OpenGL upload.

The cartridge/save seed now has three layers. `MemoryBus::describe` classifies Game Pak
ROM and save apertures, and `MemoryBus::describe_cartridge` reports ROM wait-state
window, SRAM/Flash, and EEPROM-candidate metadata. `MemoryBus::load_game_pak_rom`
accepts explicit caller-provided in-memory ROM bytes, exposes read-only Game Pak ROM
reads, and parses header metadata. It intentionally does not scan, download, persist,
or infer cartridge contents from storage. `MemoryBus::configure_game_pak_save` and
`import_game_pak_save` add explicit caller-owned in-memory save backing for SRAM,
Flash, and EEPROM raw bytes. SRAM/Flash expose byte reads/writes through the save
aperture; EEPROM raw backing is import/export only until the serial protocol is modeled.

The first IO register routing seed exposes a small `IoRegisters` facade over already
modeled devices. It routes 16-bit and selected 32-bit accesses for DISPSTAT/VCOUNT,
timers, DMA channels, IE/IF/IME, WAITCNT, SOUNDCNT/SOUNDBIAS, and Direct Sound FIFO
writes. Unsupported and read-only registers fail explicitly so later measurement and
scheduler work can distinguish missing behavior from modeled behavior. Selected 32-bit
IO writes preflight both halfwords so rejected mixed read/write pairs do not partially
mutate device state.

The first measurement harness times current synthetic hot paths with `std::chrono`:
IWRAM `MemoryBus` read/write pairs, `Arm7tdmi::step_arm`, mixed `IoRegisters` traffic,
immediate DMA word-copy units, background plus OBJ pixel fetches, timer/PPU/APU tick
surfaces, scheduler fetch/dispatch surfaces, WAITCNT-aware timing lookups, and the
full `CoreSession` state-hash path. It records checksums to keep the measured work
observable.

The first save-state seed lives at the core-session level. `CoreSession::save_state`
copies the currently modeled component state, `load_state` restores it, and
`state_hash` combines deterministic hashes from the owned components. The verifier
checks that saving, restoring, and rerunning the same bounded instruction stream
produces the same final state hash. This is not yet a portable on-disk save-state file
format or compatibility promise.

The first integration boundary is intentionally C++ only. `CoreSession` is the future
opaque-handle target for Android, but no JNI, Gradle, Android UI, SAF import, OpenGL,
Oboe, lifecycle, ROM scanner, or bundled BIOS/ROM work is introduced here.

The first legal-program harness is also C++ only. `run_legal_program` and
`run_loaded_legal_program` wrap `CoreSession` for tiny caller-provided in-memory byte
blobs, optional cartridge-header fixed-byte validation, deterministic expected-state
checks, and clean stop-reason reporting. It does not add checked-in ROM files, BIOS
files, filesystem loading, scanners, downloaders, or compatibility claims.

The first global scheduler seed wraps existing components without owning platform or
cartridge state. `CoreScheduler::step_arm` executes one modeled ARM instruction through
the CPU/memory path, advances timers, PPU timing, and APU frame timing by the reported
elapsed cycles, runs currently enabled immediate DMA, and then attempts pending IRQ
service. The device-advance path also observes timer0/timer1 overflow counts and routes
selected Direct Sound FIFO consumption into the APU. It intentionally does not run BIOS
handlers, trigger HBlank/VBlank/FIFO DMA, connect APU FIFO refill requests to DMA, or
integrate a platform audio backend yet.

The first scheduler fetch loop is synthetic and memory-backed only. It fetches a
32-bit ARM instruction from the current PC through `MemoryBus`, executes it through
`CoreScheduler::step_arm`, advances PC by one ARM word only when the instruction did
not change PC and was not unsupported, and stops bounded runs on fetch failure,
unsupported instruction, or max-step count. It is not a hardware pipeline, prefetch,
BIOS, ROM, WAITCNT, or mixed ARM/Thumb dispatch model yet.

The first dispatch-mode fetch seed chooses ARM or Thumb width from CPU Thumb state.
`CoreScheduler::step_from_pc` fetches ARM instructions with `read32` and Thumb
instructions with `read16`, then advances PC by 4 or 2 respectively when the executed
instruction did not change PC. When constructed with a `WaitStateControl`, it also
applies Game Pak ROM fetch timing and tracks adjacent sequential cartridge fetches. It
now exposes a bounded prefetch seed tied to WAITCNT bit 14: selected cartridge opcodes
can refill a capped eight-halfword buffer, later sequential opcode fetches can report a
prefetch hit with zero cartridge wait cycles, and 128 KiB Game Pak boundaries force
non-sequential timing. It still does not model the full hardware prefetch disable bug,
DMA bus stealing, or full mixed ARM/Thumb instruction coverage. The CPU now has selected
pipeline-visible PC reads for ARM data-processing and Thumb high-register sources.

The benchmark gate in `tools/check-core-benchmark.ps1` reruns the synthetic benchmark,
checks every required row is present, verifies positive timing/throughput fields, and
requires nonzero checksums. It is a local output-shape and measurement-integrity gate,
not a hardware performance threshold.

The performance regression gate in `tools/check-core-performance-regression.ps1` is a
local guardrail over the same synthetic rows. It uses calibrated checked-in thresholds
to catch material accidental slowdowns while tolerating normal workstation variance.

The first WAITCNT seed records Game Pak timing metadata and routes the register through
the local IO facade. `WaitStateControl` decodes SRAM wait control, ROM wait-state 0/1/2
first and subsequent access fields, prefetch enable, and PHI terminal output.
`MemoryBus::timing(address, width, waitcnt)` exposes those values for cartridge/save
metadata. Explicit SRAM/Flash save backing can now service byte reads/writes through the
save aperture, while EEPROM remains a raw core-state buffer until its serial protocol is
modeled. An opt-in scheduler path now applies WAITCNT timing to Game Pak instruction
fetches and supported ARM data loads from explicit ROM bytes. Prefetch buffering, DMA
bus stealing/stalls, Flash command state, EEPROM serial protocol, and platform
integration remain out of scope. `Arm7tdmi::estimate_arm_elapsed_cycles(..., waitcnt)`
composes those timing descriptors for scheduler-facing estimates.

See `docs/performance-research.md` for the current source-backed timing notes.
