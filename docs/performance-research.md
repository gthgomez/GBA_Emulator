# Performance Research Notes

Status: Phase 58 source-backed notes
Date: 2026-05-07

## Metrics To Preserve

- CPU clock target: 16 * 1024 * 1024 Hz.
- Frame pacing target: 280,896 CPU cycles per full video frame.
- Scanline budget target: 1,232 CPU cycles per line.
- Visible-line budget target: 960 cycles; H-Blank budget target: 272 cycles.

These figures make cycle accounting a first-class engine concern. Even before PPU/APU
work exists, CPU instructions should be structured so a scheduler can later charge work
by CPU cycles and memory-region wait states.

## Function Impact On The Engine

- `MemoryBus::read8` / `write8`: base unit for current bus access. Every wider access is
  built on this, so region classification, writable/readable policy, and optional save
  backing checks are hot-path decisions.
- `MemoryBus::read16` / `write16`: enforces halfword alignment and little-endian byte
  layout. Later timing should charge by target memory region and access width.
- `MemoryBus::read32` / `write32`: word reads now align the backing read and rotate the
  returned little-endian word for unaligned addresses; word writes still reject
  unaligned addresses to avoid partial mutation. Odd halfword reads/writes remain
  rejected as unsupported/unpredictable in this scaffold.
- `MemoryBus::timing`: first memory-region timing metadata layer. It reports
  non-sequential and sequential cycle costs for currently implemented internal memory
  regions, while keeping BIOS, IO, and unknown regions inaccessible until those buses
  are modeled. The WAITCNT-aware overload reports Game Pak ROM/save wait-state metadata
  from `WaitStateControl` while cartridge/save addresses remain unreadable/unwritable
  until explicit backing data exists.
- `MemoryBus::describe`: first memory mirror metadata layer. It normalizes implemented
  internal memory mirrors to canonical offsets before reads, writes, and timing
  metadata are applied. It also classifies Game Pak ROM and save apertures without
  making them readable.
- `MemoryBus::describe_cartridge`: first cartridge/save-bus metadata surface. It reports
  ROM wait-state window identity, SRAM/Flash save apertures, large-ROM EEPROM candidate
  addresses, bus width, mirroring, and external-data requirements while avoiding any ROM
  scanning or save persistence.
- `MemoryBus::configure_game_pak_save` / `import_game_pak_save` /
  `export_game_pak_save`: first explicit in-memory save backing surface. It supports
  caller-owned SRAM32K, Flash64K, Flash128K, EEPROM512, and EEPROM8K raw buffers without
  filesystem assumptions. SRAM/Flash are byte-addressable through the save aperture;
  EEPROM is raw import/export only until the serial protocol is modeled.
- `Arm7tdmi::execute_arm(instruction)`: pure CPU instruction path for data processing,
  branches, and multiplies. Register-specified shifts and multiplies need explicit cycle
  metadata later because their cost is input-dependent.
- `Arm7tdmi::execute_arm(instruction, memory)`: memory instruction path. This is the
  future bridge between instruction execution and wait-state-aware scheduling.
- Block transfers: `LDM`/`STM` are performance-significant because one instruction moves
  a register list across consecutive words. Later timing can model the first access as
  non-sequential and the rest as sequential accesses in the selected memory region.
- `Arm7tdmi::estimate_arm_cycles`: first scheduler-facing metadata layer. It reports
  functional ARM cycle classes (`S`, `N`, `I`) plus a data-dependent flag for multiply
  families, but it does not yet convert those classes through GBA memory wait states.
- `Arm7tdmi::estimate_arm_elapsed_cycles`: first elapsed-cycle composition helper. It
  combines supported instruction cycle classes with `MemoryBus::timing` for current
  internal-memory paths, leaves non-memory instructions on unit cycle-class timing,
  returns no estimate for out-of-scope memory regions, and now has a WAITCNT-aware
  overload that composes Game Pak ROM/save timing metadata without making those ranges
  readable or executable.
- `Arm7tdmi::step_arm`: first scheduler-step surface. It executes one supported ARM
  instruction, derives the effective data address for current supported memory
  instructions, and accumulates elapsed cycles only when execution succeeds.
- `Arm7tdmi::execute_thumb`: first Thumb decoder/execution seed. It covers narrow
  `MOV`, `CMP`, `ADD`, and `SUB` forms so the core can begin modeling common 16-bit
  instruction streams before broader Thumb state and fetch work exists.
- `Arm7tdmi::cpsr` / `set_cpsr` / `spsr`: first status-register and CPU-mode state
  layer. It keeps flag, interrupt-mask, Thumb-state, and mode bits visible before
  banked registers and mixed ARM/Thumb dispatch are implemented.
- `Arm7tdmi::enter_exception`: first exception-entry seed. It is not cycle-accurate yet,
  but it makes IRQ/FIQ/SWI/abort/reset transitions explicit so later timer, DMA, and
  scheduler work can trigger a modeled CPU mode transition without BIOS execution.
- `InterruptController`: first IE/IF/IME gate. It separates interrupt request flags from
  CPU exception entry, which keeps timer, DMA, and PPU sources testable before
  memory-mapped IO and BIOS dispatch exist.
- `Timers::tick`: first timer timing source. It advances four local 16-bit timers from
  CPU cycles, handles reload/overflow, supports count-up cascading, and requests timer
  IRQ flags through `InterruptController`.
- `DmaController::run_immediate`: first DMA transfer surface. It runs enabled immediate
  channels in priority order, copies 16-bit or 32-bit units through `MemoryBus`, applies
  source/destination address controls, and requests completion IRQ flags through
  `InterruptController`. Later scheduler work still needs to charge DMA bus occupancy
  and handle H-Blank, V-Blank, FIFO, and video-capture triggers.
- `PpuTiming::tick`: first LCD timing surface. It advances DISPSTAT/VCOUNT-style scanline
  state from CPU cycles, exposes visible/HBlank/VBlank/VCount-match flags, and requests
  LCD interrupt flags through `InterruptController`. It gives the eventual scheduler a
  stable video clock boundary before pixel fetch, sprite evaluation, and renderer work
  exist.
- `PpuBackgroundFetcher::fetch_text_pixel`: first regular text-background data path. It
  resolves BG control metadata into screenblock, charblock, tile, map-entry, flip,
  bit-depth, and palette reads through `MemoryBus`.
- `PpuSpriteFetcher::read_sprite` / `fetch_sprite_pixel`: first regular OBJ data path.
  It decodes OAM attributes and resolves non-affine OBJ local pixels through OBJ tile
  memory and OBJ palette memory.
- `Apu::tick`: first audio timing and sample-generation surface. It advances a 512 Hz
  frame sequencer from CPU cycles and emits deterministic 32,768 Hz mixed samples into a
  fixed-capacity core buffer.
- `Apu::timer_overflow`: first Direct Sound timing hook. It consumes FIFO A/B samples
  on the selected timer overflow, latches the latest signed sample for mixing, and
  reports FIFO refill needs for later DMA integration.
- `Apu::pop_audio_sample`: first platform-neutral audio drain surface. It lets a future
  backend consume already-mixed samples without Android, Oboe, or heap allocation in the
  core generation path.
- `IoRegisters::read16` / `write16` / `read32` / `write32`: first memory-mapped IO
  routing facade for already-modeled device state, including WAITCNT. It keeps
  unsupported and read-only addresses explicit, which matters for measurement because a
  benchmark harness can later count rejected IO paths separately from modeled register
  traffic.
