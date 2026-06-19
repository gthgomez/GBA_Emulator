#include "gba/core/android_runtime.hpp"
#include "gba/core/arm7tdmi.hpp"
#include "gba/core/bios.hpp"

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
  constexpr std::uint32_t kBranchToRomBase = 0xEAFFFFFBU;
  AndroidRuntime runtime;
  expect(runtime.load_rom({}) == AndroidRuntimeStatus::invalid_argument,
         "empty ROM rejects");
  expect(runtime.set_button_mask(0x0400) == AndroidRuntimeStatus::input_rejected,
         "invalid input mask rejects");

  std::vector<std::uint8_t> rom(4096);
  for (std::size_t offset = 0; offset + 4 < rom.size(); offset += 4) {
    write_word(rom, offset, kAddR0R0Imm1);
  }
  write_word(rom, rom.size() - 4, kBranchToRomBase);
  expect(runtime.load_rom(rom) == AndroidRuntimeStatus::ok, "runtime loads explicit ROM");
  expect(runtime.session().bios().mode() == gba::core::BiosExecutionMode::hle,
         "runtime load enables BIOS HLE for retail boot");
  expect(runtime.session().cpu().register_value(gba::core::Arm7tdmi::kPc) == 0x08000000U,
         "runtime load sets entry PC to ROM base");
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

  constexpr std::uint32_t kFullFrameStepBudget = 500'000;
  const gba::core::AndroidRuntimeFrameResult frame =
      runtime.step_frame(kFullFrameStepBudget);
  expect(frame.status == AndroidRuntimeStatus::ok, "runtime frame succeeds");
  expect(frame.run.executed_steps > 0, "runtime frame steps core");
  expect(frame.run.scheduler_cycles >= gba::core::PpuTiming::kCyclesPerFrame,
         "runtime frame advances scheduler by one frame budget");
  expect(frame.rendered_scanlines == gba::core::PpuRenderer::kScreenHeight,
         "runtime renders full framebuffer");
  expect(runtime.pixel(0, 0) == 0x1234, "runtime framebuffer exposes rendered pixel");
  expect(frame.audio_samples >= 3, "runtime drains seeded audio samples");
  expect(frame.audio_underruns == 0, "runtime reports no underrun when samples exist");
  expect(!runtime.last_audio_batch().empty(), "runtime exposes last audio batch");

  while (runtime.session().apu().pop_audio_sample().has_value()) {
  }
  const gba::core::AndroidRuntimeFrameResult underrun = runtime.step_frame(1);
  expect(underrun.audio_underruns == 1, "runtime reports audio underrun when buffer empty");

  const std::uint64_t cycles_before_bounded =
      runtime.session().scheduler().scheduler_cycles();
  const gba::core::AndroidRuntimeFrameResult cycle_bounded =
      runtime.step_frame(1);
  const std::uint64_t cycle_delta_bounded =
      cycle_bounded.run.scheduler_cycles - cycles_before_bounded;
  expect(cycle_bounded.status == AndroidRuntimeStatus::ok,
         "cycle-bounded frame keeps ok status");
  expect(cycle_bounded.run.stop_reason == gba::core::CoreRunStopReason::max_steps,
         "cycle-bounded frame stops on step budget");
  expect(cycle_delta_bounded < gba::core::PpuTiming::kCyclesPerFrame,
         "cycle-bounded frame does not meet full frame budget");
  expect(cycle_bounded.rendered_scanlines == 0,
         "cycle-bounded frame skips scanline render");

  runtime.session().cpu().set_register(gba::core::Arm7tdmi::kPc, 0x12000000U);
  const gba::core::AndroidRuntimeFrameResult fetch_failed =
      runtime.step_frame(kFullFrameStepBudget);
  expect(fetch_failed.run.stop_reason == gba::core::CoreRunStopReason::fetch_failed,
         "fetch failure stops frame stepping");
  expect(fetch_failed.run.attempted_steps == 0,
         "fetch failure does not count as attempted step");
  expect(fetch_failed.rendered_scanlines == 0,
         "fetch failure skips scanline render");

  expect(runtime.load_rom(rom) == AndroidRuntimeStatus::ok,
         "reload clears prior failure state");
  expect(runtime.pixel(0, 0) == 0, "reload clears framebuffer presentation");

  std::cout << "android_runtime_test: PASS\n";
  return 0;
}
