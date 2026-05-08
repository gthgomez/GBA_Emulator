#pragma once

#include "gba/core/android_runtime.hpp"

#include <cstdint>

namespace gba::core {

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
