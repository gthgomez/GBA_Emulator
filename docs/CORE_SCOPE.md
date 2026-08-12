# GBA_Emulator — Current Core Scope

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

Out of scope for this repository tree (core + tools):

- Production Android app features (SAF import, GLES upload, Oboe/AAudio, lifecycle soak).
  A minimal dev shell lives in the sibling module [`../GbaEmulatorAndroid`](../GbaEmulatorAndroid)
  (Gradle/CMake/JNI bridge self-test only).
- Checked-in ROMs or BIOS images.
- Filesystem cartridge loading and save-file persistence.
- Downloaders or storage scanners.
- App-store deployment or public retail-game compatibility claims.

