#pragma once

#include "gba/core/program_harness.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace gba::core {

enum class FixtureLicenseStatus : std::uint8_t {
  accepted,
  missing_license,
  nonredistributable,
  empty_rom,
};

struct CompatibilityFixture {
  std::string_view name;
  std::string_view license;
  bool redistributable = false;
  std::vector<std::uint8_t> rom;
  LoadedProgramRunSpec run_spec;
};

// LIFETIME CONTRACT: the string_views in CompatibilityFixtureResult::name
// alias the corresponding CompatibilityFixture::name storage. Every result in
// CompatibilityCorpusResult::results therefore remains valid only while the
// fixtures vector passed to run_compatibility_corpus() is alive and unmodified
// (no reallocation, no element mutation). Callers that must outlive the
// fixtures should copy the names into owning strings.
struct CompatibilityFixtureResult {
  std::string_view name;
  FixtureLicenseStatus license_status = FixtureLicenseStatus::missing_license;
  ProgramHarnessResult harness;
};

struct CompatibilityCorpusResult {
  std::uint32_t fixtures_seen = 0;
  std::uint32_t fixtures_run = 0;
  std::uint32_t fixtures_passed = 0;
  std::uint32_t fixtures_rejected = 0;
  std::uint64_t combined_state_hash = 0;
  std::vector<CompatibilityFixtureResult> results;
};

[[nodiscard]] FixtureLicenseStatus validate_fixture_license(
    const CompatibilityFixture& fixture);
[[nodiscard]] CompatibilityCorpusResult run_compatibility_corpus(
    const std::vector<CompatibilityFixture>& fixtures);

}  // namespace gba::core
