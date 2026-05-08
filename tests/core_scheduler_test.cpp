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

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

namespace {

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void expect_read32(const gba::core::MemoryBus& memory, std::uint32_t address,
                   std::uint32_t expected, std::string_view message) {
  const std::optional<std::uint32_t> value = memory.read32(address);
  expect(value.has_value(), message);
  expect(value.value() == expected, message);
}

constexpr std::uint16_t irq_bit(gba::core::InterruptSource source) {
  return static_cast<std::uint16_t>(1U << static_cast<std::uint8_t>(source));
}

std::vector<std::uint8_t> rom_with_word(std::uint32_t word) {
  std::vector<std::uint8_t> rom(256);
  rom.at(0) = static_cast<std::uint8_t>(word & 0xFFU);
  rom.at(1) = static_cast<std::uint8_t>((word >> 8) & 0xFFU);
  rom.at(2) = static_cast<std::uint8_t>((word >> 16) & 0xFFU);
  rom.at(3) = static_cast<std::uint8_t>((word >> 24) & 0xFFU);
  return rom;
}

void write_rom_word(std::vector<std::uint8_t>& rom, std::size_t offset, std::uint32_t word) {
  rom.at(offset + 0U) = static_cast<std::uint8_t>(word & 0xFFU);
  rom.at(offset + 1U) = static_cast<std::uint8_t>((word >> 8) & 0xFFU);
  rom.at(offset + 2U) = static_cast<std::uint8_t>((word >> 16) & 0xFFU);
  rom.at(offset + 3U) = static_cast<std::uint8_t>((word >> 24) & 0xFFU);
}

void write_rom_halfword(std::vector<std::uint8_t>& rom, std::size_t offset,
                        std::uint16_t halfword) {
  rom.at(offset + 0U) = static_cast<std::uint8_t>(halfword & 0xFFU);
  rom.at(offset + 1U) = static_cast<std::uint8_t>((halfword >> 8) & 0xFFU);
}

}  // namespace

