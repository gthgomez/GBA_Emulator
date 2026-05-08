#include "gba/core/android_runtime.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
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
  using gba::core::AndroidRuntime;
  using gba::core::AndroidRuntimeStatus;
  using gba::core::Apu;

  constexpr std::uint32_t kAddR0R0Imm1 = 0xE2800001U;
  AndroidRuntime runtime;
  expect(runtime.load_rom({}) == AndroidRuntimeStatus::invalid_argument,
         "empty ROM rejects");
  expect(runtime.set_button_mask(0x0400) == AndroidRuntimeStatus::input_rejected,
         "invalid input mask rejects");

  std::vector<std::uint8_t> rom(12);
  write_word(rom, 0, kAddR0R0Imm1);
  write_word(rom, 4, kAddR0R0Imm1);
  write_word(rom, 8, kAddR0R0Imm1);
  expect(runtime.load_rom(rom) == AndroidRuntimeStatus::ok, "runtime loads explicit ROM");
  expect(runtime.set_button_mask(0x0009) == AndroidRuntimeStatus::ok,
         "runtime maps button mask");
  expect(runtime.session().keypad().keyinput() == 0x03F6,
         "runtime input reaches core keypad");
  expect(runtime.session().memory().write16(0x05000000, 0x1234),
         "runtime seeds backdrop");
  runtime.session().apu().write_soundcnt_x(0x0080);
  runtime.session().apu().configure_square_channel(0, 2, 8, 4);
  [[maybe_unused]] const std::optional<gba::core::ApuFrameStep> audio_seed =
      runtime.session().apu().tick(Apu::kCpuCyclesPerAudioSample * 3U);

  const gba::core::AndroidRuntimeFrameResult frame = runtime.step_frame(3);
  expect(frame.status == AndroidRuntimeStatus::ok, "runtime frame succeeds");
  expect(frame.run.executed_steps == 3, "runtime frame steps core");
  expect(frame.rendered_scanlines == gba::core::PpuRenderer::kScreenHeight,
         "runtime renders full framebuffer");
  expect(runtime.pixel(0, 0) == 0x1234, "runtime framebuffer exposes rendered pixel");
  expect(frame.audio_samples == 3, "runtime drains audio samples");
  expect(frame.audio_underruns == 0, "runtime reports no underrun when samples exist");
  expect(!runtime.last_audio_batch().empty(), "runtime exposes last audio batch");

  const gba::core::AndroidRuntimeFrameResult underrun = runtime.step_frame(1);
  expect(underrun.audio_underruns == 1, "runtime reports audio underrun when buffer empty");

  std::cout << "android_runtime_test: PASS\n";
  return 0;
}
