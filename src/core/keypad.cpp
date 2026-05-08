#include "gba/core/keypad.hpp"

#include "gba/core/state_hash.hpp"

namespace gba::core {

Keypad::Keypad() {
  reset();
}

void Keypad::reset() {
  pressed_mask_ = 0;
  keycnt_ = 0;
}

bool Keypad::set_pressed_mask(std::uint16_t pressed_mask) {
  if ((pressed_mask & ~kButtonMask) != 0) {
    return false;
  }
  pressed_mask_ = pressed_mask;
  return true;
}

void Keypad::press(KeypadButton button) {
  pressed_mask_ = static_cast<std::uint16_t>(
      pressed_mask_ | static_cast<std::uint16_t>(button));
}

void Keypad::release(KeypadButton button) {
  pressed_mask_ = static_cast<std::uint16_t>(
      pressed_mask_ & ~static_cast<std::uint16_t>(button));
}

void Keypad::write_keycnt(std::uint16_t value) {
  keycnt_ = static_cast<std::uint16_t>(value & (kButtonMask | kIrqEnable | kIrqConditionAnd));
}

void Keypad::poll_interrupt(InterruptController& interrupts) const {
  if (irq_condition_met()) {
    interrupts.request(InterruptSource::keypad);
  }
}

std::uint16_t Keypad::pressed_mask() const {
  return pressed_mask_;
}

std::uint16_t Keypad::keyinput() const {
  return static_cast<std::uint16_t>((~pressed_mask_) & kButtonMask);
}

std::uint16_t Keypad::keycnt() const {
  return keycnt_;
}

bool Keypad::irq_enabled() const {
  return (keycnt_ & kIrqEnable) != 0;
}

bool Keypad::irq_condition_and() const {
  return (keycnt_ & kIrqConditionAnd) != 0;
}

bool Keypad::irq_condition_met() const {
  if (!irq_enabled()) {
    return false;
  }
  const std::uint16_t selected = static_cast<std::uint16_t>(keycnt_ & kButtonMask);
  if (selected == 0) {
    return false;
  }
  const std::uint16_t pressed_selected = static_cast<std::uint16_t>(pressed_mask_ & selected);
  return irq_condition_and() ? pressed_selected == selected : pressed_selected != 0;
}

std::uint64_t Keypad::state_hash() const {
  StateHasher hasher;
  hasher.add_u16(pressed_mask_);
  hasher.add_u16(keycnt_);
  return hasher.value();
}

}  // namespace gba::core