int main() {
  using gba::core::Apu;
  using gba::core::Arm7tdmi;
  using gba::core::CoreScheduler;
  using gba::core::DmaController;
  using gba::core::DirectSoundChannel;
  using gba::core::ExecuteStatus;
  using gba::core::InterruptController;
  using gba::core::InterruptSource;
  using gba::core::MemoryBus;
  using gba::core::PpuTiming;
  using gba::core::Timers;
  using gba::core::WaitStateControl;

  constexpr std::uint32_t kAddR0R0Imm1 = 0xE2800001U;
  constexpr std::uint32_t kAddEqR0R0Imm1 = 0x02800001U;
  constexpr std::uint32_t kAddR1R1Imm2 = 0xE2811002U;
  constexpr std::uint32_t kBranchPlusZero = 0xEA000000U;
  constexpr std::uint32_t kUnsupportedInstruction = 0xEC000000U;
  constexpr std::uint32_t kSyntheticProgramBase = 0x03000000U;
  constexpr std::uint32_t kGamePakProgramBase = 0x08000000U;
  constexpr std::uint32_t kThumbStateSupervisor = 0x00000033U;
  constexpr std::uint16_t kThumbMovR2Imm3 = 0x2203U;
  constexpr std::uint16_t kThumbAddR2Imm4 = 0x3204U;
  constexpr std::uint16_t kThumbStrR0R1Imm0 = 0x6008U;
  constexpr std::uint16_t kThumbLdrR2R1Imm0 = 0x680AU;
  constexpr std::uint16_t kThumbSwi0 = 0xDF00U;
  constexpr std::uint16_t kThumbBranchPlusOneHalfword = 0xE001U;
  constexpr std::uint16_t kThumbBranchPlusTwoHalfwords = 0xE002U;
  constexpr std::uint32_t kLdrR1FromR2Plus0x10 = 0xE5921010U;

  Arm7tdmi cpu;
  MemoryBus memory;
  InterruptController interrupts;
  Timers timers;
  DmaController dma;
  PpuTiming ppu;
  Apu apu;
  CoreScheduler scheduler(cpu, memory, interrupts, timers, dma, ppu, apu);

  timers.write_reload(0, 0xFFF0);
  timers.write_control(0, 0x0080);
  const gba::core::CoreSchedulerStepResult first = scheduler.step_arm(kAddR0R0Imm1);
  expect(first.cpu_step.status == ExecuteStatus::executed, "scheduler executes ARM step");
  expect(first.cpu_step.elapsed_cycles == 1, "scheduler captures CPU elapsed cycle");
  expect(first.devices.cycles == 1, "scheduler advances devices by CPU cycles");
  expect(first.scheduler_cycles == 1, "scheduler accumulates global cycles");
  expect(cpu.register_value(0) == 1, "scheduler mutates CPU through step");
  expect(timers.counter(0) == 0xFFF1, "scheduler ticks timers");
  expect(ppu.line_cycle() == 1, "scheduler ticks PPU timing");

  const gba::core::CoreSchedulerStepResult skipped = scheduler.step_arm(kAddEqR0R0Imm1);
  expect(skipped.cpu_step.status == ExecuteStatus::skipped_condition,
         "condition-failed instruction is reported");
  expect(skipped.devices.cycles == 0, "skipped instruction does not advance devices");
  expect(scheduler.scheduler_cycles() == 1, "skipped instruction preserves global cycles");
  expect(timers.counter(0) == 0xFFF1, "skipped instruction preserves timer counter");

  const gba::core::CoreDeviceTickResult apu_tick =
      scheduler.advance_devices(Apu::kCpuCyclesPerFrameSequencerStep);
  expect(apu_tick.cycles == Apu::kCpuCyclesPerFrameSequencerStep,
         "manual device advance reports cycles");
  expect(scheduler.scheduler_cycles() == 1U + Apu::kCpuCyclesPerFrameSequencerStep,
         "manual device advance accumulates cycles");
  expect(!apu_tick.apu_frame_step.has_value(),
         "disabled APU reports no frame step during device advance");

  apu.write_soundcnt_x(0x0080);
  const gba::core::CoreDeviceTickResult enabled_apu_tick =
      scheduler.advance_devices(Apu::kCpuCyclesPerFrameSequencerStep);
  expect(enabled_apu_tick.apu_frame_step.has_value(),
         "enabled APU frame sequencer is surfaced by scheduler");
  expect(enabled_apu_tick.apu_frame_step->step == 1,
         "scheduler reports APU frame step metadata");

  timers.reset();
  ppu.reset();
  apu.reset();
  scheduler.reset_scheduler_cycles();
  apu.write_soundcnt_x(0x0080);
  apu.write_soundcnt_h(0x0304);
  apu.write_fifo(DirectSoundChannel::a, 0x04030201);
  timers.write_reload(0, 0xFFFF);
  timers.write_control(0, 0x0080);
  const gba::core::CoreDeviceTickResult direct_sound_tick =
      scheduler.advance_devices(1);
  expect(direct_sound_tick.apu_timer_events == 1,
         "scheduler reports timer-driven APU FIFO event");
  expect(direct_sound_tick.last_direct_sound.fifo_a.produced,
         "scheduler routes timer0 overflow into Direct Sound A");
  expect(direct_sound_tick.last_direct_sound.fifo_a.sample == 1,
         "scheduler-routed Direct Sound A pops first FIFO byte");
  expect(apu.fifo_size(DirectSoundChannel::a) == 3,
         "scheduler-routed Direct Sound A consumes one FIFO sample");

  scheduler.reset_scheduler_cycles();
  expect(scheduler.scheduler_cycles() == 0, "scheduler cycle reset clears local clock");
  expect(cpu.elapsed_cycles() != 0, "scheduler cycle reset does not reset CPU clock");

  expect(memory.write32(0x02000000, 0xAABBCCDD), "seed DMA source");
  dma.write_source(0, 0x02000000);
  dma.write_destination(0, 0x03000000);
  dma.write_word_count(0, 1);
  dma.write_control(0, 0x8400);
  const gba::core::CoreSchedulerStepResult dma_step = scheduler.step_arm(kAddR0R0Imm1);
  expect(dma_step.immediate_dma.channels_executed == 1,
         "scheduler runs enabled immediate DMA after CPU step");
  expect(dma_step.immediate_dma.units_transferred == 1,
         "scheduler reports DMA transfer units");
  expect_read32(memory, 0x03000000, 0xAABBCCDD, "scheduler DMA copied word");

  cpu.reset();
  scheduler.reset_scheduler_cycles();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timers.write_reload(0, 0xFFFF);
  timers.write_control(0, 0x00C0);
  interrupts.write_interrupt_enable(irq_bit(InterruptSource::timer0));
  interrupts.write_ime(1);
  const gba::core::CoreSchedulerStepResult irq_step = scheduler.step_arm(kAddR0R0Imm1);
  expect(interrupts.requested(InterruptSource::timer0),
         "scheduler device tick can request timer IRQ");
  expect(!irq_step.irq_serviced, "scheduler defers same-instruction timer IRQ service");
  bool arm_irq_serviced = false;
  for (int i = 0; i < 6 && !arm_irq_serviced; ++i) {
    arm_irq_serviced = scheduler.step_arm(kAddR0R0Imm1).irq_serviced;
  }
  expect(arm_irq_serviced, "scheduler services pending ARM IRQ after recognition latency");
  expect(cpu.current_mode() == gba::core::CpuMode::irq,
         "scheduler IRQ service enters IRQ mode");
  expect(cpu.register_value(Arm7tdmi::kPc) == 0x18, "scheduler IRQ service vectors PC");
  expect(cpu.register_value(Arm7tdmi::kLinkRegister) == 8,
         "scheduler IRQ service writes IRQ-bank LR for the next ARM instruction");

  cpu.reset();
  scheduler.reset_scheduler_cycles();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  expect(cpu.set_cpsr(0x00000030), "scheduler Thumb IRQ seed enters user Thumb mode");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  timers.write_reload(0, 0xFFFF);
  timers.write_control(0, 0x00C0);
  interrupts.write_interrupt_enable(irq_bit(InterruptSource::timer0));
  interrupts.write_ime(1);
  const gba::core::CoreSchedulerStepResult thumb_irq_step =
      scheduler.step_thumb(0x1C40U);
  expect(interrupts.requested(InterruptSource::timer0),
         "scheduler Thumb device tick can request timer IRQ");
  expect(!thumb_irq_step.irq_serviced,
         "scheduler defers same-instruction Thumb timer IRQ service");
  bool thumb_irq_serviced = false;
  for (int i = 0; i < 6 && !thumb_irq_serviced; ++i) {
    thumb_irq_serviced = scheduler.step_thumb(0x1C40U).irq_serviced;
  }
  expect(thumb_irq_serviced,
         "scheduler services pending Thumb IRQ after recognition latency");
  expect(cpu.current_mode() == gba::core::CpuMode::irq,
         "scheduler Thumb IRQ service enters IRQ mode");
  expect(cpu.register_value(Arm7tdmi::kPc) == 0x18,
         "scheduler Thumb IRQ service vectors PC");
  expect(cpu.register_value(Arm7tdmi::kLinkRegister) == kGamePakProgramBase + 6U,
         "scheduler Thumb IRQ service writes LR for the next Thumb instruction");

  cpu.reset();
  scheduler.reset_scheduler_cycles();
  interrupts.reset();
  scheduler.halt_until_interrupt();
  expect(scheduler.halted(), "HALT seed latches halted state");
  expect(!scheduler.wake_from_halt_if_irq_pending(),
         "HALT seed stays halted without pending IRQ line");
  interrupts.write_interrupt_enable(irq_bit(InterruptSource::timer0));
  interrupts.write_ime(1);
  interrupts.request(InterruptSource::timer0);
  expect(scheduler.wake_from_halt_if_irq_pending(),
         "HALT seed wakes on enabled pending IRQ line");
  expect(!scheduler.halted(), "HALT seed clears halted state after wake");
  expect(scheduler.service_pending_irq(), "woken HALT seed can service IRQ");
  expect(cpu.current_mode() == gba::core::CpuMode::irq,
         "woken HALT seed enters IRQ mode when CPSR I is clear");
  expect(cpu.register_value(Arm7tdmi::kPc) == 0x18, "woken HALT seed vectors PC");

  cpu.reset();
  memory.hard_reset();
  scheduler.reset_scheduler_cycles();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  gba::core::BiosController bios;
  bios.set_mode(gba::core::BiosExecutionMode::hle);
  WaitStateControl hle_waitcnt;
  CoreScheduler hle_irq_scheduler(cpu, memory, interrupts, timers, dma, ppu, apu,
                                  hle_waitcnt, bios);
  std::vector<std::uint8_t> irq_rom(256);
  write_rom_halfword(irq_rom, 0, 0x2701U);
  write_rom_halfword(irq_rom, 2, 0x4770U);
  expect(memory.load_game_pak_rom(irq_rom), "HLE IRQ handler ROM loads");
  expect(memory.write32(0x03007FFCU, 0x08000001U), "HLE IRQ user handler pointer writes");
  cpu.set_register(7, 0xDEADBEEFU);
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase + 0x10U);
  interrupts.write_interrupt_enable(irq_bit(InterruptSource::timer0));
  interrupts.write_ime(1);
  interrupts.request(InterruptSource::timer0);
  expect(hle_irq_scheduler.service_pending_irq(), "HLE IRQ seed enters IRQ mode");
  expect(cpu.register_value(Arm7tdmi::kPc) == 0x18, "HLE IRQ seed vectors to BIOS IRQ");
  expect(hle_irq_scheduler.step_from_pc().step.has_value(),
         "HLE IRQ seed dispatches to user handler");
  expect(hle_irq_scheduler.step_from_pc().step.has_value(),
         "HLE IRQ user handler executes clobber");
  expect(cpu.register_value(7) == 1, "HLE IRQ user handler clobbers r7 before return");
  expect(hle_irq_scheduler.step_from_pc().step.has_value(),
         "HLE IRQ user handler branches to return sentinel");
  expect(hle_irq_scheduler.step_from_pc().step.has_value(),
         "HLE IRQ return sentinel restores interrupted context");
  expect(cpu.register_value(7) == 0xDEADBEEFU,
         "HLE IRQ return restores r7 along with caller-saved registers");

  cpu.reset();
  memory.reset();
  scheduler.reset_scheduler_cycles();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  timers.write_reload(0, 0xFFF0);
  timers.write_control(0, 0x0080);
  expect(memory.write32(kSyntheticProgramBase, kAddR0R0Imm1), "seed fetched ADD");
  expect(memory.write32(kSyntheticProgramBase + 4U, kAddEqR0R0Imm1),
         "seed skipped fetched ADD");

  const gba::core::CoreSchedulerFetchStepResult fetched_add = scheduler.step_arm_from_pc();
  expect(!fetched_add.fetch_failed, "fetch step reads instruction from memory");
  expect(fetched_add.fetch_address == kSyntheticProgramBase,
         "fetch step reports fetch address");
  expect(fetched_add.instruction.has_value() &&
             fetched_add.instruction.value() == kAddR0R0Imm1,
         "fetch step reports fetched instruction");
  expect(fetched_add.step.has_value(), "fetch step reports scheduler step");
  expect(fetched_add.step->cpu_step.status == ExecuteStatus::executed,
         "fetch step executes fetched ADD");
  expect(fetched_add.pc_advanced, "fetch step advances PC after non-branch execution");
  expect(cpu.register_value(Arm7tdmi::kPc) == kSyntheticProgramBase + 4U,
         "fetch step advances PC by one ARM word");
  expect(cpu.register_value(0) == 1, "fetch step mutates CPU register");
  expect(scheduler.scheduler_cycles() == 1, "fetch step advances scheduler cycles");

  const gba::core::CoreSchedulerFetchStepResult fetched_skip = scheduler.step_arm_from_pc();
  expect(fetched_skip.step.has_value(), "skipped fetch step reports scheduler step");
  expect(fetched_skip.step->cpu_step.status == ExecuteStatus::skipped_condition,
         "fetch step reports skipped condition");
  expect(fetched_skip.pc_advanced, "skipped fetched instruction still advances PC");
  expect(cpu.register_value(Arm7tdmi::kPc) == kSyntheticProgramBase + 8U,
         "skipped fetched instruction advances PC by one ARM word");
  expect(scheduler.scheduler_cycles() == 1,
         "skipped fetched instruction does not advance scheduler cycles");

  cpu.reset();
  memory.reset();
  scheduler.reset_scheduler_cycles();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  expect(memory.write32(kSyntheticProgramBase, kAddR0R0Imm1), "seed run ADD");
  expect(memory.write32(kSyntheticProgramBase + 4U, kBranchPlusZero),
         "seed run branch");
  expect(memory.write32(kSyntheticProgramBase + 12U, kAddR1R1Imm2),
         "seed branch target ADD");
  const gba::core::CoreSchedulerRunResult run = scheduler.run_arm_from_pc(3);
  expect(run.stop_reason == gba::core::CoreRunStopReason::max_steps,
         "run stops at requested max steps");
  expect(run.requested_steps == 3, "run records requested max steps");
  expect(run.attempted_steps == 3, "run records attempted fetched steps");
  expect(run.executed_steps == 3, "run records executed fetched steps");
  expect(run.skipped_steps == 0, "run records skipped count");
  expect(run.unsupported_steps == 0, "run records unsupported count");
  expect(run.fetch_failures == 0, "run records fetch failures");
  expect(cpu.register_value(0) == 1, "run executes first ADD");
  expect(cpu.register_value(1) == 2, "run executes branch target ADD");
  expect(run.final_pc == kSyntheticProgramBase + 16U,
         "run preserves branch PC change and advances target ADD");
  expect(run.scheduler_cycles == 5, "run accumulates ADD, branch, ADD cycles");

  cpu.reset();
  memory.reset();
  scheduler.reset_scheduler_cycles();
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  expect(memory.write32(kSyntheticProgramBase, kUnsupportedInstruction),
         "seed unsupported fetched instruction");
  const gba::core::CoreSchedulerRunResult unsupported_run = scheduler.run_arm_from_pc(4);
  expect(unsupported_run.stop_reason ==
             gba::core::CoreRunStopReason::unsupported_instruction,
         "run stops on unsupported fetched instruction");
  expect(unsupported_run.attempted_steps == 1,
         "unsupported run records attempted fetched instruction");
  expect(unsupported_run.unsupported_steps == 1,
         "unsupported run records unsupported count");
  expect(unsupported_run.final_pc == kSyntheticProgramBase,
         "unsupported fetched instruction does not advance PC");

  cpu.reset();
  memory.reset();
  scheduler.reset_scheduler_cycles();
  cpu.set_register(Arm7tdmi::kPc, 0x12000000U);
  const gba::core::CoreSchedulerRunResult fetch_failed_run = scheduler.run_arm_from_pc(4);
  expect(fetch_failed_run.stop_reason == gba::core::CoreRunStopReason::fetch_failed,
         "run stops on failed instruction fetch");
  expect(fetch_failed_run.attempted_steps == 0,
         "fetch failure does not count as attempted execution");
  expect(fetch_failed_run.fetch_failures == 1, "fetch failure count is recorded");
  expect(fetch_failed_run.final_pc == 0x12000000U, "fetch failure preserves PC");

  cpu.reset();
  memory.reset();
  scheduler.reset_scheduler_cycles();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  expect(cpu.set_cpsr(kThumbStateSupervisor), "scheduler dispatch enters Thumb state");
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  expect(memory.write16(kSyntheticProgramBase, kThumbMovR2Imm3), "seed Thumb MOV");
  expect(memory.write16(kSyntheticProgramBase + 2U, kThumbAddR2Imm4),
         "seed Thumb ADD");
  const gba::core::CoreSchedulerFetchStepResult thumb_mov = scheduler.step_from_pc();
  expect(thumb_mov.instruction_set == gba::core::CoreInstructionSet::thumb,
         "dispatch fetch reports Thumb instruction set");
  expect(thumb_mov.instruction_width_bytes == 2,
         "dispatch fetch reports Thumb instruction width");
  expect(thumb_mov.instruction.has_value() &&
             thumb_mov.instruction.value() == kThumbMovR2Imm3,
         "dispatch fetch reports Thumb instruction");
  expect(thumb_mov.step.has_value(), "dispatch fetch reports Thumb scheduler step");
  expect(thumb_mov.step->cpu_step.status == ExecuteStatus::executed,
         "dispatch fetch executes Thumb instruction");
  expect(thumb_mov.step->cpu_step.elapsed_cycles == 1,
         "Thumb dispatch step reports seed elapsed cycle");
  expect(thumb_mov.pc_advanced, "Thumb dispatch advances PC by one halfword");
  expect(cpu.register_value(Arm7tdmi::kPc) == kSyntheticProgramBase + 2U,
         "Thumb dispatch advances PC by two bytes");
  expect(cpu.register_value(2) == 3, "Thumb dispatch mutates CPU register");

  const gba::core::CoreSchedulerRunResult thumb_run = scheduler.run_from_pc(1);
  expect(thumb_run.stop_reason == gba::core::CoreRunStopReason::max_steps,
         "Thumb dispatch run stops at max steps");
  expect(thumb_run.executed_steps == 1, "Thumb dispatch run records executed step");
  expect(cpu.register_value(2) == 7, "Thumb dispatch run executes ADD immediate");
  expect(thumb_run.final_pc == kSyntheticProgramBase + 4U,
         "Thumb dispatch run advances final PC");

  cpu.reset();
  memory.reset();
  scheduler.reset_scheduler_cycles();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "scheduler Thumb memory dispatch enters Thumb state");
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  cpu.set_register(0, 0xCAFEBABE);
  cpu.set_register(1, 0x03000100);
  expect(memory.write16(kSyntheticProgramBase, kThumbStrR0R1Imm0),
         "seed Thumb STR dispatch");
  expect(memory.write16(kSyntheticProgramBase + 2U, kThumbLdrR2R1Imm0),
         "seed Thumb LDR dispatch");
  const gba::core::CoreSchedulerFetchStepResult thumb_str = scheduler.step_from_pc();
  expect(thumb_str.step.has_value(), "Thumb STR dispatch reports scheduler step");
  expect(thumb_str.step->cpu_step.status == ExecuteStatus::executed,
         "Thumb STR dispatch executes");
  expect_read32(memory, 0x03000100, 0xCAFEBABE, "Thumb STR dispatch stores through memory");
  const gba::core::CoreSchedulerFetchStepResult thumb_ldr = scheduler.step_from_pc();
  expect(thumb_ldr.step.has_value(), "Thumb LDR dispatch reports scheduler step");
  expect(thumb_ldr.step->cpu_step.status == ExecuteStatus::executed,
         "Thumb LDR dispatch executes");
  expect(cpu.register_value(2) == 0xCAFEBABE, "Thumb LDR dispatch loads from memory");
  expect(cpu.register_value(Arm7tdmi::kPc) == kSyntheticProgramBase + 4U,
         "Thumb memory dispatch advances PC across two halfwords");

  cpu.reset();
  memory.reset();
  scheduler.reset_scheduler_cycles();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  expect(cpu.set_cpsr(kThumbStateSupervisor), "scheduler Thumb SWI enters Thumb state");
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  expect(memory.write16(kSyntheticProgramBase, kThumbSwi0), "seed Thumb SWI dispatch");
  const gba::core::CoreSchedulerFetchStepResult thumb_swi = scheduler.step_from_pc();
  expect(thumb_swi.step.has_value(), "Thumb SWI dispatch reports scheduler step");
  expect(thumb_swi.step->cpu_step.status == ExecuteStatus::executed,
         "Thumb SWI dispatch executes");
  expect(!thumb_swi.pc_advanced, "Thumb SWI dispatch preserves exception PC");
  expect(cpu.current_mode() == gba::core::CpuMode::supervisor,
         "Thumb SWI dispatch enters supervisor mode");
  expect(cpu.register_value(Arm7tdmi::kPc) == 0x00000008,
         "Thumb SWI dispatch vectors PC");

  cpu.reset();
  memory.reset();
  scheduler.reset_scheduler_cycles();
  expect(cpu.set_cpsr(kThumbStateSupervisor), "scheduler branch dispatch enters Thumb state");
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  expect(memory.write16(kSyntheticProgramBase, kThumbBranchPlusTwoHalfwords),
         "seed Thumb branch");
  expect(memory.write16(kSyntheticProgramBase + 8U, kThumbMovR2Imm3),
         "seed Thumb branch target");
  const gba::core::CoreSchedulerFetchStepResult thumb_branch = scheduler.step_from_pc();
  expect(thumb_branch.step.has_value(), "Thumb branch dispatch reports scheduler step");
  expect(thumb_branch.step->cpu_step.status == ExecuteStatus::executed,
         "Thumb branch dispatch executes branch");
  expect(!thumb_branch.pc_advanced, "Thumb branch dispatch preserves changed PC");
  expect(cpu.register_value(Arm7tdmi::kPc) == kSyntheticProgramBase + 8U,
         "Thumb branch updates PC to target");
  const gba::core::CoreSchedulerRunResult thumb_branch_target = scheduler.run_from_pc(1);
  expect(thumb_branch_target.executed_steps == 1,
         "Thumb branch target run executes one step");
  expect(cpu.register_value(2) == 3, "Thumb branch target instruction executes");
  expect(cpu.register_value(Arm7tdmi::kPc) == kSyntheticProgramBase + 10U,
         "Thumb branch target advances PC after target instruction");

  cpu.reset();
  memory.reset();
  scheduler.reset_scheduler_cycles();
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  expect(memory.write32(kSyntheticProgramBase, kAddR0R0Imm1), "seed ARM dispatch ADD");
  const gba::core::CoreSchedulerFetchStepResult arm_dispatch = scheduler.step_from_pc();
  expect(arm_dispatch.instruction_set == gba::core::CoreInstructionSet::arm,
         "dispatch fetch reports ARM instruction set");
  expect(arm_dispatch.instruction_width_bytes == 4,
         "dispatch fetch reports ARM instruction width");
  expect(cpu.register_value(Arm7tdmi::kPc) == kSyntheticProgramBase + 4U,
         "ARM dispatch advances PC by four bytes");

  cpu.reset();
  memory.hard_reset();
  scheduler.reset_scheduler_cycles();
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  const gba::core::CoreSchedulerFetchStepResult unloaded_cartridge_dispatch =
      scheduler.step_from_pc();
  expect(unloaded_cartridge_dispatch.fetch_failed,
         "dispatch fetch from unloaded cartridge ROM fails cleanly");
  expect(!unloaded_cartridge_dispatch.step.has_value(),
         "unloaded cartridge fetch does not execute a step");
  expect(scheduler.scheduler_cycles() == 0,
         "unloaded cartridge fetch does not advance scheduler cycles");

  cpu.reset();
  memory.reset();
  scheduler.reset_scheduler_cycles();
  expect(memory.load_game_pak_rom(rom_with_word(kAddR0R0Imm1)),
         "scheduler test loads explicit ROM blob");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  const gba::core::CoreSchedulerFetchStepResult cartridge_dispatch = scheduler.step_from_pc();
  expect(!cartridge_dispatch.fetch_failed, "dispatch fetch reads explicit cartridge ROM");
  expect(cartridge_dispatch.fetch_address == kGamePakProgramBase,
         "cartridge dispatch reports Game Pak fetch address");
  expect(cartridge_dispatch.instruction.has_value() &&
             cartridge_dispatch.instruction.value() == kAddR0R0Imm1,
         "cartridge dispatch reports fetched ARM instruction");
  expect(cartridge_dispatch.step.has_value(), "cartridge dispatch reports scheduler step");
  expect(cartridge_dispatch.step->cpu_step.status == ExecuteStatus::executed,
         "cartridge dispatch executes fetched ARM instruction");
  expect(cpu.register_value(0) == 1, "cartridge dispatch mutates CPU register");
  expect(cpu.register_value(Arm7tdmi::kPc) == kGamePakProgramBase + 4U,
         "cartridge dispatch advances PC by one ARM word");

  WaitStateControl waitcnt;
  waitcnt.write_control(WaitStateControl::kStandardGamePakSetting);
  CoreScheduler timed_scheduler(cpu, memory, interrupts, timers, dma, ppu, apu, waitcnt);
  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  std::vector<std::uint8_t> timed_rom(256);
  write_rom_word(timed_rom, 0, kAddR0R0Imm1);
  write_rom_word(timed_rom, 4, kAddR0R0Imm1);
  expect(memory.load_game_pak_rom(timed_rom), "timed scheduler loads explicit ROM blob");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  const gba::core::CoreSchedulerFetchStepResult timed_first = timed_scheduler.step_from_pc();
  expect(timed_first.fetch_timing_applied, "first cartridge fetch applies WAITCNT timing");
  expect(!timed_first.fetch_sequential, "first cartridge fetch is non-sequential");
  expect(timed_first.fetch_cycles == 3, "standard WAITCNT first wait0 fetch costs 3 cycles");
  expect(timed_first.step.has_value(), "first timed cartridge fetch executes instruction");
  expect(timed_first.step->cpu_step.elapsed_cycles == 1,
         "first timed cartridge ADD keeps unit CPU execution cost");
  expect(timed_scheduler.scheduler_cycles() == 4,
         "first timed cartridge fetch includes fetch plus execution cycles");
  const gba::core::CoreSchedulerFetchStepResult timed_second = timed_scheduler.step_from_pc();
  expect(timed_second.fetch_timing_applied, "second cartridge fetch applies WAITCNT timing");
  expect(timed_second.fetch_sequential, "second adjacent cartridge fetch is sequential");
  expect(timed_second.fetch_cycles == 1,
         "standard WAITCNT subsequent wait0 fetch costs 1 cycle");
  expect(timed_scheduler.scheduler_cycles() == 6,
         "second timed cartridge fetch includes sequential fetch plus execution cycles");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  std::vector<std::uint8_t> boundary_rom((128U * 1024U) + 8U);
  write_rom_word(boundary_rom, (128U * 1024U) - 4U, kAddR0R0Imm1);
  write_rom_word(boundary_rom, 128U * 1024U, kAddR0R0Imm1);
  expect(memory.load_game_pak_rom(boundary_rom), "timed scheduler loads boundary ROM blob");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase + (128U * 1024U) - 4U);
  const gba::core::CoreSchedulerFetchStepResult boundary_first =
      timed_scheduler.step_from_pc();
  expect(boundary_first.fetch_cycles == 3,
         "first 128 KiB boundary setup fetch is non-sequential");
  const gba::core::CoreSchedulerFetchStepResult boundary_second =
      timed_scheduler.step_from_pc();
  expect(!boundary_second.fetch_sequential,
         "fetch at a 128 KiB Game Pak boundary is forced non-sequential");
  expect(boundary_second.boundary_forced_nonsequential,
         "boundary fetch reports forced non-sequential timing");
  expect(boundary_second.fetch_cycles == 3,
         "boundary fetch uses non-sequential wait0 timing");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  std::vector<std::uint8_t> timed_thumb_branch_rom(256);
  write_rom_halfword(timed_thumb_branch_rom, 0, kThumbBranchPlusOneHalfword);
  write_rom_halfword(timed_thumb_branch_rom, 6, kThumbMovR2Imm3);
  expect(memory.load_game_pak_rom(timed_thumb_branch_rom),
         "timed scheduler loads Thumb branch ROM blob");
  expect(cpu.set_cpsr(kThumbStateSupervisor), "timed scheduler enters Thumb state");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  const gba::core::CoreSchedulerFetchStepResult timed_branch = timed_scheduler.step_from_pc();
  expect(timed_branch.fetch_cycles == 3, "timed Thumb branch charges first fetch");
  expect(!timed_branch.pc_advanced, "timed Thumb branch changes PC directly");
  expect(cpu.register_value(Arm7tdmi::kPc) == kGamePakProgramBase + 6U,
         "timed Thumb branch targets PC-plus-four-relative halfword");
  const gba::core::CoreSchedulerFetchStepResult timed_branch_target =
      timed_scheduler.step_from_pc();
  expect(timed_branch_target.fetch_timing_applied,
         "timed Thumb branch target applies fetch timing");
  expect(!timed_branch_target.fetch_sequential,
         "branch target fetch resets sequential tracking after control-flow change");
  expect(timed_branch_target.fetch_cycles == 3,
         "branch target fetch is charged as non-sequential after control-flow change");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  std::vector<std::uint8_t> timed_load_rom(256);
  write_rom_word(timed_load_rom, 0, kLdrR1FromR2Plus0x10);
  write_rom_word(timed_load_rom, 4, kAddR0R0Imm1);
  write_rom_word(timed_load_rom, 0x10, 0xCAFEBABEU);
  expect(memory.load_game_pak_rom(timed_load_rom),
         "timed scheduler loads explicit ROM data blob");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, kGamePakProgramBase);
  const gba::core::CoreSchedulerFetchStepResult timed_ldr = timed_scheduler.step_from_pc();
  expect(timed_ldr.fetch_cycles == 3, "timed cartridge LDR charges fetch timing");
  expect(timed_ldr.step.has_value(), "timed cartridge LDR reports scheduler step");
  expect(timed_ldr.step->cpu_step.status == ExecuteStatus::executed,
         "timed cartridge LDR executes");
  expect(timed_ldr.step->cpu_step.elapsed_cycles == 5,
         "timed cartridge LDR applies WAITCNT data-load timing");
  expect(timed_ldr.step->cpu_step.memory_timing_applied,
         "timed cartridge LDR reports memory timing applied");
  expect(cpu.register_value(1) == 0xCAFEBABE, "timed cartridge LDR reads ROM data");
  expect(timed_scheduler.scheduler_cycles() == 8,
         "timed cartridge LDR accumulates fetch plus data-load cycles");
  expect(timed_ldr.prefetch_enabled, "timed cartridge LDR observes WAITCNT prefetch bit");
  expect(timed_ldr.prefetch_buffer_halfwords > 0,
         "timed cartridge LDR refills the bounded prefetch buffer");
  const gba::core::CoreSchedulerFetchStepResult prefetched_add =
      timed_scheduler.step_from_pc();
  expect(prefetched_add.fetch_timing_applied == false,
         "prefetched opcode does not charge cartridge wait cycles");
  expect(prefetched_add.prefetch_hit,
         "sequential opcode after cartridge LDR is served from prefetch buffer seed");
  expect(prefetched_add.fetch_cycles == 0, "prefetch hit reports zero fetch cycles");
  expect(timed_scheduler.scheduler_cycles() == 9,
         "prefetch hit still advances devices for executed instruction cycles");

  std::cout << "core_scheduler_test: PASS\n";
  return 0;
}
