#pragma once

#include "gba/core/memory_bus.hpp"

#include <cstdint>

namespace gba::core {

enum class PhiTerminalOutput : std::uint8_t {
  disabled,
  four_mhz,
  eight_mhz,
  sixteen_mhz,
};

struct GamePakWaitStates {
  std::uint8_t nonsequential;
  std::uint8_t sequential;
  bool prefetch_enabled;
};

class WaitStateControl {
 public:
  static constexpr std::uint16_t kStandardGamePakSetting = 0x4317;

  WaitStateControl();

  void reset();
  void write_control(std::uint16_t value);

  [[nodiscard]] std::uint16_t read_control() const;
  [[nodiscard]] std::uint8_t save_wait_states() const;
  [[nodiscard]] GamePakWaitStates rom_wait_states(CartridgeWindow window) const;
  [[nodiscard]] bool prefetch_enabled() const;
  [[nodiscard]] PhiTerminalOutput phi_terminal_output() const;
  [[nodiscard]] std::uint64_t state_hash() const;

 private:
  std::uint16_t control_;
};

}  // namespace gba::core
