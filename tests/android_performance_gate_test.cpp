#include "gba/core/android_performance_gate.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

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
  expect(summary.audio_underruns == 4, "empty audio buffer records underruns");
  expect(summary.final_state_hash != 0, "performance gate records final hash");
  expect(summary.thermal == ThermalObservation::nominal,
         "wide thermal threshold records nominal observation");

  config.target_frame_ms = 0.0;
  const gba::core::AndroidFramePacingSummary strict =
      gba::core::run_android_performance_gate(runtime, config);
  expect(strict.missed_frames == 4, "strict target records missed frames");

  std::cout << "android_performance_gate_test: PASS\n";
  return 0;
}