- `benchmarks/core_benchmark.cpp`: first synthetic measurement harness. It times current
  no-ROM/no-BIOS workloads with `std::chrono`, prints CSV rows, and emits checksums so
  optimized builds cannot silently discard the measured work. The current rows include
  a WAITCNT-aware `MemoryBus::timing` lookup baseline and a WAITCNT-aware ARM
  elapsed-cycle estimate baseline.
- `CoreScheduler::step_arm`: first global frame-step seed. It coordinates one modeled
  CPU step with timer, PPU timing, APU frame/audio timing, Direct Sound timer-overflow
  consumption, immediate DMA, and IRQ service surfaces while keeping instruction fetch,
  BIOS dispatch, WAITCNT, triggered DMA, and platform integration out of scope.
- `CoreScheduler::step_arm_from_pc` / `run_arm_from_pc`: first scheduler-integrated
  synthetic fetch/decode loop seed. It measures memory-backed instruction fetch,
  modeled ARM execution, PC advancement, and bounded run control together while keeping
  hardware pipeline prefetch, ROM backing, BIOS dispatch, WAITCNT, and mixed ARM/Thumb
  dispatch out of scope.
- `CoreScheduler::step_from_pc` / `run_from_pc`: first dispatch-mode fetch seed. It
  selects 32-bit ARM or 16-bit Thumb fetch from CPU Thumb state, preserving branch PC
  changes and using synthetic PC advancement only for modeled non-PC-changing
  instructions.
- `CoreSession`: first owned core boundary. It keeps CPU, memory, interrupts, timers,
  DMA, PPU timing, APU, WAITCNT, and scheduler state together for future platform
  integration without JNI, Android UI, Gradle, CMake, filesystem scanning, or ROM/BIOS
  bundling.
- `CoreSession::state_hash`: first full modeled-state determinism metric. It is
  intentionally heavier than hot-path CPU/memory rows because it hashes RAM, VRAM, OAM,
  ROM backing, save backing, audio buffers, and device state for regression custody.
- `tools/check-core-benchmark.ps1`: first benchmark output gate. It validates required
  benchmark rows, positive timing/throughput fields, and nonzero checksums.
- `WaitStateControl`: first WAITCNT metadata seed. It decodes SRAM wait control,
  Game Pak wait-state 0/1/2 first and subsequent access fields, prefetch enable, and
  PHI terminal output. The value is now reachable through `IoRegisters` and can drive
  cartridge/save timing metadata through the WAITCNT-aware `MemoryBus::timing` overload;
  scheduler-facing elapsed-cycle estimates can now compose those timing descriptors,
  while actual cartridge execution and prefetch buffering remain future work.

## Phase 16 Choice

Phase 16 selects block transfers instead of a deeper memory-bus verifier because block
transfers expose the next major CPU/memory performance shape while staying inside the
existing desktop C++ verifier boundary.

The first implementation intentionally supports only narrow, deterministic forms:

- ARM block data transfer group only.
- `S`/force-user-bank bit unsupported.
- Empty register list unsupported.
- `R15` as base or in the register list unsupported.
- Writeback with base register included in the register list unsupported.
- 32-bit aligned memory accesses through the existing `MemoryBus`.

## Phase 17 Choice

Phase 17 adds cycle-accounting metadata before deeper engine components because the CPU
core needs a stable way to describe work before a scheduler can coordinate CPU, PPU,
APU, timers, DMA, and memory wait states.

The first estimator is intentionally metadata-only:

- Data-processing instructions report base `S` cycles, with register-controlled shifts
  charging an extra internal cycle.
- Branches report pipeline-refill shape as `2S + 1N`.
- Loads report transfer shape as `1S + 1N + 1I`; stores report `2N`.
- Block loads report `nS + 1N + 1I`; block stores report `(n - 1)S + 2N`.
- Multiply families are marked data-dependent because final cycle count depends on
  operand-dependent multiplier timing.
- Unsupported instructions return no estimate.

Phase 18 should decide how to convert `S`/`N`/`I` classes into elapsed cycles using
memory-region metadata and GBA wait-state control.

## Phase 18 Choice

Phase 18 adds static timing metadata to the current memory regions. It does not change
read/write behavior and does not add cartridge, BIOS, IO, or WAITCNT modeling.

The first timing table is intentionally conservative:

- EWRAM: 3 cycles for byte/halfword and 6 cycles for word access.
- IWRAM: 1 cycle for byte/halfword/word access.
- Palette, VRAM, and OAM: 1 cycle for byte/halfword and 2 cycles for word access.
- BIOS and IO: timing metadata exists but remains unreadable/unwritable in this scaffold.
- Cartridge/unknown space: Phase 18 left it out of scope; Phase 31 adds address
  metadata only, while wait-state control and backing data remain future work.

## Phase 19 Choice

Phase 19 connects `Arm7tdmi::estimate_arm_cycles` and `MemoryBus::timing` through a
small scheduler-facing elapsed-cycle calculation. It does not execute instructions,
advance a global clock, or model WAITCNT/cartridge/BIOS behavior.

The first composition rule is intentionally narrow:

- Non-memory instructions use unit cycle-class timing and report that memory timing was
  not applied.
- Supported memory instructions select byte, halfword, or word timing from the decoded
  transfer family.
- The elapsed estimate charges `S * sequential + N * nonsequential + I`.
- Cartridge, BIOS, IO, and unknown spaces return no elapsed estimate until backing data,
  IO routing, and WAITCNT-aware timing are modeled.
- Multiply families preserve their data-dependent flag, but still need a later
  operand-sensitive timing model.

## Phase 20 Choice

Phase 20 begins scheduler-level cycle accumulation without introducing a full device
scheduler. It keeps the timing state inside `Arm7tdmi`, so the current verifier can prove
that instruction execution and elapsed-cycle accounting move together.

The first step API is intentionally narrow:

- `step_arm(instruction)` executes non-memory instructions and charges elapsed cycles
  from Phase 19 metadata.
- `step_arm(instruction, memory)` executes memory instructions, derives the first
  effective data address from the current register state, and charges through
  `MemoryBus::timing`.
- Only `ExecuteStatus::executed` increments the accumulated total in this scaffold.
- Skipped-condition and unsupported instructions return their status while preserving
  the cycle total.
- No global CPU/PPU/APU/timer/DMA scheduler, WAITCNT, cartridge backing data, BIOS
  behavior, or benchmark harness exists yet.

Phase 21 selects memory mirroring metadata because it improves address behavior before
broader scheduler or PPU/DMA timing work depends on it.

## Phase 21 Choice

Phase 21 adds memory mirroring for currently implemented internal memory regions. This
improves address behavior before later scheduler, PPU, DMA, and cartridge work depends
on memory normalization.

The first mirror rules are intentionally narrow:

- EWRAM mirrors every 256 KiB across the `02xxxxxx` bank.
- IWRAM mirrors every 32 KiB across the `03xxxxxx` bank.
- Palette RAM and OAM mirror every 1 KiB across their implemented banks.
- VRAM repeats every 128 KiB across the `06xxxxxx` bank, with the final 32 KiB of each
  128 KiB window folded onto the OBJ VRAM half.
- BIOS, IO, cartridge ROM data, save backing storage, and the undocumented IO mirror
  remain out of scope; Phase 31 later adds cartridge/save address metadata only.

## Phase 22 Choice

Phase 22 begins the Thumb decoder seed because GBA programs commonly mix ARM and Thumb
code, and the core needs a narrow 16-bit instruction path before broader fetch and mode
work can be meaningful.

The first Thumb seed is intentionally narrow:

