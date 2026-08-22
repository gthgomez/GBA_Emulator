#include "gba/core/io_registers.hpp"
#include "gba/core/keypad.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

#include "test_helpers.hpp"

int main() {
  using gba::core::InterruptController;
  using gba::core::InterruptSource;
  using gba::core::Keypad;
  using gba::core::KeypadButton;

  Keypad keypad;
  InterruptController interrupts;
  expect(keypad.keyinput() == Keypad::kButtonMask, "KEYINPUT resets all released");
  expect(!keypad.set_pressed_mask(0x0400), "impossible button mask rejects");
  expect(keypad.set_pressed_mask(0x0009), "A+Start mask accepts");
  expect(keypad.keyinput() == 0x03F6, "KEYINPUT is active-low");
  keypad.write_keycnt(0x4001);
  keypad.poll_interrupt(interrupts);
  expect(interrupts.requested(InterruptSource::keypad),
         "OR keypad interrupt requests when selected key is pressed");

  interrupts.reset();
  keypad.write_keycnt(0xC003);
  keypad.poll_interrupt(interrupts);
  expect(!interrupts.requested(InterruptSource::keypad),
         "AND keypad interrupt waits for all selected keys");
  keypad.press(KeypadButton::b);
  keypad.poll_interrupt(interrupts);
  expect(interrupts.requested(InterruptSource::keypad),
         "AND keypad interrupt requests when all selected keys are pressed");
  keypad.release(KeypadButton::a);
  expect(keypad.pressed_mask() == 0x000A, "button release clears one bit");
  expect(keypad.state_hash() == keypad.state_hash(), "keypad hash is stable");

  std::cout << "keypad_test: PASS\n";
  return 0;
}
