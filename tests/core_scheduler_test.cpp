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

#include "test_helpers.hpp"

namespace {

void expect_read32(const gba::core::MemoryBus& memory, std::uint32_t address,
                   std::uint32_t expected, std::string_view message) {
  const std::optional<std::uint32_t> value = memory.read32(address);
  expect(value.has_value(), message);
  expect(value.value() == expected, message);
}

std::vector<std::uint8_t> rom_with_word(std::uint32_t word) {
  std::vector<std::uint8_t> rom(256);
  rom.at(0) = static_cast<std::uint8_t>(word & 0xFFU);
  rom.at(1) = static_cast<std::uint8_t>((word >> 8) & 0xFFU);
  rom.at(2) = static_cast<std::uint8_t>((word >> 16) & 0xFFU);
  rom.at(3) = static_cast<std::uint8_t>((word >> 24) & 0xFFU);
  return rom;
}

struct TimerIoCallbackContext {
  gba::core::Timers* timers = nullptr;
};

struct PpuIoCallbackContext {
  gba::core::PpuTiming* ppu = nullptr;
};

std::optional<std::uint16_t> read_ppu_io16(void* context,
                                           std::uint32_t address) {
  constexpr std::uint32_t kDispstat = 0x04000004U;
  if (context == nullptr || address != kDispstat) {
    return std::nullopt;
  }

  const auto* callback_context =
      static_cast<const PpuIoCallbackContext*>(context);
  if (callback_context->ppu == nullptr) {
    return std::nullopt;
  }
  return callback_context->ppu->dispstat();
}

std::optional<std::uint16_t> read_timer_io16(void* context,
                                             std::uint32_t address) {
  constexpr std::uint32_t kTimerBase = 0x04000100U;
  constexpr std::uint32_t kTimerStride = 4U;
  constexpr std::uint32_t kTimerRegisterBytes =
      kTimerStride * gba::core::Timers::kTimerCount;
  if (context == nullptr || address < kTimerBase ||
      address >= kTimerBase + kTimerRegisterBytes) {
    return std::nullopt;
  }

  const auto* callback_context =
      static_cast<const TimerIoCallbackContext*>(context);
  if (callback_context->timers == nullptr) {
    return std::nullopt;
  }
  const std::uint32_t relative = address - kTimerBase;
  const std::size_t timer = relative / kTimerStride;
  const std::uint32_t offset = relative % kTimerStride;
  if (offset == 0) {
    return callback_context->timers->counter(timer);
  }
  if (offset == 2) {
    return callback_context->timers->control(timer);
  }
  return std::nullopt;
}

std::optional<std::uint32_t> read_timer_io32(void* context,
                                             std::uint32_t address) {
  const std::optional<std::uint16_t> low = read_timer_io16(context, address);
  const std::optional<std::uint16_t> high =
      read_timer_io16(context, address + 2U);
  if (!low.has_value() || !high.has_value()) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(low.value()) |
         (static_cast<std::uint32_t>(high.value()) << 16U);
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
  constexpr std::uint32_t kEwramProgramBase = 0x02000000U;
  constexpr std::uint32_t kGamePakProgramBase = 0x08000000U;
  constexpr std::uint32_t kThumbStateSupervisor = 0x00000033U;
  constexpr std::uint16_t kThumbMovR2Imm3 = 0x2203U;
  constexpr std::uint16_t kThumbAddR2Imm4 = 0x3204U;
  constexpr std::uint16_t kThumbStrR0R1Imm0 = 0x6008U;
  constexpr std::uint16_t kThumbLdrR2R1Imm0 = 0x680AU;
  constexpr std::uint16_t kThumbLdrR2R4Imm0 = 0x6822U;
  constexpr std::uint16_t kThumbLdrhR0R1Imm0 = 0x8808U;
  constexpr std::uint16_t kThumbLdmiaR2R3R7 = 0xCAF8U;
  constexpr std::uint16_t kThumbSwi0 = 0xDF00U;
  constexpr std::uint16_t kThumbBranchPlusOneHalfword = 0xE001U;
  constexpr std::uint16_t kThumbBranchPlusTwoHalfwords = 0xE002U;
  constexpr std::uint32_t kLdrR1FromR2Plus0x180 = 0xE5921180U;
  constexpr std::uint32_t kLdmiaSpR2 = 0xE89D0004U;
  constexpr std::uint32_t kLdmiaSpR2R3 = 0xE89D000CU;
  constexpr std::uint32_t kLdmiaSpR2R7 = 0xE89D00FCU;
  constexpr std::uint32_t kLdmiaR2WritebackR3R7 = 0xE8B200F8U;
  constexpr std::uint32_t kStmiaSpR2R3 = 0xE88D000CU;
  constexpr std::uint32_t kStmiaSpR2R7 = 0xE88D00FCU;

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
  expect(skipped.devices.cycles == 1, "skipped ARM instruction reports no device cycles");
  expect(scheduler.scheduler_cycles() == 2,
         "skipped ARM instruction leaves global cycles unchanged");
  expect(timers.counter(0) == 0xFFF2,
         "skipped ARM instruction leaves timer counter unchanged");

  const gba::core::CoreDeviceTickResult apu_tick =
      scheduler.advance_devices(Apu::kCpuCyclesPerFrameSequencerStep);
  expect(apu_tick.cycles == Apu::kCpuCyclesPerFrameSequencerStep,
         "manual device advance reports cycles");
  expect(scheduler.scheduler_cycles() == 2U + Apu::kCpuCyclesPerFrameSequencerStep,
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
  expect(dma_step.immediate_dma.bus_cycles == 9,
         "scheduler reports immediate DMA bus occupancy");
  expect(scheduler.scheduler_cycles() == 10,
         "scheduler advances global cycles during immediate DMA bus occupancy");
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
  expect(cpu.register_value(Arm7tdmi::kLinkRegister) == kGamePakProgramBase + 4U,
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
  interrupts.write_interrupt_flags(irq_bit(InterruptSource::timer0));
  interrupts.request(InterruptSource::timer0);
  const gba::core::CoreSchedulerFetchStepResult hle_irq_return =
      hle_irq_scheduler.step_from_pc();
  expect(hle_irq_return.step.has_value(),
         "HLE IRQ return sentinel restores interrupted context");
  expect(hle_irq_return.step->irq_serviced,
         "HLE IRQ return immediately services an already pending IRQ line");
  expect(cpu.register_value(7) == 0xDEADBEEFU,
         "HLE IRQ return restores r7 along with caller-saved registers");
  expect(cpu.current_mode() == gba::core::CpuMode::irq,
         "HLE IRQ return re-enters IRQ mode before the next user instruction");

  cpu.reset();
  memory.reset();
  scheduler.reset_scheduler_cycles();
  scheduler.load_state(gba::core::CoreSchedulerState{});
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  TimerIoCallbackContext timer0_data_phase_context{&timers};
  memory.set_io_callbacks({&timer0_data_phase_context, read_timer_io16,
                           read_timer_io32, nullptr, nullptr});
  // Mirrors the 10b,0x0012 loose-loop trace: phase 179, counter FFFF,
  // and five cycles until the 1024-prescaler overflow.
  timers.tick(179, interrupts);
  timers.write_reload(0, 0xFFEE);
  timers.write_control(0, 0x00C3);
  timers.tick(845, interrupts);
  timers.tick(16U * 1024U, interrupts);
  timers.tick(1019, interrupts);
  expect(timers.counter(0) == 0xFFFF,
         "slow chained timer load seed reaches pre-overflow counter");
  expect(timers.cycles_until_next_prescaler_tick(0) == 5,
         "slow chained timer load seed is five cycles from overflow");
  interrupts.reset();
  interrupts.write_interrupt_enable(irq_bit(InterruptSource::timer0));
  interrupts.write_ime(1);
  gba::core::CoreSchedulerState chained_timer_state = scheduler.save_state();
  chained_timer_state.hle_irq_return_latency_pending = true;
  chained_timer_state.hle_irq_post_return_chain_active = true;
  chained_timer_state.timer_io_access_gap_cycles = 20;
  scheduler.load_state(chained_timer_state);
  cpu.set_register(Arm7tdmi::kPc, 0x030007BCU);
  cpu.set_register(2, 0x0BADF00DU);
  cpu.set_register(4, 0x04000100U);
  const gba::core::CoreSchedulerStepResult slow_chained_timer_load =
      scheduler.step_arm(0xE5942000U);
  expect(slow_chained_timer_load.cpu_step.status == ExecuteStatus::executed,
         "slow chained timer load executes the ARM load instruction");
  expect(slow_chained_timer_load.data_access.has_value(),
         "slow chained timer load reports data access metadata");
  expect(slow_chained_timer_load.data_access->load &&
             slow_chained_timer_load.data_access->timer_io,
         "slow chained timer load reports a timer I/O load");
  expect(slow_chained_timer_load.data_access->pre_access_cycles == 5,
         "slow chained timer load matures IRQ during the I/O pre-access window");
  expect(cpu.register_value(2) == 0x00C3FFEEU,
         "slow chained timer load completes before IRQ service disables timer0");
  expect((cpu.register_value(2) & 0x00800000U) != 0,
         "slow chained timer load preserves the timer0 enable bit for the branch test");
  expect(slow_chained_timer_load.irq_serviced,
         "slow chained timer load services IRQ after the load completes");
  expect(cpu.current_mode() == gba::core::CpuMode::irq,
         "slow chained timer load enters IRQ mode after the load");
  expect(cpu.register_value(Arm7tdmi::kPc) == 0x18,
         "slow chained timer load vectors to IRQ after the load");
  expect(interrupts.requested(InterruptSource::timer0),
         "slow chained timer load still leaves the timer0 IRQ requested");
  gba::core::CoreSchedulerState post_arm_slow_load_state = scheduler.save_state();
  expect(post_arm_slow_load_state.hle_irq_chained_post_return_data_dispatch_pending,
         "slow chained timer load records a data-access chained IRQ");
  expect(post_arm_slow_load_state.hle_irq_chained_post_return_spaced_data_dispatch_pending,
         "slow chained timer load records a spaced data-access chained IRQ");

  cpu.reset();
  scheduler.reset_scheduler_cycles();
  scheduler.load_state(gba::core::CoreSchedulerState{});
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timers.tick(179, interrupts);
  timers.write_reload(0, 0xFFEE);
  timers.write_control(0, 0x00C3);
  timers.tick(845, interrupts);
  timers.tick(16U * 1024U, interrupts);
  timers.tick(1019, interrupts);
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "Thumb slow chained timer load enters Thumb mode");
  interrupts.write_interrupt_enable(irq_bit(InterruptSource::timer0));
  interrupts.write_ime(1);
  gba::core::CoreSchedulerState thumb_chained_timer_state = scheduler.save_state();
  thumb_chained_timer_state.hle_irq_return_latency_pending = true;
  thumb_chained_timer_state.hle_irq_post_return_chain_active = true;
  thumb_chained_timer_state.timer_io_access_gap_cycles = 20;
  scheduler.load_state(thumb_chained_timer_state);
  cpu.set_register(Arm7tdmi::kPc, 0x030007BCU);
  cpu.set_register(2, 0x0BADF00DU);
  cpu.set_register(4, 0x04000100U);
  const gba::core::CoreSchedulerStepResult thumb_slow_chained_timer_load =
      scheduler.step_thumb(kThumbLdrR2R4Imm0);
  expect(thumb_slow_chained_timer_load.cpu_step.status == ExecuteStatus::executed,
         "Thumb slow chained timer load executes the load instruction");
  expect(thumb_slow_chained_timer_load.data_access.has_value(),
         "Thumb slow chained timer load reports data access metadata");
  expect(thumb_slow_chained_timer_load.data_access->load &&
             thumb_slow_chained_timer_load.data_access->timer_io,
         "Thumb slow chained timer load reports a timer I/O load");
  expect(thumb_slow_chained_timer_load.data_access->pre_access_cycles == 5,
         "Thumb slow chained timer load matures IRQ during the I/O pre-access window");
  expect(cpu.register_value(2) == 0x00C3FFEEU,
         "Thumb slow chained timer load completes before IRQ service disables timer0");
  expect(thumb_slow_chained_timer_load.irq_serviced,
         "Thumb slow chained timer load services IRQ after the load completes");
  expect(cpu.current_mode() == gba::core::CpuMode::irq,
         "Thumb slow chained timer load enters IRQ mode after the load");
  expect(cpu.register_value(Arm7tdmi::kPc) == 0x18,
         "Thumb slow chained timer load vectors to IRQ after the load");
  gba::core::CoreSchedulerState post_thumb_slow_load_state =
      scheduler.save_state();
  expect(post_thumb_slow_load_state.hle_irq_chained_post_return_data_dispatch_pending,
         "Thumb slow chained timer load records a data-access chained IRQ");
  expect(post_thumb_slow_load_state.hle_irq_chained_post_return_spaced_data_dispatch_pending,
         "Thumb slow chained timer load records a spaced data-access chained IRQ");
  memory.clear_io_callbacks();
  scheduler.load_state(gba::core::CoreSchedulerState{});

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
  expect(scheduler.scheduler_cycles() == 2,
         "skipped fetched ARM instruction leaves scheduler cycles unchanged");

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
  expect(timed_first.fetch_cycles == 4,
         "standard WAITCNT first ARM wait0 fetch costs non-sequential plus sequential cycles");
  expect(timed_first.step.has_value(), "first timed cartridge fetch executes instruction");
  expect(timed_first.step->cpu_step.elapsed_cycles == 2,
         "first timed cartridge ADD includes prefetch execute bubble");
  expect(timed_scheduler.scheduler_cycles() == 6,
         "first timed cartridge fetch includes fetch plus prefetch-bubbled execution cycles");
  const gba::core::CoreSchedulerFetchStepResult timed_second = timed_scheduler.step_from_pc();
  expect(timed_second.fetch_timing_applied, "second cartridge fetch applies WAITCNT timing");
  expect(timed_second.fetch_sequential, "second adjacent cartridge fetch is sequential");
  expect(timed_second.fetch_cycles == 2,
         "standard WAITCNT subsequent ARM wait0 fetch costs two sequential halfwords");
  expect(timed_scheduler.scheduler_cycles() == 10,
         "second timed cartridge fetch includes sequential fetch plus prefetch-bubbled execution cycles");

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
  expect(boundary_first.fetch_cycles == 4,
         "first 128 KiB boundary setup fetch is non-sequential");
  const gba::core::CoreSchedulerFetchStepResult boundary_second =
      timed_scheduler.step_from_pc();
  expect(!boundary_second.fetch_sequential,
         "fetch at a 128 KiB Game Pak boundary is forced non-sequential");
  expect(boundary_second.boundary_forced_nonsequential,
         "boundary fetch reports forced non-sequential timing");
  expect(boundary_second.fetch_cycles == 4,
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
  std::vector<std::uint8_t> timed_load_rom(512);
  write_rom_word(timed_load_rom, 0, kLdrR1FromR2Plus0x180);
  write_rom_word(timed_load_rom, 4, kAddR0R0Imm1);
  write_rom_word(timed_load_rom, 0x180, 0xCAFEBABEU);
  expect(memory.load_game_pak_rom(timed_load_rom),
         "timed scheduler loads explicit ROM data blob");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, kGamePakProgramBase);
  const gba::core::CoreSchedulerFetchStepResult timed_ldr = timed_scheduler.step_from_pc();
  expect(timed_ldr.fetch_cycles == 4, "timed cartridge LDR charges ARM fetch timing");
  expect(timed_ldr.step.has_value(), "timed cartridge LDR reports scheduler step");
  expect(timed_ldr.step->cpu_step.status == ExecuteStatus::executed,
         "timed cartridge LDR executes");
  expect(timed_ldr.step->cpu_step.elapsed_cycles == 8,
         "timed cartridge LDR applies WAITCNT data-load timing");
  expect(timed_ldr.step->cpu_step.memory_timing_applied,
         "timed cartridge LDR reports memory timing applied");
  expect(cpu.register_value(1) == 0xCAFEBABE, "timed cartridge LDR reads ROM data");
  expect(timed_scheduler.scheduler_cycles() == 12,
         "timed cartridge LDR accumulates fetch plus data-load cycles");
  expect(timed_ldr.prefetch_enabled, "timed cartridge LDR observes WAITCNT prefetch bit");
  expect(timed_ldr.prefetch_buffer_halfwords == 0,
         "timed cartridge word LDR invalidates the bounded prefetch buffer");
  const gba::core::CoreSchedulerFetchStepResult prefetched_add =
      timed_scheduler.step_from_pc();
  expect(prefetched_add.fetch_timing_applied,
         "sequential opcode after cartridge LDR charges timer-visible cartridge cycles");
  expect(!prefetched_add.prefetch_hit,
         "sequential opcode after cartridge word LDR is not served from prefetch buffer");
  expect(prefetched_add.fetch_cycles == 5,
         "ARM fetch after cartridge word LDR restarts as a non-prefetched fetch");
  expect(timed_scheduler.scheduler_cycles() == 19,
         "post-cartridge-word-load fetch advances devices with invalidated prefetch");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x0000);
  std::vector<std::uint8_t> timed_ldmia_rom(256);
  write_rom_word(timed_ldmia_rom, 0, kLdmiaSpR2);
  write_rom_word(timed_ldmia_rom, 4, kAddR0R0Imm1);
  expect(memory.load_game_pak_rom(timed_ldmia_rom),
         "timed scheduler loads ARM LDMIA ROM blob");
  expect(memory.write32(0x03000080U, 0xDEADBEEFU),
         "timed scheduler seeds LDMIA stack word");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(13, 0x03000080U);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia.fetch_cycles == 7,
         "default WAITCNT charges first ARM LDMIA fetch as non-sequential");
  expect(timed_ldmia.step.has_value(), "timed ARM LDMIA reports scheduler step");
  expect(timed_ldmia.step->data_access.has_value(),
         "ARM LDMIA reports its first data access to scheduler timing");
  expect(timed_ldmia.step->data_access->address == 0x03000080U,
         "ARM LDMIA data access reports the stack base address");
  expect(timed_ldmia.step->data_access->width_bytes == 4,
         "ARM LDMIA data access reports word width");
  expect(timed_ldmia.step->data_access->load,
         "ARM LDMIA data access reports a load");
  expect(timed_ldmia.step->cpu_step.elapsed_cycles == 3,
         "ARM LDMIA keeps internal word-load execution timing");
  expect(cpu.register_value(2) == 0xDEADBEEFU, "ARM LDMIA loads selected register");
  const gba::core::CoreSchedulerFetchStepResult after_ldmia =
      timed_scheduler.step_from_pc();
  expect(after_ldmia.fetch_timing_applied,
         "ARM fetch after LDMIA charges timer-visible cartridge cycles");
  expect(!after_ldmia.fetch_sequential,
         "ARM fetch after LDMIA restarts non-sequential after data access");
  expect(after_ldmia.fetch_cycles == 7,
         "default WAITCNT ARM fetch after LDMIA is non-sequential");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4000);
  std::vector<std::uint8_t> timed_ldmia_prefetch_rom(256);
  write_rom_word(timed_ldmia_prefetch_rom, 0, kLdmiaSpR2R3);
  expect(memory.load_game_pak_rom(timed_ldmia_prefetch_rom),
         "timed scheduler loads ARM LDMIA prefetch ROM blob");
  expect(memory.write32(0x03000090U, 0x12345678U),
         "timed scheduler seeds first LDMIA prefetch stack word");
  expect(memory.write32(0x03000094U, 0x9ABCDEF0U),
         "timed scheduler seeds second LDMIA prefetch stack word");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(13, 0x03000090U);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_prefetch =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_prefetch.step.has_value(),
         "timed ARM LDMIA prefetch reports scheduler step");
  expect(timed_ldmia_prefetch.step->cpu_step.elapsed_cycles == 2,
         "prefetched two-register ARM LDMIA overlaps both internal load cycles");
  expect(timed_ldmia_prefetch.step->data_access.has_value(),
         "prefetched ARM LDMIA reports its first data access");
  expect(timed_ldmia_prefetch.step->data_access->address == 0x03000090U,
         "prefetched ARM LDMIA reports the first stack word address");
  expect(cpu.register_value(2) == 0x12345678U,
         "prefetched ARM LDMIA loads first selected register");
  expect(cpu.register_value(3) == 0x9ABCDEF0U,
         "prefetched ARM LDMIA loads second selected register");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4000);
  std::vector<std::uint8_t> timed_stmia_prefetch_rom(256);
  write_rom_word(timed_stmia_prefetch_rom, 0, kStmiaSpR2R3);
  expect(memory.load_game_pak_rom(timed_stmia_prefetch_rom),
         "timed scheduler loads ARM STMIA prefetch ROM blob");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(13, 0x030000C0U);
  cpu.set_register(2, 0xCAFEBABEU);
  cpu.set_register(3, 0x0BADF00DU);
  const gba::core::CoreSchedulerFetchStepResult timed_stmia_prefetch =
      timed_scheduler.step_from_pc();
  expect(timed_stmia_prefetch.step.has_value(),
         "timed ARM STMIA prefetch reports scheduler step");
  expect(timed_stmia_prefetch.step->cpu_step.elapsed_cycles == 2,
         "prefetched two-register ARM STMIA overlaps one store bus cycle");
  expect(timed_stmia_prefetch.step->data_access.has_value(),
         "prefetched ARM STMIA reports its first data access");
  expect(timed_stmia_prefetch.step->data_access->address == 0x030000C0U,
         "prefetched ARM STMIA reports the first stack word address");
  expect(!timed_stmia_prefetch.step->data_access->load,
         "prefetched ARM STMIA reports a store access");
  expect_read32(memory, 0x030000C0U, 0xCAFEBABEU,
                "prefetched ARM STMIA stores first selected register");
  expect_read32(memory, 0x030000C4U, 0x0BADF00DU,
                "prefetched ARM STMIA stores second selected register");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4000);
  std::vector<std::uint8_t> timed_stmia_six_prefetch_rom(256);
  write_rom_word(timed_stmia_six_prefetch_rom, 0, kStmiaSpR2R7);
  expect(memory.load_game_pak_rom(timed_stmia_six_prefetch_rom),
         "timed scheduler loads six-register ARM STMIA prefetch ROM blob");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(13, 0x030000D0U);
  for (std::uint32_t index = 0; index < 6; ++index) {
    cpu.set_register(static_cast<std::uint8_t>(2U + index), 0xD0D00000U + index);
  }
  const gba::core::CoreSchedulerFetchStepResult timed_stmia_six_prefetch =
      timed_scheduler.step_from_pc();
  expect(timed_stmia_six_prefetch.step.has_value(),
         "timed six-register ARM STMIA prefetch reports scheduler step");
  expect(timed_stmia_six_prefetch.step->cpu_step.elapsed_cycles == 3,
         "prefetched six-register ARM STMIA uses the wider store overlap window");
  expect_read32(memory, 0x030000D0U, 0xD0D00000U,
                "prefetched six-register ARM STMIA stores first selected register");
  expect_read32(memory, 0x030000E4U, 0xD0D00005U,
                "prefetched six-register ARM STMIA stores last selected register");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4010);
  std::vector<std::uint8_t> timed_stmia_six_fast_prefetch_rom(256);
  write_rom_word(timed_stmia_six_fast_prefetch_rom, 0, kStmiaSpR2R7);
  expect(memory.load_game_pak_rom(timed_stmia_six_fast_prefetch_rom),
         "timed scheduler loads fast-sequential ARM STMIA prefetch ROM blob");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(13, 0x030000F0U);
  for (std::uint32_t index = 0; index < 6; ++index) {
    cpu.set_register(static_cast<std::uint8_t>(2U + index), 0xF0F00000U + index);
  }
  const gba::core::CoreSchedulerFetchStepResult timed_stmia_six_fast_prefetch =
      timed_scheduler.step_from_pc();
  expect(timed_stmia_six_fast_prefetch.step.has_value(),
         "timed fast-sequential ARM STMIA prefetch reports scheduler step");
  expect(timed_stmia_six_fast_prefetch.step->cpu_step.elapsed_cycles == 5,
         "fast-sequential prefetched ARM STMIA uses the smaller store overlap window");
  expect_read32(memory, 0x030000F0U, 0xF0F00000U,
                "fast-sequential prefetched ARM STMIA stores first selected register");
  expect_read32(memory, 0x03000104U, 0xF0F00005U,
                "fast-sequential prefetched ARM STMIA stores last selected register");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4000);
  std::vector<std::uint8_t> timed_ldmia_six_prefetch_rom(256);
  write_rom_word(timed_ldmia_six_prefetch_rom, 0, kLdmiaSpR2R7);
  expect(memory.load_game_pak_rom(timed_ldmia_six_prefetch_rom),
         "timed scheduler loads six-register ARM LDMIA prefetch ROM blob");
  for (std::uint32_t index = 0; index < 6; ++index) {
    expect(memory.write32(0x030000A0U + index * 4U,
                          0xA0A00000U + index),
           "timed scheduler seeds six-register LDMIA prefetch stack word");
  }
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(13, 0x030000A0U);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_six_prefetch =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_six_prefetch.step.has_value(),
         "timed six-register ARM LDMIA prefetch reports scheduler step");
  expect(timed_ldmia_six_prefetch.step->cpu_step.elapsed_cycles == 4,
         "prefetched six-register ARM LDMIA overlaps bounded internal load cycles");
  expect(cpu.register_value(2) == 0xA0A00000U,
         "prefetched six-register ARM LDMIA loads first selected register");
  expect(cpu.register_value(7) == 0xA0A00005U,
         "prefetched six-register ARM LDMIA loads last selected register");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4010);
  std::vector<std::uint8_t> timed_ldmia_six_fast_prefetch_rom(256);
  write_rom_word(timed_ldmia_six_fast_prefetch_rom, 0, kLdmiaSpR2R7);
  expect(memory.load_game_pak_rom(timed_ldmia_six_fast_prefetch_rom),
         "timed scheduler loads fast-sequential six-register ARM LDMIA ROM blob");
  for (std::uint32_t index = 0; index < 6; ++index) {
    expect(memory.write32(0x030000B0U + index * 4U,
                          0xB0B00000U + index),
           "timed scheduler seeds fast-sequential LDMIA prefetch stack word");
  }
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(13, 0x030000B0U);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_six_fast_prefetch =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_six_fast_prefetch.step.has_value(),
         "timed fast-sequential ARM LDMIA prefetch reports scheduler step");
  expect(timed_ldmia_six_fast_prefetch.step->cpu_step.elapsed_cycles == 6,
         "fast-sequential prefetched ARM LDMIA uses the smaller overlap window");
  expect(cpu.register_value(2) == 0xB0B00000U,
         "fast-sequential prefetched ARM LDMIA loads first selected register");
  expect(cpu.register_value(7) == 0xB0B00005U,
         "fast-sequential prefetched ARM LDMIA loads last selected register");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x0000);
  std::vector<std::uint8_t> timed_ldmia_oam_to_rom(256);
  write_rom_word(timed_ldmia_oam_to_rom, 0, kLdmiaR2WritebackR3R7);
  write_rom_word(timed_ldmia_oam_to_rom, 0x04, 0x11223344U);
  write_rom_word(timed_ldmia_oam_to_rom, 0x08, 0x55667788U);
  write_rom_word(timed_ldmia_oam_to_rom, 0x0C, 0x99AABBCCU);
  expect(memory.load_game_pak_rom(timed_ldmia_oam_to_rom),
         "timed scheduler loads mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFFCU);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam_rom =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam_rom.step.has_value(),
         "timed mirrored-OAM-to-ROM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam_rom.step->cpu_step.elapsed_cycles == 29,
         "mirrored-OAM-to-ROM ARM LDMIA charges each ROM beat as non-sequential");
  expect(timed_ldmia_oam_rom.step->data_access.has_value(),
         "mirrored-OAM-to-ROM ARM LDMIA reports its first data access");
  expect(timed_ldmia_oam_rom.step->data_access->address == 0x07FFFFFCU,
         "mirrored-OAM-to-ROM ARM LDMIA reports the boundary address");
  expect(cpu.register_value(2) == 0x08000010U,
         "mirrored-OAM-to-ROM ARM LDMIA writes back across the ROM boundary");
  expect(cpu.register_value(4) == kLdmiaR2WritebackR3R7,
         "mirrored-OAM-to-ROM ARM LDMIA loads the first ROM word after OAM");
  expect(cpu.register_value(7) == 0x99AABBCCU,
         "mirrored-OAM-to-ROM ARM LDMIA loads the last ROM boundary word");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4000);
  std::vector<std::uint8_t> timed_ldmia_oam_to_rom_prefetch(256);
  write_rom_word(timed_ldmia_oam_to_rom_prefetch, 0, kLdmiaR2WritebackR3R7);
  write_rom_word(timed_ldmia_oam_to_rom_prefetch, 0x04, 0x10203040U);
  write_rom_word(timed_ldmia_oam_to_rom_prefetch, 0x08, 0x50607080U);
  write_rom_word(timed_ldmia_oam_to_rom_prefetch, 0x0C, 0x90A0B0C0U);
  expect(memory.load_game_pak_rom(timed_ldmia_oam_to_rom_prefetch),
         "timed scheduler loads prefetched mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFFCU);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam_rom_prefetch =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam_rom_prefetch.step.has_value(),
         "timed prefetched mirrored-OAM-to-ROM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam_rom_prefetch.step->cpu_step.elapsed_cycles == 32,
         "prefetched mirrored-OAM-to-ROM ARM LDMIA includes boundary recovery");
  expect(cpu.register_value(2) == 0x08000010U,
         "prefetched mirrored-OAM-to-ROM ARM LDMIA writes back across ROM");
  expect(cpu.register_value(7) == 0x90A0B0C0U,
         "prefetched mirrored-OAM-to-ROM ARM LDMIA loads the last ROM word");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x0004);
  std::vector<std::uint8_t> timed_ldmia_oam_to_rom_nonseq(256);
  write_rom_word(timed_ldmia_oam_to_rom_nonseq, 0, kLdmiaR2WritebackR3R7);
  expect(memory.load_game_pak_rom(timed_ldmia_oam_to_rom_nonseq),
         "timed scheduler loads nonsequential mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFFCU);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam_rom_nonseq =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam_rom_nonseq.step.has_value(),
         "timed nonsequential mirrored-OAM-to-ROM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam_rom_nonseq.step->cpu_step.elapsed_cycles == 28,
         "nonsequential mirrored-OAM-to-ROM ARM LDMIA keeps boundary recovery");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4004);
  std::vector<std::uint8_t> timed_ldmia_oam_to_rom_prefetch_nonseq(256);
  write_rom_word(timed_ldmia_oam_to_rom_prefetch_nonseq, 0, kLdmiaR2WritebackR3R7);
  write_rom_word(timed_ldmia_oam_to_rom_prefetch_nonseq, 0x04, 0x21436587U);
  write_rom_word(timed_ldmia_oam_to_rom_prefetch_nonseq, 0x08, 0xA1B2C3D4U);
  write_rom_word(timed_ldmia_oam_to_rom_prefetch_nonseq, 0x0C, 0x01020304U);
  expect(memory.load_game_pak_rom(timed_ldmia_oam_to_rom_prefetch_nonseq),
         "timed scheduler loads prefetched nonsequential mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFFCU);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam_rom_prefetch_nonseq =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam_rom_prefetch_nonseq.step.has_value(),
         "timed prefetched nonsequential mirrored-OAM-to-ROM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam_rom_prefetch_nonseq.step->cpu_step.elapsed_cycles == 30,
         "prefetched nonsequential mirrored-OAM-to-ROM ARM LDMIA shares one recovery cycle");
  expect(cpu.register_value(2) == 0x08000010U,
         "prefetched nonsequential mirrored-OAM-to-ROM ARM LDMIA writes back across ROM");
  expect(cpu.register_value(7) == 0x01020304U,
         "prefetched nonsequential mirrored-OAM-to-ROM ARM LDMIA loads the last ROM word");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x0010);
  std::vector<std::uint8_t> timed_ldmia_oam_to_rom_fast_seq(256);
  write_rom_word(timed_ldmia_oam_to_rom_fast_seq, 0, kLdmiaR2WritebackR3R7);
  expect(memory.load_game_pak_rom(timed_ldmia_oam_to_rom_fast_seq),
         "timed scheduler loads fast-sequential mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFFCU);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam_rom_fast_seq =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam_rom_fast_seq.step.has_value(),
         "timed fast-sequential mirrored-OAM-to-ROM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam_rom_fast_seq.step->cpu_step.elapsed_cycles == 22,
         "fast-sequential mirrored-OAM-to-ROM ARM LDMIA shares boundary recovery");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x0014);
  std::vector<std::uint8_t> timed_ldmia_oam_to_rom_fast_seq_nonseq(256);
  write_rom_word(timed_ldmia_oam_to_rom_fast_seq_nonseq, 0, kLdmiaR2WritebackR3R7);
  expect(memory.load_game_pak_rom(timed_ldmia_oam_to_rom_fast_seq_nonseq),
         "timed scheduler loads fast-sequential nonsequential mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFFCU);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam_rom_fast_seq_nonseq =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam_rom_fast_seq_nonseq.step.has_value(),
         "timed fast-sequential nonsequential mirrored-OAM-to-ROM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam_rom_fast_seq_nonseq.step->cpu_step.elapsed_cycles == 21,
         "fast-sequential nonsequential mirrored-OAM-to-ROM ARM LDMIA shares boundary recovery");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x0000);
  std::vector<std::uint8_t> timed_thumb_ldmia_oam_to_rom(256);
  write_rom_halfword(timed_thumb_ldmia_oam_to_rom, 0, kThumbLdmiaR2R3R7);
  write_rom_word(timed_thumb_ldmia_oam_to_rom, 0x04, 0x76543210U);
  write_rom_word(timed_thumb_ldmia_oam_to_rom, 0x08, 0xFEDCBA98U);
  write_rom_word(timed_thumb_ldmia_oam_to_rom, 0x0C, 0x89ABCDEFU);
  expect(memory.load_game_pak_rom(timed_thumb_ldmia_oam_to_rom),
         "timed scheduler loads Thumb mirrored-OAM-to-ROM LDMIA fixture");
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "timed scheduler enters Thumb state for mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFFCU);
  const gba::core::CoreSchedulerFetchStepResult timed_thumb_ldmia_oam_rom =
      timed_scheduler.step_from_pc();
  expect(timed_thumb_ldmia_oam_rom.step.has_value(),
         "timed Thumb mirrored-OAM-to-ROM LDMIA reports scheduler step");
  expect(timed_thumb_ldmia_oam_rom.step->cpu_step.elapsed_cycles == 29,
         "Thumb mirrored-OAM-to-ROM LDMIA applies block data timing");
  expect(timed_thumb_ldmia_oam_rom.step->data_access.has_value(),
         "Thumb mirrored-OAM-to-ROM LDMIA reports its first data access");
  expect(timed_thumb_ldmia_oam_rom.step->data_access->address == 0x07FFFFFCU,
         "Thumb mirrored-OAM-to-ROM LDMIA reports the boundary address");
  expect(cpu.register_value(2) == 0x08000010U,
         "Thumb mirrored-OAM-to-ROM LDMIA writes back across ROM");
  expect(cpu.register_value(7) == 0x89ABCDEFU,
         "Thumb mirrored-OAM-to-ROM LDMIA loads the last ROM boundary word");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4000);
  std::vector<std::uint8_t> timed_thumb_ldmia_oam5_prefetch(256);
  write_rom_halfword(timed_thumb_ldmia_oam5_prefetch, 0, kThumbLdmiaR2R3R7);
  expect(memory.load_game_pak_rom(timed_thumb_ldmia_oam5_prefetch),
         "timed scheduler loads Thumb prefetched all-mirrored-OAM LDMIA fixture");
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "timed scheduler enters Thumb state for prefetched all-mirrored-OAM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFECU);
  const gba::core::CoreSchedulerFetchStepResult timed_thumb_ldmia_oam5_prefetch_step =
      timed_scheduler.step_from_pc();
  expect(timed_thumb_ldmia_oam5_prefetch_step.step.has_value(),
         "timed Thumb prefetched all-mirrored-OAM LDMIA reports scheduler step");
  expect(timed_thumb_ldmia_oam5_prefetch_step.step->cpu_step.elapsed_cycles == 6,
         "Thumb prefetched all-mirrored-OAM LDMIA keeps the mGBA timing-suite overlap");
  expect(timed_thumb_ldmia_oam5_prefetch_step.step->data_access.has_value(),
         "Thumb prefetched all-mirrored-OAM LDMIA reports its first data access");
  expect(timed_thumb_ldmia_oam5_prefetch_step.step->data_access->address == 0x07FFFFECU,
         "Thumb prefetched all-mirrored-OAM LDMIA reports the mirrored OAM address");
  expect(cpu.register_value(2) == 0x08000000U,
         "Thumb prefetched all-mirrored-OAM LDMIA writes back to the ROM boundary");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4010);
  std::vector<std::uint8_t> timed_thumb_ldmia_oam5_prefetch_fast_seq(256);
  write_rom_halfword(timed_thumb_ldmia_oam5_prefetch_fast_seq, 0, kThumbLdmiaR2R3R7);
  expect(memory.load_game_pak_rom(timed_thumb_ldmia_oam5_prefetch_fast_seq),
         "timed scheduler loads Thumb prefetched fast-sequential all-mirrored-OAM LDMIA fixture");
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "timed scheduler enters Thumb state for prefetched fast-sequential all-mirrored-OAM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFECU);
  const gba::core::CoreSchedulerFetchStepResult
      timed_thumb_ldmia_oam5_prefetch_fast_seq_step = timed_scheduler.step_from_pc();
  expect(timed_thumb_ldmia_oam5_prefetch_fast_seq_step.step.has_value(),
         "timed Thumb prefetched fast-sequential all-mirrored-OAM LDMIA reports scheduler step");
  expect(timed_thumb_ldmia_oam5_prefetch_fast_seq_step.step->cpu_step.elapsed_cycles == 7,
         "Thumb prefetched fast-sequential all-mirrored-OAM LDMIA keeps the fast-sequential timing-suite overlap");
  expect(cpu.register_value(2) == 0x08000000U,
         "Thumb prefetched fast-sequential all-mirrored-OAM LDMIA writes back to the ROM boundary");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x0004);
  std::vector<std::uint8_t> timed_ldmia_oam3_to_rom_nonseq(256);
  write_rom_word(timed_ldmia_oam3_to_rom_nonseq, 0, kLdmiaR2WritebackR3R7);
  expect(memory.load_game_pak_rom(timed_ldmia_oam3_to_rom_nonseq),
         "timed scheduler loads nonsequential three-word mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFF4U);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam3_rom_nonseq =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam3_rom_nonseq.step.has_value(),
         "timed nonsequential three-word mirrored-OAM-to-ROM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam3_rom_nonseq.step->cpu_step.elapsed_cycles == 18,
         "nonsequential three-word mirrored-OAM-to-ROM ARM LDMIA uses the shorter boundary recovery");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x0010);
  std::vector<std::uint8_t> timed_ldmia_oam3_to_rom_fast_seq(256);
  write_rom_word(timed_ldmia_oam3_to_rom_fast_seq, 0, kLdmiaR2WritebackR3R7);
  expect(memory.load_game_pak_rom(timed_ldmia_oam3_to_rom_fast_seq),
         "timed scheduler loads fast-sequential three-word mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFF4U);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam3_rom_fast_seq =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam3_rom_fast_seq.step.has_value(),
         "timed fast-sequential three-word mirrored-OAM-to-ROM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam3_rom_fast_seq.step->cpu_step.elapsed_cycles == 16,
         "fast-sequential three-word mirrored-OAM-to-ROM ARM LDMIA keeps the full ROM recovery");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4010);
  std::vector<std::uint8_t> timed_ldmia_oam3_to_rom_prefetch_fast_seq(256);
  write_rom_word(timed_ldmia_oam3_to_rom_prefetch_fast_seq, 0, kLdmiaR2WritebackR3R7);
  expect(memory.load_game_pak_rom(timed_ldmia_oam3_to_rom_prefetch_fast_seq),
         "timed scheduler loads prefetched fast-sequential three-word mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFF4U);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam3_rom_prefetch_fast_seq =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam3_rom_prefetch_fast_seq.step.has_value(),
         "timed prefetched fast-sequential three-word mirrored-OAM-to-ROM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam3_rom_prefetch_fast_seq.step->cpu_step.elapsed_cycles == 21,
         "prefetched fast-sequential three-word mirrored-OAM-to-ROM ARM LDMIA keeps the longer recovery");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4014);
  std::vector<std::uint8_t> timed_ldmia_oam3_to_rom_prefetch_nonseq_fast_seq(256);
  write_rom_word(timed_ldmia_oam3_to_rom_prefetch_nonseq_fast_seq, 0, kLdmiaR2WritebackR3R7);
  expect(memory.load_game_pak_rom(timed_ldmia_oam3_to_rom_prefetch_nonseq_fast_seq),
         "timed scheduler loads prefetched nonsequential fast-sequential three-word mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFF4U);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam3_rom_prefetch_nonseq_fast_seq =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam3_rom_prefetch_nonseq_fast_seq.step.has_value(),
         "timed prefetched nonsequential fast-sequential three-word mirrored-OAM-to-ROM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam3_rom_prefetch_nonseq_fast_seq.step->cpu_step.elapsed_cycles == 19,
         "prefetched nonsequential fast-sequential three-word mirrored-OAM-to-ROM ARM LDMIA keeps the longer recovery");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x0004);
  std::vector<std::uint8_t> timed_ldmia_oam4_to_rom_nonseq(256);
  write_rom_word(timed_ldmia_oam4_to_rom_nonseq, 0, kLdmiaR2WritebackR3R7);
  expect(memory.load_game_pak_rom(timed_ldmia_oam4_to_rom_nonseq),
         "timed scheduler loads nonsequential four-word mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFF0U);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam4_rom_nonseq =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam4_rom_nonseq.step.has_value(),
         "timed nonsequential four-word mirrored-OAM-to-ROM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam4_rom_nonseq.step->cpu_step.elapsed_cycles == 13,
         "nonsequential four-word mirrored-OAM-to-ROM ARM LDMIA has no extra boundary recovery");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x0010);
  std::vector<std::uint8_t> timed_ldmia_oam4_to_rom_fast_seq(256);
  write_rom_word(timed_ldmia_oam4_to_rom_fast_seq, 0, kLdmiaR2WritebackR3R7);
  expect(memory.load_game_pak_rom(timed_ldmia_oam4_to_rom_fast_seq),
         "timed scheduler loads fast-sequential four-word mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFF0U);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam4_rom_fast_seq =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam4_rom_fast_seq.step.has_value(),
         "timed fast-sequential four-word mirrored-OAM-to-ROM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam4_rom_fast_seq.step->cpu_step.elapsed_cycles == 13,
         "fast-sequential four-word mirrored-OAM-to-ROM ARM LDMIA adds the boundary recovery");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4010);
  std::vector<std::uint8_t> timed_ldmia_oam4_to_rom_prefetch_fast_seq(256);
  write_rom_word(timed_ldmia_oam4_to_rom_prefetch_fast_seq, 0, kLdmiaR2WritebackR3R7);
  expect(memory.load_game_pak_rom(timed_ldmia_oam4_to_rom_prefetch_fast_seq),
         "timed scheduler loads prefetched fast-sequential four-word mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFF0U);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam4_rom_prefetch_fast_seq =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam4_rom_prefetch_fast_seq.step.has_value(),
         "timed prefetched fast-sequential four-word mirrored-OAM-to-ROM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam4_rom_prefetch_fast_seq.step->cpu_step.elapsed_cycles == 17,
         "prefetched fast-sequential four-word mirrored-OAM-to-ROM ARM LDMIA uses the wider recovery");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4014);
  std::vector<std::uint8_t> timed_ldmia_oam4_to_rom_prefetch_nonseq_fast_seq(256);
  write_rom_word(timed_ldmia_oam4_to_rom_prefetch_nonseq_fast_seq, 0, kLdmiaR2WritebackR3R7);
  expect(memory.load_game_pak_rom(timed_ldmia_oam4_to_rom_prefetch_nonseq_fast_seq),
         "timed scheduler loads prefetched nonsequential fast-sequential four-word mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFF0U);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam4_rom_prefetch_nonseq_fast_seq =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam4_rom_prefetch_nonseq_fast_seq.step.has_value(),
         "timed prefetched nonsequential fast-sequential four-word mirrored-OAM-to-ROM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam4_rom_prefetch_nonseq_fast_seq.step->cpu_step.elapsed_cycles == 15,
         "prefetched nonsequential fast-sequential four-word mirrored-OAM-to-ROM ARM LDMIA uses the shared recovery");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4000);
  std::vector<std::uint8_t> timed_ldmia_oam5_prefetch(256);
  write_rom_word(timed_ldmia_oam5_prefetch, 0, kLdmiaR2WritebackR3R7);
  expect(memory.load_game_pak_rom(timed_ldmia_oam5_prefetch),
         "timed scheduler loads prefetched all-mirrored-OAM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFECU);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam5_prefetch_step =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam5_prefetch_step.step.has_value(),
         "timed prefetched all-mirrored-OAM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam5_prefetch_step.step->cpu_step.elapsed_cycles == 3,
         "prefetched all-mirrored-OAM ARM LDMIA collapses to the prefetch overlap timing");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4010);
  std::vector<std::uint8_t> timed_ldmia_oam5_prefetch_fast_seq(256);
  write_rom_word(timed_ldmia_oam5_prefetch_fast_seq, 0, kLdmiaR2WritebackR3R7);
  expect(memory.load_game_pak_rom(timed_ldmia_oam5_prefetch_fast_seq),
         "timed scheduler loads prefetched fast-sequential all-mirrored-OAM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFECU);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam5_prefetch_fast_seq_step =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam5_prefetch_fast_seq_step.step.has_value(),
         "timed prefetched fast-sequential all-mirrored-OAM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam5_prefetch_fast_seq_step.step->cpu_step.elapsed_cycles == 5,
         "prefetched fast-sequential all-mirrored-OAM ARM LDMIA keeps the fast-sequential calibration gap");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4000);
  std::vector<std::uint8_t> timed_ldmia_oam2_to_rom_prefetch(256);
  write_rom_word(timed_ldmia_oam2_to_rom_prefetch, 0, kLdmiaR2WritebackR3R7);
  write_rom_word(timed_ldmia_oam2_to_rom_prefetch, 0x04, 0x13579BDFU);
  write_rom_word(timed_ldmia_oam2_to_rom_prefetch, 0x08, 0x2468ACE0U);
  expect(memory.load_game_pak_rom(timed_ldmia_oam2_to_rom_prefetch),
         "timed scheduler loads two-word mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFF8U);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam2_rom_prefetch =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam2_rom_prefetch.step.has_value(),
         "timed two-word mirrored-OAM-to-ROM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam2_rom_prefetch.step->cpu_step.elapsed_cycles == 28,
         "prefetched two-word mirrored-OAM-to-ROM ARM LDMIA shares one less recovery cycle");
  expect(cpu.register_value(2) == 0x0800000CU,
         "prefetched two-word mirrored-OAM-to-ROM ARM LDMIA writes back across ROM");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x0004);
  std::vector<std::uint8_t> timed_ldmia_oam2_to_rom_nonseq(256);
  write_rom_word(timed_ldmia_oam2_to_rom_nonseq, 0, kLdmiaR2WritebackR3R7);
  expect(memory.load_game_pak_rom(timed_ldmia_oam2_to_rom_nonseq),
         "timed scheduler loads nonsequential two-word mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFF8U);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam2_rom_nonseq =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam2_rom_nonseq.step.has_value(),
         "timed nonsequential two-word mirrored-OAM-to-ROM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam2_rom_nonseq.step->cpu_step.elapsed_cycles == 23,
         "nonsequential two-word mirrored-OAM-to-ROM ARM LDMIA shares boundary recovery");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4004);
  std::vector<std::uint8_t> timed_ldmia_oam2_to_rom_prefetch_nonseq(256);
  write_rom_word(timed_ldmia_oam2_to_rom_prefetch_nonseq, 0, kLdmiaR2WritebackR3R7);
  expect(memory.load_game_pak_rom(timed_ldmia_oam2_to_rom_prefetch_nonseq),
         "timed scheduler loads prefetched nonsequential two-word mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFF8U);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam2_rom_prefetch_nonseq =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam2_rom_prefetch_nonseq.step.has_value(),
         "timed prefetched nonsequential two-word mirrored-OAM-to-ROM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam2_rom_prefetch_nonseq.step->cpu_step.elapsed_cycles == 26,
         "prefetched nonsequential two-word mirrored-OAM-to-ROM ARM LDMIA shares boundary recovery");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x0010);
  std::vector<std::uint8_t> timed_ldmia_oam2_to_rom_fast_seq(256);
  write_rom_word(timed_ldmia_oam2_to_rom_fast_seq, 0, kLdmiaR2WritebackR3R7);
  expect(memory.load_game_pak_rom(timed_ldmia_oam2_to_rom_fast_seq),
         "timed scheduler loads fast-sequential two-word mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFF8U);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam2_rom_fast_seq =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam2_rom_fast_seq.step.has_value(),
         "timed fast-sequential two-word mirrored-OAM-to-ROM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam2_rom_fast_seq.step->cpu_step.elapsed_cycles == 19,
         "fast-sequential two-word mirrored-OAM-to-ROM ARM LDMIA shares boundary recovery");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4010);
  std::vector<std::uint8_t> timed_ldmia_oam2_to_rom_prefetch_fast_seq(256);
  write_rom_word(timed_ldmia_oam2_to_rom_prefetch_fast_seq, 0, kLdmiaR2WritebackR3R7);
  expect(memory.load_game_pak_rom(timed_ldmia_oam2_to_rom_prefetch_fast_seq),
         "timed scheduler loads prefetched fast-sequential two-word mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFF8U);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam2_rom_prefetch_fast_seq =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam2_rom_prefetch_fast_seq.step.has_value(),
         "timed prefetched fast-sequential two-word mirrored-OAM-to-ROM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam2_rom_prefetch_fast_seq.step->cpu_step.elapsed_cycles == 23,
         "prefetched fast-sequential two-word mirrored-OAM-to-ROM ARM LDMIA shares boundary recovery");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4014);
  std::vector<std::uint8_t> timed_ldmia_oam2_to_rom_prefetch_nonseq_fast_seq(256);
  write_rom_word(timed_ldmia_oam2_to_rom_prefetch_nonseq_fast_seq, 0, kLdmiaR2WritebackR3R7);
  expect(memory.load_game_pak_rom(timed_ldmia_oam2_to_rom_prefetch_nonseq_fast_seq),
         "timed scheduler loads prefetched nonsequential fast-sequential two-word mirrored-OAM-to-ROM LDMIA fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(2, 0x07FFFFF8U);
  const gba::core::CoreSchedulerFetchStepResult timed_ldmia_oam2_rom_prefetch_nonseq_fast_seq =
      timed_scheduler.step_from_pc();
  expect(timed_ldmia_oam2_rom_prefetch_nonseq_fast_seq.step.has_value(),
         "timed prefetched nonsequential fast-sequential two-word mirrored-OAM-to-ROM ARM LDMIA reports scheduler step");
  expect(timed_ldmia_oam2_rom_prefetch_nonseq_fast_seq.step->cpu_step.elapsed_cycles == 21,
         "prefetched nonsequential fast-sequential two-word mirrored-OAM-to-ROM ARM LDMIA shares boundary recovery");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x0010);
  std::vector<std::uint8_t> timed_halfword_rom(512);
  write_rom_word(timed_halfword_rom, 0, 0xE1D320B0U);
  write_rom_word(timed_halfword_rom, 4, kAddR0R0Imm1);
  write_rom_halfword(timed_halfword_rom, 0x180, 0x1234U);
  expect(memory.load_game_pak_rom(timed_halfword_rom),
         "timed scheduler loads ROM halfword data blob");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(3, kGamePakProgramBase + 0x180U);
  const gba::core::CoreSchedulerFetchStepResult timed_ldrh =
      timed_scheduler.step_from_pc();
  expect(timed_ldrh.fetch_cycles == 6,
         "fast sequential WAITCNT charges ARM ROM halfword-load fetch");
  expect(timed_ldrh.step.has_value(), "timed cartridge LDRH reports scheduler step");
  expect(timed_ldrh.step->cpu_step.elapsed_cycles == 6,
         "fast sequential WAITCNT charges ARM ROM halfword-load data cycles");
  expect(cpu.register_value(2) == 0x1234U,
         "timed cartridge LDRH reads ROM halfword data");
  const gba::core::CoreSchedulerFetchStepResult after_halfword_data =
      timed_scheduler.step_from_pc();
  expect(!after_halfword_data.prefetch_hit,
         "non-word cartridge data access invalidates the next prefetch hit");
  expect(after_halfword_data.fetch_cycles == 7,
         "non-word cartridge data access adds fast-sequential bus recovery to next ARM fetch");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x0010);
  std::vector<std::uint8_t> timed_thumb_halfword_rom(512);
  write_rom_halfword(timed_thumb_halfword_rom, 0, 0x881AU);
  write_rom_halfword(timed_thumb_halfword_rom, 2, kThumbAddR2Imm4);
  write_rom_halfword(timed_thumb_halfword_rom, 0x180, 0x5678U);
  expect(memory.load_game_pak_rom(timed_thumb_halfword_rom),
         "timed scheduler loads Thumb ROM halfword data blob");
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "timed scheduler enters Thumb state for ROM halfword fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(3, kGamePakProgramBase + 0x180U);
  const gba::core::CoreSchedulerFetchStepResult timed_thumb_ldrh =
      timed_scheduler.step_from_pc();
  expect(timed_thumb_ldrh.fetch_cycles == 4,
         "fast sequential WAITCNT charges Thumb ROM halfword-load fetch");
  expect(timed_thumb_ldrh.step.has_value(),
         "timed cartridge Thumb LDRH reports scheduler step");
  expect(timed_thumb_ldrh.step->cpu_step.elapsed_cycles == 6,
         "fast sequential WAITCNT charges Thumb ROM halfword-load data cycles");
  expect(cpu.register_value(2) == 0x5678U,
         "timed cartridge Thumb LDRH reads ROM halfword data");
  const gba::core::CoreSchedulerFetchStepResult after_thumb_halfword_data =
      timed_scheduler.step_from_pc();
  expect(!after_thumb_halfword_data.prefetch_hit,
         "non-word cartridge data access invalidates the next Thumb prefetch hit");
  expect(after_thumb_halfword_data.fetch_cycles == 5,
         "non-word cartridge data access adds fast-sequential bus recovery to next Thumb fetch");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4000);
  std::vector<std::uint8_t> timed_thumb_rom_word_then_stack_rom(0x184);
  write_rom_halfword(timed_thumb_rom_word_then_stack_rom, 0, 0x681AU);
  write_rom_halfword(timed_thumb_rom_word_then_stack_rom, 2, 0x9A00U);
  write_rom_halfword(timed_thumb_rom_word_then_stack_rom, 4, kThumbAddR2Imm4);
  write_rom_word(timed_thumb_rom_word_then_stack_rom, 0x180, 0xDEADBEEFU);
  expect(memory.load_game_pak_rom(timed_thumb_rom_word_then_stack_rom),
         "timed scheduler loads Thumb ROM word plus stack-load blob");
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "timed scheduler enters Thumb state for ROM word plus stack fixture");
  constexpr std::uint32_t kStackFixtureSp = 0x03007A64U;
  expect(memory.write32(kStackFixtureSp, 0x3003300U),
         "timed scheduler seeds stack word fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(3, kGamePakProgramBase + 0x180U);
  cpu.set_register(13, kStackFixtureSp);
  const gba::core::CoreSchedulerFetchStepResult timed_thumb_rom_word =
      timed_scheduler.step_from_pc();
  expect(timed_thumb_rom_word.step.has_value(),
         "Thumb ROM word load reports scheduler step");
  expect(timed_thumb_rom_word.step->cpu_step.memory_timing_applied,
         "Thumb ROM word load reports WAITCNT data-load timing");
  expect(cpu.register_value(2) == 0xDEADBEEFU,
         "Thumb ROM word fixture reads cartridge data");
  expect(!timed_thumb_rom_word.prefetch_hit,
         "first Thumb ROM word load is not served from prefetch");
  const gba::core::CoreSchedulerFetchStepResult timed_thumb_stack_after_rom =
      timed_scheduler.step_from_pc();
  expect(timed_thumb_stack_after_rom.step.has_value(),
         "Thumb stack load after ROM word load reports scheduler step");
  expect(!timed_thumb_stack_after_rom.prefetch_enabled,
         "ROM word data load suppresses the following Thumb prefetch fetch");
  expect(timed_thumb_stack_after_rom.fetch_cycles == 4,
         "stack load after ROM word data fetch restarts as non-prefetched ROM fetch");
  expect(timed_thumb_stack_after_rom.step->cpu_step.elapsed_cycles == 2,
         "suppressed-prefetch Thumb stack load still overlaps one internal data cycle");
  expect(timed_thumb_stack_after_rom.prefetch_buffer_halfwords >= 1,
         "overlapped internal stack load gives prefetch one recovery halfword");
  const gba::core::CoreSchedulerFetchStepResult timed_thumb_after_stack =
      timed_scheduler.step_from_pc();
  expect(timed_thumb_after_stack.prefetch_hit,
         "prefetch resumes immediately after overlapped stack-load recovery");
  expect(timed_thumb_after_stack.fetch_cycles <= 1,
         "post-stack Thumb fetch uses recovered prefetch halfword");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(0x4010);
  std::vector<std::uint8_t> timed_thumb_stack_then_rom_word_rom(0x184);
  write_rom_halfword(timed_thumb_stack_then_rom_word_rom, 0, 0x9A00U);
  write_rom_halfword(timed_thumb_stack_then_rom_word_rom, 2, 0x681AU);
  write_rom_halfword(timed_thumb_stack_then_rom_word_rom, 4, kThumbAddR2Imm4);
  write_rom_word(timed_thumb_stack_then_rom_word_rom, 0x180, 0xA5A55A5AU);
  expect(memory.load_game_pak_rom(timed_thumb_stack_then_rom_word_rom),
         "timed scheduler loads Thumb stack plus ROM word blob");
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "timed scheduler enters Thumb state for stack plus ROM word fixture");
  expect(memory.write32(kStackFixtureSp, 0x3003300U),
         "timed scheduler seeds stack word for stack plus ROM fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  cpu.set_register(3, kGamePakProgramBase + 0x180U);
  cpu.set_register(13, kStackFixtureSp);
  const gba::core::CoreSchedulerFetchStepResult timed_thumb_stack_before_rom =
      timed_scheduler.step_from_pc();
  expect(timed_thumb_stack_before_rom.step.has_value(),
         "Thumb stack load before ROM word reports scheduler step");
  expect(timed_thumb_stack_before_rom.step->data_access.has_value(),
         "Thumb stack load before ROM word reports its internal data access");
  const gba::core::CoreSchedulerFetchStepResult timed_thumb_rom_after_stack =
      timed_scheduler.step_from_pc();
  expect(timed_thumb_rom_after_stack.prefetch_hit,
         "Thumb ROM word after stack load is fetched from prefetch");
  expect(timed_thumb_rom_after_stack.fetch_cycles == 0,
         "fast-sequential prefetched Thumb ROM word fetch is hidden");
  expect(timed_thumb_rom_after_stack.step.has_value(),
         "Thumb ROM word after stack load reports scheduler step");
  expect(timed_thumb_rom_after_stack.step->cpu_step.elapsed_cycles == 10,
         "fast-sequential Thumb ROM word after internal load keeps composition penalty");
  expect(cpu.register_value(2) == 0xA5A55A5AU,
         "Thumb stack plus ROM fixture reads cartridge data");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  waitcnt.write_control(WaitStateControl::kStandardGamePakSetting);
  std::vector<std::uint8_t> timed_prefill_rom(256);
  write_rom_word(timed_prefill_rom, 0, kAddR0R0Imm1);
  write_rom_word(timed_prefill_rom, 4, kAddR0R0Imm1);
  expect(memory.load_game_pak_rom(timed_prefill_rom),
         "timed scheduler reloads ROM for WAITCNT reset fixture");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);
  const gba::core::CoreSchedulerFetchStepResult prefilling_add =
      timed_scheduler.step_from_pc();
  expect(prefilling_add.prefetch_buffer_halfwords > 0,
         "WAITCNT reset fixture starts with a populated prefetch buffer");
  waitcnt.write_control(0);
  const gba::core::CoreSchedulerFetchStepResult after_waitcnt_write =
      timed_scheduler.step_from_pc();
  expect(!after_waitcnt_write.prefetch_hit,
         "WAITCNT control changes invalidate pending prefetch halfwords");
  expect(!after_waitcnt_write.prefetch_enabled,
         "WAITCNT control change reports the latest prefetch disable state");
  expect(after_waitcnt_write.fetch_cycles == 7,
         "post-WAITCNT ARM fetch restarts with timer-visible non-prefetch "
         "non-sequential wait0 timing");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  expect(memory.write32(kEwramProgramBase, kAddR0R0Imm1),
         "timed scheduler seeds ARM instruction in EWRAM");
  cpu.set_register(Arm7tdmi::kPc, kEwramProgramBase);
  const gba::core::CoreSchedulerFetchStepResult ewram_arm =
      timed_scheduler.step_from_pc();
  expect(ewram_arm.fetch_timing_applied,
         "EWRAM ARM opcode fetch applies memory-region timing");
  expect(ewram_arm.fetch_cycles == 5,
         "EWRAM ARM opcode fetch charges word bus cost minus unit CPU cycle");
  expect(ewram_arm.step.has_value() && ewram_arm.step->cpu_step.elapsed_cycles == 1,
         "EWRAM ARM ADD still reports unit CPU execution cost");
  expect(timed_scheduler.scheduler_cycles() == 6,
         "EWRAM ARM dispatch totals documented word fetch cost");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  expect(memory.write16(kSyntheticProgramBase, kThumbBranchPlusOneHalfword),
         "timed scheduler seeds line-rollover Thumb branch in IWRAM");
  expect(memory.write16(kSyntheticProgramBase + 6U, kThumbMovR2Imm3),
         "timed scheduler seeds line-rollover Thumb branch target");
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "timed scheduler enters Thumb for line-rollover branch fixture");
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  ppu.tick(PpuTiming::kCyclesPerLine - 1U, interrupts);
  const gba::core::CoreSchedulerFetchStepResult rollover_thumb_branch =
      timed_scheduler.step_from_pc();
  expect(rollover_thumb_branch.step.has_value(),
         "line-rollover Thumb branch reports scheduler step");
  expect(rollover_thumb_branch.step->cpu_step.elapsed_cycles == 3,
         "ordinary internal Thumb branch crossing line rollover keeps core branch timing");
  expect(ppu.line_cycle() == 2,
         "ordinary line-rollover Thumb branch advances devices without status stall");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  expect(memory.write16(kSyntheticProgramBase, kThumbBranchPlusOneHalfword),
         "timed scheduler seeds HBlank-sensitive line-rollover Thumb branch");
  expect(memory.write16(kSyntheticProgramBase + 6U, kThumbMovR2Imm3),
         "timed scheduler seeds HBlank-sensitive line-rollover branch target");
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "timed scheduler enters Thumb for HBlank-sensitive branch fixture");
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  ppu.write_dispstat(0x0010U);
  ppu.tick(PpuTiming::kCyclesPerLine - 1U, interrupts);
  const gba::core::CoreSchedulerFetchStepResult hblank_rollover_thumb_branch =
      timed_scheduler.step_from_pc();
  expect(hblank_rollover_thumb_branch.step.has_value(),
         "HBlank-sensitive line-rollover Thumb branch reports scheduler step");
  expect(hblank_rollover_thumb_branch.step->cpu_step.elapsed_cycles == 5,
         "HBlank-sensitive internal Thumb branch carries the rollover stall");
  expect(ppu.line_cycle() == 4,
         "HBlank-sensitive line-rollover Thumb branch advances through the stall");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  expect(memory.write16(kEwramProgramBase, kThumbMovR2Imm3),
         "timed scheduler seeds Thumb instruction in EWRAM");
  expect(cpu.set_cpsr(kThumbStateSupervisor), "timed scheduler enters Thumb for EWRAM");
  cpu.set_register(Arm7tdmi::kPc, kEwramProgramBase);
  const gba::core::CoreSchedulerFetchStepResult ewram_thumb =
      timed_scheduler.step_from_pc();
  expect(ewram_thumb.fetch_timing_applied,
         "EWRAM Thumb opcode fetch applies memory-region timing");
  expect(ewram_thumb.fetch_cycles == 2,
         "EWRAM Thumb opcode fetch charges halfword bus cost minus unit CPU cycle");
  expect(timed_scheduler.scheduler_cycles() == 3,
         "EWRAM Thumb dispatch totals documented halfword fetch cost");

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  PpuIoCallbackContext ppu_io_context{&ppu};
  memory.set_io_callbacks({&ppu_io_context, read_ppu_io16, nullptr, nullptr,
                           nullptr});
  expect(memory.write16(kSyntheticProgramBase, kThumbLdrhR0R1Imm0),
         "timed scheduler seeds Thumb DISPSTAT load instruction in IWRAM");
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "timed scheduler enters Thumb for DISPSTAT data-phase fixture");
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  cpu.set_register(1, 0x04000004U);
  ppu.tick(PpuTiming::kHblankFlagCycles - 4U, interrupts);
  expect((ppu.dispstat() & 0x0002U) == 0,
         "DISPSTAT stable fixture starts before the CPU-visible HBlank flag");
  const gba::core::CoreSchedulerFetchStepResult stable_dispstat_load =
      timed_scheduler.step_from_pc();
  expect(stable_dispstat_load.step.has_value(),
         "stable Thumb DISPSTAT load reports scheduler step");
  expect(stable_dispstat_load.step->data_access.has_value(),
         "stable Thumb DISPSTAT load reports data access");
  expect(stable_dispstat_load.step->data_access->pre_access_cycles == 2,
         "stable Thumb DISPSTAT load samples at the LCD status data phase");
  expect(stable_dispstat_load.step->cpu_step.elapsed_cycles == 3,
         "stable Thumb DISPSTAT load keeps ordinary IO timing");
  expect((cpu.register_value(0) & 0x0002U) == 0,
         "stable Thumb DISPSTAT load does not see HBlank early");

  cpu.reset();
  ppu.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "timed scheduler re-enters Thumb for DISPSTAT completion fixture");
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  cpu.set_register(1, 0x04000004U);
  ppu.tick(PpuTiming::kHblankFlagCycles - 3U, interrupts);
  expect((ppu.dispstat() & 0x0002U) == 0,
         "DISPSTAT completion fixture starts before the HBlank flag");
  const gba::core::CoreSchedulerFetchStepResult completing_dispstat_load =
      timed_scheduler.step_from_pc();
  expect(completing_dispstat_load.step.has_value(),
         "completing Thumb DISPSTAT load reports scheduler step");
  expect(completing_dispstat_load.step->data_access.has_value(),
         "completing Thumb DISPSTAT load reports data access");
  expect(completing_dispstat_load.step->data_access->pre_access_cycles == 3,
         "Thumb DISPSTAT load completing on the status edge samples completion");
  expect(completing_dispstat_load.step->cpu_step.elapsed_cycles == 4,
         "Thumb DISPSTAT load completing on the status edge carries the transition stall");
  expect((cpu.register_value(0) & 0x0002U) != 0,
         "Thumb DISPSTAT completion load observes HBlank at completion");

  cpu.reset();
  ppu.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "timed scheduler re-enters Thumb for DISPSTAT transition fixture");
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  cpu.set_register(1, 0x04000004U);
  ppu.tick(PpuTiming::kHblankFlagCycles - 2U, interrupts);
  expect((ppu.dispstat() & 0x0002U) == 0,
         "exact-edge DISPSTAT fixture starts before the CPU-visible HBlank flag");
  const gba::core::CoreSchedulerFetchStepResult exact_edge_dispstat_load =
      timed_scheduler.step_from_pc();
  expect(exact_edge_dispstat_load.step.has_value(),
         "exact-edge Thumb DISPSTAT load reports scheduler step");
  expect(exact_edge_dispstat_load.step->data_access.has_value(),
         "exact-edge Thumb DISPSTAT load reports data access");
  expect(exact_edge_dispstat_load.step->data_access->pre_access_cycles == 0,
         "exact-edge Thumb DISPSTAT load keeps the old HBlank bit");
  expect((cpu.register_value(0) & 0x0002U) == 0,
         "exact-edge Thumb DISPSTAT load observes HBlank clear");

  cpu.reset();
  ppu.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "timed scheduler re-enters Thumb for DISPSTAT transition fixture");
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  cpu.set_register(1, 0x04000004U);
  ppu.tick(PpuTiming::kHblankFlagCycles - 1U, interrupts);
  expect((ppu.dispstat() & 0x0002U) == 0,
         "DISPSTAT fixture starts before the CPU-visible HBlank flag");
  const gba::core::CoreSchedulerFetchStepResult dispstat_load =
      timed_scheduler.step_from_pc();
  expect(dispstat_load.step.has_value(),
         "Thumb DISPSTAT load reports scheduler step");
  expect(dispstat_load.step->data_access.has_value(),
         "Thumb DISPSTAT load reports data access");
  expect(dispstat_load.step->data_access->pre_access_cycles == 2,
         "Thumb DISPSTAT load samples at the LCD status data phase");
  expect(dispstat_load.step->cpu_step.elapsed_cycles == 4,
         "Thumb DISPSTAT load crossing the status transition carries the status stall");
  expect((cpu.register_value(0) & 0x0002U) != 0,
         "Thumb DISPSTAT load observes HBlank after its data phase");

  cpu.reset();
  ppu.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "timed scheduler re-enters Thumb for DISPSTAT rollover fixture");
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  cpu.set_register(1, 0x04000004U);
  ppu.write_dispstat(0x0010U);
  ppu.tick(PpuTiming::kCyclesPerLine - 1U, interrupts);
  expect((ppu.dispstat() & 0x0002U) != 0,
         "DISPSTAT rollover fixture starts with HBlank set");
  const gba::core::CoreSchedulerFetchStepResult rollover_dispstat_load =
      timed_scheduler.step_from_pc();
  expect(rollover_dispstat_load.step.has_value(),
         "rollover Thumb DISPSTAT load reports scheduler step");
  expect(rollover_dispstat_load.step->data_access.has_value(),
         "rollover Thumb DISPSTAT load reports data access");
  expect(rollover_dispstat_load.step->data_access->pre_access_cycles == 2,
         "rollover Thumb DISPSTAT load keeps the LCD status data phase");
  expect(rollover_dispstat_load.step->cpu_step.elapsed_cycles == 6,
         "rollover Thumb DISPSTAT load carries the line status stall");
  expect((cpu.register_value(0) & 0x0002U) == 0,
         "rollover Thumb DISPSTAT load observes the cleared HBlank bit");

  cpu.reset();
  timers.reset();
  ppu.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  TimerIoCallbackContext late_hblank_timer_context{&timers};
  memory.set_io_callbacks({&late_hblank_timer_context, read_timer_io16,
                           read_timer_io32, nullptr, nullptr});
  expect(memory.write16(kSyntheticProgramBase, kThumbLdrhR0R1Imm0),
         "timed scheduler seeds Thumb late-HBlank timer load instruction");
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "timed scheduler enters Thumb for late-HBlank timer load fixture");
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  cpu.set_register(1, 0x04000100U);
  timers.write_reload(0, 0);
  timers.write_control(0, 0x0080);
  timers.tick(0x0100U, interrupts);
  interrupts.write_interrupt_enable(
      1U << static_cast<std::uint8_t>(InterruptSource::hblank));
  ppu.tick(PpuTiming::kVisibleCycles + 64U, interrupts);
  const gba::core::CoreSchedulerFetchStepResult late_hblank_timer_load =
      timed_scheduler.step_from_pc();
  expect(late_hblank_timer_load.step.has_value(),
         "late-HBlank timer load reports scheduler step");
  expect(late_hblank_timer_load.step->data_access.has_value(),
         "late-HBlank timer load reports data access");
  expect(late_hblank_timer_load.step->data_access->pre_access_cycles == 3,
         "IME-masked late-HBlank timer load samples at the data phase");
  expect(cpu.register_value(0) == 0x0103U,
         "IME-masked late-HBlank timer load observes timer after data phase");

  cpu.reset();
  timers.reset();
  ppu.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "timed scheduler re-enters Thumb for early-HBlank timer load fixture");
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  cpu.set_register(1, 0x04000100U);
  timers.write_reload(0, 0);
  timers.write_control(0, 0x0080);
  timers.tick(0x0100U, interrupts);
  interrupts.write_interrupt_enable(
      1U << static_cast<std::uint8_t>(InterruptSource::hblank));
  ppu.tick(PpuTiming::kVisibleCycles + 58U, interrupts);
  const gba::core::CoreSchedulerFetchStepResult early_hblank_timer_load =
      timed_scheduler.step_from_pc();
  expect(early_hblank_timer_load.step.has_value(),
         "early-HBlank timer load reports scheduler step");
  expect(early_hblank_timer_load.step->data_access.has_value(),
         "early-HBlank timer load reports data access");
  expect(early_hblank_timer_load.step->data_access->pre_access_cycles == 3,
         "IME-masked early-HBlank timer load samples at the data phase");
  expect(cpu.register_value(0) == 0x0103U,
         "IME-masked early-HBlank timer load observes timer after data phase");

  cpu.reset();
  timers.reset();
  ppu.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "timed scheduler re-enters Thumb for clear-side VBlank timer load");
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  cpu.set_register(1, 0x04000100U);
  timers.write_reload(0, 0);
  timers.write_control(0, 0x0080);
  timers.tick(0x0100U, interrupts);
  interrupts.write_interrupt_enable(
      1U << static_cast<std::uint8_t>(InterruptSource::hblank));
  interrupts.request(InterruptSource::hblank);
  ppu.tick(PpuTiming::kCyclesPerLine * 162U + 10U, interrupts);
  expect(ppu.vblank() && !ppu.hblank(),
         "clear-side timer fixture starts on a VBlank line before the HBlank flag");
  const gba::core::CoreSchedulerFetchStepResult clear_side_timer_load =
      timed_scheduler.step_from_pc();
  expect(clear_side_timer_load.step.has_value(),
         "clear-side VBlank timer load reports scheduler step");
  expect(clear_side_timer_load.step->data_access.has_value(),
         "clear-side VBlank timer load reports data access");
  expect(clear_side_timer_load.step->data_access->pre_access_cycles == 4,
         "pending HBlank clear-side timer load samples at the line-clear settle point");
  expect(cpu.register_value(0) == 0x0104U,
         "pending HBlank clear-side timer load observes timer after clear settle");

  cpu.reset();
  timers.reset();
  ppu.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "timed scheduler re-enters Thumb for long HBlank poll timer load");
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  cpu.set_register(1, 0x04000100U);
  timers.write_reload(0, 0);
  timers.write_control(0, 0x0080);
  timers.tick(0x0100U, interrupts);
  interrupts.write_interrupt_enable(
      1U << static_cast<std::uint8_t>(InterruptSource::hblank));
  ppu.tick(PpuTiming::kHblankFlagCycles + 10U, interrupts);
  gba::core::CoreSchedulerState long_hblank_poll_state =
      timed_scheduler.save_state();
  long_hblank_poll_state.timer_io_access_gap_cycles =
      PpuTiming::kHblankFlagCycles - 5U;
  timed_scheduler.load_state(long_hblank_poll_state);
  const gba::core::CoreSchedulerFetchStepResult long_hblank_timer_load =
      timed_scheduler.step_from_pc();
  expect(long_hblank_timer_load.step.has_value(),
         "long HBlank poll timer load reports scheduler step");
  expect(long_hblank_timer_load.step->data_access.has_value(),
         "long HBlank poll timer load reports data access");
  expect(long_hblank_timer_load.step->data_access->pre_access_cycles == 2,
         "long HBlank poll timer load samples before normal completion");
  expect(cpu.register_value(0) == 0x0102U,
         "long HBlank poll timer load observes timer at the early sample point");

  cpu.reset();
  timers.reset();
  ppu.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "timed scheduler re-enters Thumb for IME-enabled late-HBlank timer load");
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  cpu.set_register(1, 0x04000100U);
  timers.write_reload(0, 0);
  timers.write_control(0, 0x0080);
  timers.tick(0x0100U, interrupts);
  ppu.tick(PpuTiming::kVisibleCycles + 64U, interrupts);
  interrupts.write_ime(1);
  const gba::core::CoreSchedulerFetchStepResult ime_enabled_timer_load =
      timed_scheduler.step_from_pc();
  expect(ime_enabled_timer_load.step.has_value(),
         "IME-enabled late-HBlank timer load reports scheduler step");
  expect(ime_enabled_timer_load.step->data_access.has_value(),
         "IME-enabled late-HBlank timer load reports data access");
  expect(ime_enabled_timer_load.step->data_access->pre_access_cycles == 0,
         "IME-enabled late-HBlank timer load keeps ordinary timer sampling");
  expect(cpu.register_value(0) == 0x0100U,
         "IME-enabled late-HBlank timer load observes timer without data-phase delay");
  memory.clear_io_callbacks();

  cpu.reset();
  memory.reset();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  timed_scheduler.reset_scheduler_cycles();
  expect(memory.write16(kSyntheticProgramBase, kThumbStrR0R1Imm0),
         "timed scheduler seeds Thumb store instruction in IWRAM");
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "timed scheduler enters Thumb for internal-store fixture");
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  cpu.set_register(0, 0x11223344U);
  cpu.set_register(1, kSyntheticProgramBase + 0x100U);
  const gba::core::CoreSchedulerFetchStepResult iwram_thumb_store =
      timed_scheduler.step_from_pc();
  expect(iwram_thumb_store.step.has_value(),
         "IWRAM Thumb store reports scheduler step");
  expect(iwram_thumb_store.step->cpu_step.status == ExecuteStatus::executed,
         "IWRAM Thumb store executes");
  expect(iwram_thumb_store.step->cpu_step.memory_timing_applied,
         "IWRAM Thumb store uses internal data timing");
  expect(iwram_thumb_store.step->cpu_step.elapsed_cycles == 2,
         "IWRAM Thumb store charges the internal store bus cycle");
  expect(timed_scheduler.scheduler_cycles() == 2,
         "IWRAM Thumb store dispatch accounts for internal timing");
  expect_read32(memory, kSyntheticProgramBase + 0x100U, 0x11223344U,
                "IWRAM Thumb store writes through the timed scheduler");

  cpu.reset();
  ppu.reset();
  timed_scheduler.reset_scheduler_cycles();
  expect(memory.write16(kSyntheticProgramBase, kThumbStrR0R1Imm0),
         "timed scheduler seeds HBlank Thumb store instruction in IWRAM");
  expect(cpu.set_cpsr(kThumbStateSupervisor),
         "timed scheduler enters Thumb for HBlank internal-store fixture");
  cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
  cpu.set_register(0, 0x55667788U);
  cpu.set_register(1, kSyntheticProgramBase + 0x104U);
  ppu.tick(PpuTiming::kVisibleCycles, interrupts);
  interrupts.request(InterruptSource::hblank);
  const gba::core::CoreSchedulerFetchStepResult hblank_iwram_thumb_store =
      timed_scheduler.step_from_pc();
  expect(hblank_iwram_thumb_store.step.has_value(),
         "HBlank IWRAM Thumb store reports scheduler step");
  expect(hblank_iwram_thumb_store.step->cpu_step.status == ExecuteStatus::executed,
         "HBlank IWRAM Thumb store executes");
  expect(hblank_iwram_thumb_store.step->cpu_step.memory_timing_applied,
         "HBlank IWRAM Thumb store uses internal data timing");
  expect(hblank_iwram_thumb_store.step->cpu_step.elapsed_cycles == 1,
         "HBlank IWRAM Thumb store overlaps the internal store bus cycle");
  expect_read32(memory, kSyntheticProgramBase + 0x104U, 0x55667788U,
                "HBlank IWRAM Thumb store writes through the timed scheduler");

  cpu.reset();
  memory.reset();
  scheduler.reset_scheduler_cycles();
  timers.reset();
  ppu.reset();
  apu.reset();
  dma.reset();
  interrupts.reset();
  gba::core::BiosController vblank_bios;
  vblank_bios.set_mode(gba::core::BiosExecutionMode::hle);
  gba::core::CoreScheduler vblank_scheduler(cpu, memory, interrupts, timers, dma, ppu, apu,
                                            waitcnt, vblank_bios);
  std::vector<std::uint8_t> vblank_irq_rom(256);
  write_rom_halfword(vblank_irq_rom, 0, 0x4700U);
  expect(memory.load_game_pak_rom(vblank_irq_rom), "VBlankIntrWait IRQ handler ROM loads");
  expect(memory.write32(0x03007FFCU, 0x08000001U),
         "VBlankIntrWait user handler pointer writes");
  interrupts.write_interrupt_enable(irq_bit(gba::core::InterruptSource::vblank));
  interrupts.write_ime(1);
  expect(cpu.set_cpsr(0x0000001FU | 0x20U),
         "VBlankIntrWait seed enters Thumb system mode");
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase + 0x20U);
  ppu.tick(gba::core::PpuTiming::kCyclesPerFrame, interrupts);
  interrupts.request(gba::core::InterruptSource::vblank);
  expect(memory.write16(kGamePakProgramBase + 0x20U, 0xDF05U),
         "VBlankIntrWait seeds Thumb SWI 5");
  const gba::core::CoreSchedulerFetchStepResult vblank_wait = vblank_scheduler.step_from_pc();
  expect(vblank_wait.step.has_value(), "VBlankIntrWait SWI 5 executes");
  expect(vblank_wait.step->cpu_step.status == ExecuteStatus::executed,
         "VBlankIntrWait SWI 5 reports executed");
  expect(cpu.register_value(Arm7tdmi::kPc) == kGamePakProgramBase + 0x22U,
         "VBlankIntrWait SWI 5 advances Thumb PC past SWI");
  expect(vblank_scheduler.service_pending_irq(),
         "VBlankIntrWait services pending VBlank IRQ after SWI 5");
  expect(cpu.register_value(Arm7tdmi::kPc) == 0x18,
         "VBlankIntrWait IRQ service vectors to BIOS IRQ address");
  expect(vblank_scheduler.step_from_pc().step.has_value(),
         "VBlankIntrWait dispatches to user handler");
  expect(vblank_scheduler.step_from_pc().step.has_value(),
         "VBlankIntrWait user handler returns to sentinel");
  const gba::core::CoreSchedulerFetchStepResult vblank_return =
      vblank_scheduler.step_from_pc();
  expect(vblank_return.step.has_value(),
         "VBlankIntrWait sentinel restores interrupted context");
  expect(cpu.register_value(Arm7tdmi::kPc) != gba::core::BiosHleConstants::kIrqReturnSentinelPc,
         "VBlankIntrWait sentinel does not stall PC");

  cpu.reset();
  memory.reset();
  scheduler.reset_scheduler_cycles();
  interrupts.reset();
  gba::core::BiosController sentinel_bios;
  sentinel_bios.set_mode(gba::core::BiosExecutionMode::hle);
  gba::core::CoreScheduler sentinel_scheduler(cpu, memory, interrupts, timers, dma, ppu, apu,
                                              waitcnt, sentinel_bios);
  cpu.set_register(Arm7tdmi::kPc, gba::core::BiosHleConstants::kIrqReturnSentinelPc);
  expect(cpu.set_cpsr(0x00000092U), "sentinel without LR seed enters IRQ mode");
  const gba::core::CoreSchedulerFetchStepResult sentinel_fail =
      sentinel_scheduler.step_from_pc();
  expect(sentinel_fail.step.has_value(),
         "sentinel without LR reports a scheduler step");
  expect(sentinel_fail.step->cpu_step.status == ExecuteStatus::unsupported,
         "sentinel without LR fails cleanly");
  expect(cpu.register_value(Arm7tdmi::kPc) ==
             gba::core::BiosHleConstants::kIrqReturnSentinelPc,
         "sentinel without LR preserves PC");

  cpu.reset();
  memory.reset();
  scheduler.reset_scheduler_cycles();
  interrupts.reset();
  gba::core::BiosController thumb_sentinel_bios;
  thumb_sentinel_bios.set_mode(gba::core::BiosExecutionMode::hle);
  gba::core::CoreScheduler thumb_sentinel_scheduler(cpu, memory, interrupts, timers, dma, ppu,
                                                      apu, waitcnt, thumb_sentinel_bios);
  std::vector<std::uint8_t> thumb_irq_rom(256);
  write_rom_halfword(thumb_irq_rom, 0, 0x2701U);
  write_rom_halfword(thumb_irq_rom, 2, 0x4770U);
  expect(memory.load_game_pak_rom(thumb_irq_rom), "Thumb sentinel IRQ handler ROM loads");
  expect(memory.write32(0x03007FFCU, 0x08000001U), "Thumb sentinel handler pointer writes");
  cpu.set_register(7, 0xCAFEBABEU);
  cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase + 0x10U);
  interrupts.write_interrupt_enable(irq_bit(gba::core::InterruptSource::timer0));
  interrupts.write_ime(1);
  interrupts.request(gba::core::InterruptSource::timer0);
  expect(thumb_sentinel_scheduler.service_pending_irq(), "Thumb sentinel seed enters IRQ");
  expect(thumb_sentinel_scheduler.step_from_pc().step.has_value(),
         "Thumb sentinel dispatches to user handler");
  expect(thumb_sentinel_scheduler.step_from_pc().step.has_value(),
         "Thumb sentinel user handler executes");
  expect(thumb_sentinel_scheduler.step_from_pc().step.has_value(),
         "Thumb sentinel user handler branches to sentinel");
  expect(cpu.register_value(Arm7tdmi::kPc) ==
             gba::core::BiosHleConstants::kIrqReturnSentinelPc,
         "Thumb sentinel lands on IRQ return sentinel");
  expect(cpu.set_cpsr(cpu.cpsr() | 0x20U),
         "Thumb sentinel return fetch uses Thumb state in IRQ mode");
  const gba::core::CoreSchedulerFetchStepResult thumb_sentinel_return =
      thumb_sentinel_scheduler.step_from_pc();
  expect(thumb_sentinel_return.step.has_value(),
         "Thumb sentinel HLE return restores context");
  expect(cpu.register_value(7) == 0xCAFEBABEU,
         "Thumb sentinel HLE return restores saved registers");
  expect(cpu.register_value(Arm7tdmi::kPc) !=
             gba::core::BiosHleConstants::kIrqReturnSentinelPc,
         "Thumb sentinel HLE return advances PC away from sentinel");

  // ------------------------------------------------------------------
  // S1: WAITCNT-change snapshot roundtrip must reproduce the identical
  // fetch sequence. last_waitcnt_control_ is persisted in
  // CoreSchedulerState so apply_fetch_timing/refill_prefetch_after_step see
  // the same control value before and after a rollback.
  {
    cpu.reset();
    memory.reset();
    timers.reset();
    ppu.reset();
    apu.reset();
    dma.reset();
    interrupts.reset();
    timed_scheduler.reset_scheduler_cycles();
    timed_scheduler.load_state(gba::core::CoreSchedulerState{});
    waitcnt.write_control(WaitStateControl::kStandardGamePakSetting);
    std::vector<std::uint8_t> waitcnt_rom(64);
    write_rom_word(waitcnt_rom, 0, kAddR0R0Imm1);
    write_rom_word(waitcnt_rom, 4, kAddR0R0Imm1);
    write_rom_word(waitcnt_rom, 8, kAddR0R0Imm1);
    expect(memory.load_game_pak_rom(waitcnt_rom), "WAITCNT snapshot ROM loads");
    cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase);

    const gba::core::CoreSchedulerFetchStepResult s1_first =
        timed_scheduler.step_from_pc();
    expect(s1_first.fetch_timing_applied && !s1_first.fetch_sequential,
           "WAITCNT snapshot fixture takes a non-sequential first fetch");
    const gba::core::CoreSchedulerFetchStepResult s1_second =
        timed_scheduler.step_from_pc();
    expect(s1_second.fetch_sequential,
           "WAITCNT snapshot fixture establishes a sequential fetch chain");
    const std::uint64_t s1_hash_at_save = timed_scheduler.state_hash();
    const std::uint32_t s1_saved_pc = cpu.register_value(Arm7tdmi::kPc);
    const std::uint64_t s1_cycles_at_save = timed_scheduler.scheduler_cycles();
    const gba::core::CoreSchedulerState s1_snapshot = timed_scheduler.save_state();
    expect(s1_snapshot.last_waitcnt_control.has_value() &&
               s1_snapshot.last_waitcnt_control.value() ==
                   WaitStateControl::kStandardGamePakSetting,
           "snapshot persists last WAITCNT control for fetch-timing parity");

    // Change WAITCNT and take one post-change fetch (reference outcome).
    constexpr std::uint16_t kChangedWaitCnt = 0x0000U;
    waitcnt.write_control(kChangedWaitCnt);
    const gba::core::CoreSchedulerFetchStepResult s1_changed =
        timed_scheduler.step_from_pc();
    expect(!s1_changed.fetch_sequential,
           "WAITCNT change resets the sequential fetch chain");
    const std::uint64_t s1_cycles_after_change = timed_scheduler.scheduler_cycles();

    // Roll back to the pre-change snapshot (CPU PC rewound to the save
    // point) and refetch. The restored last_waitcnt_control_ must re-detect
    // the control change and reproduce the reference non-sequential fetch.
    timed_scheduler.load_state(s1_snapshot);
    expect(timed_scheduler.state_hash() == s1_hash_at_save,
           "WAITCNT snapshot rollback restores the exact scheduler hash");
    cpu.set_register(Arm7tdmi::kPc, s1_saved_pc);
    const gba::core::CoreSchedulerFetchStepResult s1_refetched =
        timed_scheduler.step_from_pc();
    expect(!s1_refetched.fetch_sequential,
           "post-rollback fetch re-detects the WAITCNT change as non-sequential");
    expect(s1_refetched.fetch_cycles == s1_changed.fetch_cycles &&
               s1_refetched.fetch_timing_applied == s1_changed.fetch_timing_applied &&
               s1_refetched.prefetch_hit == s1_changed.prefetch_hit,
           "post-rollback WAITCNT change reproduces the reference fetch cycles");
    expect(timed_scheduler.scheduler_cycles() - s1_cycles_at_save ==
               s1_cycles_after_change - s1_cycles_at_save,
           "post-rollback WAITCNT change reproduces the reference cycle total");

    cpu.reset();
    memory.reset();
    timers.reset();
    ppu.reset();
    apu.reset();
    dma.reset();
    interrupts.reset();
    timed_scheduler.reset_scheduler_cycles();
    timed_scheduler.load_state(gba::core::CoreSchedulerState{});
    waitcnt.write_control(WaitStateControl::kStandardGamePakSetting);
  }

  // ------------------------------------------------------------------
  // S2: the Thumb step path must mirror ARM's slow-timer chained IRQ
  // mechanisms — the spaced-load PC-replay service block and the chained
  // post-return deferral gate — with width-appropriate PC offsets.
  {
    auto seed_replay_load = [&](bool thumb_state) {
      cpu.reset();
      memory.reset();
      timers.reset();
      ppu.reset();
      apu.reset();
      dma.reset();
      interrupts.reset();
      scheduler.reset_scheduler_cycles();
      scheduler.load_state(gba::core::CoreSchedulerState{});
      TimerIoCallbackContext replay_context{&timers};
      memory.set_io_callbacks({&replay_context, read_timer_io16,
                               read_timer_io32, nullptr, nullptr});
      timers.tick(179, interrupts);
      timers.write_reload(0, 0xFFED);
      timers.write_control(0, 0x00C3);
      timers.tick(845, interrupts);
      timers.tick(16U * 1024U, interrupts);
      timers.tick(1019, interrupts);
      if (thumb_state) {
        (void)cpu.set_cpsr(kThumbStateSupervisor);
      }
      interrupts.write_interrupt_enable(irq_bit(InterruptSource::timer0));
      interrupts.write_ime(1);
      // Timer0 IRQ already pending from an earlier overflow; the scheduler
      // state arms the auto-IRQ line so the pre-access service branch fires
      // deterministically for the spaced 0xFFED load.
      interrupts.request(InterruptSource::timer0);
      gba::core::CoreSchedulerState replay_state = scheduler.save_state();
      replay_state.hle_irq_return_latency_pending = true;
      replay_state.hle_irq_post_return_chain_active = true;
      replay_state.timer_io_access_gap_cycles = 20;
      replay_state.auto_irq_line_high = true;
      replay_state.auto_irq_latency_cycles = 0;
      scheduler.load_state(replay_state);
      cpu.set_register(Arm7tdmi::kPc, 0x030007BCU);
      cpu.set_register(2, 0x0BADF00DU);
      cpu.set_register(4, 0x04000100U);
    };

    seed_replay_load(false);
    const gba::core::CoreSchedulerStepResult arm_replay_step =
        scheduler.step_arm(0xE5942000U);
    expect(arm_replay_step.cpu_step.status == ExecuteStatus::executed,
           "ARM spaced slow-timer load executes");
    expect(arm_replay_step.data_access.has_value() &&
               arm_replay_step.data_access->load &&
               arm_replay_step.data_access->timer_io,
           "ARM spaced slow-timer load reports a timer I/O load");
    expect(arm_replay_step.irq_serviced,
           "ARM spaced slow-timer load services the matured timer IRQ");
    expect(cpu.current_mode() == gba::core::CpuMode::irq &&
               cpu.register_value(Arm7tdmi::kPc) == 0x18,
           "ARM spaced slow-timer load vectors to the IRQ handler");
    const std::uint32_t arm_replay_r2 = cpu.register_value(2);

    seed_replay_load(true);
    const gba::core::CoreSchedulerStepResult thumb_replay_step =
        scheduler.step_thumb(kThumbLdrR2R4Imm0);
    expect(thumb_replay_step.cpu_step.status == ExecuteStatus::executed,
           "Thumb spaced slow-timer load executes");
    expect(thumb_replay_step.data_access.has_value() &&
               thumb_replay_step.data_access->load &&
               thumb_replay_step.data_access->timer_io,
           "Thumb spaced slow-timer load reports a timer I/O load");
    expect(thumb_replay_step.irq_serviced,
           "Thumb spaced slow-timer load services the matured timer IRQ");
    expect(cpu.current_mode() == gba::core::CpuMode::irq &&
               cpu.register_value(Arm7tdmi::kPc) == 0x18,
           "Thumb spaced slow-timer load vectors to the IRQ handler");
    expect(cpu.register_value(2) == arm_replay_r2,
           "Thumb slow-timer load samples the same timer value as ARM (S2 parity)");
    memory.clear_io_callbacks();

    // Chained post-return deferral: with an armed chained IRQ that only
    // becomes ready during a one-cycle ALU instruction, both widths must
    // defer service to the next instruction.
    auto seed_defer_gate = [&]() {
      cpu.reset();
      memory.reset();
      timers.reset();
      ppu.reset();
      apu.reset();
      dma.reset();
      interrupts.reset();
      scheduler.reset_scheduler_cycles();
      gba::core::CoreSchedulerState defer_state{};
      defer_state.hle_irq_post_return_latency_armed = true;
      defer_state.hle_irq_post_return_chain_active = true;
      defer_state.auto_irq_line_high = true;
      defer_state.auto_irq_latency_cycles = 1;
      scheduler.load_state(defer_state);
      interrupts.write_interrupt_enable(irq_bit(InterruptSource::timer0));
      interrupts.write_ime(1);
      interrupts.request(InterruptSource::timer0);
      timers.write_reload(0, 0xFFED);
      timers.write_control(0, 0x00C3);
      cpu.set_register(Arm7tdmi::kPc, kSyntheticProgramBase);
    };

    seed_defer_gate();
    const gba::core::CoreSchedulerStepResult arm_defer_step =
        scheduler.step_arm(kAddR0R0Imm1);
    expect(arm_defer_step.cpu_step.status == ExecuteStatus::executed,
           "ARM chained post-return defer fixture executes its instruction");
    expect(!arm_defer_step.irq_serviced,
           "ARM defers an armed chained post-return IRQ after a 1-cycle ALU step");
    expect(cpu.current_mode() != gba::core::CpuMode::irq,
           "ARM deferred chained IRQ does not enter IRQ mode yet");
    const gba::core::CoreSchedulerStepResult arm_service_next =
        scheduler.step_arm(kAddR0R0Imm1);
    expect(arm_service_next.irq_serviced,
           "ARM services the deferred chained IRQ on the next instruction");

    seed_defer_gate();
    const gba::core::CoreSchedulerStepResult thumb_defer_step =
        scheduler.step_thumb(kThumbMovR2Imm3);
    expect(thumb_defer_step.cpu_step.status == ExecuteStatus::executed,
           "Thumb chained post-return defer fixture executes its instruction");
    expect(!thumb_defer_step.irq_serviced,
           "Thumb defers an armed chained post-return IRQ like ARM (S2 parity)");
    expect(cpu.current_mode() != gba::core::CpuMode::irq,
           "Thumb deferred chained IRQ does not enter IRQ mode yet");
    const gba::core::CoreSchedulerStepResult thumb_service_next =
        scheduler.step_thumb(kThumbMovR2Imm3);
    expect(thumb_service_next.irq_serviced,
           "Thumb services the deferred chained IRQ on the next instruction");
    scheduler.load_state(gba::core::CoreSchedulerState{});
  }

  // ------------------------------------------------------------------
  // S4 guards: IntrWait (SWI 4) wake phases must stay cycle-exact now that
  // the busy-spin batches device advancement into deterministic chunks.
  {
    gba::core::BiosController intr_wait_bios;
    intr_wait_bios.set_mode(gba::core::BiosExecutionMode::hle);
    gba::core::CoreScheduler intr_wait_scheduler(cpu, memory, interrupts, timers,
                                                 dma, ppu, apu, waitcnt,
                                                 intr_wait_bios);
    constexpr std::uint16_t kVblankIrqEnableDispstat = 0x0008U;
    constexpr std::uint32_t kVblankEdge =
        static_cast<std::uint32_t>(PpuTiming::kVisibleLines) * PpuTiming::kCyclesPerLine;

    auto seed_intr_wait = [&](bool preset_flag) {
      cpu.reset();
      memory.reset();
      timers.reset();
      ppu.reset();
      apu.reset();
      dma.reset();
      interrupts.reset();
      intr_wait_scheduler.reset_scheduler_cycles();
      intr_wait_scheduler.load_state(gba::core::CoreSchedulerState{});
      std::vector<std::uint8_t> intr_wait_rom(256);
      write_rom_halfword(intr_wait_rom, 0x20U, 0xDF04U);
      expect(memory.load_game_pak_rom(intr_wait_rom), "IntrWait guard ROM loads");
      expect(cpu.set_cpsr(0x0000001FU | 0x20U),
             "IntrWait guard enters Thumb system mode");
      cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase + 0x20U);
      cpu.set_register(0, 0);  // r0: do not discard old flags
      cpu.set_register(1, irq_bit(gba::core::InterruptSource::vblank));  // r1: mask
      interrupts.write_interrupt_enable(
          irq_bit(gba::core::InterruptSource::vblank));
      interrupts.write_ime(1);
      ppu.write_dispstat(kVblankIrqEnableDispstat);
      ppu.tick(kVblankEdge - 30U, interrupts);
      expect(!ppu.vblank(), "IntrWait guard starts before the VBlank edge");
      if (preset_flag) {
        interrupts.request(gba::core::InterruptSource::vblank);
      }
    };

    // Control: IF pre-set so the wait exits without spinning.
    seed_intr_wait(true);
    const gba::core::CoreSchedulerFetchStepResult intr_wait_control =
        intr_wait_scheduler.step_from_pc();
    expect(intr_wait_control.step.has_value() &&
               intr_wait_control.step->cpu_step.status == ExecuteStatus::executed,
           "pre-flagged IntrWait executes");
    // IME is enabled and IF was pre-set, so the scheduler correctly services
    // the pending IRQ right after the SWI body returns (vectors to 0x18).
    expect(intr_wait_control.step->irq_serviced,
           "pre-flagged IntrWait services its already-pending IRQ");
    const std::uint64_t intr_wait_control_cycles =
        intr_wait_scheduler.scheduler_cycles();
    const std::uint32_t intr_wait_control_frame_after = ppu.frame_cycle();

    // Timed run A: the VBlank flag fires naturally mid-wait; batched
    // advancement must land the wake exactly on the edge 30 cycles ahead.
    seed_intr_wait(false);
    const gba::core::CoreSchedulerFetchStepResult intr_wait_timed_a =
        intr_wait_scheduler.step_from_pc();
    expect(intr_wait_timed_a.step.has_value() &&
               intr_wait_timed_a.step->cpu_step.status == ExecuteStatus::executed,
           "edge-crossing IntrWait executes");
    const std::uint64_t intr_wait_timed_a_cycles =
        intr_wait_scheduler.scheduler_cycles();
    const std::uint32_t intr_wait_timed_a_frame_cycle = ppu.frame_cycle();
    expect(intr_wait_timed_a_cycles > intr_wait_control_cycles,
           "edge-crossing IntrWait spins device cycles until the VBlank edge");
    expect(interrupts.requested(gba::core::InterruptSource::vblank) && ppu.vblank(),
           "edge-crossing IntrWait wakes on the raised VBlank flag");

    // Timed run B: identical reseed must reproduce run A bit-for-bit.
    seed_intr_wait(false);
    [[maybe_unused]] const gba::core::CoreSchedulerFetchStepResult
        intr_wait_timed_b = intr_wait_scheduler.step_from_pc();
    expect(intr_wait_scheduler.scheduler_cycles() == intr_wait_timed_a_cycles &&
               ppu.frame_cycle() == intr_wait_timed_a_frame_cycle,
           "edge-crossing IntrWait is deterministic across reseeds");

    // Exactness (self-calibrating): the control run charges fetch+return
    // device cycles, ending its frame clock at edge-30+F+R; the timed run
    // ends at edge+R. That exposes F, and the total-cycle difference must
    // equal exactly the 30-F spin to the edge — any batching overshoot
    // would inflate it beyond 30-F.
    const std::uint32_t return_device_cycles =
        intr_wait_timed_a_frame_cycle - kVblankEdge;
    const std::uint32_t fetch_device_cycles =
        intr_wait_control_frame_after - (kVblankEdge - 30U) - return_device_cycles;
    expect(intr_wait_timed_a_cycles - intr_wait_control_cycles ==
               static_cast<std::uint64_t>(fetch_device_cycles <= 30U
                                              ? 30U - fetch_device_cycles
                                              : 0U),
           "edge-crossing IntrWait wake is cycle-exact under batched "
           "device advancement");

    cpu.reset();
    memory.reset();
    timers.reset();
    ppu.reset();
    apu.reset();
    dma.reset();
    interrupts.reset();
    intr_wait_scheduler.load_state(gba::core::CoreSchedulerState{});
  }

  std::cout << "core_scheduler_test: PASS\n";
  return 0;
}
