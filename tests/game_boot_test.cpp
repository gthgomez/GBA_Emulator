#include "gba/core/core_session.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

#include "test_helpers.hpp"

int main() {
  using gba::core::Arm7tdmi;
  using gba::core::BiosExecutionMode;
  using gba::core::CoreSession;
  using gba::core::GamePakSaveType;

  constexpr std::uint32_t kAddR0R0Imm1 = 0xE2800001U;
  constexpr std::uint32_t kBranchBackOneInstruction = 0xEAFFFFFDU;

  CoreSession session;
  std::vector<std::uint8_t> rom(256);
  write_word(rom, 0, kAddR0R0Imm1);
  write_word(rom, 4, kBranchBackOneInstruction);
  expect(session.memory().load_game_pak_rom(rom), "game boot test loads ROM");
  expect(session.memory().configure_game_pak_save(GamePakSaveType::none),
         "game boot test configures save backing");
  session.configure_for_game_boot();
  expect(session.bios().mode() == BiosExecutionMode::hle,
         "configure_for_game_boot enables BIOS HLE");
  expect(session.cpu().register_value(Arm7tdmi::kPc) == 0x08000000U,
         "configure_for_game_boot sets ROM entry PC");
  expect(session.cpu().register_value(13) == 0x03007F00U,
         "configure_for_game_boot sets IWRAM stack pointer");
  expect(session.cpu().current_mode() == gba::core::CpuMode::system,
         "configure_for_game_boot enters system mode");

  const gba::core::CoreSchedulerFetchStepResult first = session.step();
  expect(!first.fetch_failed, "game boot steps without BIOS fetch failure");
  expect(first.step.has_value(), "game boot executes first instruction");

  std::cout << "game_boot_test: PASS\n";
  return 0;
}
