// Regression test: a taken branch whose target is the branch's own address
// must loop in place, not be rewritten into a fallthrough by the scheduler's
// "the instruction did not advance the PC" heuristic.
//
// Found as `B #-8` (0xEAFFFFFE) running away instead of looping; the Thumb
// forever-loop idiom `b .` (0xE7FE) had the same defect. Both step paths
// decide whether to force-advance the PC by comparing it to the fetch
// address, which a taken branch-to-self also satisfies.

#include "gba/core/arm7tdmi.hpp"
#include "gba/core/core_session.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

#include "test_helpers.hpp"

namespace {

using gba::core::Arm7tdmi;
using gba::core::CoreRunStopReason;
using gba::core::CoreSession;

constexpr std::uint32_t kEntry = 0x08000000U;

std::vector<std::uint8_t> padded_rom() {
  // Zero words decode as `andeq r0, r0, r0` (fallthrough NOPs) in ARM and as
  // `lsl r0, r0, #0` in Thumb; 1 KiB gives the fallthrough case room for the
  // whole step budget without leaving the ROM.
  return std::vector<std::uint8_t>(1024, 0);
}

void write_word(std::vector<std::uint8_t>& rom, std::uint32_t offset,
                std::uint32_t word) {
  rom.at(offset + 0) = static_cast<std::uint8_t>(word);
  rom.at(offset + 1) = static_cast<std::uint8_t>(word >> 8);
  rom.at(offset + 2) = static_cast<std::uint8_t>(word >> 16);
  rom.at(offset + 3) = static_cast<std::uint8_t>(word >> 24);
}

CoreSession booted_session(const std::vector<std::uint8_t>& rom) {
  CoreSession session;
  expect(session.memory().load_game_pak_rom(rom), "ROM loads");
  session.configure_for_game_boot();
  expect(session.cpu().register_value(Arm7tdmi::kPc) == kEntry, "entry at ROM base");
  return session;
}

// ARM `b .` at the entry word: 64 executed steps must all stay on it.
void test_arm_branch_to_self() {
  std::vector<std::uint8_t> rom = padded_rom();
  write_word(rom, 0, 0xEAFFFFFEU);  // B #-8 -> own address (AL condition).
  CoreSession session = booted_session(rom);
  const gba::core::CoreSchedulerRunResult result = session.run(64);
  expect(result.stop_reason == CoreRunStopReason::max_steps,
         "branch-to-self loops without a fetch/decode stop");
  expect(result.executed_steps == 64, "all budgeted steps execute");
  expect(session.cpu().register_value(Arm7tdmi::kPc) == kEntry,
         "ARM branch-to-self keeps the PC on itself");
}

// ARM `b eq .` with Z clear: the branch is NOT taken, so the scheduler must
// still advance (the fallthrough discriminator must survive the fix).
void test_arm_skipped_branch_to_self_advances() {
  std::vector<std::uint8_t> rom = padded_rom();
  write_word(rom, 0, 0x0AFFFFFEU);  // BEQ #-8 -> own address, condition EQ.
  CoreSession session = booted_session(rom);
  expect(session.cpu().set_cpsr(0x00000013U), "ARM supervisor, flags clear (Z=0)");
  const gba::core::CoreSchedulerRunResult result = session.run(64);
  expect(result.stop_reason == CoreRunStopReason::max_steps,
         "skipped branch falls through without a stop");
  const std::uint32_t expected_pc = kEntry + 64U * 4U;
  expect(session.cpu().register_value(Arm7tdmi::kPc) == expected_pc,
         "skipped branch-to-self advances like a fallthrough");
}

// Thumb `b .` reached the way a real game reaches Thumb: ARM `bx r0` into a
// Thumb address. The forever-loop must hold its own address.
void test_thumb_branch_to_self() {
  std::vector<std::uint8_t> rom = padded_rom();
  write_word(rom, 0, 0xE12FFF10U);  // BX r0.
  rom.at(8) = 0xFE;
  rom.at(9) = 0xE7;  // Thumb `b .` (0xE7FE) at 0x08000008.
  CoreSession session = booted_session(rom);
  session.cpu().set_register(0, 0x08000009U);  // Thumb target | T bit.
  const gba::core::CoreSchedulerRunResult result = session.run(64);
  expect(result.stop_reason == CoreRunStopReason::max_steps,
         "thumb branch-to-self loops without a stop");
  expect(result.executed_steps == 64, "all budgeted steps execute in thumb");
  expect(session.cpu().register_value(Arm7tdmi::kPc) == 0x08000008U,
         "thumb branch-to-self keeps the PC on itself");
}

}  // namespace

int main() {
  test_arm_branch_to_self();
  test_arm_skipped_branch_to_self_advances();
  test_thumb_branch_to_self();
  std::cout << "branch_to_self_test: PASS\n";
  return 0;
}
