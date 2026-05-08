#include "gba/core/compatibility_corpus.hpp"

#include "gba/core/core_session.hpp"
#include "gba/core/state_hash.hpp"

namespace gba::core {

FixtureLicenseStatus validate_fixture_license(const CompatibilityFixture& fixture) {
  if (fixture.rom.empty()) {
    return FixtureLicenseStatus::empty_rom;
  }
  if (fixture.license.empty()) {
    return FixtureLicenseStatus::missing_license;
  }
  if (!fixture.redistributable) {
    return FixtureLicenseStatus::nonredistributable;
  }
  return FixtureLicenseStatus::accepted;
}

CompatibilityCorpusResult run_compatibility_corpus(
    const std::vector<CompatibilityFixture>& fixtures) {
  CompatibilityCorpusResult corpus{};
  StateHasher combined;

  for (const CompatibilityFixture& fixture : fixtures) {
    ++corpus.fixtures_seen;
    CompatibilityFixtureResult fixture_result{};
    fixture_result.name = fixture.name;
    fixture_result.license_status = validate_fixture_license(fixture);
    if (fixture_result.license_status != FixtureLicenseStatus::accepted) {
      ++corpus.fixtures_rejected;
      corpus.results.push_back(fixture_result);
      continue;
    }

    CoreSession session;
    ProgramRunSpec spec{};
    spec.rom = fixture.rom;
    spec.loaded = fixture.run_spec;
    fixture_result.harness = run_legal_program(session, spec);
    ++corpus.fixtures_run;
    if (fixture_result.harness.status == ProgramHarnessStatus::passed) {
      ++corpus.fixtures_passed;
    }
    combined.add_u64(fixture_result.harness.state_hash);
    combined.add_u32(fixture_result.harness.final_pc);
    combined.add_u8(static_cast<std::uint8_t>(fixture_result.harness.status));
    corpus.results.push_back(fixture_result);
  }

  corpus.combined_state_hash = combined.value();
  return corpus;
}

}  // namespace gba::core
