#include "gba/core/android_runtime.hpp"

#include "gba/core/arm7tdmi.hpp"

#include <optional>

namespace gba::core {

AndroidRuntime::AndroidRuntime()
    : render_control_{0, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}} {}

AndroidRuntimeStatus AndroidRuntime::reset() {
  session_.reset();
  renderer_.clear();
  last_audio_batch_.clear();
  return AndroidRuntimeStatus::ok;
}

AndroidRuntimeStatus AndroidRuntime::load_rom(const std::vector<std::uint8_t>& rom) {
  if (rom.empty()) {
    return AndroidRuntimeStatus::invalid_argument;
  }
  session_.reset();
  if (!session_.memory().load_game_pak_rom(rom)) {
    return AndroidRuntimeStatus::rom_rejected;
  }
  session_.cpu().set_register(Arm7tdmi::kPc, 0x08000000U);
  return AndroidRuntimeStatus::ok;
}

AndroidRuntimeStatus AndroidRuntime::set_button_mask(std::uint16_t pressed_mask) {
  return session_.keypad().set_pressed_mask(pressed_mask)
             ? AndroidRuntimeStatus::ok
             : AndroidRuntimeStatus::input_rejected;
}

void AndroidRuntime::set_render_control(const PpuRenderControl& control) {
  render_control_ = control;
}

AndroidRuntimeFrameResult AndroidRuntime::step_frame(std::uint32_t max_steps) {
  if (max_steps == 0) {
    return {AndroidRuntimeStatus::invalid_argument};
  }

  AndroidRuntimeFrameResult result{};
  result.run = session_.run(max_steps);
  for (std::uint16_t y = 0; y < PpuRenderer::kScreenHeight; ++y) {
    const PpuRenderStats stats = renderer_.render_scanline(session_.memory(),
                                                           render_control_, y);
    if (stats.supported_mode) {
      ++result.rendered_scanlines;
    }
  }

  last_audio_batch_.clear();
  while (true) {
    const std::optional<ApuMixedSample> sample = session_.apu().pop_audio_sample();
    if (!sample.has_value()) {
      break;
    }
    last_audio_batch_.push_back(sample.value());
  }
  result.audio_samples = static_cast<std::uint32_t>(last_audio_batch_.size());
  result.audio_underruns = result.audio_samples == 0 ? 1U : 0U;
  result.state_hash = session_.state_hash();
  return result;
}

CoreSession& AndroidRuntime::session() {
  return session_;
}

const CoreSession& AndroidRuntime::session() const {
  return session_;
}

const PpuRenderer::Framebuffer& AndroidRuntime::framebuffer() const {
  return renderer_.framebuffer();
}

std::uint16_t AndroidRuntime::pixel(std::uint16_t x, std::uint16_t y) const {
  return renderer_.pixel(x, y);
}

const std::vector<ApuMixedSample>& AndroidRuntime::last_audio_batch() const {
  return last_audio_batch_;
}

}  // namespace gba::core
