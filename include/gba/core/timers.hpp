#pragma once

#include "gba/core/interrupt_controller.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace gba::core {

class Timers {
 public:
  static constexpr std::size_t kTimerCount = 4;

  struct TickResult {
    std::optional<std::uint32_t> first_irq_cycle;
  };

  struct TimerState {
    std::uint16_t counter = 0;
    std::uint16_t reload = 0;
    std::uint16_t control = 0;
    std::uint64_t prescaler_remainder = 0;
    std::uint64_t overflow_count = 0;
    std::uint32_t enable_delay_cycles = 0;
    std::uint16_t last_enable_phase = 0;
    bool just_enabled = false;
  };

  struct State {
    std::array<TimerState, kTimerCount> timers{};
    std::uint64_t cycle_counter = 0;
  };

  Timers();

  void reset();

  void write_reload(std::size_t index, std::uint16_t value);
  void write_control(std::size_t index, std::uint16_t value);
  void defer_newly_enabled_ticks();
  TickResult tick(std::uint32_t cycles, InterruptController& interrupts);
  [[nodiscard]] State save_state() const;
  [[nodiscard]] bool load_state(const State& state);

  [[nodiscard]] std::uint16_t counter(std::size_t index) const;
  [[nodiscard]] std::uint16_t reload(std::size_t index) const;
  [[nodiscard]] std::uint16_t control(std::size_t index) const;
  [[nodiscard]] bool enabled(std::size_t index) const;
  [[nodiscard]] bool count_up(std::size_t index) const;
  [[nodiscard]] bool irq_enabled(std::size_t index) const;
  [[nodiscard]] std::uint16_t prescaler_divisor(std::size_t index) const;
  [[nodiscard]] std::uint16_t last_enable_phase(std::size_t index) const;
  [[nodiscard]] std::uint32_t cycles_until_next_prescaler_tick(
      std::size_t index) const;
  [[nodiscard]] std::uint64_t overflow_count(std::size_t index) const;
  [[nodiscard]] std::uint64_t state_hash() const;

 private:
  struct Timer {
    std::uint16_t counter;
    std::uint16_t reload;
    std::uint16_t control;
    std::uint64_t prescaler_remainder;
    std::uint64_t overflow_count;
    std::uint32_t enable_delay_cycles;
    std::uint16_t last_enable_phase;
    bool just_enabled;
  };

  std::array<Timer, kTimerCount> timers_;
  std::uint64_t cycle_counter_;

  [[nodiscard]] Timer& checked_timer(std::size_t index);
  [[nodiscard]] const Timer& checked_timer(std::size_t index) const;
  void increment_timer(std::size_t index, std::uint64_t ticks,
                       InterruptController& interrupts, TickResult& result,
                       std::uint32_t first_tick_cycle,
                       std::uint32_t tick_stride);
  void handle_overflow(std::size_t index, InterruptController& interrupts,
                       TickResult& result, std::uint32_t cycle);
};

}  // namespace gba::core