- Format 3 immediate `MOV`, `CMP`, `ADD`, and `SUB` for low registers.
- Format 2 `ADD` and `SUB` with low-register or 3-bit immediate operands.
- N/Z/C/V flag behavior for the covered arithmetic and compare forms.
- Unsupported Thumb forms return `ExecuteStatus::unsupported`.
- No Thumb branch, load/store, high-register operation, push/pop, software interrupt,
  mode switching, mixed ARM/Thumb fetch, or Thumb cycle metadata exists yet.

## Phase 23 Choice

Phase 23 adds CPSR/SPSR and CPU-mode foundations because exception entry, IRQ handling,
and accurate ARM/Thumb transitions need status-register state before they can be
implemented honestly.

The first mode seed is intentionally narrow:

- CPSR exports and imports N/Z/C/V flags, the Thumb-state bit, and ARM mode bits.
- Reset currently starts in supervisor mode with ARM state and clear flags.
- Supervisor and IRQ modes have separate SPSR storage.
- System mode is modeled as a valid mode without SPSR access.
- Invalid CPSR mode bits are rejected without mutating CPU state.
- No banked register file, FIQ/Abort/Undefined SPSR storage, exception entry,
  interrupt dispatch, vector reads, or mode-specific stack behavior exists yet.

Phase 24 should begin exception and interrupt-entry foundations without BIOS execution
or vector contents.

## Phase 24 Choice

Phase 24 begins exception and interrupt-entry foundations because timers, DMA, and real
IRQ dispatch need a CPU-visible transition target before they can be tested honestly.

The first exception seed is intentionally narrow:

- Fixed vector metadata for reset, undefined instruction, software interrupt, prefetch
  abort, data abort, IRQ, and FIQ.
- CPSR I/F interrupt-mask bits are exported/imported.
- FIQ, supervisor, abort, IRQ, and undefined modes have separate SPSR storage.
- Exception entry saves CPSR to the target SPSR, writes LR with the architected offset,
  switches mode, clears Thumb state, applies interrupt masks, and vectors PC.
- Reset entry vectors PC and applies masks without saving CPSR or mutating LR.
- No BIOS execution, vector memory reads, banked registers, mode-specific stacks,
  timer IRQ scheduling, or handler execution exists yet.

## Phase 25 Choice

Phase 25 adds timer and IRQ verification because Phase 24 gave the CPU an IRQ entry path
but no device source could request that entry yet.

The first timer/IRQ seed is intentionally narrow:

- Four local 16-bit timers with counter, reload, and control state.
- Prescaler selections for F/1, F/64, F/256, and F/1024.
- Reload value copied into the counter on start and overflow.
- Timer 1-3 count-up mode increments from the previous timer overflow; Timer 0 ignores
  count-up mode.
- Timer overflow can request timer interrupt flags 3-6.
- IE/IF/IME gating determines whether a pending IRQ line is active.
- Servicing a pending IRQ calls the Phase 24 IRQ exception entry and leaves IF
  acknowledgement explicit.
- No memory-mapped IO bus, BIOS IRQ handler, HALT behavior, audio FIFO timing, DMA
  trigger, or global device scheduler exists yet.

## Phase 26 Choice

Phase 26 adds the first DMA verifier because timers and IRQ flags now exist, and DMA is
the next major memory-bus user that will eventually affect scheduler timing.

The first DMA seed is intentionally narrow:

- Four local DMA channels with source, destination, word-count, and control state.
- Immediate-start transfers only, executed in channel priority order from 0 to 3.
- 16-bit and 32-bit memory-to-memory copies through the current `MemoryBus`.
- Increment, decrement, fixed, and destination increment/reload address controls.
- GBA-style zero word-count normalization: 0x4000 for DMA0-2 and 0x10000 for DMA3.
- Completion IRQ requests through `InterruptController`.
- No memory-mapped IO registers, H-Blank/V-Blank/special/FIFO/video-capture triggers,
  repeating scheduled triggers, DMA bus stealing/stalls, cartridge restrictions, SRAM
  restrictions, or DMA cycle timing yet.

## Phase 27 Choice

Phase 27 adds a PPU scanline timing skeleton because CPU, timer, IRQ, and DMA seeds now
need a video-clock boundary before tile/background fetch or sprite work can be modeled
honestly.

The first PPU timing seed is intentionally narrow:

- 240 visible pixels, 160 visible lines, 68 VBlank lines, and 228 total scanlines.
- 960 visible cycles, 272 HBlank cycles, 1,232 cycles per scanline, and 280,896 cycles
  per frame.
- VCOUNT and DISPSTAT-style status flags for VBlank, HBlank, and VCount match.
- Writable DISPSTAT-style interrupt-enable bits and VCount compare setting.
- VBlank, visible-line HBlank, and VCount-match IF requests through `InterruptController`.
- No pixel output, tile/background fetch, sprite/OAM evaluation, memory-mapped IO bus,
  forced blank behavior, VRAM/OAM/Palette access-window enforcement, DMA trigger
  scheduling, or global device scheduler yet.

GBATEK's DISPSTAT text notes that the HBlank flag toggles on all lines while its timing
section says HBlank interrupts are not generated inside VBlank. This scaffold preserves
that split: the HBlank flag can be observed during hidden lines, but HBlank IRQ requests
are limited to visible lines until a deeper LCD event model exists.

## Phase 28 Choice

Phase 28 adds a regular text-background fetch seed because scanline timing exists but no
PPU data path can yet consume VRAM or palette memory.

The first background seed is intentionally narrow:

- Regular text/tiled background control decode only.
- Charblock and screenblock address calculation.
- 32x32, 64x32, 32x64, and 64x64 regular-map dimensions and coordinate wrapping.
- 4bpp and 8bpp tile pixel fetches through `MemoryBus`.
- Map-entry tile id, palette bank, horizontal flip, and vertical flip handling.
- Background palette lookup and color-zero transparency reporting.
- No affine backgrounds, bitmap modes, scrolling registers, mosaic, windows, blending,
  priority composition, VRAM access contention, or scanline renderer yet.

## Phase 29 Choice

Phase 29 adds a sprite/OAM seed because backgrounds now have a first data fetch path and
the renderer needs OBJ metadata before a future priority mixer can be meaningful.

The first sprite seed is intentionally narrow:

- 128 OAM entries at 8 bytes each.
- Attribute 0/1/2 decode for regular and affine flag visibility.
- Shape/size-to-dimensions mapping for square, wide, and tall OBJ sizes.
- 9-bit signed X wrapping, Y coordinate exposure, tile id, priority, palette bank, and
  4bpp/8bpp mode decode.
- Regular non-affine horizontal/vertical flip handling.
- OBJ tile and OBJ palette lookup through `MemoryBus`.
- Color-zero transparency reporting.
- No affine matrix transforms, double-size affine coverage, 1D/2D mapping mode
  register, modes 3-5 OBJ tile restrictions, mosaic, windows, blending, OBJ priority
  mixer, sprite overflow behavior, or scanline compositor yet.

## Phase 30 Choice

Phase 30 adds an APU timing/register skeleton because CPU, timers, DMA, PPU timing, and
first rendering-data fetches now exist but no audio timing surface can participate in a
future scheduler.

The first APU seed is intentionally narrow:

- SOUNDCNT_L, SOUNDCNT_H, SOUNDCNT_X, and SOUNDBIAS register state with conservative
  writable-bit masks.
- Master sound enable gate through SOUNDCNT_X bit 7.
- Wave RAM storage while the master sound circuit is enabled.
- 512 Hz frame sequencer events from CPU cycles, with length, sweep, and envelope clock
  metadata.
