# GBA_Emulator — Performance & Measurement

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

For the full accuracy matrix (green suites, harness aliases, video oracle rows, and
synthetic performance gate):

```powershell
.\tools\run-credibility-matrix.ps1
```

Video oracle example (does not mark the upstream `video` suite green):

```powershell
.\tools\run-mgba-suite.ps1 -Suite video -VideoProbe basic-mode-3-actual -MaxSteps 20000000 -TraceSteps 0 -UntilOutput "VIDEO:BASIC_MODE_3_ACTUAL"
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
windows, SRAM/Flash save apertures, and large-ROM EEPROM candidate addresses.
Caller-provided in-memory Game Pak ROM and save backing enable read-only cartridge
reads and bounded save aperture behavior; there is still no filesystem loader or
persistence UX.

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
