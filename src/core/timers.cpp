#include "gba/core/timers.hpp"

#include "gba/core/state_hash.hpp"

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
  for (Timer& timer : timers_) {
    timer.counter = 0;
    timer.reload = 0;
    timer.control = 0;
    timer.prescaler_remainder = 0;
    timer.overflow_count = 0;
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
  }
  if (!is_enabled) {
    timer.prescaler_remainder = 0;
  }
}

void Timers::tick(std::uint32_t cycles, InterruptController& interrupts) {
  for (std::size_t index = 0; index < kTimerCount; ++index) {
    Timer& timer = timers_.at(index);
    if (!enabled(index) || count_up(index)) {
      continue;
    }

    timer.prescaler_remainder += cycles;
    const std::uint16_t divisor = prescaler_divisor(index);
    const std::uint64_t ticks = timer.prescaler_remainder / divisor;
    timer.prescaler_remainder %= divisor;
    increment_timer(index, ticks, interrupts);
  }
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

std::uint64_t Timers::overflow_count(std::size_t index) const {
  return checked_timer(index).overflow_count;
}

std::uint64_t Timers::state_hash() const {
  StateHasher hasher;
  for (const Timer& timer : timers_) {
    hasher.add_u16(timer.counter);
    hasher.add_u16(timer.reload);
    hasher.add_u16(timer.control);
    hasher.add_u64(timer.prescaler_remainder);
    hasher.add_u64(timer.overflow_count);
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
                             InterruptController& interrupts) {
  Timer& timer = checked_timer(index);
  while (ticks > 0) {
    const std::uint32_t ticks_to_overflow =
        0x10000U - static_cast<std::uint32_t>(timer.counter);
    if (ticks < ticks_to_overflow) {
      timer.counter = static_cast<std::uint16_t>(timer.counter + ticks);
      return;
    }

    ticks -= ticks_to_overflow;
    timer.counter = timer.reload;
    handle_overflow(index, interrupts);
  }
}

void Timers::handle_overflow(std::size_t index, InterruptController& interrupts) {
  ++checked_timer(index).overflow_count;

  if (irq_enabled(index)) {
    interrupts.request(timer_interrupt_source(index));
  }

  const std::size_t next = index + 1;
  if (next < kTimerCount && enabled(next) && count_up(next)) {
    increment_timer(next, 1, interrupts);
  }
}

}  // namespace gba::core
