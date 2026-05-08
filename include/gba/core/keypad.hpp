#pragma once

#include "gba/core/interrupt_controller.hpp"

#include <cstdint>

namespace gba::core {

enum class KeypadButton : std::uint16_t {
  a = 0x0001,
  b = 0x0002,
  select = 0x0004,
  start = 0x0008,
  right = 0x0010,
  left = 0x0020,
  up = 0x0040,
  down = 0x0080,
  r = 0x0100,
  l = 0x0200,
};

class Keypad {
 public:
  static constexpr std::uint16_t kButtonMask = 0x03FF;
  static constexpr std::uint16_t kIrqEnable = 0x4000;
  static constexpr std::uint16_t kIrqConditionAnd = 0x8000;

  Keypad();

  void reset();
  [[nodiscard]] bool set_pressed_mask(std::uint16_t pressed_mask);
  void press(KeypadButton button);
  void release(KeypadButton button);
  void write_keycnt(std::uint16_t value);
  void poll_interrupt(InterruptController& interrupts) const;

  [[nodiscard]] std::uint16_t pressed_mask() const;
  [[nodiscard]] std::uint16_t keyinput() const;
  [[nodiscard]] std::uint16_t keycnt() const;
  [[nodiscard]] bool irq_enabled() const;
  [[nodiscard]] bool irq_condition_and() const;
  [[nodiscard]] bool irq_condition_met() const;
  [[nodiscard]] std::uint64_t state_hash() const;

 private:
  std::uint16_t pressed_mask_;
  std::uint16_t keycnt_;
};

}  // namespace gba::core
