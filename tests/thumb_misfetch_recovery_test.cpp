#include "gba/core/apu.hpp"
#include "gba/core/arm7tdmi.hpp"
#include "gba/core/bios.hpp"
#include "gba/core/core_scheduler.hpp"
#include "gba/core/dma_controller.hpp"
#include "gba/core/interrupt_controller.hpp"
#include "gba/core/memory_bus.hpp"
#include "gba/core/ppu_timing.hpp"
#include "gba/core/timers.hpp"
#include "gba/core/wait_state_control.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

#include "test_helpers.hpp"

namespace {

void write_rom_halfword(std::vector<std::uint8_t>& rom, std::size_t offset,
                        std::uint16_t halfword) {
  rom.at(offset + 0U) = static_cast<std::uint8_t>(halfword & 0xFFU);
  rom.at(offset + 1U) = static_cast<std::uint8_t>((halfword >> 8) & 0xFFU);
}

void write_rom_word(std::vector<std::uint8_t>& rom, std::size_t offset, std::uint32_t word) {
  rom.at(offset + 0U) = static_cast<std::uint8_t>(word & 0xFFU);
  rom.at(offset + 1U) = static_cast<std::uint8_t>((word >> 8) & 0xFFU);
  rom.at(offset + 2U) = static_cast<std::uint8_t>((word >> 16) & 0xFFU);
  rom.at(offset + 3U) = static_cast<std::uint8_t>((word >> 24) & 0xFFU);
}

}  // namespace

