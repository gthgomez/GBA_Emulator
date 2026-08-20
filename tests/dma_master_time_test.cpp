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
  // Test 1 — HBlank DMA: scheduler includes bus_cycles; timer invariant
  //
  // advance_devices(cycles) ticks timers and PPU by `cycles`, then
  // triggers HBlank DMA which produces bus_cycles.  The scheduler
  // advances by cycles + bus_cycles, but timers only tick by `cycles`.
  // This test documents that gap.
  // ==================================================================
  {
    auto session = std::make_unique<CoreSession>();
    session->reset();
    session->bios().set_mode(gba::core::BiosExecutionMode::hle);

    std::vector<std::uint8_t> rom(512, 0);
    for (std::size_t i = 0; i < rom.size(); i += 2) {
      put_rom_halfword(rom, i, kThumbNop);
    }
    expect(session->memory().load_game_pak_rom(rom), "HBlank ROM loads");
    session->waitcnt().write_control(
        gba::core::WaitStateControl::kStandardGamePakSetting);

    session->cpu().set_register(Arm7tdmi::kPc, 0x08000000U);
    expect(session->cpu().set_cpsr(0x00000030U), "Thumb user mode");

    session->timers().write_reload(0, 0);
    session->timers().write_control(0, 0x00C0U);
    expect(session->timers().enabled(0), "timer0 enabled");
    expect(session->timers().prescaler_divisor(0) == 1, "timer0 no prescaler");

    session->interrupts().write_interrupt_enable(
        irq_bit(InterruptSource::hblank));
    session->interrupts().write_ime(1);

    expect(session->memory().write16(0x02000000U, 0xBEEFU), "DMA source seed");
    session->dma().write_source(0, 0x02000000U);
    session->dma().write_destination(0, 0x03000000U);
    session->dma().write_word_count(0, 1);
    session->dma().write_control(0, 0xA000U);
    expect(session->dma().enabled(0), "DMA0 enabled for HBlank");

    const std::uint32_t pre_hblank = PpuTiming::kVisibleCycles - 1;
    [[maybe_unused]] const CoreDeviceTickResult pre =
        session->scheduler().advance_devices(pre_hblank);
    expect(session->timers().counter(0) == static_cast<std::uint16_t>(pre_hblank),
           "timer0 counter before HBlank equals elapsed cycles");

    const CoreDeviceTickResult result =
        session->scheduler().advance_devices(1);

    expect(result.triggered_dma.channels_executed == 1,
           "HBlank DMA fires on the HBlank-entry cycle");
    expect(result.triggered_dma.bus_cycles > 0,
           "HBlank DMA reports non-zero bus_cycles");

    const std::uint64_t expected_scheduler =
        pre_hblank + 1 + result.triggered_dma.bus_cycles;
    expect(session->scheduler().scheduler_cycles() == expected_scheduler,
           "scheduler_cycles = CPU_cycles + DMA_bus_cycles");

    const std::uint16_t timer_val = session->timers().counter(0);
    const std::uint16_t timer_without_dma =
        static_cast<std::uint16_t>((pre_hblank + 1) & 0xFFFFU);
    const std::uint16_t timer_with_dma =
        static_cast<std::uint16_t>((pre_hblank + 1 + result.triggered_dma.bus_cycles) & 0xFFFFU);

    // The master-time invariant:
    //   timer_counter >= expected_cycles_including_DMA_bus_cycles
    //
    // If this holds, timers are correctly advanced through DMA bus time.
    // If it fails (timer_val == timer_without_dma), the invariant is
    // disproved: DMA bus_cycles are invisible to timers.
    if (timer_val == timer_without_dma) {
      std::cerr << "  [INFO] timer0=" << timer_val
                << " expected_with_dma=" << static_cast<std::uint32_t>(timer_with_dma)
                << " bus_cycles=" << result.triggered_dma.bus_cycles << '\n';
      expect(false,
             "timer0 does NOT advance through HBlank DMA bus_cycles "
             "(master-time invariant DISPROVED)");
    }
    expect(timer_val == timer_with_dma,
           "timer0 advances through HBlank DMA bus_cycles "
           "(master-time invariant PROVED)");

    expect(session->memory().read16(0x03000000U).value_or(0) == 0xBEEFU,
           "HBlank DMA copied source to destination");

    expect(!session->dma().enabled(0),
           "one-shot HBlank DMA disables after transfer");
  }

  // ==================================================================
  // Test 2 — Immediate DMA: timers advance through bus_cycles
  //
  // For immediate DMA the scheduler feeds bus_cycles back into
  // advance_devices, which ticks timers.  This should pass.
  // ==================================================================
  {
    auto session = std::make_unique<CoreSession>();
    session->reset();
    session->bios().set_mode(gba::core::BiosExecutionMode::hle);

    std::vector<std::uint8_t> rom(256, 0);
    for (std::size_t i = 0; i < rom.size(); i += 2) {
      put_rom_halfword(rom, i, kThumbNop);
    }
    expect(session->memory().load_game_pak_rom(rom), "Immediate ROM loads");
    session->waitcnt().write_control(
        gba::core::WaitStateControl::kStandardGamePakSetting);

    session->cpu().set_register(Arm7tdmi::kPc, 0x08000000U);
    expect(session->cpu().set_cpsr(0x00000030U), "Thumb user mode");

    session->timers().write_reload(0, 0);
    session->timers().write_control(0, 0x00C0U);

    session->interrupts().write_ime(1);

    expect(session->memory().write16(0x02000000U, 0xCAFEU), "DMA source seed");
    session->dma().write_source(0, 0x02000000U);
    session->dma().write_destination(0, 0x03000000U);
    session->dma().write_word_count(0, 1);
    session->dma().write_control(0, 0x8000U);

    const DmaRunResult dma_result = session->scheduler().run_immediate_dma();
    expect(dma_result.channels_executed == 1, "immediate DMA executes one channel");
    expect(dma_result.bus_cycles > 0, "immediate DMA reports bus_cycles");

    const std::uint16_t timer_before = session->timers().counter(0);
    const std::uint64_t sched_before = session->scheduler().scheduler_cycles();

    const CoreDeviceTickResult dma_tick =
        session->scheduler().advance_devices(dma_result.bus_cycles);

    expect(session->timers().counter(0) ==
               static_cast<std::uint16_t>(timer_before + dma_result.bus_cycles),
           "timers advance through immediate DMA bus_cycles "
           "(master-time invariant PROVED for immediate DMA)");

    expect(session->scheduler().scheduler_cycles() == sched_before + dma_result.bus_cycles,
           "scheduler advances through immediate DMA bus_cycles");

    expect(dma_tick.cycles == dma_result.bus_cycles,
           "device-tick report matches DMA bus_cycles");

    expect(session->memory().read16(0x03000000U).value_or(0) == 0xCAFEU,
           "immediate DMA copied source to destination");
  }

  // ==================================================================
  // Test 3 — HBlank DMA: scheduler and timers stay synchronized
  //
  // After HBlank DMA fires, both scheduler and timers advance through
  // the DMA bus_cycles, so the gap between them should be 0.
  // ==================================================================
  {
    auto session = std::make_unique<CoreSession>();
    session->reset();
    session->bios().set_mode(gba::core::BiosExecutionMode::hle);

    std::vector<std::uint8_t> rom(512, 0);
    for (std::size_t i = 0; i < rom.size(); i += 2) {
      put_rom_halfword(rom, i, kThumbNop);
    }
    expect(session->memory().load_game_pak_rom(rom), "Gap ROM loads");
    session->waitcnt().write_control(
        gba::core::WaitStateControl::kStandardGamePakSetting);

    session->cpu().set_register(Arm7tdmi::kPc, 0x08000000U);
    expect(session->cpu().set_cpsr(0x00000030U), "Thumb user mode");

    session->timers().write_reload(0, 0);
    session->timers().write_control(0, 0x00C0U);

    session->interrupts().write_interrupt_enable(
        irq_bit(InterruptSource::hblank));
    session->interrupts().write_ime(1);

    expect(session->memory().write16(0x02000000U, 0xDEADU), "gap DMA seed");
    session->dma().write_source(0, 0x02000000U);
    session->dma().write_destination(0, 0x03000000U);
    session->dma().write_word_count(0, 1);
    session->dma().write_control(0, 0xA000U);

    const std::uint32_t pre_hblank = PpuTiming::kVisibleCycles - 1;
    [[maybe_unused]] const CoreDeviceTickResult pre =
        session->scheduler().advance_devices(pre_hblank);

    const CoreDeviceTickResult hblank_tick =
        session->scheduler().advance_devices(1);
    expect(hblank_tick.triggered_dma.channels_executed == 1, "HBlank DMA fires");

    const std::uint16_t timer_after_hblank = session->timers().counter(0);
    const std::uint64_t sched_after_hblank = session->scheduler().scheduler_cycles();

    const std::uint32_t gap_after_hblank =
        static_cast<std::uint32_t>(sched_after_hblank) -
        static_cast<std::uint32_t>(timer_after_hblank);
    expect(gap_after_hblank == 0,
           "scheduler-to-timer gap is 0 immediately after HBlank DMA "
           "(timers advance through DMA bus_cycles)");

    constexpr std::uint32_t kPostHblankSteps = 100;
    for (std::uint32_t i = 0; i < kPostHblankSteps; ++i) {
      [[maybe_unused]] const CoreDeviceTickResult step =
          session->scheduler().advance_devices(1);
    }

    const std::uint16_t timer_final = session->timers().counter(0);
    const std::uint64_t sched_final = session->scheduler().scheduler_cycles();

    const std::uint32_t gap_after =
        static_cast<std::uint32_t>(sched_final) -
        static_cast<std::uint32_t>(timer_final);
    expect(gap_after == 0,
           "scheduler-to-timer gap remains 0 after additional CPU-only cycles");

    expect(session->memory().read16(0x03000000U).value_or(0) == 0xDEADU,
           "gap-test DMA copied data");
  }

  // ==================================================================
  // Test 4 — CPU does NOT execute instructions during DMA
  //
  // DMA runs inside advance_devices, which is called between CPU
  // instruction executions.  The CPU program counter must not change
  // as a result of DMA bus time alone.
  // ==================================================================
  {
    auto session = std::make_unique<CoreSession>();
    session->reset();
    session->bios().set_mode(gba::core::BiosExecutionMode::hle);

    std::vector<std::uint8_t> rom(256, 0);
    for (std::size_t i = 0; i < rom.size(); i += 2) {
      put_rom_halfword(rom, i, kThumbNop);
    }
    expect(session->memory().load_game_pak_rom(rom), "CPU-during-DMA ROM loads");
    session->waitcnt().write_control(
        gba::core::WaitStateControl::kStandardGamePakSetting);

    session->cpu().set_register(Arm7tdmi::kPc, 0x08000000U);
    expect(session->cpu().set_cpsr(0x00000030U), "Thumb user mode");

    session->dma().write_source(0, 0x02000000U);
    session->dma().write_destination(0, 0x03000000U);
    session->dma().write_word_count(0, 1);
    session->dma().write_control(0, 0x8000U);

    const std::uint32_t pc_before = session->cpu().register_value(Arm7tdmi::kPc);
    const std::uint32_t r0_before = session->cpu().register_value(0);

    [[maybe_unused]] const DmaRunResult dma_result =
        session->scheduler().run_immediate_dma();

    expect(session->cpu().register_value(Arm7tdmi::kPc) == pc_before,
           "CPU PC unchanged during immediate DMA");
    expect(session->cpu().register_value(0) == r0_before,
           "CPU registers unchanged during immediate DMA");
  }

  std::cout << "dma_master_time_test: PASS\n";
  return 0;
}
