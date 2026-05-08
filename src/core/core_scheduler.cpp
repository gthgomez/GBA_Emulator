#include "gba/core/core_scheduler.hpp"

#include "gba/core/memory_bus.hpp"
#include "gba/core/ppu_timing.hpp"
#include "gba/core/state_hash.hpp"
#include "gba/core/timers.hpp"
#include "gba/core/wait_state_control.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace gba::core {

namespace {

constexpr std::uint32_t kGamePakSequentialBoundary = 128U * 1024U;
constexpr std::uint8_t kPrefetchBufferCapacityHalfwords = 8;
constexpr std::uint32_t kBiosIrqVector = 0x00000018U;
constexpr std::uint32_t kUserIrqHandlerPointer = 0x03007FFCU;
constexpr std::uint32_t kBiosHleIrqReturnSentinel = 0x0FFFFF00U;
constexpr std::uint32_t kMaxBiosWaitCycles = 280896U * 2U;
constexpr std::uint32_t kBiosWaitBatchCycles = 1024U;
constexpr std::uint8_t kAutoIrqLatencyCycles = 6;
constexpr std::uint32_t kBiosHleIrqDispatchCycles = 3;

[[nodiscard]] std::int32_t as_i32(std::uint32_t value) {
  return static_cast<std::int32_t>(value);
}

[[nodiscard]] std::uint32_t as_u32(std::int64_t value) {
  return static_cast<std::uint32_t>(static_cast<std::int32_t>(value));
}

[[nodiscard]] std::int32_t wrap_i32(std::uint32_t value) {
  return static_cast<std::int32_t>(value);
}

[[nodiscard]] std::uint32_t wrap_u32(std::int32_t value) {
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::int32_t wrap_mul_i32(std::int32_t left, std::int32_t right) {
  return wrap_i32(wrap_u32(left) * wrap_u32(right));
}

[[nodiscard]] std::int32_t wrap_shl_i32(std::int32_t value, std::uint8_t shift) {
  return wrap_i32(wrap_u32(value) << shift);
}

[[nodiscard]] std::int32_t hle_arc_tan(std::int32_t value,
                                       std::int32_t* scratch_r1,
                                       std::int32_t* scratch_r3) {
  const std::int32_t square = wrap_mul_i32(value, value);
  std::int32_t polynomial = -(square >> 14);
  std::int32_t factor = ((wrap_mul_i32(0xA9, polynomial)) >> 14) + 0x390;
  factor = ((wrap_mul_i32(factor, polynomial)) >> 14) + 0x91C;
  factor = ((wrap_mul_i32(factor, polynomial)) >> 14) + 0xFB6;
  factor = ((wrap_mul_i32(factor, polynomial)) >> 14) + 0x16AA;
  factor = ((wrap_mul_i32(factor, polynomial)) >> 14) + 0x2081;
  factor = ((wrap_mul_i32(factor, polynomial)) >> 14) + 0x3651;
  factor = ((wrap_mul_i32(factor, polynomial)) >> 14) + 0xA2F9;
  if (scratch_r1 != nullptr) {
    *scratch_r1 = polynomial;
  }
  if (scratch_r3 != nullptr) {
    *scratch_r3 = factor;
  }
  return static_cast<std::int16_t>(wrap_mul_i32(value, factor) >> 16);
}

[[nodiscard]] std::int32_t hle_arc_tan2(std::int32_t x, std::int32_t y,
                                        std::int32_t* scratch_r1) {
  const std::int64_t wide_x = x;
  const std::int64_t wide_y = y;
  if (y == 0) {
    return x >= 0 ? 0 : 0x8000;
  }
  if (x == 0) {
    return y >= 0 ? 0x4000 : 0xC000;
  }
  if (y >= 0) {
    if (x >= 0) {
      if (x >= y) {
        return hle_arc_tan(wrap_shl_i32(y, 14) / x, scratch_r1, nullptr);
      }
    } else if (-wide_x >= wide_y) {
      return hle_arc_tan(wrap_shl_i32(y, 14) / x, scratch_r1, nullptr) + 0x8000;
    }
    return 0x4000 - hle_arc_tan(wrap_shl_i32(x, 14) / y, scratch_r1, nullptr);
  }

  if (x <= 0) {
    if (-wide_x > -wide_y) {
      return hle_arc_tan(wrap_shl_i32(y, 14) / x, scratch_r1, nullptr) + 0x8000;
    }
  } else if (wide_x >= -wide_y) {
    return hle_arc_tan(wrap_shl_i32(y, 14) / x, scratch_r1, nullptr) + 0x10000;
  }
  return 0xC000 - hle_arc_tan(wrap_shl_i32(x, 14) / y, scratch_r1, nullptr);
}

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
      bios_(nullptr),
      scheduler_cycles_(0),
      halted_(false),
      waitcnt_(nullptr),
      prefetch_buffer_halfwords_(0),
      hle_irq_return_lr_(std::nullopt),
      hle_irq_saved_registers_(std::nullopt),
      auto_irq_line_high_(false),
      auto_irq_latency_cycles_(0) {}

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
      bios_(nullptr),
      scheduler_cycles_(0),
      halted_(false),
      waitcnt_(&waitcnt),
      prefetch_buffer_halfwords_(0),
      hle_irq_return_lr_(std::nullopt),
      hle_irq_saved_registers_(std::nullopt),
      auto_irq_line_high_(false),
      auto_irq_latency_cycles_(0) {}

CoreScheduler::CoreScheduler(Arm7tdmi& cpu, MemoryBus& memory,
                             InterruptController& interrupts, Timers& timers,
                             DmaController& dma, PpuTiming& ppu, Apu& apu,
                             const WaitStateControl& waitcnt, BiosController& bios)
    : cpu_(cpu),
      memory_(memory),
      interrupts_(interrupts),
      timers_(timers),
      dma_(dma),
      ppu_(ppu),
      apu_(apu),
      bios_(&bios),
      scheduler_cycles_(0),
      halted_(false),
      waitcnt_(&waitcnt),
      prefetch_buffer_halfwords_(0),
      hle_irq_return_lr_(std::nullopt),
      hle_irq_saved_registers_(std::nullopt),
      auto_irq_line_high_(false),
      auto_irq_latency_cycles_(0) {}

std::uint64_t CoreScheduler::scheduler_cycles() const {
  return scheduler_cycles_;
}

void CoreScheduler::reset_scheduler_cycles() {
  scheduler_cycles_ = 0;
  halted_ = false;
  hle_irq_return_lr_.reset();
  hle_irq_saved_registers_.reset();
  reset_auto_irq_latency();
  reset_fetch_timing_sequence();
}

CoreSchedulerState CoreScheduler::save_state() const {
  return {scheduler_cycles_, halted_, last_fetch_address_, last_fetch_width_bytes_,
          last_fetch_window_, prefetch_buffer_halfwords_, hle_irq_return_lr_,
          hle_irq_saved_registers_, auto_irq_line_high_, auto_irq_latency_cycles_};
}

void CoreScheduler::load_state(const CoreSchedulerState& state) {
  scheduler_cycles_ = state.scheduler_cycles;
  halted_ = state.halted;
  last_fetch_address_ = state.last_fetch_address;
  last_fetch_width_bytes_ = state.last_fetch_width_bytes;
  last_fetch_window_ = state.last_fetch_window;
  prefetch_buffer_halfwords_ = state.prefetch_buffer_halfwords;
  hle_irq_return_lr_ = state.hle_irq_return_lr;
  hle_irq_saved_registers_ = state.hle_irq_saved_registers;
  auto_irq_line_high_ = state.auto_irq_line_high;
  auto_irq_latency_cycles_ = state.auto_irq_latency_cycles;
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
  hasher.add_bool(hle_irq_return_lr_.has_value());
  if (hle_irq_return_lr_.has_value()) {
    hasher.add_u32(hle_irq_return_lr_.value());
  }
  hasher.add_bool(hle_irq_saved_registers_.has_value());
  if (hle_irq_saved_registers_.has_value()) {
    hasher.add_bytes(hle_irq_saved_registers_.value());
  }
  hasher.add_bool(auto_irq_line_high_);
  hasher.add_u8(auto_irq_latency_cycles_);
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
    return {0, std::nullopt, 0, {}, {}};
  }

  const std::uint64_t timer0_overflows_before = timers_.overflow_count(0);
  const std::uint64_t timer1_overflows_before = timers_.overflow_count(1);
  timers_.tick(cycles, interrupts_);
  const PpuTickEvents ppu_events = ppu_.tick(cycles, interrupts_);
  std::uint32_t apu_timer_events = 0;
  DirectSoundTimerResult last_direct_sound{};
  DmaRunResult triggered_dma{0, 0, false, 0};
  const auto accumulate_dma = [&](const DmaRunResult& event_result) {
    triggered_dma.channels_executed = static_cast<std::uint8_t>(
        std::min<std::uint32_t>(255U, triggered_dma.channels_executed +
                                          event_result.channels_executed));
    triggered_dma.units_transferred += event_result.units_transferred;
    triggered_dma.bus_cycles += event_result.bus_cycles;
    triggered_dma.unsupported_request =
        triggered_dma.unsupported_request || event_result.unsupported_request;
  };
  for (std::uint16_t event = 0; event < ppu_events.hblank_entries; ++event) {
    accumulate_dma(dma_.run_trigger(DmaTrigger::hblank, memory_, interrupts_));
  }
  for (std::uint16_t event = 0; event < ppu_events.vblank_entries; ++event) {
    accumulate_dma(dma_.run_trigger(DmaTrigger::vblank, memory_, interrupts_));
  }
  const auto consume_direct_sound_timer = [&](std::uint8_t timer_index,
                                              std::uint64_t overflow_delta) {
    for (std::uint64_t event = 0; event < overflow_delta; ++event) {
      last_direct_sound = apu_.timer_overflow(timer_index);
      if (last_direct_sound.fifo_a.refill_request) {
        accumulate_dma(dma_.run_sound_fifo(DmaTrigger::fifo_a, memory_, apu_,
                                           interrupts_));
      }
      if (last_direct_sound.fifo_b.refill_request) {
        accumulate_dma(dma_.run_sound_fifo(DmaTrigger::fifo_b, memory_, apu_,
                                           interrupts_));
      }
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
  scheduler_cycles_ += cycles + triggered_dma.bus_cycles;
  update_auto_irq_latency(cycles + triggered_dma.bus_cycles);
  return {cycles, apu_frame_step, apu_timer_events, last_direct_sound, triggered_dma};
}

DmaRunResult CoreScheduler::run_immediate_dma() {
  return dma_.run_immediate(memory_, interrupts_);
}

bool CoreScheduler::service_pending_irq() {
  return interrupts_.service_pending_irq(cpu_);
}

CoreSchedulerStepResult CoreScheduler::step_arm(std::uint32_t instruction) {
  const std::uint32_t pre_step_pc = cpu_.register_value(Arm7tdmi::kPc);
  const std::optional<ArmStepResult> hle_step = execute_hle_arm_swi(instruction);
  const ArmStepResult cpu_step =
      hle_step.has_value()
          ? hle_step.value()
          : (waitcnt_ != nullptr ? cpu_.step_arm(instruction, memory_, *waitcnt_)
                                 : cpu_.step_arm(instruction, memory_));
  CoreDeviceTickResult devices{0, std::nullopt};
  DmaRunResult dma_result{0, 0, false, 0};
  bool irq_serviced = false;

  if (cpu_step.status == ExecuteStatus::executed && cpu_step.elapsed_cycles > 0) {
    devices = advance_devices(cpu_step.elapsed_cycles);
    dma_result = run_immediate_dma();
    const bool sequential_pc = cpu_.register_value(Arm7tdmi::kPc) == pre_step_pc;
    if (sequential_pc && auto_irq_ready()) {
      cpu_.set_register(Arm7tdmi::kPc, pre_step_pc + 4U);
    }
    irq_serviced = sequential_pc && auto_irq_ready() && service_pending_irq();
    if (irq_serviced) {
      reset_auto_irq_latency();
    }
    if (!irq_serviced && sequential_pc &&
        cpu_.register_value(Arm7tdmi::kPc) == pre_step_pc + 4U) {
      cpu_.set_register(Arm7tdmi::kPc, pre_step_pc);
    }
  }

  return {cpu_step, devices, dma_result, irq_serviced, scheduler_cycles_};
}

CoreSchedulerStepResult CoreScheduler::step_thumb(std::uint16_t instruction) {
  const std::uint32_t pre_step_pc = cpu_.register_value(Arm7tdmi::kPc);
  const std::optional<ArmStepResult> hle_step = execute_hle_thumb_swi(instruction);
  const ArmStepResult cpu_step =
      hle_step.has_value() ? hle_step.value() : cpu_.step_thumb(instruction, memory_);
  CoreDeviceTickResult devices{0, std::nullopt};
  DmaRunResult dma_result{0, 0, false, 0};
  bool irq_serviced = false;

  if (cpu_step.status == ExecuteStatus::executed && cpu_step.elapsed_cycles > 0) {
    devices = advance_devices(cpu_step.elapsed_cycles);
    dma_result = run_immediate_dma();
    const bool sequential_pc = cpu_.register_value(Arm7tdmi::kPc) == pre_step_pc;
    if (sequential_pc && auto_irq_ready()) {
      cpu_.set_register(Arm7tdmi::kPc, pre_step_pc + 2U);
    }
    irq_serviced = sequential_pc && auto_irq_ready() && service_pending_irq();
    if (irq_serviced) {
      reset_auto_irq_latency();
    }
    if (!irq_serviced && sequential_pc &&
        cpu_.register_value(Arm7tdmi::kPc) == pre_step_pc + 2U) {
      cpu_.set_register(Arm7tdmi::kPc, pre_step_pc);
    }
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

void CoreScheduler::update_auto_irq_latency(std::uint32_t elapsed_cycles) {
  const bool line_active = interrupts_.irq_line() && !cpu_.irq_disabled();
  if (!line_active) {
    reset_auto_irq_latency();
    return;
  }

  if (!auto_irq_line_high_) {
    auto_irq_line_high_ = true;
    auto_irq_latency_cycles_ = kAutoIrqLatencyCycles;
    return;
  }

  if (auto_irq_latency_cycles_ > elapsed_cycles) {
    auto_irq_latency_cycles_ =
        static_cast<std::uint8_t>(auto_irq_latency_cycles_ - elapsed_cycles);
  } else {
    auto_irq_latency_cycles_ = 0;
  }
}

bool CoreScheduler::auto_irq_ready() const {
  return interrupts_.irq_line() && !cpu_.irq_disabled() &&
         auto_irq_line_high_ && auto_irq_latency_cycles_ == 0;
}

void CoreScheduler::reset_auto_irq_latency() {
  auto_irq_line_high_ = false;
  auto_irq_latency_cycles_ = 0;
}

bool CoreScheduler::bios_hle_enabled() const {
  return bios_ != nullptr && bios_->mode() == BiosExecutionMode::hle;
}

std::optional<CoreSchedulerStepResult> CoreScheduler::dispatch_hle_irq_vector(
    std::uint32_t fetch_address) {
  if (!bios_hle_enabled() || fetch_address != kBiosIrqVector ||
      cpu_.current_mode() != CpuMode::irq) {
    return std::nullopt;
  }

  const std::optional<std::uint32_t> handler = memory_.read32(kUserIrqHandlerPointer);
  if (!handler.has_value() || handler.value() == 0) {
    return std::nullopt;
  }

  const bool thumb_handler = (handler.value() & 0x1U) != 0;
  const std::uint32_t cpsr = cpu_.cpsr();
  if (!cpu_.set_cpsr(thumb_handler ? (cpsr | 0x20U) : (cpsr & ~0x20U))) {
    return std::nullopt;
  }
  hle_irq_return_lr_ = cpu_.register_value(Arm7tdmi::kLinkRegister);
  std::array<std::uint32_t, 13> saved_registers{};
  for (std::uint8_t reg = 0; reg < saved_registers.size(); ++reg) {
    saved_registers.at(reg) = cpu_.register_value(reg);
  }
  hle_irq_saved_registers_ = saved_registers;
  cpu_.set_register(Arm7tdmi::kLinkRegister, kBiosHleIrqReturnSentinel);
  cpu_.set_register(Arm7tdmi::kPc, handler.value() & ~1U);
  const ArmStepResult cpu_step{ExecuteStatus::executed, kBiosHleIrqDispatchCycles,
                               cpu_.elapsed_cycles(), false, false};
  const CoreDeviceTickResult devices = advance_devices(cpu_step.elapsed_cycles);
  return CoreSchedulerStepResult{cpu_step, devices, {}, false, scheduler_cycles_};
}

std::optional<CoreSchedulerStepResult> CoreScheduler::dispatch_hle_irq_return(
    std::uint32_t fetch_address) {
  if (!bios_hle_enabled() || !hle_irq_return_lr_.has_value() ||
      fetch_address != kBiosHleIrqReturnSentinel || cpu_.current_mode() != CpuMode::irq) {
    return std::nullopt;
  }

  cpu_.set_register(Arm7tdmi::kLinkRegister, hle_irq_return_lr_.value());
  hle_irq_return_lr_.reset();
  if (hle_irq_saved_registers_.has_value()) {
    for (std::uint8_t reg = 0; reg < hle_irq_saved_registers_->size(); ++reg) {
      cpu_.set_register(reg, hle_irq_saved_registers_->at(reg));
    }
    hle_irq_saved_registers_.reset();
  }
  const ExecuteStatus status = cpu_.return_from_exception(4);
  const ArmStepResult cpu_step{status, 3, cpu_.elapsed_cycles(), false, false};
  const CoreDeviceTickResult devices =
      status == ExecuteStatus::executed ? advance_devices(cpu_step.elapsed_cycles)
                                        : CoreDeviceTickResult{};
  return CoreSchedulerStepResult{cpu_step, devices, {}, false, scheduler_cycles_};
}

std::optional<ArmStepResult> CoreScheduler::execute_hle_arm_swi(std::uint32_t instruction) {
  if (!bios_hle_enabled() || !Arm7tdmi::can_decode_software_interrupt(instruction)) {
    return std::nullopt;
  }
  const ArmCondition condition =
      static_cast<ArmCondition>((instruction >> 28U) & 0xFU);
  if (condition != ArmCondition::al) {
    return std::nullopt;
  }
  return execute_hle_swi(BiosController::decode_arm_swi(instruction));
}

std::optional<ArmStepResult> CoreScheduler::execute_hle_thumb_swi(std::uint16_t instruction) {
  if (!bios_hle_enabled() || !Arm7tdmi::can_decode_thumb_software_interrupt(instruction)) {
    return std::nullopt;
  }
  return execute_hle_swi(BiosController::decode_thumb_swi(instruction));
}

ArmStepResult CoreScheduler::execute_hle_swi(BiosSwiCall call) {
  const auto unsupported = [&]() {
    return ArmStepResult{ExecuteStatus::unsupported, 1, cpu_.elapsed_cycles(), false, false};
  };
  const auto executed = [&](std::uint32_t cycles = 3) {
    return ArmStepResult{ExecuteStatus::executed, cycles, cpu_.elapsed_cycles(), false, false};
  };

  const BiosSwiResult policy = bios_->handle_swi(call);
  if (policy.status != BiosSwiStatus::handled) {
    return unsupported();
  }

  switch (call.service) {
    case 0x02:
      halt_until_interrupt();
      return executed(1);
    case 0x04: {
      const bool discard = cpu_.register_value(0) != 0;
      const std::uint16_t mask = static_cast<std::uint16_t>(cpu_.register_value(1));
      return wait_for_interrupt_mask(mask, discard) ? executed(1) : unsupported();
    }
    case 0x05:
      return wait_for_interrupt_mask(0x0001U, true) ? executed(1) : unsupported();
    case 0x06:
    case 0x07: {
      const std::int32_t numerator =
          call.service == 0x06 ? as_i32(cpu_.register_value(0)) : as_i32(cpu_.register_value(1));
      const std::int32_t denominator =
          call.service == 0x06 ? as_i32(cpu_.register_value(1)) : as_i32(cpu_.register_value(0));
      if (denominator == 0) {
        cpu_.set_register(0, numerator < 0 ? 0xFFFFFFFFU : 1U);
        cpu_.set_register(1, wrap_u32(numerator));
        cpu_.set_register(3, 1U);
        return executed();
      }
      if (denominator == -1 && numerator == std::numeric_limits<std::int32_t>::min()) {
        cpu_.set_register(0, wrap_u32(std::numeric_limits<std::int32_t>::min()));
        cpu_.set_register(1, 0);
        cpu_.set_register(3, wrap_u32(std::numeric_limits<std::int32_t>::min()));
        return executed();
      }
      const std::int64_t quotient =
          static_cast<std::int64_t>(numerator) / static_cast<std::int64_t>(denominator);
      const std::int64_t remainder =
          static_cast<std::int64_t>(numerator) % static_cast<std::int64_t>(denominator);
      const std::int64_t absolute = quotient < 0 ? -quotient : quotient;
      cpu_.set_register(0, as_u32(quotient));
      cpu_.set_register(1, as_u32(remainder));
      cpu_.set_register(3, static_cast<std::uint32_t>(absolute));
      return executed();
    }
    case 0x08: {
      const double value = static_cast<double>(cpu_.register_value(0));
      cpu_.set_register(0, static_cast<std::uint32_t>(std::sqrt(value)));
      return executed();
    }
    case 0x09: {
      std::int32_t scratch_r1 = 0;
      std::int32_t scratch_r3 = 0;
      const std::int32_t result =
          hle_arc_tan(as_i32(cpu_.register_value(0)), &scratch_r1, &scratch_r3);
      cpu_.set_register(0, wrap_u32(result));
      cpu_.set_register(1, wrap_u32(scratch_r1));
      cpu_.set_register(3, wrap_u32(scratch_r3));
      return executed();
    }
    case 0x0A: {
      std::int32_t scratch_r1 = as_i32(cpu_.register_value(1));
      const std::int32_t result = hle_arc_tan2(as_i32(cpu_.register_value(0)),
                                              as_i32(cpu_.register_value(1)),
                                              &scratch_r1);
      cpu_.set_register(0, static_cast<std::uint16_t>(result));
      cpu_.set_register(1, wrap_u32(scratch_r1));
      cpu_.set_register(3, 0x170U);
      return executed();
    }
    case 0x0B:
      return hle_cpu_set(false) ? executed() : unsupported();
    case 0x0C:
      return hle_cpu_set(true) ? executed() : unsupported();
    default:
      return unsupported();
  }
}

bool CoreScheduler::wait_for_interrupt_mask(std::uint16_t mask, bool discard_old_flags) {
  if (mask == 0) {
    return false;
  }
  if (discard_old_flags) {
    interrupts_.write_interrupt_flags(mask);
  }
  for (std::uint32_t cycles = 0; cycles < kMaxBiosWaitCycles;) {
    if ((interrupts_.interrupt_flags() & mask) != 0) {
      if (interrupts_.irq_line() && !cpu_.irq_disabled()) {
        auto_irq_line_high_ = true;
        auto_irq_latency_cycles_ = 0;
      }
      return true;
    }
    const std::uint32_t batch =
        std::min<std::uint32_t>(kBiosWaitBatchCycles, kMaxBiosWaitCycles - cycles);
    [[maybe_unused]] const CoreDeviceTickResult devices = advance_devices(batch);
    cycles += batch;
  }
  const bool matched = (interrupts_.interrupt_flags() & mask) != 0;
  if (matched && interrupts_.irq_line() && !cpu_.irq_disabled()) {
    auto_irq_line_high_ = true;
    auto_irq_latency_cycles_ = 0;
  }
  return matched;
}

bool CoreScheduler::hle_cpu_set(bool fast) {
  const std::uint32_t source = cpu_.register_value(0);
  const std::uint32_t dest = cpu_.register_value(1);
  const std::uint32_t control = cpu_.register_value(2);
  const bool fill = (control & (1U << 24U)) != 0;
  const bool word = fast || (control & (1U << 26U)) != 0;
  std::uint32_t count = control & 0x001FFFFFU;
  if (fast) {
    count = (count + 7U) & ~7U;
  }

  if (word) {
    std::optional<std::uint32_t> fill_value = std::nullopt;
    const auto read_word = [&](std::uint32_t address) {
      if (MemoryBus::describe(address).region == Region::bios) {
        return std::optional<std::uint32_t>{0};
      }
      return memory_.read32(MemoryBus::describe(address).region == Region::game_pak_save
                                ? address
                                : (address & ~0x3U));
    };
    for (std::uint32_t index = 0; index < count; ++index) {
      const std::optional<std::uint32_t> value =
          fill ? (fill_value.has_value() ? fill_value : (fill_value = read_word(source)))
               : read_word(source + index * 4U);
      const std::uint32_t write_address = dest + index * 4U;
      const std::uint32_t effective_write_address =
          MemoryBus::describe(write_address).region == Region::game_pak_save
              ? write_address
              : (write_address & ~0x3U);
      if (!value.has_value() || !memory_.write32(effective_write_address, value.value())) {
        return false;
      }
    }
    return true;
  }

  std::optional<std::uint16_t> fill_value = std::nullopt;
  const auto read_halfword = [&](std::uint32_t address) -> std::optional<std::uint16_t> {
    if (MemoryBus::describe(address).region == Region::bios) {
      return 0;
    }
    if ((address & 0x1U) == 0) {
      return memory_.read16(address);
    }
    const std::optional<std::uint8_t> value = memory_.read8(address);
    if (!value.has_value()) {
      return std::nullopt;
    }
    return static_cast<std::uint16_t>(value.value());
  };
  for (std::uint32_t index = 0; index < count; ++index) {
    const std::optional<std::uint16_t> value =
        fill ? (fill_value.has_value() ? fill_value : (fill_value = read_halfword(source)))
             : read_halfword(source + index * 2U);
    if (!value.has_value() || !memory_.write16(dest + index * 2U, value.value())) {
      return false;
    }
  }
  return true;
}

CoreSchedulerFetchStepResult CoreScheduler::step_arm_from_pc() {
  const std::uint32_t fetch_address = cpu_.register_value(Arm7tdmi::kPc);
  const std::optional<CoreSchedulerStepResult> hle_irq_return =
      dispatch_hle_irq_return(fetch_address);
  if (hle_irq_return.has_value()) {
    reset_fetch_timing_sequence();
    return {CoreInstructionSet::arm, 4, fetch_address, std::nullopt, hle_irq_return, 0,
            false, false, false, false, false, false, 0, false};
  }

  const std::optional<CoreSchedulerStepResult> hle_irq =
      dispatch_hle_irq_vector(fetch_address);
  if (hle_irq.has_value()) {
    reset_fetch_timing_sequence();
    return {CoreInstructionSet::arm, 4, fetch_address, std::nullopt, hle_irq, 0,
            false, false, false, false, false, false, 0, false};
  }

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