- Direct Sound FIFO A/B storage as 32 signed 8-bit samples per channel.
- SOUNDCNT_H FIFO reset pulses, Direct Sound output routing bits, and timer 0/1
  selection bits.
- Timer-overflow sample consumption and FIFO refill-request metadata.
- No DMG channel waveform generation, PSG length/sweep/envelope mutation, noise LFSR,
  wave-channel playback, PCM mixing, sample-rate conversion, DMA refill integration,
  memory-mapped IO bus, SOUNDBIAS ramping, or platform audio backend yet.

## Phase 31 Choice

Phase 31 adds cartridge/save-bus metadata because CPU, memory, DMA, PPU, and APU seed
surfaces now exist but cartridge address space was still an undifferentiated unsupported
region.

The first cartridge/save seed is intentionally narrow:

- Game Pak ROM window classification for `0x08000000-0x09FFFFFF`,
  `0x0A000000-0x0BFFFFFF`, and `0x0C000000-0x0DFFFFFF`.
- Local ROM-window offsets and mirror metadata for the alternate ROM wait-state windows.
- SRAM/Flash save-aperture classification for `0x0E000000-0x0FFFFFFF`, normalized to a
  64 KiB metadata window.
- Large-ROM EEPROM candidate metadata for `0x0DFFFF00-0x0DFFFFFF`, including the serial
  1-bit bus marker.
- Bus-width and external-data requirement metadata for ROM, SRAM/Flash, and EEPROM
  candidates.
- Read/write behavior remains disabled for all cartridge/save regions because no ROM
  bytes, save files, fixture packs, storage scanner, or cartridge loader exists.
- No WAITCNT register, prefetch buffer, ROM loading, save persistence, Flash command
  state machine, EEPROM serial protocol, GPIO/RTC, or cartridge bus timing integration
  yet.

## Phase 32 Choice

Phase 32 adds memory-mapped IO register routing for already-modeled devices because the
core had timer, DMA, interrupt, PPU timing, and APU state surfaces but no shared address
router to exercise them through GBA IO addresses.

The first IO routing seed is intentionally narrow:

- `IoRegisters` is a facade over existing `InterruptController`, `Timers`,
  `DmaController`, `PpuTiming`, and `Apu` instances.
- Supported 16-bit routes include DISPSTAT/VCOUNT, timer counter/control pairs, DMA
  source/destination/count/control halves, IE/IF/IME, SOUNDCNT_L/H/X, and SOUNDBIAS.
- Supported 32-bit routes combine adjacent 16-bit registers where both halves are
  modeled and handle Direct Sound FIFO A/B word writes.
- Unsupported, unaligned, and read-only addresses fail explicitly instead of silently
  mutating state.
- No `MemoryBus` IO bank integration, WAITCNT, keypad, serial, HALT/STOP, DMA triggers,
  audio FIFO DMA refill, renderer registers, BIOS behavior, Android/JNI/UI, or benchmark
  harness exists yet.

## Phase 33 Choice

Phase 33 adds a local benchmark harness because prior performance work documented
targets and hot functions but still had no repeatable local measurement artifact.

The first harness is intentionally narrow:

- `tools/run-core-benchmarks.ps1` compiles `benchmarks/core_benchmark.cpp` with
  `g++ -std=c++17 -O2 -DNDEBUG -Wall -Wextra -Werror`.
- Benchmark output is CSV-shaped:
  `benchmark,operations,elapsed_ms,ops_per_second,ns_per_operation,checksum`.
- Workloads are synthetic and use only generated in-memory state: IWRAM read/write
  pairs, ARM ADD scheduler steps, mixed IO register traffic, immediate DMA word-copy
  units, regular BG plus OBJ pixel fetches, and timer/PPU/APU ticks.
- Checksums are emitted for each row to keep work observable under optimization.
- The harness intentionally does not load ROMs, BIOS images, save files, external
  fixtures, Android/JNI/UI code, Gradle, CMake, or third-party emulator code.

First local run on this workstation:

```text
memory_bus_iwram_rw32_pair: 1,000,000 ops, 46.123 ms, 21,681,297.93 ops/s, 46.12 ns/op
cpu_step_arm_add: 1,000,000 ops, 31.133 ms, 32,120,051.91 ops/s, 31.13 ns/op
io_register_mixed_access: 2,000,000 ops, 15.188 ms, 131,680,306.55 ops/s, 7.59 ns/op
dma_immediate_word_copy_units: 640,000 ops, 37.789 ms, 16,935,966.17 ops/s, 59.05 ns/op
ppu_bg_obj_pixel_fetches: 1,000,000 ops, 44.626 ms, 22,408,561.86 ops/s, 44.63 ns/op
device_timer_ppu_apu_ticks: 1,500,000 ops, 11.435 ms, 131,172,772.03 ops/s, 7.62 ns/op
```

These measurements are local synthetic baselines only. They are useful for detecting
regressions in this scaffold, but they are not hardware-cycle accuracy, Android-device
performance, thermal behavior, battery behavior, compatibility, or production readiness
evidence.

## Phase 34 Choice

Phase 34 adds a global core scheduler/frame-step seed because the current engine had
separate CPU, timer, DMA, PPU timing, APU timing, IO, and benchmark surfaces but no
single local coordinator that advanced already-modeled devices from CPU elapsed cycles.

The first scheduler seed is intentionally narrow:

- `CoreScheduler` is a non-owning facade over `Arm7tdmi`, `MemoryBus`,
  `InterruptController`, `Timers`, `DmaController`, `PpuTiming`, and `Apu`.
- `step_arm` executes one ARM instruction through the existing CPU/memory step path.
- Executed instructions with positive elapsed cycles advance timers, PPU timing, and
  APU frame timing by the exact CPU-reported elapsed cycles.
- The scheduler then runs currently enabled immediate DMA channels and attempts pending
  IRQ service through `InterruptController::service_pending_irq`.
- `advance_devices`, `run_immediate_dma`, and `service_pending_irq` remain separately
  callable so future frame loops can compose work without hidden side effects.
- No instruction fetch, ROM backing, BIOS vector fetch/handler execution, WAITCNT,
  HBlank/VBlank/FIFO DMA triggers, audio FIFO refill integration, renderer frame loop,
  Android/JNI/UI, Gradle, CMake, or threading model exists yet.

Phase 34 benchmark delta added a scheduler row:

```text
scheduler_step_arm_add: 2,000,000 synthetic ops, 43.223 ms, 46,272,089.14 ops/s, 21.61 ns/op
```

The scheduler row counts a synthetic bundle of CPU step plus timer/PPU/APU advancement
surface work. It is a local regression baseline, not a claim of GBA hardware-cycle
accuracy or Android-device performance.

## Phase 35 Choice

Phase 35 adds a scheduler-integrated synthetic instruction fetch/decode loop because
the Phase 34 scheduler could coordinate already-modeled devices, but callers still had
to supply instruction words manually.

The first fetch-loop seed is intentionally narrow:

- `step_arm_from_pc` reads one aligned 32-bit ARM instruction from the current PC
  through `MemoryBus::read32`.
- If fetch fails, no CPU step, device tick, DMA, IRQ service, or PC advancement occurs.
- If fetch succeeds, the instruction executes through `CoreScheduler::step_arm`.
- If the instruction is executed or skipped and did not change PC, the scheduler
  advances PC by 4 bytes.
- If the instruction changes PC, such as a modeled ARM branch, the scheduler preserves
  that PC value.
