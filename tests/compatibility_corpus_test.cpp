#include "gba/core/compatibility_corpus.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

#include "test_helpers.hpp"

int main() {
  using gba::core::CompatibilityFixture;
  using gba::core::CoreRunStopReason;
  using gba::core::FixtureLicenseStatus;
  using gba::core::ProgramHarnessStatus;

  constexpr std::uint32_t kAddR0R0Imm1 = 0xE2800001U;
  std::vector<std::uint8_t> rom(12);
  write_word(rom, 0, kAddR0R0Imm1);
  write_word(rom, 4, kAddR0R0Imm1);
  write_word(rom, 8, kAddR0R0Imm1);

  CompatibilityFixture accepted{};
  accepted.name = "hand-authored-add-loop";
  accepted.license = "CC0-1.0";
  accepted.redistributable = true;
  accepted.rom = rom;
  accepted.run_spec.max_steps = 3;
  accepted.run_spec.expected.stop_reason = CoreRunStopReason::max_steps;
  accepted.run_spec.expected.final_pc = 0x0800000CU;
  accepted.run_spec.expected.registers.push_back({0, 3});

  CompatibilityFixture rejected = accepted;
  rejected.name = "nonredistributable";
  rejected.redistributable = false;

  // Negative path: license-accepted fixture whose expectations deliberately
  // disagree with the program (the ADD loop yields r0 == 3, not 99). The
  // corpus must run it and surface the failure instead of counting it as a
  // pass.
  CompatibilityFixture failing = accepted;
  failing.name = "expectation-mismatch";
  failing.run_spec.expected.registers.clear();
  failing.run_spec.expected.registers.push_back({0, 99});

  const gba::core::CompatibilityCorpusResult result =
      gba::core::run_compatibility_corpus({accepted, rejected, failing});
  expect(result.fixtures_seen == 3, "corpus sees all three fixtures");
  expect(result.fixtures_run == 2, "corpus runs both accepted fixtures");
  expect(result.fixtures_passed == 1, "only the passing fixture counts");
  expect(result.fixtures_rejected == 1, "rejected fixture is counted");
  expect(result.results.at(0).license_status == FixtureLicenseStatus::accepted,
         "accepted fixture license is accepted");
  expect(result.results.at(0).harness.status == ProgramHarnessStatus::passed,
         "accepted fixture harness passes");
  expect(result.results.at(1).license_status == FixtureLicenseStatus::nonredistributable,
         "nonredistributable fixture rejects");
  expect(result.results.at(2).license_status == FixtureLicenseStatus::accepted,
         "failing fixture license is accepted");
  expect(result.results.at(2).harness.status == ProgramHarnessStatus::register_mismatch,
         "failing fixture surfaces register mismatch");
  expect(result.results.at(2).harness.failed_register.has_value() &&
             result.results.at(2).harness.failed_register.value() == 0,
         "mismatch names register r0");
  expect(result.results.at(2).harness.expected_value == 99,
         "mismatch reports expected value");
  expect(result.results.at(2).harness.actual_value == 3,
         "mismatch reports actual ADD-loop result");
  expect(result.combined_state_hash != 0, "corpus emits combined hash");

  std::cout << "compatibility_corpus_test: PASS\n";
  return 0;
}
