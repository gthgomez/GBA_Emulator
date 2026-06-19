#include "gba/core/core_session.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void write_bytes(std::vector<std::uint8_t>& rom, std::size_t offset,
                 const std::vector<std::uint8_t>& bytes) {
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    rom.at(offset + index) = bytes.at(index);
  }
}

constexpr std::uint32_t kProgramBase = 0x08000000U;
constexpr std::uint32_t kCompressedBase = 0x08000100U;
constexpr std::uint32_t kDestBase = 0x02000000U;
constexpr std::uint32_t kVramDestBase = 0x06000000U;

void write_arm_swi(std::vector<std::uint8_t>& rom, std::uint8_t service) {
  write_bytes(rom, 0, {0x00, 0x00, service, 0xEF});
}

void seed_empty_header(std::vector<std::uint8_t>& rom, std::uint8_t type) {
  write_bytes(rom, kCompressedBase - kProgramBase, {type, 0x00, 0x00, 0x00});
}

}  // namespace

int main() {
  using gba::core::CoreSession;
  using gba::core::ExecuteStatus;
  using gba::core::GamePakSaveType;

  {
    CoreSession session;
    std::vector<std::uint8_t> rom(512, 0);
    write_arm_swi(rom, 0x13);
    write_bytes(rom, kCompressedBase - kProgramBase,
                {0x30, 0x05, 0x00, 0x00, 0x40, 'A', 0x00, 'B', 'C'});
    expect(session.memory().load_game_pak_rom(rom), "RL ROM load");
    expect(session.memory().configure_game_pak_save(GamePakSaveType::none), "RL save");
    session.configure_for_game_boot();
    session.cpu().set_register(gba::core::Arm7tdmi::kPc, kProgramBase);
    session.cpu().set_register(0, kCompressedBase);
    session.cpu().set_register(1, kDestBase);
    const auto swi = session.step();
    expect(!swi.fetch_failed && swi.step.has_value(), "RL SWI step");
    expect(swi.step->cpu_step.status == ExecuteStatus::executed, "RL SWI executed");
    expect(session.memory().read8(kDestBase).value_or(0) == 'A', "RL literal A");
    expect(session.memory().read8(kDestBase + 1).value_or(0) == 'B', "RL run B");
    expect(session.memory().read8(kDestBase + 2).value_or(0) == 'B', "RL run B+1");
    expect(session.memory().read8(kDestBase + 3).value_or(0) == 'B', "RL run B+2");
    expect(session.memory().read8(kDestBase + 4).value_or(0) == 'C', "RL literal C");
  }

  {
    CoreSession session;
    std::vector<std::uint8_t> rom(512, 0);
    write_arm_swi(rom, 0x15);
    write_bytes(rom, kCompressedBase - kProgramBase, {0x21, 0x04, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04});
    expect(session.memory().load_game_pak_rom(rom), "Diff8 ROM load");
    expect(session.memory().configure_game_pak_save(GamePakSaveType::none), "Diff8 save");
    session.configure_for_game_boot();
    session.cpu().set_register(gba::core::Arm7tdmi::kPc, kProgramBase);
    session.cpu().set_register(0, kCompressedBase);
    session.cpu().set_register(1, kDestBase);
    const auto swi = session.step();
    expect(!swi.fetch_failed && swi.step.has_value(), "Diff8 SWI step");
    expect(swi.step->cpu_step.status == ExecuteStatus::executed, "Diff8 SWI executed");
    expect(session.memory().read8(kDestBase).value_or(0) == 0x01, "Diff8 byte 0");
    expect(session.memory().read8(kDestBase + 1).value_or(0) == 0x03, "Diff8 byte 1");
    expect(session.memory().read8(kDestBase + 2).value_or(0) == 0x06, "Diff8 byte 2");
    expect(session.memory().read8(kDestBase + 3).value_or(0) == 0x0A, "Diff8 byte 3");
  }

  {
    CoreSession session;
    std::vector<std::uint8_t> rom(512, 0);
    write_arm_swi(rom, 0x17);
    write_bytes(rom, kCompressedBase - kProgramBase,
                {0x22, 0x04, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00});
    expect(session.memory().load_game_pak_rom(rom), "Diff16 ROM load");
    expect(session.memory().configure_game_pak_save(GamePakSaveType::none), "Diff16 save");
    session.configure_for_game_boot();
    session.cpu().set_register(gba::core::Arm7tdmi::kPc, kProgramBase);
    session.cpu().set_register(0, kCompressedBase);
    session.cpu().set_register(1, kVramDestBase);
    const auto swi = session.step();
    expect(!swi.fetch_failed && swi.step.has_value(), "Diff16 SWI step");
    expect(swi.step->cpu_step.status == ExecuteStatus::executed, "Diff16 SWI executed");
    expect(session.memory().read16(kVramDestBase).value_or(0) == 0x0001, "Diff16 halfword 0");
    expect(session.memory().read16(kVramDestBase + 2).value_or(0) == 0x0003, "Diff16 halfword 1");
  }

  {
    std::vector<std::uint8_t> rom(512, 0);
    write_arm_swi(rom, 0x14);
    seed_empty_header(rom, 0x30);
    CoreSession session;
    expect(session.memory().load_game_pak_rom(rom), "RL VRAM ROM");
    expect(session.memory().configure_game_pak_save(GamePakSaveType::none), "RL VRAM save");
    session.configure_for_game_boot();
    session.cpu().set_register(gba::core::Arm7tdmi::kPc, kProgramBase);
    session.cpu().set_register(0, kCompressedBase);
    session.cpu().set_register(1, kVramDestBase);
    const auto swi = session.step();
    expect(!swi.fetch_failed && swi.step.has_value(), "RL VRAM SWI");
    expect(swi.step->cpu_step.status == ExecuteStatus::executed, "RL VRAM executed");
  }

  {
    std::vector<std::uint8_t> rom(512, 0);
    write_arm_swi(rom, 0x16);
    seed_empty_header(rom, 0x21);
    CoreSession session;
    expect(session.memory().load_game_pak_rom(rom), "Diff8 VRAM ROM");
    session.configure_for_game_boot();
    session.cpu().set_register(gba::core::Arm7tdmi::kPc, kProgramBase);
    session.cpu().set_register(0, kCompressedBase);
    session.cpu().set_register(1, kVramDestBase);
    const auto swi = session.step();
    expect(swi.step.has_value() && swi.step->cpu_step.status == ExecuteStatus::executed,
           "Diff8 VRAM executed");
  }

  std::cout << "hle_decompress_swi_test: PASS\n";
  return 0;
}
