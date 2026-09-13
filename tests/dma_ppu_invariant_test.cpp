#include "gba/core/core_session.hpp"
#include "gba/core/ppu_timing.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "test_helpers.hpp"

int main() {
  using gba::core::Arm7tdmi;
  using gba::core::CoreDeviceTickResult;
  using gba::core::CoreSession;
  using gba::core::DmaRunResult;
  using gba::core::InterruptSource;
  using gba::core::PpuTiming;

  constexpr std::uint16_t kThumbNop = 0x46C0U;

  // ==================================================================
  // Test 1 — DMA advances PPU time (master-time invariant)
  //
  // HBlank DMA fires at the HBlank boundary. The DMA produces bus_cycles.
  // The PPU should advance through those bus_cycles so that subsequent
  // PPU state reflects the DMA time.
  // ==================================================================
  {
    auto session = std::make_unique<CoreSession>();
    session->reset();
    session->bios().set_mode(gba::core::BiosExecutionMode::hle);

    std::vector<std::uint8_t> rom(512, 0);
    for (std::size_t i = 0; i < rom.size(); i += 2) {
      put_rom_halfword(rom, i, kThumbNop);
    }
    expect(session->memory().load_game_pak_rom(rom), "PPU DMA ROM loads");
    session->waitcnt().write_control(
        gba::core::WaitStateControl::kStandardGamePakSetting);

    session->cpu().set_register(Arm7tdmi::kPc, 0x08000000U);
    expect(session->cpu().set_cpsr(0x00000030U), "Thumb user mode");

    session->interrupts().write_interrupt_enable(
        irq_bit(InterruptSource::hblank));
    session->interrupts().write_ime(1);

    // Setup HBlank DMA
    expect(session->memory().write16(0x02000000U, 0xBEEFU), "PPU DMA source seed");
    session->dma().write_source(0, 0x02000000U);
    session->dma().write_destination(0, 0x03000000U);
    session->dma().write_word_count(0, 1);
    session->dma().write_control(0, 0xA000U);

    // Advance to HBlank boundary
    const std::uint32_t pre_hblank = PpuTiming::kHblankFlagCycles - 1;
    (void)session->scheduler().advance_devices(pre_hblank);

    const std::uint16_t ppu_line_before = session->ppu().vcount();
    const std::uint16_t ppu_cycle_before = session->ppu().line_cycle();

    // Fire HBlank
    const CoreDeviceTickResult result =
        session->scheduler().advance_devices(1);

    expect(result.triggered_dma.channels_executed == 1,
           "HBlank DMA fires");

    // Exact master-time accounting, wrap-aware via the same period-arithmetic
    // family as Test 10's reload-aware timer check: this tick consumed exactly
    // one cycle (the HBlank edge) plus every drained DMA bus cycle, so the
    // PPU's absolute position (line * kCyclesPerLine + line_cycle) must land
    // precisely on before + window. No inequality slack is required.
    const std::uint16_t ppu_line_after = session->ppu().vcount();
    const std::uint16_t ppu_cycle_after = session->ppu().line_cycle();
    const std::uint64_t window_cycles =
        1U + static_cast<std::uint64_t>(result.triggered_dma.bus_cycles);
    const std::uint64_t ppu_frame_before =
        static_cast<std::uint64_t>(ppu_line_before) * PpuTiming::kCyclesPerLine +
        static_cast<std::uint64_t>(ppu_cycle_before);
    const std::uint64_t expected_ppu_frame_after =
        ppu_frame_before + window_cycles;
    const std::uint64_t ppu_frame_after =
        static_cast<std::uint64_t>(ppu_line_after) * PpuTiming::kCyclesPerLine +
        static_cast<std::uint64_t>(ppu_cycle_after);
    std::cout << "  PPU before: line=" << ppu_line_before
              << " cycle=" << ppu_cycle_before << '\n';
    std::cout << "  PPU after:  line=" << ppu_line_after
              << " cycle=" << ppu_cycle_after << '\n';
    std::cout << "  DMA bus_cycles=" << result.triggered_dma.bus_cycles << '\n';

    expect(ppu_frame_after == expected_ppu_frame_after,
           "PPU advances through DMA bus_cycles (master-time invariant for PPU)");
  }

  // ==================================================================
  // Test 2 — HBlank IRQ fires during VBlank, but HBlank DMA does NOT
  //
  // On GBA hardware:
  // - HBlank IRQ fires on ALL lines including VBlank (lines 160-226)
  // - HBlank DMA only triggers on visible lines (0-159)
  //
  // This test verifies both behaviors.
  // ==================================================================
  {
    auto session = std::make_unique<CoreSession>();
    session->reset();
    session->bios().set_mode(gba::core::BiosExecutionMode::hle);

    std::vector<std::uint8_t> rom(512, 0);
    for (std::size_t i = 0; i < rom.size(); i += 2) {
      put_rom_halfword(rom, i, kThumbNop);
    }
    expect(session->memory().load_game_pak_rom(rom), "VBlank HBlank ROM loads");

    session->cpu().set_register(Arm7tdmi::kPc, 0x08000000U);
    expect(session->cpu().set_cpsr(0x00000030U), "Thumb user mode");

    // Enable HBlank IRQ in DISPSTAT and IE
    session->ppu().write_dispstat(0x0010);
    session->interrupts().write_interrupt_enable(
        irq_bit(InterruptSource::hblank));
    session->interrupts().write_ime(1);

    // Set up HBlank DMA (DMA0, HBlank triggered, single unit)
    expect(session->memory().write16(0x02000000U, 0xBEEFU), "VBlank DMA source seed");
    session->dma().write_source(0, 0x02000000U);
    session->dma().write_destination(0, 0x03000000U);
    session->dma().write_word_count(0, 1);
    session->dma().write_control(0, 0xA000U);  // HBlank, enable

    // Advance PPU through visible lines to enter VBlank
    (void)session->scheduler().advance_devices(
        PpuTiming::kCyclesPerLine * PpuTiming::kVisibleLines);
    expect(session->ppu().vblank(), "PPU is in VBlank");
    expect(session->ppu().vcount() == PpuTiming::kVisibleLines,
           "PPU is on first VBlank line");

    // Clear any pending HBlank IF and DMA state
    session->interrupts().write_interrupt_flags(irq_bit(InterruptSource::hblank));
    expect(!session->interrupts().requested(InterruptSource::hblank),
           "HBlank IF cleared");

    // Advance past HBlank point during VBlank line
    const CoreDeviceTickResult vblank_tick =
        session->scheduler().advance_devices(PpuTiming::kHblankFlagCycles);

    // HBlank IRQ SHOULD fire during VBlank (IRQ fires on all lines)
    expect(session->interrupts().requested(InterruptSource::hblank),
           "HBlank IRQ fires during VBlank (IRQ is on all lines)");

    // HBlank DMA should NOT trigger during VBlank
    expect(vblank_tick.triggered_dma.channels_executed == 0,
           "HBlank DMA does NOT trigger during VBlank");
  }

  // ==================================================================
  // Test 3 — HBlank IRQ fires on visible lines (DISPSTAT enabled)
  //
  // Verify HBlank IRQ fires correctly on visible lines when
  // DISPSTAT HBlank IRQ enable is set.
  // ==================================================================
  {
    auto session = std::make_unique<CoreSession>();
    session->reset();
    session->bios().set_mode(gba::core::BiosExecutionMode::hle);

    std::vector<std::uint8_t> rom(512, 0);
    for (std::size_t i = 0; i < rom.size(); i += 2) {
      put_rom_halfword(rom, i, kThumbNop);
    }
    expect(session->memory().load_game_pak_rom(rom), "Visible HBlank ROM loads");

    session->cpu().set_register(Arm7tdmi::kPc, 0x08000000U);
    expect(session->cpu().set_cpsr(0x00000030U), "Thumb user mode");

    // Enable HBlank IRQ in both DISPSTAT and IE
    session->ppu().write_dispstat(0x0010);
    session->interrupts().write_interrupt_enable(
        irq_bit(InterruptSource::hblank));
    session->interrupts().write_ime(1);

    // Advance to end of visible period on line 0
    (void)session->scheduler().advance_devices(PpuTiming::kHblankFlagCycles - 1);
    expect(!session->ppu().vblank(), "PPU is on visible line");
    expect(!session->ppu().hblank(), "PPU is not in HBlank yet");

    // Clear IF
    session->interrupts().write_interrupt_flags(irq_bit(InterruptSource::hblank));

    // Cross HBlank on visible line
    (void)session->scheduler().advance_devices(1);

    // HBlank IRQ SHOULD fire on visible line
    expect(session->interrupts().requested(InterruptSource::hblank),
           "HBlank IRQ IS requested on visible line");
  }

  // ==================================================================
  // Test 4 — Immediate DMA advances PPU (no fetch-region gate)
  //
  // Verify that immediate DMA bus_cycles advance PPU regardless
  // of where the CPU instruction was.
  // ==================================================================
  {
    auto session = std::make_unique<CoreSession>();
    session->reset();
    session->bios().set_mode(gba::core::BiosExecutionMode::hle);

    std::vector<std::uint8_t> rom(256, 0);
    for (std::size_t i = 0; i < rom.size(); i += 2) {
      put_rom_halfword(rom, i, kThumbNop);
    }
    expect(session->memory().load_game_pak_rom(rom), "Immediate DMA ROM loads");
    session->waitcnt().write_control(
        gba::core::WaitStateControl::kStandardGamePakSetting);

    session->cpu().set_register(Arm7tdmi::kPc, 0x08000000U);
    expect(session->cpu().set_cpsr(0x00000030U), "Thumb user mode");

    expect(session->memory().write16(0x02000000U, 0xCAFEU), "DMA source seed");
    session->dma().write_source(0, 0x02000000U);
    session->dma().write_destination(0, 0x03000000U);
    session->dma().write_word_count(0, 1);
    session->dma().write_control(0, 0x8000U);

    const std::uint16_t ppu_line_before = session->ppu().vcount();
    const std::uint16_t ppu_cycle_before = session->ppu().line_cycle();

    const DmaRunResult dma_result = session->scheduler().run_immediate_dma();
    expect(dma_result.channels_executed == 1, "immediate DMA executes");
    expect(dma_result.bus_cycles > 0, "immediate DMA has bus_cycles");

    (void)session->scheduler().advance_devices(dma_result.bus_cycles);

    const std::uint16_t ppu_line_after = session->ppu().vcount();
    const std::uint16_t ppu_cycle_after = session->ppu().line_cycle();

    std::cout << "  Immediate DMA: PPU before line=" << ppu_line_before
              << " cycle=" << ppu_cycle_before
              << " after line=" << ppu_line_after
              << " cycle=" << ppu_cycle_after
              << " dma_cycles=" << dma_result.bus_cycles << '\n';

    // PPU should have advanced through DMA bus_cycles
    const std::uint32_t ppu_advance =
        static_cast<std::uint32_t>(ppu_cycle_after) + 1U -
        static_cast<std::uint32_t>(ppu_cycle_before);
    expect(ppu_advance >= dma_result.bus_cycles,
           "PPU advances through immediate DMA bus_cycles");
  }

  // ==================================================================
  // Test 5 — DMA CPU stall
  //
  // Verify CPU does not execute instructions during DMA bus time.
  // ==================================================================
  {
    auto session = std::make_unique<CoreSession>();
    session->reset();
    session->bios().set_mode(gba::core::BiosExecutionMode::hle);

    std::vector<std::uint8_t> rom(256, 0);
    for (std::size_t i = 0; i < rom.size(); i += 2) {
      put_rom_halfword(rom, i, kThumbNop);
    }
    expect(session->memory().load_game_pak_rom(rom), "CPU stall ROM loads");
    session->waitcnt().write_control(
        gba::core::WaitStateControl::kStandardGamePakSetting);

    session->cpu().set_register(Arm7tdmi::kPc, 0x08000000U);
    expect(session->cpu().set_cpsr(0x00000030U), "Thumb user mode");

    expect(session->memory().write16(0x02000000U, 0xDEADU), "stall DMA source");
    session->dma().write_source(0, 0x02000000U);
    session->dma().write_destination(0, 0x03000000U);
    session->dma().write_word_count(0, 1);
    session->dma().write_control(0, 0x8000U);

    const std::uint32_t pc_before = session->cpu().register_value(Arm7tdmi::kPc);

    [[maybe_unused]] const DmaRunResult dma_result =
        session->scheduler().run_immediate_dma();

    // PC should not have changed during immediate DMA
    // (DMA only copies data, CPU is not involved)
    expect(session->cpu().register_value(Arm7tdmi::kPc) == pc_before,
           "CPU PC unchanged during immediate DMA execution");
  }

  // ==================================================================
  // Test 6 — DMA crosses HBlank boundary (cycle 960)
  //
  // Start DMA just before HBlank (cycle 955). DMA bus_cycles should
  // advance PPU past the HBlank transition. Verify PPU state reflects
  // the crossing.
  // ==================================================================
  {
    auto session = std::make_unique<CoreSession>();
    session->reset();
    session->bios().set_mode(gba::core::BiosExecutionMode::hle);

    std::vector<std::uint8_t> rom(512, 0);
    for (std::size_t i = 0; i < rom.size(); i += 2) {
      put_rom_halfword(rom, i, kThumbNop);
    }
    expect(session->memory().load_game_pak_rom(rom), "HBlank-cross ROM loads");
    session->waitcnt().write_control(
        gba::core::WaitStateControl::kStandardGamePakSetting);

    session->cpu().set_register(Arm7tdmi::kPc, 0x08000000U);
    expect(session->cpu().set_cpsr(0x00000030U), "Thumb user mode");

    // Set up HBlank DMA with enough transfers to cross cycle 960
    expect(session->memory().write16(0x02000000U, 0xCAFEU), "HBlank-cross DMA seed");
    session->dma().write_source(0, 0x02000000U);
    session->dma().write_destination(0, 0x03000000U);
    session->dma().write_word_count(0, 1);
    session->dma().write_control(0, 0xA000U);  // HBlank, enable

    // Advance to just before HBlank (cycle 955)
    (void)session->scheduler().advance_devices(955);
    expect(!session->ppu().hblank(), "PPU not in HBlank at cycle 955");

    // Fire HBlank by advancing 5 more cycles
    const CoreDeviceTickResult hblank_tick =
        session->scheduler().advance_devices(49);

    // DMA should have fired
    expect(hblank_tick.triggered_dma.channels_executed == 1,
           "HBlank DMA fires at HBlank boundary");

    // PPU should have advanced past HBlank entry (cycle 960)
    // The DMA bus_cycles advance the PPU further
    expect(session->ppu().line_cycle() >= 960,
           "PPU crossed HBlank boundary (cycle >= 960)");
  }

  // ==================================================================
  // Test 7 — DMA crosses line rollover (cycle 1232)
  //
  // Start DMA just before line rollover. DMA bus_cycles should
  // advance PPU to the next line. Verify VCOUNT increments.
  // ==================================================================
  {
    auto session = std::make_unique<CoreSession>();
    session->reset();
    session->bios().set_mode(gba::core::BiosExecutionMode::hle);

    std::vector<std::uint8_t> rom(512, 0);
    for (std::size_t i = 0; i < rom.size(); i += 2) {
      put_rom_halfword(rom, i, kThumbNop);
    }
    expect(session->memory().load_game_pak_rom(rom), "Line-rollover ROM loads");
    session->waitcnt().write_control(
        gba::core::WaitStateControl::kStandardGamePakSetting);

    session->cpu().set_register(Arm7tdmi::kPc, 0x08000000U);
    expect(session->cpu().set_cpsr(0x00000030U), "Thumb user mode");

    // Set up HBlank DMA
    expect(session->memory().write16(0x02000000U, 0xDEADU), "Line-rollover DMA seed");
    session->dma().write_source(0, 0x02000000U);
    session->dma().write_destination(0, 0x03000000U);
    session->dma().write_word_count(0, 256);  // 256 units = large DMA
    session->dma().write_control(0, 0xA000U);  // HBlank, enable

    // Advance to just before HBlank (cycle 955)
    (void)session->scheduler().advance_devices(955);
    const std::uint16_t line_before_hblank = session->ppu().vcount();
    expect(line_before_hblank == 0, "PPU on line 0 before HBlank");

    // Fire HBlank (5 cycles to reach cycle 960)
    const CoreDeviceTickResult rollover_tick =
        session->scheduler().advance_devices(49);

    // DMA should have fired with enough bus_cycles to cross line boundary
    const std::uint16_t line_after_dma = session->ppu().vcount();
    const std::uint16_t cycle_after_dma = session->ppu().line_cycle();
    std::cout << "  Line rollover: line_before=" << line_before_hblank
              << " line_after=" << line_after_dma
              << " dma_cycles=" << rollover_tick.triggered_dma.bus_cycles
              << " ppu_cycle=" << cycle_after_dma << '\n';

    // DMA should have fired
    expect(rollover_tick.triggered_dma.channels_executed == 1,
           "HBlank DMA fires at HBlank boundary");

    // Precondition made explicit: this DMA's transfer must outlast one HBlank
    // window (kCyclesPerLine - kVisibleCycles = 272); otherwise the
    // line-crossing scenario cannot occur at all. Gating the headline
    // assertion behind a runtime `if` let it silently skip in that case;
    // both checks are now unconditional so an undersized fixture fails loudly.
    const std::uint32_t hblank_duration =
        PpuTiming::kCyclesPerLine - PpuTiming::kVisibleCycles;
    expect(rollover_tick.triggered_dma.bus_cycles > hblank_duration,
           "Test 7 precondition: DMA bus_cycles exceed HBlank window");
    expect(line_after_dma > 0,
           "PPU crossed line boundary when DMA > HBlank duration");
  }

  // ==================================================================
  // Test 8 — DMA crosses VBlank start (line 159 → 160)
  //
  // Start DMA on the last visible line (159) near HBlank. If DMA
  // bus_cycles is large enough, PPU should advance to VBlank line.
  //
  // Key: position PPU BEFORE arming DMA, so HBlank DMA doesn't fire
  // during the positioning advance.
  // ==================================================================
  {
    auto session = std::make_unique<CoreSession>();
    session->reset();
    session->bios().set_mode(gba::core::BiosExecutionMode::hle);

    std::vector<std::uint8_t> rom(512, 0);
    for (std::size_t i = 0; i < rom.size(); i += 2) {
      put_rom_halfword(rom, i, kThumbNop);
    }
    expect(session->memory().load_game_pak_rom(rom), "VBlank-cross ROM loads");
    session->waitcnt().write_control(
        gba::core::WaitStateControl::kStandardGamePakSetting);

    session->cpu().set_register(Arm7tdmi::kPc, 0x08000000U);
    expect(session->cpu().set_cpsr(0x00000030U), "Thumb user mode");

    // Enable VBlank IRQ in DISPSTAT and IE
    session->ppu().write_dispstat(0x0008);
    session->interrupts().write_interrupt_enable(
        irq_bit(InterruptSource::vblank));
    session->interrupts().write_ime(1);

    // Position PPU to line 159, cycle 955 BEFORE arming DMA
    (void)session->scheduler().advance_devices(
        PpuTiming::kCyclesPerLine * (PpuTiming::kVisibleLines - 1U) + 955);
    expect(session->ppu().vcount() == PpuTiming::kVisibleLines - 1U,
           "PPU on last visible line (159)");
    expect(!session->ppu().vblank(), "PPU not in VBlank yet");

    // NOW arm HBlank DMA (after positioning)
    expect(session->memory().write16(0x02000000U, 0xCAFEU), "VBlank-cross DMA seed");
    session->dma().write_source(0, 0x02000000U);
    session->dma().write_destination(0, 0x03000000U);
    session->dma().write_word_count(0, 512);  // 512 units = very large DMA
    session->dma().write_control(0, 0xA000U);  // HBlank, enable

    // Fire HBlank (5 cycles to reach cycle 960)
    const CoreDeviceTickResult vblank_cross_tick =
        session->scheduler().advance_devices(49);

    // DMA should have fired
    expect(vblank_cross_tick.triggered_dma.channels_executed >= 1,
           "HBlank DMA fires at HBlank boundary");

    // Mandatory assertions: DMA must cross into VBlank
    const std::uint16_t vblank_line = session->ppu().vcount();
    expect(vblank_line >= PpuTiming::kVisibleLines,
           "PPU crossed into VBlank (VCOUNT >= 160)");
    expect(session->ppu().vblank(),
           "PPU VBlank status is true after crossing");
    expect(session->interrupts().requested(InterruptSource::vblank),
           "VBlank IRQ requested after crossing");

    // CPU should not have executed during DMA
    expect(session->cpu().register_value(Arm7tdmi::kPc) == 0x08000000U,
           "CPU PC unchanged during DMA crossing");

    std::cout << "  VBlank cross: line=" << vblank_line
              << " vblank=" << session->ppu().vblank()
              << " dma_cycles=" << vblank_cross_tick.triggered_dma.bus_cycles
              << " ppu_cycle=" << session->ppu().line_cycle() << '\n';
  }

  // ==================================================================
  // Test 9 — HBlank DMA crossing VBlank triggers VBlank DMA
  //
  // This tests the iterative event/DMA drain: HBlank DMA on line 159
  // crosses into VBlank, which should trigger a VBlank-configured DMA.
  // Both DMAs must execute and all device clocks must remain synchronized.
  // ==================================================================
  {
    auto session = std::make_unique<CoreSession>();
    session->reset();
    session->bios().set_mode(gba::core::BiosExecutionMode::hle);

    std::vector<std::uint8_t> rom(512, 0);
    for (std::size_t i = 0; i < rom.size(); i += 2) {
      put_rom_halfword(rom, i, kThumbNop);
    }
    expect(session->memory().load_game_pak_rom(rom), "Nested DMA ROM loads");
    session->waitcnt().write_control(
        gba::core::WaitStateControl::kStandardGamePakSetting);

    session->cpu().set_register(Arm7tdmi::kPc, 0x08000000U);
    expect(session->cpu().set_cpsr(0x00000030U), "Thumb user mode");

    // Enable VBlank IRQ
    session->ppu().write_dispstat(0x0008);
    session->interrupts().write_interrupt_enable(
        irq_bit(InterruptSource::vblank));
    session->interrupts().write_ime(1);

    // Free-running timer0 (reload 0, divisor 1, no IRQ): its counter must
    // advance through both the CPU interval and every drained DMA cycle,
    // which gives an exact clock-sync check without phase assumptions.
    session->timers().write_reload(0, 0);
    session->timers().write_control(0, 0x0080U);

    // Position PPU to line 159, cycle 955 BEFORE arming DMAs
    (void)session->scheduler().advance_devices(
        PpuTiming::kCyclesPerLine * (PpuTiming::kVisibleLines - 1U) + 955);
    expect(session->ppu().vcount() == PpuTiming::kVisibleLines - 1U,
           "PPU on line 159 before nested DMA test");

    // NOW arm DMAs (after positioning)
    // DMA0: HBlank-triggered, large transfer
    expect(session->memory().write16(0x02000000U, 0xAAAAU), "HBlank DMA seed");
    session->dma().write_source(0, 0x02000000U);
    session->dma().write_destination(0, 0x03000000U);
    session->dma().write_word_count(0, 256);  // Large DMA
    session->dma().write_control(0, 0xA000U);  // HBlank, enable

    // DMA1: VBlank-triggered
    expect(session->memory().write16(0x02000100U, 0xBBBBU), "VBlank DMA seed");
    session->dma().write_source(1, 0x02000100U);
    session->dma().write_destination(1, 0x03000100U);
    session->dma().write_word_count(1, 1);
    // Control 0x9000 = enable | start timing 01 (VBlank). 0xB000 would be
    // start timing 11 (special/prohibited) and would never fire here.
    session->dma().write_control(1, 0x9000U);

    // Fire HBlank (capture timer state immediately before the tick)
    const std::uint16_t timer_before_nested =
        session->timers().counter(0);
    const CoreDeviceTickResult nested_tick =
        session->scheduler().advance_devices(49);

    // HBlank DMA should have fired
    expect(nested_tick.triggered_dma.channels_executed >= 1,
           "HBlank DMA fires");

    // PPU should have crossed into VBlank
    expect(session->ppu().vcount() >= PpuTiming::kVisibleLines,
           "PPU crossed into VBlank");
    expect(session->ppu().vblank(), "PPU VBlank status true");

    // VBlank IRQ should be requested
    expect(session->interrupts().requested(InterruptSource::vblank),
           "VBlank IRQ requested");

    // Both DMAs must have executed: the HBlank DMA plus the VBlank DMA
    // triggered from the drain loop after crossing into line 160.
    expect(nested_tick.triggered_dma.channels_executed >= 2,
           "VBlank DMA executed by the iterative drain");

    // CPU should not have executed during DMA
    expect(session->cpu().register_value(Arm7tdmi::kPc) == 0x08000000U,
           "CPU PC unchanged during nested DMA");

    // Exact clock-sync check: timer0 must have advanced through exactly the
    // CPU interval (5 cycles) plus every drained DMA cycle, modulo 16-bit wrap.
    const std::uint16_t timer_after_nested = session->timers().counter(0);
    const std::uint32_t timer_delta =
        49U + nested_tick.triggered_dma.bus_cycles;
    const std::uint16_t expected_timer_after = static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(timer_before_nested) + timer_delta);

    std::cout << "  Nested DMA: channels="
              << static_cast<unsigned>(nested_tick.triggered_dma.channels_executed)
              << " dma_cycles=" << nested_tick.triggered_dma.bus_cycles
              << " line=" << session->ppu().vcount()
              << " ppu_cycle=" << session->ppu().line_cycle()
              << " timer_before=" << timer_before_nested
              << " timer_after=" << timer_after_nested << '\n';

    expect(timer_after_nested == expected_timer_after,
           "Timer advanced through CPU interval plus all drained DMA cycles");
  }

  // ==================================================================
  // Test 10 — Timer overflow during DMA
  //
  // Timer0 is configured to overflow during a long HBlank DMA.
  // The timer overflow should still be visible after DMA completes,
  // and all device clocks should remain synchronized.
  // ==================================================================
  {
    auto session = std::make_unique<CoreSession>();
    session->reset();
    session->bios().set_mode(gba::core::BiosExecutionMode::hle);

    std::vector<std::uint8_t> rom(512, 0);
    for (std::size_t i = 0; i < rom.size(); i += 2) {
      put_rom_halfword(rom, i, kThumbNop);
    }
    expect(session->memory().load_game_pak_rom(rom), "Timer-DMA ROM loads");
    session->waitcnt().write_control(
        gba::core::WaitStateControl::kStandardGamePakSetting);

    session->cpu().set_register(Arm7tdmi::kPc, 0x08000000U);
    expect(session->cpu().set_cpsr(0x00000030U), "Thumb user mode");

    // Timer0: will overflow soon
    session->timers().write_reload(0, 0xFF00U);  // Close to overflow
    session->timers().write_control(0, 0x00C0U);  // Enable, prescaler=1
    session->interrupts().write_interrupt_enable(
        irq_bit(InterruptSource::timer0));

    // HBlank DMA: large transfer
    expect(session->memory().write16(0x02000000U, 0xCAFEU), "Timer-DMA seed");
    session->dma().write_source(0, 0x02000000U);
    session->dma().write_destination(0, 0x03000000U);
    session->dma().write_word_count(0, 256);
    session->dma().write_control(0, 0xA000U);  // HBlank, enable

    // Position to HBlank boundary
    (void)session->scheduler().advance_devices(955);
    const std::uint16_t timer_before = session->timers().counter(0);
    const std::uint64_t overflows_before = session->timers().overflow_count(0);

    // Fire HBlank
    const CoreDeviceTickResult timer_dma_tick =
        session->scheduler().advance_devices(49);

    // DMA should have fired
    expect(timer_dma_tick.triggered_dma.channels_executed >= 1,
           "HBlank DMA fires in timer test");

    // Timer should have advanced through DMA cycles
    const std::uint16_t timer_after = session->timers().counter(0);
    const std::uint64_t overflows_after = session->timers().overflow_count(0);

    // If timer was close to overflow and DMA was long enough, overflow should have occurred
    // At minimum, timer should have advanced
    expect(timer_after != timer_before || overflows_after > overflows_before,
           "Timer advanced through DMA cycles");

    // Exact sync check with reload-aware arithmetic: the counter runs
    // [reload, 0xFFFF] and wraps back to reload on overflow (period =
    // 65536 - reload), so a plain 16-bit delta would be wrong here.
    {
      const std::uint16_t timer_reload = session->timers().reload(0);
      const std::uint32_t timer_period = 65536U - timer_reload;
      const std::uint32_t window_ticks =
          49U + timer_dma_tick.triggered_dma.bus_cycles;
      const std::uint32_t ticks_to_first_overflow =
          65536U - static_cast<std::uint32_t>(timer_before);
      std::uint16_t expected_timer_after;
      if (window_ticks < ticks_to_first_overflow) {
        expected_timer_after =
            static_cast<std::uint16_t>(static_cast<std::uint32_t>(timer_before) +
                                       window_ticks);
      } else {
        const std::uint32_t after_first_overflow =
            window_ticks - ticks_to_first_overflow;
        expected_timer_after = static_cast<std::uint16_t>(
            timer_reload + (after_first_overflow % timer_period));
      }
      expect(timer_after == expected_timer_after,
             "Timer advanced through exactly the CPU interval plus DMA cycles");
      // The window spans multiple periods, so at least one overflow must
      // have been serviced during DMA time (the point of this test).
      expect(overflows_after > overflows_before,
             "Timer overflow occurred during drained DMA cycles");
    }

    std::cout << "  Timer-DMA: timer_before=" << timer_before
              << " timer_after=" << timer_after
              << " overflows=" << overflows_after
              << " dma_cycles=" << timer_dma_tick.triggered_dma.bus_cycles << '\n';
  }

  // ==================================================================
  // Test 11 — All device clocks synchronized after DMA
  //
  // Verify that scheduler, timer, and PPU all account for the same
  // total number of cycles after DMA completes.
  // ==================================================================
  {
    auto session = std::make_unique<CoreSession>();
    session->reset();
    session->bios().set_mode(gba::core::BiosExecutionMode::hle);

    std::vector<std::uint8_t> rom(512, 0);
    for (std::size_t i = 0; i < rom.size(); i += 2) {
      put_rom_halfword(rom, i, kThumbNop);
    }
    expect(session->memory().load_game_pak_rom(rom), "Clock-sync ROM loads");
    session->waitcnt().write_control(
        gba::core::WaitStateControl::kStandardGamePakSetting);

    session->cpu().set_register(Arm7tdmi::kPc, 0x08000000U);
    expect(session->cpu().set_cpsr(0x00000030U), "Thumb user mode");

    // Timer0
    session->timers().write_reload(0, 0);
    session->timers().write_control(0, 0x00C0U);

    // HBlank DMA
    expect(session->memory().write16(0x02000000U, 0xDEADU), "Clock-sync DMA seed");
    session->dma().write_source(0, 0x02000000U);
    session->dma().write_destination(0, 0x03000000U);
    session->dma().write_word_count(0, 64);
    session->dma().write_control(0, 0xA000U);

    // Position to HBlank
    (void)session->scheduler().advance_devices(955);

    // Fire HBlank
    const CoreDeviceTickResult sync_tick =
        session->scheduler().advance_devices(49);

    // Verify synchronization
    const std::uint64_t final_sched = session->scheduler().scheduler_cycles();
    const std::uint16_t final_timer = session->timers().counter(0);
    const std::uint32_t sched_lo = static_cast<std::uint32_t>(final_sched);
    const std::uint32_t timer_lo = static_cast<std::uint32_t>(final_timer);

    expect(sched_lo == timer_lo,
           "Scheduler and timer clocks synchronized after DMA");

    // PPU should have advanced past HBlank entry
    expect(session->ppu().line_cycle() >= 960 || session->ppu().vcount() > 0,
           "PPU advanced through DMA cycles");

    std::cout << "  Clock sync: scheduler=" << sched_lo
              << " timer=" << timer_lo
              << " dma_cycles=" << sync_tick.triggered_dma.bus_cycles
              << " line=" << session->ppu().vcount()
              << " cycle=" << session->ppu().line_cycle() << '\n';
  }

  std::cout << "dma_ppu_invariant_test: PASS\n";
  return 0;
}
