#include "gba/core/core_scheduler.hpp"

#include "gba/core/memory_bus.hpp"
#include "gba/core/ppu_timing.hpp"
#include "gba/core/state_hash.hpp"
#include "gba/core/timers.hpp"
#include "gba/core/wait_state_control.hpp"

#include <algorithm>
#include <limits>

namespace gba::core {

namespace {

constexpr std::uint32_t kGamePakSequentialBoundary = 128U * 1024U;
constexpr std::uint8_t kPrefetchBufferCapacityHalfwords = 8;

}  // namespace

CoreScheduler::CoreScheduler(Arm7tdmi& cpu, MemoryBus& memory,
                             InterruptController& interrupts, Timers& timers,
                             DmaController& dma, PpuTiming& ppu, Apu& apu)
    : cpu_(cpu),
      memory_(memory),
      interrupts_(interrupts),
      timers_(timers),
      dma_(dma),
      ppu_(ppu),
      apu_(apu),
      scheduler_cycles_(0),
      halted_(false),
      waitcnt_(nullptr),
      prefetch_buffer_halfwords_(0) {}

CoreScheduler::CoreScheduler(Arm7tdmi& cpu, MemoryBus& memory,
                             InterruptController& interrupts, Timers& timers,
                             DmaController& dma, PpuTiming& ppu, Apu& apu,
                             const WaitStateControl& waitcnt)
    : cpu_(cpu),
      memory_(memory),
      interrupts_(interrupts),
      timers_(timers),
      dma_(dma),
      ppu_(ppu),
      apu_(apu),
      scheduler_cycles_(0),
      halted_(false),
      waitcnt_(&waitcnt),
      prefetch_buffer_halfwords_(0) {}

std::uint64_t CoreScheduler::scheduler_cycles() const {
  return scheduler_cycles_;
}

void CoreScheduler::reset_scheduler_cycles() {
  scheduler_cycles_ = 0;
  halted_ = false;
  reset_fetch_timing_sequence();
}

CoreSchedulerState CoreScheduler::save_state() const {
  return {scheduler_cycles_, halted_, last_fetch_address_, last_fetch_width_bytes_,
          last_fetch_window_, prefetch_buffer_halfwords_};
}

void CoreScheduler::load_state(const CoreSchedulerState& state) {
  scheduler_cycles_ = state.scheduler_cycles;
  halted_ = state.halted;
  last_fetch_address_ = state.last_fetch_address;
  last_fetch_width_bytes_ = state.last_fetch_width_bytes;
  last_fetch_window_ = state.last_fetch_window;
  prefetch_buffer_halfwords_ = state.prefetch_buffer_halfwords;
}

std::uint64_t CoreScheduler::state_hash() const {
  StateHasher hasher;
  hasher.add_u64(scheduler_cycles_);
  hasher.add_bool(halted_);
  hasher.add_bool(last_fetch_address_.has_value());
  if (last_fetch_address_.has_value()) {
    hasher.add_u32(last_fetch_address_.value());
  }
  hasher.add_bool(last_fetch_width_bytes_.has_value());
  if (last_fetch_width_bytes_.has_value()) {
    hasher.add_u8(last_fetch_width_bytes_.value());
  }
  hasher.add_bool(last_fetch_window_.has_value());
  if (last_fetch_window_.has_value()) {
    hasher.add_u8(last_fetch_window_.value());
  }
  hasher.add_u8(prefetch_buffer_halfwords_);
  return hasher.value();
}

bool CoreScheduler::halted() const {
  return halted_;
}

void CoreScheduler::halt_until_interrupt() {
  halted_ = true;
}

bool CoreScheduler::wake_from_halt_if_irq_pending() {
  if (!halted_ || !interrupts_.irq_line()) {
    return false;
  }
  halted_ = false;
  return true;
}

CoreDeviceTickResult CoreScheduler::advance_devices(std::uint32_t cycles) {
  if (cycles == 0) {
    return {0, std::nullopt};
  }

  const std::uint64_t timer0_overflows_before = timers_.overflow_count(0);
  const std::uint64_t timer1_overflows_before = timers_.overflow_count(1);
  timers_.tick(cycles, interrupts_);
  ppu_.tick(cycles, interrupts_);
  std::uint32_t apu_timer_events = 0;
  DirectSoundTimerResult last_direct_sound{};
  const auto consume_direct_sound_timer = [&](std::uint8_t timer_index,
                                              std::uint64_t overflow_delta) {
    for (std::uint64_t event = 0; event < overflow_delta; ++event) {
      last_direct_sound = apu_.timer_overflow(timer_index);
      if (apu_timer_events != std::numeric_limits<std::uint32_t>::max()) {
        ++apu_timer_events;
      }
    }
  };
  consume_direct_sound_timer(
      0, timers_.overflow_count(0) - timer0_overflows_before);
  consume_direct_sound_timer(
      1, timers_.overflow_count(1) - timer1_overflows_before);
  std::optional<ApuFrameStep> apu_frame_step = apu_.tick(cycles);
  scheduler_cycles_ += cycles;
  return {cycles, apu_frame_step, apu_timer_events, last_direct_sound};
}

DmaRunResult CoreScheduler::run_immediate_dma() {
  return dma_.run_immediate(memory_, interrupts_);
}

bool CoreScheduler::service_pending_irq() {
  return interrupts_.service_pending_irq(cpu_);
}

CoreSchedulerStepResult CoreScheduler::step_arm(std::uint32_t instruction) {
  const ArmStepResult cpu_step = waitcnt_ == nullptr ? cpu_.step_arm(instruction, memory_)
                                                     : cpu_.step_arm(instruction, memory_,
                                                                     *waitcnt_);
  CoreDeviceTickResult devices{0, std::nullopt};
  DmaRunResult dma_result{0, 0, false};
  bool irq_serviced = false;

  if (cpu_step.status == ExecuteStatus::executed && cpu_step.elapsed_cycles > 0) {
    devices = advance_devices(cpu_step.elapsed_cycles);
    dma_result = run_immediate_dma();
    irq_serviced = service_pending_irq();
  }

  return {cpu_step, devices, dma_result, irq_serviced, scheduler_cycles_};
}

CoreSchedulerStepResult CoreScheduler::step_thumb(std::uint16_t instruction) {
  const ArmStepResult cpu_step = cpu_.step_thumb(instruction, memory_);
  CoreDeviceTickResult devices{0, std::nullopt};
  DmaRunResult dma_result{0, 0, false};
  bool irq_serviced = false;

  if (cpu_step.status == ExecuteStatus::executed && cpu_step.elapsed_cycles > 0) {
    devices = advance_devices(cpu_step.elapsed_cycles);
    dma_result = run_immediate_dma();
    irq_serviced = service_pending_irq();
  }

  return {cpu_step, devices, dma_result, irq_serviced, scheduler_cycles_};
}

std::uint32_t CoreScheduler::apply_fetch_timing(std::uint32_t fetch_address,
                                                std::uint8_t width_bytes,
                                                bool& timing_applied,
                                                bool& sequential,
                                                bool& prefetch_enabled,
                                                bool& prefetch_hit,
                                                bool& boundary_forced_nonsequential) {
  timing_applied = false;
  sequential = false;
  prefetch_enabled = false;
  prefetch_hit = false;
  boundary_forced_nonsequential = false;

  if (waitcnt_ == nullptr) {
    reset_fetch_timing_sequence();
    return 0;
  }

  const AddressInfo address = MemoryBus::describe(fetch_address);
  const CartridgeAddressInfo cartridge = MemoryBus::describe_cartridge(fetch_address);
  if (address.region != Region::game_pak_rom) {
    reset_fetch_timing_sequence();
    return 0;
  }

  const std::uint8_t window = static_cast<std::uint8_t>(cartridge.window);
  if (last_fetch_address_.has_value() && last_fetch_width_bytes_.has_value() &&
      last_fetch_window_.has_value() &&
      fetch_address == last_fetch_address_.value() + last_fetch_width_bytes_.value() &&
      window == last_fetch_window_.value()) {
    sequential = true;
  }
  if (sequential && (fetch_address % kGamePakSequentialBoundary) == 0) {
    sequential = false;
    boundary_forced_nonsequential = true;
  }

  const MemoryAccessTiming timing =
      MemoryBus::timing(fetch_address,
                        width_bytes == 4 ? AccessWidth::word : AccessWidth::halfword,
                        *waitcnt_);
  prefetch_enabled = waitcnt_->prefetch_enabled();
  const std::uint8_t required_halfwords = static_cast<std::uint8_t>(width_bytes / 2U);
  std::uint32_t cycles = sequential ? timing.sequential : timing.nonsequential;
  if (prefetch_enabled && sequential &&
      prefetch_buffer_halfwords_ >= required_halfwords) {
    cycles = 0;
    prefetch_hit = true;
    prefetch_buffer_halfwords_ =
        static_cast<std::uint8_t>(prefetch_buffer_halfwords_ - required_halfwords);
  }
  timing_applied = cycles != 0;
  if (timing_applied) {
    [[maybe_unused]] const CoreDeviceTickResult devices = advance_devices(cycles);
  }

  last_fetch_address_ = fetch_address;
  last_fetch_width_bytes_ = width_bytes;
  last_fetch_window_ = window;
  return cycles;
}

void CoreScheduler::refill_prefetch_after_step(std::uint32_t fetch_address,
                                               std::uint8_t width_bytes,
                                               const CoreSchedulerStepResult& step) {
  if (waitcnt_ == nullptr || !waitcnt_->prefetch_enabled() ||
      step.cpu_step.status != ExecuteStatus::executed ||
      step.cpu_step.elapsed_cycles <= 1) {
    return;
  }

  const AddressInfo address = MemoryBus::describe(fetch_address);
  if (address.region != Region::game_pak_rom) {
    prefetch_buffer_halfwords_ = 0;
    return;
  }

  const std::uint32_t next_fetch_address = fetch_address + width_bytes;
  if ((next_fetch_address % kGamePakSequentialBoundary) == 0) {
    prefetch_buffer_halfwords_ = 0;
    return;
  }

  const MemoryAccessTiming timing =
      MemoryBus::timing(next_fetch_address, AccessWidth::halfword, *waitcnt_);
  if (timing.sequential == 0) {
    return;
  }

  const std::uint32_t spare_cycles = step.cpu_step.elapsed_cycles - 1U;
  const std::uint32_t refill_halfwords = spare_cycles / timing.sequential;
  const std::uint32_t capped =
      std::min<std::uint32_t>(kPrefetchBufferCapacityHalfwords,
                              prefetch_buffer_halfwords_ + refill_halfwords);
  prefetch_buffer_halfwords_ = static_cast<std::uint8_t>(capped);
}

void CoreScheduler::reset_fetch_timing_sequence() {
  last_fetch_address_.reset();
  last_fetch_width_bytes_.reset();
  last_fetch_window_.reset();
  prefetch_buffer_halfwords_ = 0;
}

CoreSchedulerFetchStepResult CoreScheduler::step_arm_from_pc() {
  const std::uint32_t fetch_address = cpu_.register_value(Arm7tdmi::kPc);
  const std::optional<std::uint32_t> instruction = memory_.read32(fetch_address);
  if (!instruction.has_value()) {
    reset_fetch_timing_sequence();
    return {CoreInstructionSet::arm, 4, fetch_address, std::nullopt, std::nullopt, 0,
            true, false, false, false, false, false, 0, false};
  }

  bool fetch_timing_applied = false;
  bool fetch_sequential = false;
  bool prefetch_enabled = false;
  bool prefetch_hit = false;
  bool boundary_forced_nonsequential = false;
  const std::uint32_t fetch_cycles = apply_fetch_timing(
      fetch_address, 4, fetch_timing_applied, fetch_sequential, prefetch_enabled,
      prefetch_hit, boundary_forced_nonsequential);
  const CoreSchedulerStepResult step = step_arm(instruction.value());
  const bool should_advance_pc =
      step.cpu_step.status != ExecuteStatus::unsupported &&
      cpu_.register_value(Arm7tdmi::kPc) == fetch_address;
  if (should_advance_pc) {
    cpu_.set_register(Arm7tdmi::kPc, fetch_address + 4U);
    refill_prefetch_after_step(fetch_address, 4, step);
  } else {
    reset_fetch_timing_sequence();
  }

  return {CoreInstructionSet::arm, 4, fetch_address, instruction, step, fetch_cycles,
          false, should_advance_pc, fetch_timing_applied, fetch_sequential,
          prefetch_enabled, prefetch_hit, prefetch_buffer_halfwords_,
          boundary_forced_nonsequential};
}

CoreSchedulerFetchStepResult CoreScheduler::step_from_pc() {
  if (!cpu_.thumb_state()) {
    return step_arm_from_pc();
  }

  const std::uint32_t fetch_address = cpu_.register_value(Arm7tdmi::kPc);
  const std::optional<std::uint16_t> instruction = memory_.read16(fetch_address);
  if (!instruction.has_value()) {
    reset_fetch_timing_sequence();
    return {CoreInstructionSet::thumb, 2, fetch_address, std::nullopt, std::nullopt,
            0, true, false, false, false, false, false, 0, false};
  }

  bool fetch_timing_applied = false;
  bool fetch_sequential = false;
  bool prefetch_enabled = false;
  bool prefetch_hit = false;
  bool boundary_forced_nonsequential = false;
  const std::uint32_t fetch_cycles = apply_fetch_timing(
      fetch_address, 2, fetch_timing_applied, fetch_sequential, prefetch_enabled,
      prefetch_hit, boundary_forced_nonsequential);
  const CoreSchedulerStepResult step = step_thumb(instruction.value());
  const bool should_advance_pc =
      step.cpu_step.status != ExecuteStatus::unsupported &&
      cpu_.register_value(Arm7tdmi::kPc) == fetch_address;
  if (should_advance_pc) {
    cpu_.set_register(Arm7tdmi::kPc, fetch_address + 2U);
    refill_prefetch_after_step(fetch_address, 2, step);
  } else {
    reset_fetch_timing_sequence();
  }

  return {CoreInstructionSet::thumb, 2, fetch_address, instruction.value(), step,
          fetch_cycles, false, should_advance_pc, fetch_timing_applied,
          fetch_sequential, prefetch_enabled, prefetch_hit,
          prefetch_buffer_halfwords_, boundary_forced_nonsequential};
}

CoreSchedulerRunResult CoreScheduler::run_arm_from_pc(std::uint32_t max_steps) {
  CoreSchedulerRunResult result{
      max_steps,
      0,
      0,
      0,
      0,
      0,
      CoreRunStopReason::max_steps,
      cpu_.register_value(Arm7tdmi::kPc),
      scheduler_cycles_,
  };

  for (std::uint32_t step_index = 0; step_index < max_steps; ++step_index) {
    const CoreSchedulerFetchStepResult fetched = step_arm_from_pc();
    if (fetched.fetch_failed) {
      ++result.fetch_failures;
      result.stop_reason = CoreRunStopReason::fetch_failed;
      break;
    }

    ++result.attempted_steps;
    const ExecuteStatus status = fetched.step->cpu_step.status;
    if (status == ExecuteStatus::executed) {
      ++result.executed_steps;
      continue;
    }
    if (status == ExecuteStatus::skipped_condition) {
      ++result.skipped_steps;
      continue;
    }

    ++result.unsupported_steps;
    result.stop_reason = CoreRunStopReason::unsupported_instruction;
    break;
  }

  result.final_pc = cpu_.register_value(Arm7tdmi::kPc);
  result.scheduler_cycles = scheduler_cycles_;
  return result;
}

CoreSchedulerRunResult CoreScheduler::run_from_pc(std::uint32_t max_steps) {
  CoreSchedulerRunResult result{
      max_steps,
      0,
      0,
      0,
      0,
      0,
      CoreRunStopReason::max_steps,
      cpu_.register_value(Arm7tdmi::kPc),
      scheduler_cycles_,
  };

  for (std::uint32_t step_index = 0; step_index < max_steps; ++step_index) {
    const CoreSchedulerFetchStepResult fetched = step_from_pc();
    if (fetched.fetch_failed) {
      ++result.fetch_failures;
      result.stop_reason = CoreRunStopReason::fetch_failed;
      break;
    }

    ++result.attempted_steps;
    const ExecuteStatus status = fetched.step->cpu_step.status;
    if (status == ExecuteStatus::executed) {
      ++result.executed_steps;
      continue;
    }
    if (status == ExecuteStatus::skipped_condition) {
      ++result.skipped_steps;
      continue;
    }

    ++result.unsupported_steps;
    result.stop_reason = CoreRunStopReason::unsupported_instruction;
    break;
  }

  result.final_pc = cpu_.register_value(Arm7tdmi::kPc);
  result.scheduler_cycles = scheduler_cycles_;
  return result;
}

}  // namespace gba::core
