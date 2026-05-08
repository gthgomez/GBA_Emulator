#pragma once

#include "gba/core/core_session.hpp"
#include "gba/core/ppu_renderer.hpp"

#include <cstdint>
#include <vector>

namespace gba::core {

enum class AndroidRuntimeStatus : std::uint8_t {
  ok,
  invalid_argument,
  rom_rejected,
  input_rejected,
};

struct AndroidRuntimeFrameResult {
  AndroidRuntimeStatus status = AndroidRuntimeStatus::ok;
  CoreSchedulerRunResult run = {};
  std::uint16_t rendered_scanlines = 0;
  std::uint32_t audio_samples = 0;
  std::uint32_t audio_underruns = 0;
  std::uint64_t state_hash = 0;
};

class AndroidRuntime {
 public:
  AndroidRuntime();

  [[nodiscard]] AndroidRuntimeStatus reset();
  [[nodiscard]] AndroidRuntimeStatus load_rom(const std::vector<std::uint8_t>& rom);
  [[nodiscard]] AndroidRuntimeStatus set_button_mask(std::uint16_t pressed_mask);
  void set_render_control(const PpuRenderControl& control);
  [[nodiscard]] AndroidRuntimeFrameResult step_frame(std::uint32_t max_steps);

  [[nodiscard]] CoreSession& session();
  [[nodiscard]] const CoreSession& session() const;
  [[nodiscard]] const PpuRenderer::Framebuffer& framebuffer() const;
  [[nodiscard]] std::uint16_t pixel(std::uint16_t x, std::uint16_t y) const;
  [[nodiscard]] const std::vector<ApuMixedSample>& last_audio_batch() const;

 private:
  CoreSession session_;
  PpuRenderer renderer_;
  PpuRenderControl render_control_;
  std::vector<ApuMixedSample> last_audio_batch_;
};

}  // namespace gba::core