- Unsupported fetched instructions stop without synthetic PC advancement.
- `run_arm_from_pc` performs a bounded local run and reports requested, attempted,
  executed, skipped, unsupported, fetch-failure, stop-reason, final-PC, and scheduler
  cycle metadata.
- No ROM backing, BIOS vector fetch/handler execution, hardware pipeline prefetch,
  WAITCNT, ARM/Thumb mixed dispatch loop, interrupt handler execution, Android/JNI/UI,
  Gradle, CMake, or threading model exists yet.

Phase 35 benchmark delta added a fetch-loop row:

```text
scheduler_fetch_loop_arm_add_branch: 500,000 synthetic instruction steps, 43.919 ms, 11,384,646.21 ops/s, 87.84 ns/op
```

The fetch-loop row executes a generated two-instruction IWRAM program: `ADD r0, r0, #1`
followed by a backward ARM branch. It is useful for local regression detection across
instruction fetch, scheduler dispatch, PC changes, and device ticking; it is not ROM
compatibility, hardware prefetch, or Android-device performance evidence.

## Phase 36 Choice

Phase 36 adds ARM/Thumb dispatch-mode fetch because the Phase 35 loop could fetch ARM
instructions from memory but ignored the CPU Thumb-state bit that Phase 23 and Phase 22
had already exposed.

The first dispatch seed is intentionally narrow:

- `step_from_pc` chooses ARM fetch when `Arm7tdmi::thumb_state()` is false and Thumb
  fetch when it is true.
- ARM fetch remains a 32-bit `MemoryBus::read32` path and Thumb fetch uses
  `MemoryBus::read16`.
- ARM instructions advance PC by 4 and Thumb instructions advance PC by 2 only when the
  modeled instruction did not change PC and was not unsupported.
- `run_from_pc` mirrors the bounded run metadata from `run_arm_from_pc` while using
  dispatch-mode fetch.
- Thumb elapsed cycles are a seed value of one cycle for currently supported Thumb ALU
  instructions.
- No Thumb branches, high-register operations, interworking, pipeline-visible PC,
  prefetch, WAITCNT integration, BIOS dispatch, ROM backing, Android/JNI/UI, Gradle, or
  CMake exists yet.

Phase 36 benchmark delta added a Thumb dispatch row:

```text
scheduler_dispatch_thumb_add: 500,000 synthetic steps, 23.774 ms, 21,031,467.28 ops/s, 47.55 ns/op
```

## Phase 37 Choice

Phase 37 adds `tools/check-core-benchmark.ps1` as a measurement-integrity gate before
adding more timing-sensitive engine features.

The gate intentionally checks output shape and minimum evidence, not machine-specific
performance thresholds:

- It reruns `tools/run-core-benchmarks.ps1`.
- It requires every known benchmark row to be present.
- It requires positive operation count, elapsed time, throughput, and ns/op values.
- It requires nonzero checksums for all rows so optimized builds cannot silently erase
  the measured work.
- It requires the benchmark output to end with `core_benchmark: PASS`.
- It does not enforce Android device metrics, thermal behavior, battery behavior, or
  hardware comparison.

The pre-Phase 42 measurement gate adds local baseline enforcement on top of that output
shape check:

- `tools/core-benchmark-baseline.json` records expected operation counts, deterministic
  checksums, Phase 41 baseline ns/op values, and calibrated maximum median ns/op
  thresholds for every synthetic row.
- `tools/check-core-performance-regression.ps1` reruns the benchmark three times by
  default, compares median ns/op against the checked-in thresholds, verifies operation
  counts and checksums, and writes `build/core_benchmark_regression_latest.json`.
- `tools/test-core-performance-regression-gate.ps1` creates deterministic good and bad
  synthetic benchmark-output fixtures so the gate itself is tested without depending on
  host timing noise.
- `tools/calibrate-core-performance-thresholds.ps1` runs a longer benchmark sample and
  writes a proposed threshold file under `build/` so future threshold changes can be
  data-backed before editing the checked-in baseline.
- This is a local severe-regression detector, not an Android-device benchmark,
  compatibility result, thermal result, battery result, or production performance
  claim.

Final Phase 37 gate run ended with:

```text
core_benchmark_check: PASS
```

## Phase 38 Choice

Phase 38 adds `WaitStateControl` as a source-backed WAITCNT metadata seed because
Game Pak timing needs a local representation before cartridge memory, WAITCNT IO
routing, prefetch, or scheduler elapsed-cycle integration can be modeled.

The first WAITCNT seed is intentionally metadata-only:

- Reset value is `0x0000`.
- Bit 15 is treated as read-only and masked out of writes.
- SRAM wait control decodes bit values `0..3` as `4, 3, 2, 8` wait states.
- ROM wait-state 0/1/2 first access fields decode bit values `0..3` as `4, 3, 2, 8`
  wait states.
- ROM wait-state 0 second access decodes as `2` or `1` wait states.
- ROM wait-state 1 second access decodes as `4` or `1` wait states.
- ROM wait-state 2 second access decodes as `8` or `1` wait states.
- Bit 14 exposes Game Pak prefetch enable metadata.
- Bits 11-12 expose PHI terminal output metadata.
- The standard `0x4317` setting is available as a named constant for tests and future
  initialization work.
- No cartridge data reads, save persistence, prefetch buffer, 128 KiB boundary timing,
  MemoryBus integration, IO register routing, or scheduler timing integration exists
  yet.

## Phase 39 Choice

Phase 39 routes WAITCNT through `IoRegisters` before broader bus integration because the
engine now has a wait-state object but no memory-mapped way to mutate it.

The first route is intentionally narrow:

- `0x04000204` is exposed as `IoRegisters::kWaitcnt`.
- 16-bit reads/writes route to `WaitStateControl::read_control` and `write_control`.
- Bit 15 remains masked by `WaitStateControl`.
- 32-bit writes to WAITCNT are rejected if the upper halfword is not modeled, preserving
  the existing no-partial-mutation IO preflight behavior.
- No full `MemoryBus` IO bank integration, keypad, serial, HALT/STOP, or Android/JNI/UI
  scope exists yet.

## Phase 40 Choice

Phase 40 connects WAITCNT metadata to cartridge/save timing descriptors without adding
ROM bytes, save backing, or scheduler execution.

The first integration is intentionally metadata-only:

- `MemoryBus::timing(address, width, waitcnt)` returns WAITCNT ROM wait-state 0/1/2
  metadata for Game Pak ROM windows.
- SRAM/Flash save aperture timing uses WAITCNT SRAM wait control for both fields.
- EEPROM-candidate addresses continue to classify through the ROM wait-state 2 window.
- Cartridge/save regions remain `readable=false` and `writable=false`.
- Existing `MemoryBus::timing(address, width)` remains available for previous internal
  memory timing behavior and current CPU elapsed-cycle code.
- No ROM loading, save persistence, Flash/EEPROM protocol, prefetch buffer, 128 KiB
  boundary behavior, scheduler elapsed-cycle application, Android/JNI/UI, Gradle, or
  CMake scope exists yet.

## Phase 41 Choice

Phase 41 selects scheduler-facing WAITCNT elapsed-cycle composition before benchmark
thresholds because timing correctness needs a queryable estimate surface before fixed
performance budgets can be meaningful.

The first scheduler-facing integration is estimate-only:

- `Arm7tdmi::estimate_arm_elapsed_cycles(instruction, data_address, waitcnt)` composes
  supported ARM memory cycle classes with `MemoryBus::timing(address, width, waitcnt)`.
- Game Pak ROM wait-state 0/1/2 and save-aperture timing now produce elapsed-cycle
  estimates. Game Pak ROM reads exist only after an explicit caller-provided in-memory
  ROM blob is loaded; save reads and writes still reject without backing data.
