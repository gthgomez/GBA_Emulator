#include "gba/core/wait_state_control.hpp"

#include "gba/core/state_hash.hpp"

#include <array>

namespace gba::core {
namespace {

constexpr std::uint16_t kWritableMask = 0x7FFF;
constexpr std::uint16_t kPrefetchBit = 0x4000;
constexpr std::array<std::uint8_t, 4> kFirstAccessWaitStates{4, 3, 2, 8};

[[nodiscard]] std::uint8_t bits(std::uint16_t value, std::uint8_t shift,
                                std::uint16_t mask) {
  return static_cast<std::uint8_t>((value >> shift) & mask);
}

[[nodiscard]] std::uint8_t first_wait_states(std::uint16_t value, std::uint8_t shift) {
  return kFirstAccessWaitStates.at(bits(value, shift, 0x3U));
}

[[nodiscard]] std::uint8_t second_wait_state(bool fast, std::uint8_t slow_value) {
  return fast ? 1 : slow_value;
}

}  // namespace

WaitStateControl::WaitStateControl() {
  reset();
}

void WaitStateControl::reset() {
  control_ = 0;
}

void WaitStateControl::write_control(std::uint16_t value) {
  control_ = value & kWritableMask;
}

std::uint16_t WaitStateControl::read_control() const {
  return control_;
}

std::uint8_t WaitStateControl::save_wait_states() const {
  return first_wait_states(control_, 0);
}

GamePakWaitStates WaitStateControl::rom_wait_states(CartridgeWindow window) const {
  switch (window) {
    case CartridgeWindow::rom_wait0:
      return {first_wait_states(control_, 2),
              second_wait_state((control_ & 0x0010U) != 0, 2),
              prefetch_enabled()};
    case CartridgeWindow::rom_wait1:
      return {first_wait_states(control_, 5),
              second_wait_state((control_ & 0x0080U) != 0, 4),
              prefetch_enabled()};
    case CartridgeWindow::rom_wait2:
      return {first_wait_states(control_, 8),
              second_wait_state((control_ & 0x0400U) != 0, 8),
              prefetch_enabled()};
    case CartridgeWindow::none:
    case CartridgeWindow::save:
      return {0, 0, prefetch_enabled()};
  }

  return {0, 0, prefetch_enabled()};
}

bool WaitStateControl::prefetch_enabled() const {
  return (control_ & kPrefetchBit) != 0;
}

PhiTerminalOutput WaitStateControl::phi_terminal_output() const {
  return static_cast<PhiTerminalOutput>(bits(control_, 11, 0x3U));
}

std::uint64_t WaitStateControl::state_hash() const {
  StateHasher hasher;
  hasher.add_u16(control_);
  return hasher.value();
}

}  // namespace gba::core