int main() {
  using gba::core::Apu;
  using gba::core::Arm7tdmi;
  using gba::core::CoreScheduler;
  using gba::core::DmaController;
  using gba::core::InterruptController;
  using gba::core::MemoryBus;
  using gba::core::PpuTiming;
  using gba::core::Timers;
  using gba::core::WaitStateControl;

  constexpr std::uint32_t kGamePakProgramBase = 0x08000000U;
  constexpr std::uint32_t kThumbStateSupervisor = 0x00000033U;

  // Thumb instructions
  constexpr std::uint16_t kThumbMovR0Imm8 = 0x2001U;  // MOV R0, #1
  constexpr std::uint16_t kThumbAddR0Imm8 = 0x3002U;  // ADD R0, #2
  constexpr std::uint16_t kThumbMovR1Imm8 = 0x2103U;  // MOV R1, #3
  constexpr std::uint16_t kThumbAddR1Imm8 = 0x3104U;  // ADD R1, #4
  constexpr std::uint16_t kThumbBranchForward2 = 0xE001U;  // B +4 (skip 1 halfword)

  // ---------------------------------------------------------------
  // Test 1: Thumb program through scheduler does NOT increment
  //         thumb_misfetch_recovery_count
  // ---------------------------------------------------------------
  {
    Arm7tdmi cpu;
    MemoryBus memory;
    InterruptController interrupts;
    Timers timers;
    DmaController dma;
    PpuTiming ppu;
    Apu apu;
    CoreScheduler scheduler(cpu, memory, interrupts, timers, dma, ppu, apu);

    cpu.reset();
    memory.reset();
    timers.reset();
    ppu.reset();
    apu.reset();
    dma.reset();
    interrupts.reset();
    scheduler.reset_scheduler_cycles();

    expect(scheduler.thumb_misfetch_recovery_count() == 0,
           "recovery counter starts at zero");

    // Load a simple Thumb program into game_pak_rom
    std::vector<std::uint8_t> rom(256);
    write_rom_halfword(rom, 0, kThumbMovR0Imm8);   // MOV R0, #1
    write_rom_halfword(rom, 2, kThumbAddR0Imm8);   // ADD R0, #2
    write_rom_halfword(rom, 4, kThumbMovR1Imm8);   // MOV R1, #3
    write_rom_halfword(rom, 6, kThumbAddR1Imm8);   // ADD R1, #4
    write_rom_halfword(rom, 8, kThumbBranchForward2);  // B +4 (skip one halfword)
    write_rom_halfword(rom, 0xC, kThumbMovR0Imm8);  // MOV R0, #1 (branch target)
    expect(memory.load_game_pak_rom(rom), "Thumb test ROM loads");

    // Set up CPU in Thumb supervisor mode
    expect(cpu.set_cpsr(kThumbStateSupervisor), "enter Thumb supervisor mode");
    cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);

    // Run several steps through the scheduler
    const gba::core::CoreSchedulerRunResult run_result = scheduler.run_from_pc(10);
    expect(run_result.stop_reason == gba::core::CoreRunStopReason::max_steps,
           "Thumb run stops at max steps");
    expect(run_result.executed_steps >= 4,
           "Thumb run executes several instructions");

    expect(scheduler.thumb_misfetch_recovery_count() == 0,
           "Thumb program does not trigger recovery counter");
  }

  // ---------------------------------------------------------------
  // Test 2: ARM mode with condition 0xF at game_pak_rom triggers the
  //         thumb_misfetch recovery heuristic
  // ---------------------------------------------------------------
  {
    Arm7tdmi cpu;
    MemoryBus memory;
    InterruptController interrupts;
    Timers timers;
    DmaController dma;
    PpuTiming ppu;
    Apu apu;
    CoreScheduler scheduler(cpu, memory, interrupts, timers, dma, ppu, apu);

    cpu.reset();
    memory.reset();
    timers.reset();
    ppu.reset();
    apu.reset();
    dma.reset();
    interrupts.reset();
    scheduler.reset_scheduler_cycles();

    expect(scheduler.thumb_misfetch_recovery_count() == 0,
           "recovery counter starts at zero");

    // Place an ARM instruction with condition 0xF (always/unconditional 0xFxxxxxxx)
    // in game_pak_rom. The heuristic checks: ARM word has condition == 0xF,
    // and the halfword at the same address is readable.
    constexpr std::uint32_t kArmConditionAlwaysWithF = 0xF1A00000U;  // ADD R0, R0, #0 with cond=0xF
    std::vector<std::uint8_t> rom(256);
    write_rom_word(rom, 0, kArmConditionAlwaysWithF);
    expect(memory.load_game_pak_rom(rom), "recovery test ROM loads");

    // Set CPU to ARM mode (CPSR T bit clear) pointing at game_pak_rom
    expect(cpu.set_cpsr(static_cast<std::uint32_t>(gba::core::CpuMode::supervisor)),
           "enter ARM supervisor mode");
    cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);

    expect(!cpu.thumb_state(), "CPU starts in ARM state");

    // Call step_from_pc which invokes the recovery heuristic
    [[maybe_unused]] const gba::core::CoreSchedulerFetchStepResult step_result =
        scheduler.step_from_pc();

    // The heuristic should have fired: the CPU was in ARM mode, the word at PC
    // has condition 0xF, and the halfword is readable.
    expect(scheduler.thumb_misfetch_recovery_count() >= 1,
           "ARM condition 0xF at game_pak_rom triggers recovery counter");
    expect(cpu.thumb_state(),
           "recovery heuristic switches CPU to Thumb state");
  }

  // ---------------------------------------------------------------
  // Test 3: ARM mode with normal condition (0xE) does NOT trigger
  //         the recovery heuristic
  // ---------------------------------------------------------------
  {
    Arm7tdmi cpu;
    MemoryBus memory;
    InterruptController interrupts;
    Timers timers;
    DmaController dma;
    PpuTiming ppu;
    Apu apu;
    CoreScheduler scheduler(cpu, memory, interrupts, timers, dma, ppu, apu);

    cpu.reset();
    memory.reset();
    timers.reset();
    ppu.reset();
    apu.reset();
    dma.reset();
    interrupts.reset();
    scheduler.reset_scheduler_cycles();

    // Normal ARM instruction with condition 0xE (always on real hardware)
    constexpr std::uint32_t kArmConditionAlways = 0xE1A00000U;  // MOV R0, R0 with cond=0xE
    std::vector<std::uint8_t> rom(256);
    write_rom_word(rom, 0, kArmConditionAlways);
    expect(memory.load_game_pak_rom(rom), "no-recovery test ROM loads");

    expect(cpu.set_cpsr(static_cast<std::uint32_t>(gba::core::CpuMode::supervisor)),
           "enter ARM supervisor mode for no-recovery test");
    cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);

    expect(!cpu.thumb_state(), "CPU starts in ARM state");

    [[maybe_unused]] const gba::core::CoreSchedulerFetchStepResult step_result =
        scheduler.step_from_pc();

    expect(scheduler.thumb_misfetch_recovery_count() == 0,
           "normal ARM condition 0xE does not trigger recovery");
    expect(!cpu.thumb_state(),
           "CPU remains in ARM state after normal instruction");
  }

  std::cout << "thumb_misfetch_recovery_test: PASS\n";
  return 0;
}
