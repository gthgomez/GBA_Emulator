#include "gba/core/android_runtime.hpp"
#include "gba/core/arm7tdmi.hpp"
#include "gba/core/bios.hpp"
#include "gba/core/save_state_codec.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "test_helpers.hpp"

namespace {

void test_hblank_scroll_rendering(bool long_transfer) {
  using namespace gba::core;
  auto owned_runtime = std::make_unique<AndroidRuntime>();
  auto& runtime = *owned_runtime;
  std::vector<std::uint8_t> rom(4096, 0);
  // B .: a legal ARM loop, with no guest display writes.
  write_word(rom, 0, 0xEAFFFFFEU);
  expect(runtime.load_rom(rom) == AndroidRuntimeStatus::ok,
         "raster fixture loads synthetic ARM loop");
  auto& memory = runtime.session().memory();
  expect(memory.write16(0x04000000U, 0x0100U), "raster enables text BG0");
  expect(memory.write16(0x04000008U, 0x1F00U), "raster separates map from tiles");
  for (std::uint16_t index = 1; index <= 8; ++index) {
    expect(memory.write16(0x05000000U + index * 2U, index),
           "raster seeds distinct palette entries");
  }
  for (std::uint32_t row = 0; row < 8; ++row) {
    expect(memory.write32(0x06000000U + row * 4U, 0x87654321U),
           "raster seeds horizontal texels");
  }
  for (std::uint32_t line = 0; line < PpuTiming::kVisibleLines; ++line) {
    expect(memory.write16(0x02000000U + line * 2U,
                          static_cast<std::uint16_t>(line + 1U)),
           "raster seeds per-line scroll table");
  }
  auto& dma = runtime.session().dma();
  dma.write_source(0, 0x02000000U);
  dma.write_destination(0, 0x04000010U);
  dma.write_word_count(0, 1);
  // Enable, HBlank, repeat, fixed destination, incrementing source, halfword.
  dma.write_control(0, 0xA240U);
  if (long_transfer) {
    // The scheduler must charge this single immediate transfer through more
    // than two frames. Later rows must not overwrite the first ready surface.
    dma.write_source(3, 0x02001000U);
    dma.write_destination(3, 0x02002000U);
    dma.write_word_count(3, 65'535);
    dma.write_control(3, 0x8540U);
  }
  const auto frame = runtime.step_frame(500'000);
  expect(frame.frame_complete, "raster fixture completes frame");
  expect(long_transfer || runtime.session().ppu().render_control().bg_scroll_x[0] == 160,
         "raster fixture really executes all HBlank transfers");
  expect(runtime.pixel(0, 0) == 1, "row zero uses initial scroll");
  for (std::uint16_t line = 1; line < PpuTiming::kVisibleLines; ++line) {
    expect(runtime.pixel(0, line) == (line % 8U) + 1U,
           "each visible row uses the preceding HBlank scroll transfer");
  }
  if (long_transfer) {
    expect(frame.run.scheduler_cycles > 2U * PpuTiming::kCyclesPerFrame,
           "long transfer really crosses multiple frames in one step");
    const auto tail = runtime.step_frame(500'000);
    expect(tail.frame_complete && tail.rendered_scanlines == 0 && runtime.pixel(0, 1) == 2,
           "partial frame following overshoot retains the first coherent surface");
    expect(runtime.step_frame(500'000).rendered_scanlines == 160,
           "a subsequent full visible frame resumes publication");
  }
}

void test_raster_presentation_lifecycle() {
  using namespace gba::core;
  auto owned_runtime = std::make_unique<AndroidRuntime>();
  auto& runtime = *owned_runtime;
  std::vector<std::uint8_t> rom(4096, 0);
  write_word(rom, 0, 0xEAFFFFFEU);
  expect(runtime.load_rom(rom) == AndroidRuntimeStatus::ok,
         "raster lifecycle loop loads");
  expect(runtime.session().memory().write16(0x05000000U, 1),
         "raster lifecycle initial backdrop");
  const auto partial = runtime.step_frame(1'000);
  expect(!partial.frame_complete && partial.rendered_scanlines == 0,
         "partially accumulated rows are not published");
  expect(runtime.session().ppu().frame_cycle() > PpuTiming::kVisibleCycles &&
             runtime.session().ppu().vcount() < PpuTiming::kVisibleLines,
         "partial fixture actually completes some visible rows");
  expect(runtime.pixel(0, 0) == 0, "partial rows leave published surface unchanged");
  expect(runtime.session().memory().write16(0x05000000U, 2),
         "raster lifecycle changes later rows");
  const auto completed = runtime.step_frame(500'000);
  expect(completed.frame_complete && completed.rendered_scanlines == 160,
         "resuming a partial call publishes a complete frame");
  expect(runtime.pixel(0, 0) == 1 && runtime.pixel(0, 159) == 2,
         "resumed frame retains early rows and samples later memory changes");

  expect(runtime.session().memory().write16(0x05000000U, 3),
         "raster lifecycle seeds next partial frame");
  expect(!runtime.step_frame(1'000).frame_complete, "next partial frame starts");
  auto restored = runtime.session().save_state();
  expect(restored.memory.write16(0x05000000U, 4), "restored snapshot changes palette");
  const auto generation = runtime.session().state_generation();
  auto serialized_session = std::make_unique<CoreSession>();
  serialized_session->load_state(restored);
  const auto encoded = SaveStateCodec::encode(*serialized_session);
  expect(SaveStateCodec::decode_into(runtime.session(), encoded).status ==
             SaveStateDecodeStatus::ok,
         "partial raster snapshot codec round trips");
  expect(runtime.session().state_generation() != generation,
         "same-cycle codec restore invalidates presentation accumulation");
  const auto restored_tail = runtime.step_frame(500'000);
  expect(restored_tail.frame_complete && restored_tail.rendered_scanlines == 0,
         "midframe restore completes time without publishing a mixed frame");
  expect(runtime.pixel(0, 0) == 1 && runtime.pixel(0, 159) == 2,
         "restore retains last coherent surface until another complete frame");
  const auto restored_full = runtime.step_frame(500'000);
  expect(restored_full.rendered_scanlines == 160 && runtime.pixel(0, 0) == 4 &&
             runtime.pixel(0, 159) == 4,
         "first coherent frame after restore replaces the old surface");

  expect(!runtime.step_frame(1'000).frame_complete, "abort fixture has partial rows");
  runtime.session().cpu().set_register(Arm7tdmi::kPc, 0x12000000U);
  const auto aborted = runtime.step_frame(500'000);
  expect(!aborted.frame_complete && aborted.rendered_scanlines == 0 &&
             runtime.pixel(0, 0) == 4 && runtime.pixel(0, 159) == 4,
         "fetch failure preserves the completed surface");
  expect(runtime.reset() == AndroidRuntimeStatus::ok && runtime.pixel(0, 0) == 0,
         "reset clears completed presentation");
  expect(runtime.load_rom(rom) == AndroidRuntimeStatus::ok,
         "reload after reset succeeds");
  expect(runtime.session().memory().write16(0x05000000U, 5),
         "reload fixture seeds a fresh backdrop");
  expect(runtime.step_frame(500'000).rendered_scanlines == 160 &&
             runtime.pixel(0, 0) == 5,
         "reload discards old accumulation and renders a fresh frame");
}

}  // namespace

int main() {
  using gba::core::AndroidRuntime;
  using gba::core::AndroidRuntimeStatus;
  using gba::core::Apu;

  constexpr std::uint32_t kAddR0R0Imm1 = 0xE2800001U;
  constexpr std::uint32_t kBranchToRomBase = 0xEAFFFFFBU;
  auto owned_runtime = std::make_unique<AndroidRuntime>();
  auto& runtime = *owned_runtime;
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
  expect(frame.frame_complete, "full-budget frame reports frame complete");
  expect(frame.run.executed_steps > 0, "runtime frame steps core");
  expect(frame.run.scheduler_cycles >= gba::core::PpuTiming::kCyclesPerFrame,
         "runtime frame advances scheduler by one frame budget");
  expect(frame.rendered_scanlines == gba::core::PpuRenderer::kScreenHeight,
         "runtime renders full framebuffer");
  expect(runtime.pixel(0, 0) == 0x1234, "runtime framebuffer exposes rendered pixel");
  expect(frame.audio_samples >= 3, "runtime drains seeded audio samples");
  expect(frame.audio_underruns == 0, "runtime reports no underrun when samples exist");
  expect(!runtime.last_audio_batch().empty(), "runtime exposes last audio batch");

  // State-hash hot-path contract: per-frame hashing defaults to OFF so the
  // 16 MB ROM traversal never runs in normal gameplay; opt-in only.
  expect(!runtime.state_hash_enabled(), "state hashing defaults to disabled");
  expect(frame.state_hash == 0, "disabled hashing reports state_hash 0");
  runtime.set_state_hash_enabled(true);
  const gba::core::AndroidRuntimeFrameResult hashed =
      runtime.step_frame(kFullFrameStepBudget);
  expect(hashed.state_hash != 0, "enabled hashing reports a real state hash");
  expect(hashed.frame_complete, "hashed full-budget frame reports frame complete");
  runtime.set_state_hash_enabled(false);
  expect(!runtime.state_hash_enabled(), "state hashing flag restores after opt-in");

  // T8, designed-silent side: with the APU master enable OFF an empty sample
  // batch is intentional, so it must not count as an underrun.
  runtime.session().apu().write_soundcnt_x(0x0000);
  while (runtime.session().apu().pop_audio_sample().has_value()) {
  }
  const gba::core::AndroidRuntimeFrameResult silent = runtime.step_frame(1);
  expect(silent.audio_samples == 0, "master-disabled frame produces no samples");
  expect(silent.audio_underruns == 0,
         "designed-silent frame does not count as audio underrun");

  // T8, audio-expected side: identical drained-buffer frame with the master
  // enable restored must report the underrun.
  runtime.session().apu().write_soundcnt_x(0x0080);
  const gba::core::AndroidRuntimeFrameResult expected_audio_underrun =
      runtime.step_frame(1);
  expect(expected_audio_underrun.audio_samples == 0,
         "master-enabled drained frame produces no samples");
  expect(expected_audio_underrun.audio_underruns == 1,
         "audio-expected frame counts an underrun when buffer is empty");

  const std::uint64_t cycles_before_bounded =
      runtime.session().scheduler().scheduler_cycles();
  const gba::core::AndroidRuntimeFrameResult cycle_bounded =
      runtime.step_frame(1);
  const std::uint64_t cycle_delta_bounded =
      cycle_bounded.run.scheduler_cycles - cycles_before_bounded;
  expect(cycle_bounded.status == AndroidRuntimeStatus::ok,
         "cycle-bounded frame keeps ok status");
  expect(!cycle_bounded.frame_complete,
         "cycle-bounded frame reports frame incomplete");
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
  expect(!fetch_failed.frame_complete,
         "abnormally stopped frame reports frame incomplete");
  expect(fetch_failed.rendered_scanlines == 0,
         "fetch failure skips scanline render");

  expect(runtime.load_rom(rom) == AndroidRuntimeStatus::ok,
         "reload clears prior failure state");
  expect(runtime.pixel(0, 0) == 0, "reload clears framebuffer presentation");

  // T18: a KEYCNT-enabled press applied through the runtime entry point must
  // poll the keypad IRQ line immediately (previously the direct keypad mask
  // mutation bypassed polling until some later core step).
  runtime.session().keypad().write_keycnt(
      static_cast<std::uint16_t>(gba::core::Keypad::kIrqEnable |
                                 static_cast<std::uint16_t>(gba::core::KeypadButton::a)));
  expect(!runtime.session().interrupts().requested(gba::core::InterruptSource::keypad),
         "keypad IRQ idle before input");
  expect(runtime.set_button_mask(static_cast<std::uint16_t>(gba::core::KeypadButton::a)) ==
             AndroidRuntimeStatus::ok,
         "KEYCNT-enabled press accepted through runtime entry point");
  expect(runtime.session().interrupts().requested(gba::core::InterruptSource::keypad),
         "KEYCNT-enabled press raises keypad IRQ through runtime entry point");

  test_hblank_scroll_rendering(false);
  test_hblank_scroll_rendering(true);
  test_raster_presentation_lifecycle();
  std::cout << "android_runtime_test: PASS\n";
  return 0;
}
