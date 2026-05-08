#include "gba/core/compatibility_corpus.hpp"

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

void write_word(std::vector<std::uint8_t>& bytes, std::size_t offset,
                std::uint32_t value) {
  bytes.at(offset + 0U) = static_cast<std::uint8_t>(value & 0xFFU);
  bytes.at(offset + 1U) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  bytes.at(offset + 2U) = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  bytes.at(offset + 3U) = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

}  // namespace

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

  const gba::core::CompatibilityCorpusResult result =
      gba::core::run_compatibility_corpus({accepted, rejected});
  expect(result.fixtures_seen == 2, "corpus sees both fixtures");
  expect(result.fixtures_run == 1, "corpus runs only accepted fixture");
  expect(result.fixtures_passed == 1, "accepted fixture passes");
  expect(result.fixtures_rejected == 1, "rejected fixture is counted");
  expect(result.results.at(0).license_status == FixtureLicenseStatus::accepted,
         "accepted fixture license is accepted");
  expect(result.results.at(0).harness.status == ProgramHarnessStatus::passed,
         "accepted fixture harness passes");
  expect(result.results.at(1).license_status == FixtureLicenseStatus::nonredistributable,
         "nonredistributable fixture rejects");
  expect(result.combined_state_hash != 0, "corpus emits combined hash");

  std::cout << "compatibility_corpus_test: PASS\n";
  return 0;
}