- Non-memory instructions keep unit cycle-class elapsed estimates and report
  `memory_timing_applied=false`.
- BIOS, IO, unknown, unsupported instruction, unsupported memory-shape, and
  WAITCNT-applied cartridge execution timing remain out of scope.
- The benchmark harness adds `cpu_waitcnt_elapsed_estimate` and the benchmark gate
  requires that row with positive timing/throughput and a nonzero checksum.
- A pre-Phase 42 local regression gate now has loose thresholds for existing synthetic
  rows; filesystem ROM loading, save persistence, Flash/EEPROM protocol, prefetch
  buffer, Android/JNI/UI, Gradle, or CMake scope does not exist yet.

## Phase 42 Choice

Phase 42 tightens the local benchmark discipline rather than adding a new emulator
runtime feature. The first gate was intentionally loose; this phase calibrates it into a
more useful synthetic guardrail before CPU coverage, cartridge execution, or renderer
scope expands.

The calibrated threshold pass is still local-only:

- Existing benchmark rows keep deterministic operation-count and checksum checks.
- Median `ns_per_operation` ceilings are tightened from severe-regression placeholders
  to calibrated local thresholds for current synthetic rows.
- `calibrate-core-performance-thresholds.ps1` can collect longer local samples and
  produce proposed future ceilings under `build/`.
- The checked-in thresholds remain tolerant enough for workstation noise and do not
  claim Android-device, thermal, battery, compatibility, hardware accuracy, or
  production performance.
- No CPU coverage expansion, ROM loading, BIOS handling, save persistence, renderer,
  APU synthesis, Android/JNI/UI, Gradle, or CMake scope is added by this phase.

## Phases 43-44 Choice

Phases 43 and 44 add the smallest execution boundary needed before deeper timing work:
more Thumb control flow and an explicit read-only Game Pak ROM byte source.

The Phase 43 CPU seed is intentionally narrow:

- Thumb unconditional branch and supported EQ/NE conditional branch execute.
- Thumb high-register MOV/ADD/CMP execute.
- Thumb BX updates PC and switches ARM/Thumb state from target bit 0.
- Full Thumb load/store coverage, broader condition-code forms, complete register
  banking, pipeline-visible PC behavior, and prefetch behavior remain later work.

The Phase 44 cartridge boundary is intentionally explicit:

- `MemoryBus::load_game_pak_rom` accepts caller-provided in-memory bytes only.
- Game Pak ROM reads work through the existing wait0/wait1/wait2 address windows and
  mirrors after an explicit blob is loaded.
- Cartridge header metadata can be parsed from the loaded blob.
- `CoreScheduler::step_from_pc` can fetch and execute an ARM instruction from
  `0x08000000`.
- No ROM files, BIOS images, fixture packs, downloaders, storage scanners, save
  persistence, Android/JNI/UI, Gradle, or CMake scope is added.

## Phase 45 Timing Seed Choice

Phase 45 starts with a narrow, testable timing seed instead of attempting the full
pipeline/prefetch/DMA interaction model at once.

Implemented timing behavior:

- `CoreScheduler` can be constructed with a `WaitStateControl` reference.
- Game Pak ROM instruction fetches apply WAITCNT non-sequential/subsequent timing.
- Adjacent fetches in the same Game Pak window are marked sequential and use the
  subsequent access timing.
- Supported ARM data loads from explicit Game Pak ROM bytes use WAITCNT-aware elapsed
  cycle composition.
- Existing scheduler behavior is preserved when no `WaitStateControl` is attached.

Still deferred:

- Pipeline-visible PC behavior.
- Real GBA prefetch buffer fill/drain behavior.
- 128 KiB Game Pak boundary effects.
- DMA bus stealing/stalls.
- Timer/DMA/PPU interaction timing beyond existing seed device advancement.
- Android-device timing, thermal, and performance-per-watt measurement.

## Phase 46 Renderer Seed Choice

Phase 46 starts renderer work with a deterministic scanline framebuffer seed that
reuses the already-tested BG and OBJ fetchers.

Implemented renderer behavior:

- `PpuRenderer` owns a fixed-size 240x160 16-bit framebuffer.
- `render_scanline` fills backdrop color from palette entry 0.
- BG0 regular text pixels can render with scroll offsets through
  `PpuBackgroundFetcher`.
- Regular non-affine OBJ pixels can overlay BG/backdrop through `PpuSpriteFetcher`.
- OBJ priority is compared against the current BG priority, and lower OAM indices win
  equal-priority OBJ conflicts.
- Transparent BG/OBJ color index 0 leaves the prior pixel visible.

Still deferred:

- Multi-BG priority composition.
- Affine backgrounds and bitmap modes.
- Windowing, blending, mosaic, forced blank, and sprite overflow behavior.
- VRAM/OAM/palette access-window contention.
- Full frame scheduling and Android/OpenGL texture upload.

## Phase 47 APU Direct Sound Core Choice

Phase 47 starts audio output with the most scheduler-relevant path already present in
the core: Direct Sound FIFO consumption and deterministic sample buffering. This keeps
audio measurable without Android, Oboe, DMA refill triggers, ROM fixtures, or a broad
PSG implementation.

Implemented audio behavior:

- `Apu` now generates mixed stereo samples at a fixed 32,768 Hz core rate, using the
  exact 512 CPU-cycle interval derived from the current 16,777,216 Hz clock target.
- Timer overflow consumption latches Direct Sound A/B FIFO samples before mixing.
- SOUNDCNT_H Direct Sound route bits select left/right output and half/full volume.
- Mixed samples are stored in a fixed-capacity ring buffer with deterministic overwrite
  behavior under sustained generation.
- `CoreScheduler::advance_devices` observes timer0/timer1 overflow counts and routes
  selected Direct Sound FIFO consumption through the APU.

Still deferred:

- DMG square, wave, and noise waveform generation.
- Length, sweep, envelope, wave playback, noise LFSR, and SOUNDBIAS ramp behavior.
- DMA FIFO refill triggers and special DMA timing.
- Platform audio backend integration, including Android Oboe.

## Phase 48 Save Backing Choice

Phase 48 adds explicit in-memory save backing because the cartridge/save aperture had
metadata and timing descriptors but no way for the core to persist save bytes through an
API. The implementation stays deliberately platform-neutral: callers provide and receive
byte vectors; no Android filesystem, storage scanner, save-file detection, ROM fixture,
or downloader is introduced.

Implemented save behavior:

- `GamePakSaveType` selects `SRAM32K`, `Flash64K`, `Flash128K`, `EEPROM512`, or
  `EEPROM8K` raw backing.
- `configure_game_pak_save` allocates erased in-memory backing with `0xFF` bytes.
- `import_game_pak_save` accepts exact-size caller-provided data and rejects mismatched
  sizes without mutating the previous save state.
- `export_game_pak_save` returns the current raw backing bytes for caller-owned
  persistence.
- SRAM/Flash backing supports byte reads/writes through the `0x0E000000` save aperture
  and existing mirror metadata.
- EEPROM backing is intentionally raw import/export only until a serial EEPROM protocol
  is implemented.

Still deferred:

- Flash command state machine, banking, chip IDs, erase/program command timing, and
  128 KiB bank switching.
- EEPROM serial protocol, DMA-sized transfers, and address-width negotiation.
- Save type auto-detection from ROM contents.
- Filesystem persistence, Android SAF integration, and save migration UX.

## Phase 49 Save State and Determinism Seed Choice

