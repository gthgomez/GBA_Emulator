#include "gba/core/wait_state_control.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void expect_timing(const gba::core::GamePakWaitStates& timing,
                   std::uint8_t nonsequential, std::uint8_t sequential,
                   bool prefetch, std::string_view message) {
  expect(timing.nonsequential == nonsequential, message);
  expect(timing.sequential == sequential, message);
  expect(timing.prefetch_enabled == prefetch, message);
}

}  // namespace

int main() {
  using gba::core::CartridgeWindow;
  using gba::core::PhiTerminalOutput;
  using gba::core::WaitStateControl;

  WaitStateControl waitcnt;
  expect(waitcnt.read_control() == 0, "WAITCNT reset value is zero");
  expect(waitcnt.save_wait_states() == 4, "default SRAM wait control decodes");
  expect_timing(waitcnt.rom_wait_states(CartridgeWindow::rom_wait0), 4, 2, false,
                "default wait-state 0 timing decodes");
  expect_timing(waitcnt.rom_wait_states(CartridgeWindow::rom_wait1), 4, 4, false,
                "default wait-state 1 timing decodes");
  expect_timing(waitcnt.rom_wait_states(CartridgeWindow::rom_wait2), 4, 8, false,
                "default wait-state 2 timing decodes");
  expect(!waitcnt.prefetch_enabled(), "default prefetch is disabled");
  expect(waitcnt.phi_terminal_output() == PhiTerminalOutput::disabled,
         "default PHI output is disabled");

  waitcnt.write_control(WaitStateControl::kStandardGamePakSetting);
  expect(waitcnt.read_control() == WaitStateControl::kStandardGamePakSetting,
         "standard WAITCNT setting round-trips");
  expect(waitcnt.save_wait_states() == 8, "standard SRAM wait control decodes");
  expect_timing(waitcnt.rom_wait_states(CartridgeWindow::rom_wait0), 3, 1, true,
                "standard wait-state 0 timing decodes");
  expect_timing(waitcnt.rom_wait_states(CartridgeWindow::rom_wait1), 4, 4, true,
                "standard wait-state 1 timing decodes");
  expect_timing(waitcnt.rom_wait_states(CartridgeWindow::rom_wait2), 8, 8, true,
                "standard wait-state 2 timing decodes");
  expect(waitcnt.prefetch_enabled(), "standard WAITCNT enables prefetch");

  waitcnt.write_control(0xFFFF);
  expect(waitcnt.read_control() == 0x7FFF, "WAITCNT masks read-only game-pak type bit");
  expect(waitcnt.save_wait_states() == 8, "max SRAM wait control decodes");
  expect_timing(waitcnt.rom_wait_states(CartridgeWindow::rom_wait0), 8, 1, true,
                "max wait-state 0 timing decodes");
  expect_timing(waitcnt.rom_wait_states(CartridgeWindow::rom_wait1), 8, 1, true,
                "max wait-state 1 timing decodes");
  expect_timing(waitcnt.rom_wait_states(CartridgeWindow::rom_wait2), 8, 1, true,
                "max wait-state 2 timing decodes");
  expect(waitcnt.phi_terminal_output() == PhiTerminalOutput::sixteen_mhz,
         "PHI terminal output decodes");

  waitcnt.reset();
  expect(waitcnt.read_control() == 0, "WAITCNT reset clears control");
  expect_timing(waitcnt.rom_wait_states(CartridgeWindow::save), 0, 0, false,
                "save aperture has no ROM wait-state tuple");
  expect_timing(waitcnt.rom_wait_states(CartridgeWindow::none), 0, 0, false,
                "non-cartridge aperture has no ROM wait-state tuple");

  std::cout << "wait_state_control_test: PASS\n";
  return 0;
}
