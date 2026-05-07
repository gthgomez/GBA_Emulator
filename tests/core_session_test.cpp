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

void write_word(std::vector<std::uint8_t>& bytes, std::size_t offset,
                std::uint32_t value) {
  bytes.at(offset + 0U) = static_cast<std::uint8_t>(value & 0xFFU);
  bytes.at(offset + 1U) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  bytes.at(offset + 2U) = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  bytes.at(offset + 3U) = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

}  // namespace

int main() {
  using gba::core::Arm7tdmi;
  using gba::core::CoreRunStopReason;
  using gba::core::CoreSession;
  using gba::core::ExecuteStatus;
  using gba::core::GamePakSaveType;

  constexpr std::uint32_t kProgramBase = 0x08000000U;
  constexpr std::uint32_t kAddR0R0Imm1 = 0xE2800001U;
  constexpr std::uint32_t kBranchBackOneInstruction = 0xEAFFFFFDU;

  CoreSession session;
  expect(session.state_hash() == session.state_hash(), "state hash is stable");
  expect(session.memory().configure_game_pak_save(GamePakSaveType::sram32k),
         "session exposes save backing API");
  expect(session.memory().write8(0x0E000000, 0x42),
         "session save backing writes through memory bus");
  const std::uint64_t with_save_hash = session.state_hash();
  expect(session.memory().write8(0x0E000000, 0x43),
         "changing save backing mutates session state");
  expect(session.state_hash() != with_save_hash, "state hash includes save backing");

  session.reset();
  std::vector<std::uint8_t> rom(256);
  write_word(rom, 0, kAddR0R0Imm1);
  write_word(rom, 4, kBranchBackOneInstruction);
  expect(session.memory().load_game_pak_rom(rom), "session loads explicit ROM bytes");
  session.waitcnt().write_control(gba::core::WaitStateControl::kStandardGamePakSetting);
  session.cpu().set_register(Arm7tdmi::kPc, kProgramBase);

  const gba::core::CoreSchedulerFetchStepResult first = session.step();
  expect(!first.fetch_failed, "session step fetches through owned scheduler");
  expect(first.step->cpu_step.status == ExecuteStatus::executed,
         "session step executes instruction");
  expect(session.cpu().register_value(0) == 1, "session step mutates owned CPU");
  const gba::core::CoreSessionState saved = session.save_state();
  const std::uint64_t saved_hash = session.state_hash();

  const gba::core::CoreSchedulerRunResult run_a = session.run(8);
  expect(run_a.stop_reason == CoreRunStopReason::max_steps,
         "session run reaches max-steps stop");
  const std::uint64_t after_a = session.state_hash();

  session.load_state(saved);
  expect(session.state_hash() == saved_hash, "state restore returns exact hash");
  const gba::core::CoreSchedulerRunResult run_b = session.run(8);
  expect(run_b.stop_reason == CoreRunStopReason::max_steps,
         "restored session run reaches max-steps stop");
  expect(session.state_hash() == after_a,
         "save/restore/run determinism produces identical final hash");
  expect(run_a.final_pc == run_b.final_pc, "restored run preserves final PC");
  expect(run_a.scheduler_cycles == run_b.scheduler_cycles,
         "restored run preserves scheduler-cycle result");

  CoreSession peer;
  expect(peer.memory().load_game_pak_rom(rom), "peer loads same explicit ROM bytes");
  peer.waitcnt().write_control(gba::core::WaitStateControl::kStandardGamePakSetting);
  peer.cpu().set_register(Arm7tdmi::kPc, kProgramBase);
  [[maybe_unused]] const gba::core::CoreSchedulerRunResult peer_run = peer.run(9);
  expect(peer.state_hash() == after_a,
         "fresh peer with same inputs reaches same deterministic hash");

  std::cout << "core_session_test: PASS\n";
  return 0;
}
