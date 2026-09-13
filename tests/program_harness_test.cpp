#include "gba/core/program_harness.hpp"

#include "gba/core/core_session.hpp"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "test_helpers.hpp"

namespace {

std::vector<std::uint8_t> tiny_loop_rom(std::uint8_t fixed_value = 0x96U) {
  constexpr std::uint32_t kAddR0R0Imm1 = 0xE2800001U;
  constexpr std::uint32_t kBranchBackOneInstruction = 0xEAFFFFFDU;
  std::vector<std::uint8_t> rom(256);
  write_word(rom, 0, kAddR0R0Imm1);
  write_word(rom, 4, kBranchBackOneInstruction);
  rom.at(0xB2) = fixed_value;
  return rom;
}

}  // namespace

int main() {
  using gba::core::CoreRunStopReason;
  using gba::core::CoreSession;
  using gba::core::LoadedProgramRunSpec;
  using gba::core::ProgramExpectedState;
  using gba::core::ProgramHarnessStatus;
  using gba::core::ProgramRunSpec;
  using gba::core::RegisterExpectation;

  constexpr std::uint32_t kProgramBase = 0x08000000U;
  constexpr std::uint32_t kUnsupportedInstruction = 0xEC000000U;

  auto session = std::make_unique<CoreSession>();
  ProgramRunSpec spec{};
  spec.rom = tiny_loop_rom();
  spec.loaded.max_steps = 5;
  spec.loaded.require_valid_header = true;
  spec.loaded.expected.final_pc = kProgramBase + 4U;
  spec.loaded.expected.registers.push_back({0, 3});

  const gba::core::ProgramHarnessResult first = run_legal_program(*session, spec);
  expect(first.status == ProgramHarnessStatus::passed,
         "harness runs explicit in-memory legal program bytes");
  expect(first.stop_reason == CoreRunStopReason::max_steps,
         "harness reports max-step stop reason");
  expect(first.executed_steps == 5, "harness reports executed steps");
  expect(first.final_pc == kProgramBase + 4U, "harness reports final PC");

  auto peer = std::make_unique<CoreSession>();
  spec.loaded.expected.state_hash = first.state_hash;
  const gba::core::ProgramHarnessResult second = run_legal_program(*peer, spec);
  expect(second.status == ProgramHarnessStatus::passed,
         "same explicit program reaches expected deterministic state hash");
  expect(second.state_hash == first.state_hash,
         "same explicit program produces identical final hash");

  auto empty_session = std::make_unique<CoreSession>();
  ProgramRunSpec empty_spec{};
  const gba::core::ProgramHarnessResult empty = run_legal_program(*empty_session, empty_spec);
  expect(empty.status == ProgramHarnessStatus::empty_rom,
         "empty caller-provided ROM blob is rejected");

  auto unloaded_session = std::make_unique<CoreSession>();
  LoadedProgramRunSpec loaded_spec{};
  const gba::core::ProgramHarnessResult unloaded =
      run_loaded_legal_program(*unloaded_session, loaded_spec);
  expect(unloaded.status == ProgramHarnessStatus::unloaded_rom,
         "loaded-program harness fails cleanly without loaded ROM");

  auto invalid_header_session = std::make_unique<CoreSession>();
  ProgramRunSpec invalid_header_spec{};
  invalid_header_spec.rom = tiny_loop_rom(0x00U);
  invalid_header_spec.loaded.require_valid_header = true;
  const gba::core::ProgramHarnessResult invalid_header =
      run_legal_program(*invalid_header_session, invalid_header_spec);
  expect(invalid_header.status == ProgramHarnessStatus::invalid_header,
         "invalid cartridge fixed-value byte is rejected when required");

  auto unsupported_session = std::make_unique<CoreSession>();
  ProgramRunSpec unsupported_spec{};
  unsupported_spec.rom = tiny_loop_rom();
  write_word(unsupported_spec.rom, 0, kUnsupportedInstruction);
  unsupported_spec.loaded.require_valid_header = true;
  unsupported_spec.loaded.max_steps = 1;
  unsupported_spec.loaded.expected.stop_reason =
      CoreRunStopReason::unsupported_instruction;
  const gba::core::ProgramHarnessResult unsupported =
      run_legal_program(*unsupported_session, unsupported_spec);
  expect(unsupported.status == ProgramHarnessStatus::passed,
         "unsupported instruction can be asserted as an expected clean stop");
  expect(unsupported.stop_reason == CoreRunStopReason::unsupported_instruction,
         "unsupported instruction stop reason is reported");
  expect(unsupported.unsupported_steps == 1,
         "unsupported instruction count is reported");

  auto mismatch_session = std::make_unique<CoreSession>();
  ProgramRunSpec mismatch_spec = spec;
  mismatch_spec.loaded.expected.registers.clear();
  mismatch_spec.loaded.expected.registers.push_back({0, 4});
  const gba::core::ProgramHarnessResult mismatch =
      run_legal_program(*mismatch_session, mismatch_spec);
  expect(mismatch.status == ProgramHarnessStatus::register_mismatch,
         "register expectation mismatch is reported");
  expect(mismatch.failed_register.has_value() && mismatch.failed_register.value() == 0,
         "mismatched register index is reported");
  expect(mismatch.expected_value.has_value() && mismatch.expected_value.value() == 4,
         "mismatched expected register value is reported");
  expect(mismatch.actual_value.has_value() && mismatch.actual_value.value() == 3,
         "mismatched actual register value is reported");

  std::cout << "program_harness_test: PASS\n";
  return 0;
}
