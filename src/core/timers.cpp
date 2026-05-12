#include "gba/core/timers.hpp"

#include "gba/core/state_hash.hpp"

#include <algorithm>
#include <stdexcept>

namespace gba::core {
namespace {

constexpr std::uint16_t kPrescalerMask = 0x0003;
constexpr std::uint16_t kCountUpFlag = 0x0004;
constexpr std::uint16_t kIrqFlag = 0x0040;
constexpr std::uint16_t kEnableFlag = 0x0080;
constexpr std::uint16_t kControlMask = kPrescalerMask | kCountUpFlag | kIrqFlag | kEnableFlag;

[[nodiscard]] constexpr InterruptSource timer_interrupt_source(std::size_t index) {
  return static_cast<InterruptSource>(
      static_cast<std::uint8_t>(InterruptSource::timer0) + index);
}

[[nodiscard]] constexpr std::uint16_t divisor_for_control(std::uint16_t control) {
  switch (control & kPrescalerMask) {
    case 0:
      return 1;
    case 1:
      return 64;
    case 2:
      return 256;
    case 3:
      return 1024;
  }
  return 1;
}

}  // namespace

Timers::Timers() {
  reset();
}

void Timers::reset() {
  cycle_counter_ = 0;
  for (Timer& timer : timers_) {
    timer.counter = 0;
    timer.reload = 0;
    timer.control = 0;
    timer.prescaler_remainder = 0;
    timer.overflow_count = 0;
    timer.enable_delay_cycles = 0;
    timer.last_enable_phase = 0;
    timer.just_enabled = false;
  }
}

void Timers::write_reload(std::size_t index, std::uint16_t value) {
  checked_timer(index).reload = value;
}

void Timers::write_control(std::size_t index, std::uint16_t value) {
  Timer& timer = checked_timer(index);
  const bool was_enabled = (timer.control & kEnableFlag) != 0;
  std::uint16_t sanitized = static_cast<std::uint16_t>(value & kControlMask);
  if (index == 0) {
    sanitized = static_cast<std::uint16_t>(sanitized & ~kCountUpFlag);
  }

  timer.control = sanitized;
  const bool is_enabled = (timer.control & kEnableFlag) != 0;
  if (!was_enabled && is_enabled) {
    timer.counter = timer.reload;
    timer.prescaler_remainder = 0;
    timer.enable_delay_cycles = 0;
    const std::uint16_t divisor = divisor_for_control(timer.control);
    timer.last_enable_phase =
        static_cast<std::uint16_t>(cycle_counter_ % divisor);
    timer.just_enabled = true;
  }
  if (!is_enabled) {
    timer.prescaler_remainder = 0;
    timer.enable_delay_cycles = 0;
    timer.last_enable_phase = 0;
    timer.just_enabled = false;
  }
}

void Timers::defer_newly_enabled_ticks() {
  for (Timer& timer : timers_) {
    if (timer.just_enabled) {
      timer.enable_delay_cycles = 1;
    }
  }
}

Timers::TickResult Timers::tick(std::uint32_t cycles,
                                InterruptController& interrupts) {
  TickResult result{};
  if (cycles == 0) {
    return result;
  }

  const std::uint64_t start_cycle = cycle_counter_;
  const std::uint64_t end_cycle = cycle_counter_ + cycles;
  cycle_counter_ = end_cycle;

  for (std::size_t index = 0; index < kTimerCount; ++index) {
    Timer& timer = timers_.at(index);
    if (!enabled(index)) {
      timer.just_enabled = false;
      continue;
    }
    if (count_up(index)) {
      const std::uint32_t skipped_cycles =
          std::min<std::uint32_t>(timer.enable_delay_cycles, cycles);
      timer.enable_delay_cycles -= skipped_cycles;
      timer.just_enabled = false;
      continue;
    }

    const std::uint32_t skipped_cycles =
        std::min<std::uint32_t>(timer.enable_delay_cycles, cycles);
    timer.enable_delay_cycles -= skipped_cycles;
    timer.just_enabled = false;
    if (timer.enable_delay_cycles != 0 || skipped_cycles == cycles) {
      continue;
    }

    const std::uint16_t divisor = prescaler_divisor(index);
    const std::uint64_t first_active_cycle =
        start_cycle + static_cast<std::uint64_t>(skipped_cycles) + 1U;
    const std::uint64_t first_tick_cycle =
        ((first_active_cycle + divisor - 1U) / divisor) * divisor;
    if (first_tick_cycle > end_cycle) {
      continue;
    }

    const std::uint64_t ticks =
        ((end_cycle - first_tick_cycle) / divisor) + 1U;
    increment_timer(index, ticks, interrupts, result,
                    static_cast<std::uint32_t>(first_tick_cycle - start_cycle),
                    divisor);
  }
  return result;
}

Timers::State Timers::save_state() const {
  State state{};
  state.cycle_counter = cycle_counter_;
  for (std::size_t index = 0; index < kTimerCount; ++index) {
    const Timer& timer = timers_.at(index);
    TimerState& timer_state = state.timers.at(index);
    timer_state.counter = timer.counter;
    timer_state.reload = timer.reload;
    timer_state.control = timer.control;
    timer_state.prescaler_remainder = timer.prescaler_remainder;
    timer_state.overflow_count = timer.overflow_count;
    timer_state.enable_delay_cycles = timer.enable_delay_cycles;
    timer_state.last_enable_phase = timer.last_enable_phase;
    timer_state.just_enabled = timer.just_enabled;
  }
  return state;
}

bool Timers::load_state(const State& state) {
  for (std::size_t index = 0; index < kTimerCount; ++index) {
    const TimerState& timer_state = state.timers.at(index);
    if ((timer_state.control & ~kControlMask) != 0) {
      return false;
    }
    if (index == 0 && (timer_state.control & kCountUpFlag) != 0) {
      return false;
    }
    if (timer_state.enable_delay_cycles > 1U) {
      return false;
    }
    if (timer_state.just_enabled && (timer_state.control & kEnableFlag) == 0) {
      return false;
    }
    if (timer_state.enable_delay_cycles != 0 &&
        (timer_state.control & kEnableFlag) == 0) {
      return false;
    }
  }

  cycle_counter_ = state.cycle_counter;
  for (std::size_t index = 0; index < kTimerCount; ++index) {
    const TimerState& timer_state = state.timers.at(index);
    Timer& timer = timers_.at(index);
    timer.counter = timer_state.counter;
    timer.reload = timer_state.reload;
    timer.control = timer_state.control;
    timer.prescaler_remainder = timer_state.prescaler_remainder;
    timer.overflow_count = timer_state.overflow_count;
    timer.enable_delay_cycles = timer_state.enable_delay_cycles;
    timer.last_enable_phase = timer_state.last_enable_phase;
    timer.just_enabled = timer_state.just_enabled;
  }
  return true;
}

std::uint16_t Timers::counter(std::size_t index) const {
  return checked_timer(index).counter;
}

std::uint16_t Timers::reload(std::size_t index) const {
  return checked_timer(index).reload;
}

std::uint16_t Timers::control(std::size_t index) const {
  return checked_timer(index).control;
}

bool Timers::enabled(std::size_t index) const {
  return (checked_timer(index).control & kEnableFlag) != 0;
}

bool Timers::count_up(std::size_t index) const {
  return index != 0 && (checked_timer(index).control & kCountUpFlag) != 0;
}

bool Timers::irq_enabled(std::size_t index) const {
  return (checked_timer(index).control & kIrqFlag) != 0;
}

std::uint16_t Timers::prescaler_divisor(std::size_t index) const {
  return divisor_for_control(checked_timer(index).control);
}

std::uint16_t Timers::last_enable_phase(std::size_t index) const {
  return checked_timer(index).last_enable_phase;
}

std::uint32_t Timers::cycles_until_next_prescaler_tick(std::size_t index) const {
  const Timer& timer = checked_timer(index);
  if (!enabled(index) || count_up(index) || timer.enable_delay_cycles != 0) {
    return 0;
  }

  const std::uint16_t divisor = divisor_for_control(timer.control);
  const std::uint64_t phase = cycle_counter_ % divisor;
  return static_cast<std::uint32_t>(phase == 0 ? divisor : divisor - phase);
}

std::uint64_t Timers::overflow_count(std::size_t index) const {
  return checked_timer(index).overflow_count;
}

std::uint64_t Timers::state_hash() const {
  StateHasher hasher;
  hasher.add_u64(cycle_counter_);
  for (const Timer& timer : timers_) {
    hasher.add_u16(timer.counter);
    hasher.add_u16(timer.reload);
    hasher.add_u16(timer.control);
    hasher.add_u64(timer.prescaler_remainder);
    hasher.add_u64(timer.overflow_count);
    hasher.add_u32(timer.enable_delay_cycles);
    hasher.add_u16(timer.last_enable_phase);
    hasher.add_bool(timer.just_enabled);
  }
  return hasher.value();
}

Timers::Timer& Timers::checked_timer(std::size_t index) {
  if (index >= kTimerCount) {
    throw std::out_of_range("timer index out of range");
  }
  return timers_.at(index);
}

const Timers::Timer& Timers::checked_timer(std::size_t index) const {
  if (index >= kTimerCount) {
    throw std::out_of_range("timer index out of range");
  }
  return timers_.at(index);
}

void Timers::increment_timer(std::size_t index, std::uint64_t ticks,
                             InterruptController& interrupts, TickResult& result,
                             std::uint32_t first_tick_cycle,
                             std::uint32_t tick_stride) {
  Timer& timer = checked_timer(index);
  std::uint32_t next_tick_cycle = first_tick_cycle;
  while (ticks > 0) {
    const std::uint32_t ticks_to_overflow =
        0x10000U - static_cast<std::uint32_t>(timer.counter);
    if (ticks < ticks_to_overflow) {
      timer.counter = static_cast<std::uint16_t>(timer.counter + ticks);
      return;
    }

    ticks -= ticks_to_overflow;
    const std::uint64_t overflow_cycle =
        static_cast<std::uint64_t>(next_tick_cycle) +
        static_cast<std::uint64_t>(ticks_to_overflow - 1U) * tick_stride;
    timer.counter = timer.reload;
    handle_overflow(index, interrupts, result,
                    static_cast<std::uint32_t>(overflow_cycle));
    next_tick_cycle =
        static_cast<std::uint32_t>(overflow_cycle + tick_stride);
  }
}

void Timers::handle_overflow(std::size_t index, InterruptController& interrupts,
                             TickResult& result, std::uint32_t cycle) {
  Timer& timer = checked_timer(index);
  ++timer.overflow_count;

  if (irq_enabled(index)) {
    interrupts.request(timer_interrupt_source(index));
    if (!result.first_irq_cycle.has_value() ||
        cycle < result.first_irq_cycle.value()) {
      result.first_irq_cycle = cycle;
    }
  }

  const std::size_t next = index + 1;
  if (next < kTimerCount && enabled(next) && count_up(next)) {
    const Timer& next_timer = checked_timer(next);
    if (cycle <= next_timer.enable_delay_cycles) {
      return;
    }
    increment_timer(next, 1, interrupts, result, cycle, 1);
  }
}

}  // namespace gba::core
