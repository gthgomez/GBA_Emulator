#pragma once

#include "gba/core/interrupt_controller.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gba::core {

class Timers {
 public:
  static constexpr std::size_t kTimerCount = 4;

  Timers();

  void reset();

  void write_reload(std::size_t index, std::uint16_t value);
  void write_control(std::size_t index, std::uint16_t value);
  void tick(std::uint32_t cycles, InterruptController& interrupts);

  [[nodiscard]] std::uint16_t counter(std::size_t index) const;
  [[nodiscard]] std::uint16_t reload(std::size_t index) const;
  [[nodiscard]] std::uint16_t control(std::size_t index) const;
  [[nodiscard]] bool enabled(std::size_t index) const;
  [[nodiscard]] bool count_up(std::size_t index) const;
  [[nodiscard]] bool irq_enabled(std::size_t index) const;
  [[nodiscard]] std::uint16_t prescaler_divisor(std::size_t index) const;
  [[nodiscard]] std::uint64_t overflow_count(std::size_t index) const;
  [[nodiscard]] std::uint64_t state_hash() const;

 private:
  struct Timer {
    std::uint16_t counter;
    std::uint16_t reload;
    std::uint16_t control;
    std::uint64_t prescaler_remainder;
    std::uint64_t overflow_count;
  };

  std::array<Timer, kTimerCount> timers_;

  [[nodiscard]] Timer& checked_timer(std::size_t index);
  [[nodiscard]] const Timer& checked_timer(std::size_t index) const;
  void increment_timer(std::size_t index, std::uint64_t ticks,
                       InterruptController& interrupts);
  void handle_overflow(std::size_t index, InterruptController& interrupts);
};

}  // namespace gba::core