Phase 49 adds a core-session state boundary before Android integration so determinism can
be measured while the engine is still local, synthetic, and fixture-free.

Implemented behavior:

- Each modeled core component exposes a deterministic `state_hash`.
- `CoreSessionState` stores owned CPU, memory, interrupt, timer, DMA, PPU timing, APU,
  WAITCNT, and scheduler fetch-timing state.
- `CoreSession::save_state` and `load_state` copy and restore the currently modeled
  state.
- The verifier proves that save, restore, and rerun over the same explicit in-memory
  program reaches the same final hash.

Still deferred:

- Stable cross-version binary save-state file format.
- Compression, metadata headers, compatibility negotiation, and migration.
- PPU renderer framebuffer serialization and future unmodeled hardware registers.

## Phase 50 Core Integration Boundary Choice

The roadmap's Android-facing Phase 50 remains blocked until legal test ROM execution is
more credible and Android/JNI/Gradle/UI work is explicitly authorized. This phase
therefore implements only the core-side boundary that a future JNI opaque handle can
own.

Implemented behavior:

- `CoreSession` owns all currently modeled core components.
- The API exposes explicit `step`, `run`, `save_state`, `load_state`, and `state_hash`
  methods.
- No Android platform files, JNI, Gradle, CMake, OpenGL, Oboe, SAF, lifecycle, storage
  scanning, ROM bundling, BIOS bundling, or app-store behavior is added.

## Phase 51 State-Hash Measurement Choice

Phase 51 adds a synthetic benchmark row for the new full modeled-state hash path. This
keeps save-state/determinism work measurable without starting threaded interpreters,
block caches, dynarec, or Android device profiling.

Implemented behavior:

- `core_session_state_hash` measures repeated full-session hashes while occasionally
  stepping a small explicit in-memory ROM blob and mutating SRAM backing.
- `check-core-benchmark.ps1` requires the new row.
- `core-benchmark-baseline.json` includes checksum and a loose local threshold for the
  heavy hash path.

Still deferred:

- Device-specific Android profiling.
- Threaded interpreter, block cache, ARM64 dynarec, and power/thermal profiling.

## Phase 52 Thumb Program Coverage Choice

Phase 52 hardens Thumb execution before moving to ARM status-transfer and exception
completeness because tiny legal programs are likely to use Thumb memory and stack
operations early.

Implemented behavior:

- Thumb word and byte load/store with immediate offsets.
- Thumb halfword load/store with immediate offsets.
- Thumb word, byte, halfword, signed-byte, and signed-halfword register-offset load/store
  forms where applicable.
- Thumb SP-relative word load/store.
- Thumb `PUSH` and `POP`, including LR push and PC pop through the existing aligned-PC
  seed behavior.
- Thumb `SWI` entry through the existing software-interrupt exception path without BIOS
  execution.
- `CoreScheduler::step_from_pc` now executes Thumb instructions through the memory-aware
  CPU path so scheduler-fetched Thumb programs can touch RAM.

Still deferred:

- Thumb literal-pool loads.
- Thumb long branch with link.
- Thumb multiple load/store outside `PUSH`/`POP`.
- Pipeline-visible PC behavior for loads and branches.
- BIOS service implementation or HLE.

## Phase 53 ARM CPU Completeness Choice

Phase 53 closes several ARM interpreter gaps before register banking, BIOS/HLE policy,
and deeper timing depend on ARM exception and memory-side behavior.

Implemented behavior:

- ARM `MRS` reads CPSR and mode-owned SPSR values into general registers.
- ARM `MSR` writes selected CPSR/SPSR byte fields through the existing modeled PSR state,
  preserving unselected fields.
- ARM `SWI` enters the existing software-interrupt exception path without BIOS bytes or
  vector-content fetch.
- ARM condition evaluation now covers every non-reserved ARM condition code; condition
  code `0xF` remains rejected as an unsupported/reserved encoding.
- `RRX` is modeled for data-processing operand2 and register-offset single-data-transfer
  offsets using the current carry flag.
- `SWP` and `SWPB` read the old bus value, write the source value, and update the
  destination only after both memory operations succeed.

Still deferred:

- User/privileged-mode restrictions for CPSR writes.
- Real banked register storage across all exception modes.
- Undefined-instruction exception entry for every unsupported ARM encoding.
- BIOS service execution, BIOS vector contents, or HLE policy.
- True ARM7TDMI locked-bus arbitration for `SWP`; the current behavior is deterministic
  inside the single-threaded local `MemoryBus`, but it is not a bus-contention model.
- Pipeline-visible PC, prefetch, and abort restoration timing.

Source-check notes:

- ARM7TDMI DDI0029G lists `RRX`, PSR field masks, and condition-code meanings in the
  instruction summary.
- The same manual's cycle-timing chapter lists `SWP`, `SWI`, and `MSR`/`MRS` timing
  classes and notes that an aborted `SWP` must not affect the destination register.
- Phase 53 uses those references only for local decode/behavior boundaries; it does not
  claim full ARM architecture completeness.

## Phase 54 Register Banking And Exception Realism Choice

Phase 54 makes exception-mode register state explicit before BIOS/HLE policy and legal
program harnesses depend on interrupt and supervisor behavior.

Implemented behavior:

- Mode switches preserve and restore the visible ARM7TDMI register banks currently
  needed by the scaffold: user/system shared R8-R14, SVC/IRQ/Abort/Undefined banked
  SP/LR, and FIQ banked R8-R14.
- Exception entry switches to the target exception mode before writing LR, so IRQ/SVC/FIQ
  link values land in the target mode's bank instead of the interrupted mode's LR.
- `return_from_exception(link_adjustment)` restores CPSR from the current mode SPSR and
  writes PC from the current mode LR minus the caller-provided adjustment.
- The scheduler exposes a bounded HALT seed: a latched halted state can wake when the
  interrupt controller has an enabled pending IRQ line, after which existing IRQ service
  rules still respect CPSR I masking.
- `core_session_state_hash` now includes the larger CPU banked-register state and
  scheduler HALT state; the performance baseline checksum was updated accordingly.

Still deferred:

- BIOS vector contents, BIOS IRQ dispatch, and HLE service policy.
- Full undefined-instruction exception entry for all unsupported ARM/Thumb forms.
- Data-abort restoration and base-register abort fixup.
- Exact hardware HALT/STOP IO semantics, low-power cycle behavior, and wake sources
  beyond the current IRQ-line seed.
- Privilege enforcement for user-mode CPSR control writes.

Source-check notes:

- ARM7TDMI DDI0029G documents the mode-specific visible register banks, including FIQ
  R8-R14, mode-specific SP/LR, user/system sharing, and five SPSRs.
- The same manual documents exception entry preserving CPSR to SPSR, entering ARM state,
  loading the vector PC, and recommended exception return patterns that restore CPSR
  from SPSR.

## Phase 55 BIOS/HLE Policy And SWI Boundary Choice

Phase 55 makes BIOS-dependent behavior explicit before the core tries to execute legal
programs that may use GBA software interrupts.

Implemented behavior:

- `BiosController` stores an explicit BIOS execution mode: no BIOS, future
  caller-provided BIOS, or HLE requested.
- ARM SWI comments decode the service number from the upper 8 bits of the 24-bit comment
  field.
- Thumb SWI comments decode the service number from the 8-bit immediate.
- No-BIOS mode reports deterministic vector-trap behavior without requiring BIOS bytes.
- Caller-provided BIOS mode reports vector-trap behavior and explicitly marks that BIOS
  bytes must come from a future caller-owned path.
- HLE mode returns a clean `unimplemented_service` status for both known and unknown
  services; no service is silently claimed as handled.

