#include "gba/core/core_session.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

#include "test_helpers.hpp"

int main() {
  using gba::core::Arm7tdmi;
  using gba::core::CoreSession;
  using gba::core::ExecuteStatus;
  using gba::core::GamePakSaveType;

  constexpr std::uint32_t kProgramBase = 0x08000000U;
  constexpr std::uint32_t kMovR0EwramFlag = 0xE3A00001U;
  constexpr std::uint32_t kArmSwiRegisterRamReset = 0xEF010000U;
  constexpr std::uint32_t kMovR0Zero = 0xE3A00000U;

  CoreSession session;
  std::vector<std::uint8_t> rom(16);
  write_word(rom, 0, kMovR0EwramFlag);
  write_word(rom, 4, kArmSwiRegisterRamReset);
  write_word(rom, 8, kMovR0Zero);
  write_word(rom, 12, kMovR0Zero);
  expect(session.memory().load_game_pak_rom(rom), "SWI boot test loads ROM");
  expect(session.memory().configure_game_pak_save(GamePakSaveType::none),
         "SWI boot test configures save");
  session.configure_for_game_boot();
  expect(session.memory().write8(0x02000000, 0xAB), "seed EWRAM before RegisterRamReset");
  expect(session.memory().read8(0x02000000).value_or(0) == 0xAB,
         "seeded EWRAM is readable");

  session.cpu().set_register(Arm7tdmi::kPc, kProgramBase);
  const gba::core::CoreSchedulerFetchStepResult mov = session.step();
  expect(!mov.fetch_failed && mov.step.has_value(), "mov r0 executes");
  expect(session.cpu().register_value(0) == 1, "R0 holds EWRAM reset flag");

  const gba::core::CoreSchedulerFetchStepResult swi = session.step();
  expect(!swi.fetch_failed && swi.step.has_value(), "RegisterRamReset SWI executes");
  expect(swi.step->cpu_step.status == ExecuteStatus::executed,
         "RegisterRamReset SWI is handled");
  expect(session.memory().read8(0x02000000).value_or(0xFF) == 0,
         "RegisterRamReset clears EWRAM");
  expect(session.memory().read8(0x08000000).has_value(), "ROM still mapped after reset");

  std::cout << "hle_swi_boot_test: PASS\n";
  return 0;
}
