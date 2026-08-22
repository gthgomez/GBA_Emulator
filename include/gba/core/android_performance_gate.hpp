#pragma once

#include "gba/core/android_runtime.hpp"

#include <cstdint>
#include <vector>

namespace gba::core {

// Nearest-rank percentile of an ASCENDING-sorted sample set: rank =
// ceil(percentile * n / 100), reported as a 0-based index into the samples.
// Returns 0.0 for an empty sample set.
[[nodiscard]] double nearest_rank_percentile(
    const std::vector<double>& sorted_samples, std::uint32_t percentile);


enum class ThermalObservation : std::uint8_t {
  not_measured,
  nominal,
  elevated,
};

struct AndroidFramePacingSummary {
  std::uint32_t frames = 0;
  double average_frame_ms = 0.0;
  double p95_frame_ms = 0.0;
  std::uint32_t missed_frames = 0;
  std::uint32_t audio_underruns = 0;
  std::uint64_t final_state_hash = 0;
  ThermalObservation thermal = ThermalObservation::not_measured;
};

struct AndroidPerformanceGateConfig {
  std::uint32_t frames = 1;
  std::uint32_t steps_per_frame = 1;
  double target_frame_ms = 16.67;
  double elevated_thermal_frame_ms = 25.0;
};

[[nodiscard]] AndroidFramePacingSummary run_android_performance_gate(
    AndroidRuntime& runtime, const AndroidPerformanceGateConfig& config);

}  // namespace gba::core