Still deferred:

- BIOS image loading, storage, validation, or execution.
- Any HLE implementation for arithmetic, halt, decompression, sound, or IRQ helper
  services.
- BIOS IRQ dispatch, supervisor-stack frame details, and reentrant SWI behavior.
- Public legal advice about BIOS use or redistribution.

Source-check notes:

- GBATEK documents that GBA BIOS calls are reached with SWI, that ARM SWI uses
  `NN*10000h` while Thumb uses `NN`, and that ARM mode interprets only the upper 8 bits
  of the 24-bit comment field.
- GBATEK's GBA BIOS function summary bounds the current known GBA service-number range
  used by the verifier.

## Phase 56 Memory Bus And IO Correctness Choice

Phase 56 hardens bus behavior before deeper pipeline, prefetch, DMA contention, and
renderer timing work depend on memory semantics that were previously scaffold-like.

Implemented behavior:

- `MemoryBus::read32` supports unaligned word reads by reading the aligned little-endian
  word and rotating it right by the addressed byte offset.
- Odd `read16`/`write16` and unaligned `write32` remain rejected, preserving the
  existing no-partial-mutation behavior for writes and unsupported halfword behavior.
- `MemoryBus::read_policy` explicitly classifies modeled RAM/video reads, protected BIOS
  open-bus behavior, IO facade routing, external Game Pak/save data requirements, and
  unmapped open-bus behavior without returning fake data for unmodeled regions.
- `MemoryBus::video_access_policy` exposes palette, VRAM, and OAM as future PPU
  contention hooks while marking contention timing unmodeled; unmapped addresses are
  not treated as video-accessible.
- `soft_reset` clears internal writable RAM/video state while preserving explicit
  caller-provided ROM and save backing; `hard_reset` also drops explicit ROM and save
  backing.

Still deferred:

- True open-bus data latching.
- Full IO bank integration inside `MemoryBus`; `IoRegisters` remains the modeled facade.
- PPU/CPU access-window contention enforcement and timing.
- Full instruction-family unaligned behavior beyond the current word-read rotation seed.
- BIOS data, BIOS vector contents, Android integration, filesystem loading, and external
  fixtures.

Source-check notes:

- ARM7TDMI DDI0029G documents unaligned word load rotation and treats odd halfword
  accesses as outside the normal supported path.
- GBATEK memory-map/open-bus notes bound the current decision to expose policy instead
  of manufacturing open-bus values before a latch model exists.

## Phase 57 Pipeline And Prefetch Timing Choice

Phase 57 adds a bounded pipeline/prefetch seed before deeper DMA, renderer, and legal
program compatibility work depends on cartridge timing being more than adjacent-fetch
metadata.

Implemented behavior:

- Selected ARM data-processing operands read `R15` as the current instruction address
  plus 8.
- Selected Thumb high-register source/destination reads of `R15` use the current
  instruction address plus 4.
- Game Pak fetch timing now forces a non-sequential access at 128 KiB ROM boundaries.
- WAITCNT prefetch enable now drives a capped eight-halfword scheduler prefetch buffer
  seed. Executed cartridge instructions with spare modeled cycles can refill the buffer;
  later sequential opcode fetches can report a prefetch hit and zero cartridge fetch
  cycles.
- Scheduler save-state/hash includes the prefetch-buffer count so determinism tests can
  observe the new timing state.
- The benchmark harness and performance regression baseline include
  `scheduler_prefetch_cart_loop`.

Still deferred:

- Full hardware prefetch fill/drain behavior and the documented prefetch-disable bug.
- Complete PC-visible behavior for every ARM/Thumb instruction family and PC write path.
- DMA bus stealing/stalls, PPU access contention, and timer/DMA/PPU interaction timing.
- Android-device timing, thermal, and performance-per-watt measurement.

Source-check notes:

- ARM7TDMI pipeline references document the PC as two instructions ahead for executing
  ARM code, with the compatibility-visible Thumb offset represented as plus 4.
- GBATEK WAITCNT/GamePak references document non-sequential versus sequential wait
  states, 16-bit Game Pak bus behavior, bit 14 prefetch enable, the eight-halfword
  prefetch buffer, and forced non-sequential timing at 128 KiB Game Pak ROM boundaries.

## Phase 58 Legal Program Harness Choice

Phase 58 adds a repeatable in-memory program harness before more compatibility or
optimization work. The goal is not ROM compatibility yet; it is deterministic custody
over tiny caller-provided byte blobs and their expected state.

Implemented behavior:

- `run_legal_program` resets a `CoreSession`, loads caller-provided ROM bytes through
  the existing explicit in-memory API, sets WAITCNT, runs from an explicit entry PC, and
  evaluates expected stop reason, final PC, registers, and optional final state hash.
- `run_loaded_legal_program` runs an already-loaded session and fails cleanly when no ROM
  is loaded.
- Optional header gating rejects too-small or invalid fixed-value headers before
  execution when the caller requests that check.
- Negative tests cover empty ROM blobs, unloaded ROM, invalid fixed byte, expected
  unsupported-instruction stop, and expectation mismatch reporting.
- The fixture registry now records future checked-in fixture admission rules, while the
  current verifier still uses hand-authored bytes in source code only.

Still deferred:

- Checked-in homebrew/test ROM fixtures.
- Filesystem loading, Android SAF import, storage scanning, and downloader behavior.
- Compatibility reporting, public legal claims, and third-party fixture packs.
- Broader expected-state scripting or trace comparison.

## Sources

- ARM7TDMI Technical Reference Manual, ARM DDI 0029G, Chapter 6 instruction cycle
  timing summary, instruction summary, programmer's model register banking, exception
  entry/return, PSR field masks, condition fields, pipeline PC visibility, unaligned
  word load rotation, `SWP`, `SWI`, `MSR`/`MRS`, and `RRX` references.
- GBATEK GBA-only technical reference, LCD dimensions/timings, system clock, timers,
  interrupt control, DMA registers/timing, DISPSTAT/VCOUNT behavior, WAITCNT bit
  fields, Game Pak prefetch notes, 128 KiB Game Pak boundary timing, memory-map/open-bus
  behavior, and BIOS/SWI service-number behavior.
- gbadoc DMA register and control-bit reference.
- Tonc graphics timing overview, used as a secondary cross-check for GBA LCD cycle
  constants.
- GBATEK and gbadoc OAM/OBJ attribute references, used for sprite shape/size,
  attribute, tile, and palette layout.
- Tonc regular tiled-background and sprite overview references, used for screenblock,
  charblock, map-entry, tile-size, and OBJ tile/palette layout cross-checks.
- GBATEK, gbadoc, and Tonc sound references, used for SOUNDCNT, SOUNDBIAS, Direct Sound
  FIFO, timer selection, and FIFO refill behavior.
- GBATEK GBA cartridge ROM and backup memory references, used for ROM wait-state
  windows, SRAM/Flash save aperture, EEPROM candidate address range, bus-width
  metadata, and WAITCNT timing fields.
- gbadoc REG_WAITCNT reference and Tonc waitstate control flags, used as secondary
  cross-checks for WAITCNT field names and standard setting.
- gbadoc memory layout reference, used for Game Pak ROM image windows and Cart RAM
  metadata.
- GBATEK memory mirrors reference for EWRAM, IWRAM, palette, VRAM, and OAM mirroring.
- gbadoc memory layout reference for internal-memory mirror intervals and interrupt
  handling.
- Tonc ARM assembly timing overview, used as a secondary cross-check for GBA-specific
  memory wait-state implications.
