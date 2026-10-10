// Verifies the desktop host's save-state load policy: a state must both
// decode cleanly and belong to the currently loaded ROM before it is allowed
// to replace a live session.
//
// The core SaveStateCodec is a self-contained machine snapshot (a blob carries
// its own ROM), so this host-level guard is what actually enforces "save
// states are associated with the correct ROM".

#include "../apps/desktop/save_state_guard.hpp"

#include "gba/core/core_session.hpp"
#include "gba/core/memory_bus.hpp"
#include "gba/core/save_state_codec.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "test_helpers.hpp"

namespace {

using gba::core::CoreSession;
using gba::core::GamePakSaveType;
using gba::core::SaveStateCodec;
using gba::desktop::SaveStateLoadStatus;
using gba::desktop::load_save_state_for_session;

constexpr std::uint32_t kAddR0R0Imm1 = 0xE2800001U;
constexpr std::uint32_t kBranchBackOneInstruction = 0xEAFFFFFDU;

std::vector<std::uint8_t> make_rom(std::uint8_t tag) {
  std::vector<std::uint8_t> rom(8, 0);
  write_word(rom, 0, kAddR0R0Imm1);
  write_word(rom, 4, kBranchBackOneInstruction);
  rom[0] ^= tag;  // Per-ROM distinguishing byte.
  return rom;
}

// A CoreSession embeds ~405 KB of machine RAM (EWRAM/VRAM/IWRAM arrays). This
// test holds several sessions at once, so every one of them lives on the heap:
// six stack instances (~2.4 MB) overflowed the MinGW-w64 executable's default
// 2 MiB stack reservation (observed as 0xC00000FD on Windows CI).
std::unique_ptr<CoreSession> make_session(const std::vector<std::uint8_t>& rom) {
  auto session = std::make_unique<CoreSession>();
  expect(session->memory().load_game_pak_rom(rom), "session loads ROM");
  return session;
}

}  // namespace

int main() {
  const std::vector<std::uint8_t> rom_a = make_rom(0x00);
  const std::vector<std::uint8_t> rom_b = make_rom(0x5A);

  // Capture a state from ROM A after giving it some progress to carry.
  std::unique_ptr<CoreSession> source = make_session(rom_a);
  expect(source->memory().configure_game_pak_save(GamePakSaveType::sram32k),
         "configure cartridge save");
  expect(source->memory().write8(0x0E000010, 0x5AU), "seed cartridge save");
  expect(source->memory().write32(0x02000000, 0xCAFEF00DU), "seed EWRAM");
  source->cpu().set_register(gba::core::Arm7tdmi::kPc, 0x08000000U);
  const std::vector<std::uint8_t> blob = SaveStateCodec::encode(*source);
  const std::uint64_t source_hash = source->state_hash();
  source.reset();

  // Same-ROM load commits the snapshot.
  std::unique_ptr<CoreSession> same_rom_live = make_session(rom_a);
  expect(same_rom_live->memory().configure_game_pak_save(GamePakSaveType::sram32k),
         "configure matching save protocol");
  std::unique_ptr<CoreSession> scratch = std::make_unique<CoreSession>();
  expect(load_save_state_for_session(*same_rom_live, blob, *scratch) ==
             SaveStateLoadStatus::ok,
         "same-ROM state loads");
  expect(same_rom_live->state_hash() == source_hash,
         "same-ROM load restores the captured machine");

  // A state must not silently replace an explicitly selected backup protocol.
  std::unique_ptr<CoreSession> different_save_live = make_session(rom_a);
  expect(different_save_live->memory().configure_game_pak_save(GamePakSaveType::eeprom8k),
         "configure different save protocol");
  const std::uint64_t different_save_hash = different_save_live->state_hash();
  expect(load_save_state_for_session(*different_save_live, blob, *scratch) ==
             SaveStateLoadStatus::wrong_save_type,
         "same-ROM state with different save type is refused");
  expect(different_save_live->state_hash() == different_save_hash,
         "save-type refusal leaves live session untouched");

  // Wrong-ROM load is refused and leaves the live session byte-identical.
  std::unique_ptr<CoreSession> wrong_rom_live = make_session(rom_b);
  expect(wrong_rom_live->memory().write32(0x02000004, 0x12345678U), "seed live EWRAM");
  const std::uint64_t wrong_rom_hash = wrong_rom_live->state_hash();
  std::unique_ptr<CoreSession> scratch_b = std::make_unique<CoreSession>();
  expect(load_save_state_for_session(*wrong_rom_live, blob, *scratch_b) ==
             SaveStateLoadStatus::wrong_rom,
         "state from a different ROM is refused");
  expect(wrong_rom_live->state_hash() == wrong_rom_hash,
         "wrong-ROM refusal leaves the live session untouched");
  expect(wrong_rom_live->memory().game_pak_rom_size() == rom_b.size() &&
             wrong_rom_live->memory().export_game_pak_rom() == rom_b,
         "wrong-ROM refusal keeps the live ROM in place");

  // A corrupt blob fails to decode and also leaves the live session untouched.
  std::vector<std::uint8_t> corrupt = blob;
  corrupt.at(0) = 0;  // Break the magic.
  std::unique_ptr<CoreSession> scratch_c = std::make_unique<CoreSession>();
  expect(load_save_state_for_session(*wrong_rom_live, corrupt, *scratch_c) ==
             SaveStateLoadStatus::decode_failed,
         "corrupt blob fails to decode");
  expect(wrong_rom_live->state_hash() == wrong_rom_hash,
         "decode failure leaves the live session untouched");

  // The play-mode guard must accept the full two-bank Flash128K snapshot,
  // while retaining its type/ROM checks after successful core decoding.
  auto flash_source = make_session(rom_a);
  std::vector<std::uint8_t> flash_data(131072, 0xFF);
  flash_data.back() = 0x5A;
  expect(flash_source->memory().import_game_pak_save(GamePakSaveType::flash128k, flash_data),
         "seed Flash128K state");
  const auto flash_blob = SaveStateCodec::encode(*flash_source);
  expect(same_rom_live->memory().configure_game_pak_save(GamePakSaveType::flash128k),
         "select matching Flash128K protocol");
  expect(load_save_state_for_session(*same_rom_live, flash_blob, *scratch) ==
             SaveStateLoadStatus::ok,
         "play-mode guard accepts matching Flash128K snapshot");
  expect(same_rom_live->state_hash() == flash_source->state_hash() &&
             same_rom_live->memory().export_game_pak_save() == flash_data,
         "play-mode guard restores both flash banks");
  expect(load_save_state_for_session(*different_save_live, flash_blob, *scratch) ==
             SaveStateLoadStatus::wrong_save_type,
         "decoded Flash128K state still rejects mismatched protocol");
  expect(different_save_live->state_hash() == different_save_hash,
         "Flash128K protocol refusal preserves live session");
  expect(load_save_state_for_session(*wrong_rom_live, flash_blob, *scratch) ==
             SaveStateLoadStatus::wrong_rom,
         "decoded Flash128K state still rejects wrong ROM");
  expect(wrong_rom_live->state_hash() == wrong_rom_hash,
         "Flash128K ROM refusal preserves live session");

  std::cout << "desktop_save_state_guard_test: PASS\n";
  return 0;
}
