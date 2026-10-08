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

CoreSession make_session(const std::vector<std::uint8_t>& rom) {
  CoreSession session;
  expect(session.memory().load_game_pak_rom(rom), "session loads ROM");
  return session;
}

}  // namespace

int main() {
  const std::vector<std::uint8_t> rom_a = make_rom(0x00);
  const std::vector<std::uint8_t> rom_b = make_rom(0x5A);

  // Capture a state from ROM A after giving it some progress to carry.
  CoreSession source = make_session(rom_a);
  expect(source.memory().configure_game_pak_save(GamePakSaveType::sram32k),
         "configure cartridge save");
  expect(source.memory().write8(0x0E000010, 0x5AU), "seed cartridge save");
  expect(source.memory().write32(0x02000000, 0xCAFEF00DU), "seed EWRAM");
  source.cpu().set_register(gba::core::Arm7tdmi::kPc, 0x08000000U);
  const std::vector<std::uint8_t> blob = SaveStateCodec::encode(source);
  const std::uint64_t source_hash = source.state_hash();

  // Same-ROM load commits the snapshot.
  CoreSession same_rom_live = make_session(rom_a);
  CoreSession scratch{};
  expect(load_save_state_for_session(same_rom_live, blob, scratch) ==
             SaveStateLoadStatus::ok,
         "same-ROM state loads");
  expect(same_rom_live.state_hash() == source_hash,
         "same-ROM load restores the captured machine");

  // Wrong-ROM load is refused and leaves the live session byte-identical.
  CoreSession wrong_rom_live = make_session(rom_b);
  expect(wrong_rom_live.memory().write32(0x02000004, 0x12345678U), "seed live EWRAM");
  const std::uint64_t wrong_rom_hash = wrong_rom_live.state_hash();
  CoreSession scratch_b{};
  expect(load_save_state_for_session(wrong_rom_live, blob, scratch_b) ==
             SaveStateLoadStatus::wrong_rom,
         "state from a different ROM is refused");
  expect(wrong_rom_live.state_hash() == wrong_rom_hash,
         "wrong-ROM refusal leaves the live session untouched");
  expect(wrong_rom_live.memory().game_pak_rom_size() == rom_b.size() &&
             wrong_rom_live.memory().export_game_pak_rom() == rom_b,
         "wrong-ROM refusal keeps the live ROM in place");

  // A corrupt blob fails to decode and also leaves the live session untouched.
  std::vector<std::uint8_t> corrupt = blob;
  corrupt.at(0) = 0;  // Break the magic.
  CoreSession scratch_c{};
  expect(load_save_state_for_session(wrong_rom_live, corrupt, scratch_c) ==
             SaveStateLoadStatus::decode_failed,
         "corrupt blob fails to decode");
  expect(wrong_rom_live.state_hash() == wrong_rom_hash,
         "decode failure leaves the live session untouched");

  std::cout << "desktop_save_state_guard_test: PASS\n";
  return 0;
}
