#include "gba/core/android_performance_gate.hpp"

#include <algorithm>
#include <chrono>
#include <vector>

namespace gba::core {

double nearest_rank_percentile(const std::vector<double>& sorted_samples,
                               const std::uint32_t percentile) {
  if (sorted_samples.empty()) {
    return 0.0;
  }
  // Nearest-rank: ceil(n * p / 100) as a 1-based rank. The +99 rounding is
  // what keeps e.g. n=20/p=95 from collapsing onto the maximum sample.
  const std::size_t rank =
      (sorted_samples.size() * static_cast<std::size_t>(percentile) + 99U) / 100U;
  const std::size_t index =
      std::min(rank, sorted_samples.size()) - 1U;
  return sorted_samples.at(index);
}

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
  summary.p95_frame_ms =
      nearest_rank_percentile(frame_ms, 95U);
  summary.thermal = summary.p95_frame_ms >= config.elevated_thermal_frame_ms
                        ? ThermalObservation::elevated
                        : ThermalObservation::nominal;
  // Unmeasured determinism fingerprint of the post-run session state.
  summary.final_state_hash = runtime.session().state_hash();
  runtime.set_state_hash_enabled(previous);
  return summary;
}

}  // namespace gba::core
