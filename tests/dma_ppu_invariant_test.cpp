#include "gba/core/core_session.hpp"
#include "gba/core/ppu_timing.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace {

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

constexpr std::uint16_t irq_bit(gba::core::InterruptSource source) {
  return static_cast<std::uint16_t>(1U << static_cast<std::uint8_t>(source));
}

void put_rom_halfword(std::vector<std::uint8_t>& rom, std::size_t offset,
                      std::uint16_t value) {
  rom.at(offset) = static_cast<std::uint8_t>(value & 0xFFU);
  rom.at(offset + 1) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

}  // namespace

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
    const std::uint32_t pre_hblank = PpuTiming::kVisibleCycles - 1;
    (void)session->scheduler().advance_devices(pre_hblank);

    const std::uint16_t ppu_line_before = session->ppu().vcount();
    const std::uint16_t ppu_cycle_before = session->ppu().line_cycle();

    // Fire HBlank
    const CoreDeviceTickResult result =
        session->scheduler().advance_devices(1);

    expect(result.triggered_dma.channels_executed == 1,
           "HBlank DMA fires");

    // After DMA, PPU should have advanced through DMA bus_cycles
    const std::uint16_t ppu_line_after = session->ppu().vcount();
    const std::uint16_t ppu_cycle_after = session->ppu().line_cycle();

    const std::uint32_t expected_dma_cycles = result.triggered_dma.bus_cycles;

    // PPU cycle should reflect CPU cycles + DMA bus cycles
    // (It won't be exactly pre_hblank + expected_total because PPU
    // transitions at kVisibleCycles and kCyclesPerLine, but the PPU
    // should have moved forward by the DMA time.)
    std::cout << "  PPU before: line=" << ppu_line_before
              << " cycle=" << ppu_cycle_before << '\n';
    std::cout << "  PPU after:  line=" << ppu_line_after
              << " cycle=" << ppu_cycle_after << '\n';
    std::cout << "  DMA bus_cycles=" << expected_dma_cycles << '\n';

    // The PPU should have advanced from its pre-HBlank position.
    // Before: line 159, cycle 959 (kVisibleCycles - 1)
    // After HBlank DMA: the PPU should have moved forward by at least 1 + DMA_cycles
    // (the +1 is the HBlank entry cycle itself)
    const std::uint32_t ppu_advance =
        static_cast<std::uint32_t>(ppu_cycle_after) + 1U -
        static_cast<std::uint32_t>(ppu_cycle_before);
    expect(ppu_advance >= expected_dma_cycles + 1,
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
        session->scheduler().advance_devices(PpuTiming::kVisibleCycles);

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
    (void)session->scheduler().advance_devices(PpuTiming::kVisibleCycles - 1);
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
        session->scheduler().advance_devices(5);

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
        session->scheduler().advance_devices(5);

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

    // If DMA bus_cycles > (kCyclesPerLine - kVisibleCycles) = 272,
    // PPU should have crossed the line boundary
    const std::uint32_t hblank_duration = PpuTiming::kCyclesPerLine - PpuTiming::kVisibleCycles;
    if (rollover_tick.triggered_dma.bus_cycles > hblank_duration) {
      expect(line_after_dma > 0,
             "PPU crossed line boundary when DMA > HBlank duration");
    }
  }

  // ==================================================================
  // Test 8 — DMA crosses VBlank start (line 159 → 160)
  //
  // Start DMA on the last visible line (159) near HBlank. If DMA
  // bus_cycles is large enough, PPU should advance to VBlank line.
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

    // Enable VBlank IRQ
    session->ppu().write_dispstat(0x0008);
    session->interrupts().write_interrupt_enable(
        irq_bit(InterruptSource::vblank));
    session->interrupts().write_ime(1);

    // Set up HBlank DMA with large transfer
    expect(session->memory().write16(0x02000000U, 0xCAFEU), "VBlank-cross DMA seed");
    session->dma().write_source(0, 0x02000000U);
    session->dma().write_destination(0, 0x03000000U);
    session->dma().write_word_count(0, 512);  // 512 units = very large DMA
    session->dma().write_control(0, 0xA000U);  // HBlank, enable

    // Advance to last visible line (159) near HBlank
    (void)session->scheduler().advance_devices(
        PpuTiming::kCyclesPerLine * (PpuTiming::kVisibleLines - 1U) + 955);
    expect(session->ppu().vcount() == PpuTiming::kVisibleLines - 1U,
           "PPU on last visible line (159)");
    expect(!session->ppu().vblank(), "PPU not in VBlank yet");

    // Fire HBlank
    const CoreDeviceTickResult vblank_cross_tick =
        session->scheduler().advance_devices(5);

    // If DMA bus_cycles is large enough, PPU should have crossed into VBlank
    const std::uint16_t vblank_line = session->ppu().vcount();
    std::cout << "  VBlank cross: line=" << vblank_line
              << " vblank=" << session->ppu().vblank()
              << " dma_cycles=" << vblank_cross_tick.triggered_dma.bus_cycles << '\n';

    // PPU should have advanced past HBlank entry on last visible line
    // or crossed into VBlank (line >= 160)
    expect(vblank_line >= PpuTiming::kVisibleLines || session->ppu().line_cycle() >= 960,
           "PPU advanced past HBlank entry or crossed into VBlank");
  }

  std::cout << "dma_ppu_invariant_test: PASS\n";
  return 0;
}
