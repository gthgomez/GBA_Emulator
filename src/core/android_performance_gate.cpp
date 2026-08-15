#include "gba/core/android_performance_gate.hpp"

#include <algorithm>
#include <chrono>
#include <vector>

namespace gba::core {

AndroidFramePacingSummary run_android_performance_gate(
    AndroidRuntime& runtime, const AndroidPerformanceGateConfig& config) {
  AndroidFramePacingSummary summary{};
  if (config.frames == 0 || config.steps_per_frame == 0) {
    return summary;
  }
  // Measure gameplay pacing with hashing OFF so the hash never perturbs timing.
  const bool previous = runtime.state_hash_enabled();
  runtime.set_state_hash_enabled(false);

  std::vector<double> frame_ms;
  frame_ms.reserve(config.frames);
  for (std::uint32_t frame = 0; frame < config.frames; ++frame) {
    const auto start = std::chrono::steady_clock::now();
    const AndroidRuntimeFrameResult result = runtime.step_frame(config.steps_per_frame);
    const auto end = std::chrono::steady_clock::now();
    const std::chrono::duration<double, std::milli> elapsed = end - start;
    frame_ms.push_back(elapsed.count());
    ++summary.frames;
    summary.audio_underruns += result.audio_underruns;
    if (elapsed.count() > config.target_frame_ms) {
      ++summary.missed_frames;
    }
  }

  double total = 0.0;
  for (const double value : frame_ms) {
    total += value;
  }
  summary.average_frame_ms = total / static_cast<double>(frame_ms.size());
  std::sort(frame_ms.begin(), frame_ms.end());
  const std::size_t p95_index =
      std::min<std::size_t>(frame_ms.size() - 1U,
                            static_cast<std::size_t>((frame_ms.size() * 95U) / 100U));
  summary.p95_frame_ms = frame_ms.at(p95_index);
  summary.thermal = summary.p95_frame_ms >= config.elevated_thermal_frame_ms
                        ? ThermalObservation::elevated
                        : ThermalObservation::nominal;
  // Unmeasured determinism fingerprint of the post-run session state.
  summary.final_state_hash = runtime.session().state_hash();
  runtime.set_state_hash_enabled(previous);
  return summary;
}

}  // namespace gba::core
