#pragma once

#include "gba/core/core_session.hpp"
#include "gba/core/ppu_renderer.hpp"

#include <array>
#include <cstdint>
#include <optional>
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
  std::uint64_t scheduler_cycles_delta = 0;
};

struct AndroidRuntimeFetchTraceEntry {
  std::uint32_t pc = 0;
  std::uint32_t insn = 0;
  bool thumb = false;
};

struct AndroidRuntimeUnsupportedDump {
  std::uint32_t final_pc = 0;
  std::uint32_t raw_insn = 0;
  bool thumb = false;
  std::uint8_t cpu_mode = 0;
  std::uint32_t cpsr = 0;
  std::optional<std::uint32_t> irq_spsr;
  bool hle_irq_return_lr_set = false;
  std::uint32_t hle_irq_return_lr = 0;
  std::optional<std::uint32_t> user_irq_handler;
  std::uint16_t ime = 0;
  std::uint16_t ie = 0;
  std::uint16_t interrupt_flags = 0;
  std::uint16_t dispcnt = 0;
  std::array<AndroidRuntimeFetchTraceEntry, 8> recent_fetches{};
  std::size_t fetch_trace_count = 0;
};

struct AndroidRuntimeVideoDiagnostics {
  std::uint16_t dispcnt = 0;
  bool forced_blank = false;
  std::uint8_t bg_enabled_mask = 0;
  std::uint32_t non_zero_pixel_count = 0;
  std::uint16_t sample_rgb565 = 0;
  std::uint32_t unique_color_count = 0;
  std::uint16_t dominant_color_rgb565 = 0;
  float dominant_color_ratio = 0.0F;
  std::uint32_t framebuffer_crc32 = 0;
  bool uniform_backdrop = false;
};

class AndroidRuntime {
 public:
  AndroidRuntime();

  [[nodiscard]] AndroidRuntimeStatus reset();
  [[nodiscard]] AndroidRuntimeStatus load_rom(const std::vector<std::uint8_t>& rom);
  [[nodiscard]] AndroidRuntimeStatus set_button_mask(std::uint16_t pressed_mask);
  void set_render_control(const PpuRenderControl& control);
  [[nodiscard]] AndroidRuntimeFrameResult step_frame(std::uint32_t max_steps);
  [[nodiscard]] AndroidRuntimeFrameResult step_frame_with_fetch_trace(
      std::uint32_t max_steps, AndroidRuntimeUnsupportedDump* dump_on_unsupported);

  [[nodiscard]] CoreSession& session();
  [[nodiscard]] const CoreSession& session() const;
  [[nodiscard]] const PpuRenderer::Framebuffer& framebuffer() const;
  [[nodiscard]] std::uint16_t pixel(std::uint16_t x, std::uint16_t y) const;
  [[nodiscard]] AndroidRuntimeVideoDiagnostics video_diagnostics() const;
  [[nodiscard]] const std::vector<ApuMixedSample>& last_audio_batch() const;

 private:
  CoreSession session_;
  PpuRenderer renderer_;
  PpuRenderControl render_control_;
  std::vector<ApuMixedSample> last_audio_batch_;
};

}  // namespace gba::core
