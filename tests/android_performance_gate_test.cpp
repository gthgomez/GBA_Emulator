#include "gba/core/android_performance_gate.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

#include "test_helpers.hpp"

namespace {

void expect_percentile(const std::vector<double>& sorted_samples,
                       std::uint32_t percentile, double expected,
                       std::string_view message) {
  const double actual =
      gba::core::nearest_rank_percentile(sorted_samples, percentile);
  // Exact comparison is intentional: the fixtures are crafted so the
  // nearest-rank order statistic is exactly representable.
  if (!(actual == expected)) {
    std::cerr << "FAIL: " << message << " (expected " << expected << ", got "
              << actual << ")\n";
    std::exit(1);
  }
}

void run_nearest_rank_cases() {
  // T3 regression proof at n=20: ceil(20*95/100)=19th ordered sample (19.0).
  // The previous truncating formula ((n*95)/100 -> index 19) returned the
  // maximum (20.0) instead.
  const std::vector<double> one_to_twenty = {1.0,  2.0,  3.0,  4.0,  5.0,  6.0,
                                             7.0,  8.0,  9.0,  10.0, 11.0, 12.0,
                                             13.0, 14.0, 15.0, 16.0, 17.0, 18.0,
                                             19.0, 20.0};
  expect_percentile(one_to_twenty, 95U, 19.0,
                    "p95 at n=20 picks 19th ordered sample, not max");

  // Non-uniform spacing proves it is the rank that matters, not magnitude.
  const std::vector<double> multiples_of_three = [] {
    std::vector<double> values;
    values.reserve(20U);
    for (int i = 1; i <= 20; ++i) {
      values.push_back(static_cast<double>(i * 3));
    }
    return values;
  }();
  expect_percentile(multiples_of_three, 95U, 57.0,
                    "p95 at n=20 skips the maximum on non-uniform data");

  // Full-percentile sanity: n=100 -> exactly the 95th ordered sample.
  const std::vector<double> one_to_hundred = [] {
    std::vector<double> values;
    values.reserve(100U);
    for (int i = 1; i <= 100; ++i) {
      values.push_back(static_cast<double>(i));
    }
    return values;
  }();
  expect_percentile(one_to_hundred, 95U, 95.0, "p95 at n=100 hits rank 95");
  expect_percentile(one_to_hundred, 100U, 100.0,
                    "p100 resolves to the maximum by definition");

  // Tiny-sample edges where the data forces the maximum or the sole value.
  expect_percentile({10.0, 20.0, 30.0, 40.0}, 95U, 40.0,
                    "p95 at n=4 rounds up to the maximum (rank ceil(3.8))");
  expect_percentile({5.0, 7.0}, 95U, 7.0, "p95 at n=2 rounds up to rank 2");
  expect_percentile({42.5}, 95U, 42.5, "p95 at n=1 returns the only sample");
  expect_percentile({}, 95U, 0.0, "empty sample set yields 0");
}

}  // namespace

int main() {
  using gba::core::AndroidPerformanceGateConfig;
  using gba::core::AndroidRuntime;
  using gba::core::AndroidRuntimeStatus;
  using gba::core::ThermalObservation;

  constexpr std::uint32_t kAddR0R0Imm1 = 0xE2800001U;
  std::vector<std::uint8_t> rom(32);
  for (std::size_t offset = 0; offset < rom.size(); offset += 4U) {
    write_word(rom, offset, kAddR0R0Imm1);
  }

  AndroidRuntime runtime;
  expect(runtime.load_rom(rom) == AndroidRuntimeStatus::ok, "performance gate loads ROM");
  const gba::core::AndroidFramePacingSummary empty =
      gba::core::run_android_performance_gate(runtime, {0, 1, 16.67, 25.0});
  expect(empty.frames == 0, "zero-frame gate returns empty summary");

  AndroidPerformanceGateConfig config{};
  config.frames = 4;
  config.steps_per_frame = 2;
  config.target_frame_ms = 1000.0;
  config.elevated_thermal_frame_ms = 1000.0;
  const gba::core::AndroidFramePacingSummary summary =
      gba::core::run_android_performance_gate(runtime, config);
  expect(summary.frames == 4, "performance gate records requested frames");
  expect(summary.average_frame_ms >= 0.0, "performance gate records average frame time");
  expect(summary.p95_frame_ms >= 0.0, "performance gate records p95 frame time");
  expect(summary.missed_frames == 0, "wide target records no missed frames");
  // T8, designed-silent side: this ROM never enables the APU master, so its
  // empty audio batches are intentional and must not be counted as underruns.
  expect(!runtime.session().apu().master_enabled(),
         "gate fixture leaves APU master disabled");
  expect(summary.audio_underruns == 0,
         "designed-silent frames do not record audio underruns");
  expect(summary.final_state_hash != 0, "performance gate records final hash");
  expect(summary.thermal == ThermalObservation::nominal,
          "wide thermal threshold records nominal observation");
  // The gate measured with hashing OFF; the prior (disabled) flag must
  // have been restored on return.
  expect(!runtime.state_hash_enabled(), "gate restores initially-disabled hashing");

  config.target_frame_ms = 0.0;
  // T8, audio-expected side: enable the APU master so each near-empty frame
  // now represents a real underrun instead of intentional silence.
  runtime.session().apu().write_soundcnt_x(0x0080);
  expect(runtime.session().apu().master_enabled(),
         "gate fixture enables APU master for underrun side");
  // Run once more with hashing initially ENABLED: the gate must still measure
  // with hashing off, fingerprint once, and restore the prior (enabled) flag.
  runtime.set_state_hash_enabled(true);
  const gba::core::AndroidFramePacingSummary strict =
      gba::core::run_android_performance_gate(runtime, config);
  expect(strict.missed_frames == 4, "strict target records missed frames");
  expect(strict.audio_underruns == 4,
         "audio-expected frames record one underrun each");
  expect(strict.final_state_hash != 0, "strict run records final hash");
  expect(runtime.state_hash_enabled(), "gate restores initially-enabled hashing");
  runtime.set_state_hash_enabled(false);

  run_nearest_rank_cases();

  std::cout << "android_performance_gate_test: PASS\n";
  return 0;
}
