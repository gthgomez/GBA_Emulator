#include "gba/core/android_runtime.hpp"

#include "gba/core/arm7tdmi.hpp"
#include "gba/core/bios.hpp"
#include "gba/core/io_registers.hpp"
#include "gba/core/ppu_timing.hpp"

#include <array>
#include <optional>
#include <vector>

namespace gba::core {
namespace {

[[nodiscard]] std::uint32_t crc32_rgb565_framebuffer(
    const PpuRenderer::Framebuffer& framebuffer) {
  std::uint32_t crc = 0xFFFFFFFFU;
  for (const std::uint16_t pixel : framebuffer) {
    const std::uint32_t value = static_cast<std::uint32_t>(pixel);
    crc ^= value;
    for (int bit = 0; bit < 16; ++bit) {
      const std::uint32_t mask = static_cast<std::uint32_t>(-(crc & 1U));
      crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }
  return crc ^ 0xFFFFFFFFU;
}

[[nodiscard]] AndroidRuntimeVideoDiagnostics analyze_framebuffer(
    const PpuRenderer::Framebuffer& framebuffer, const PpuRenderControl& control) {
  AndroidRuntimeVideoDiagnostics diagnostics{};
  diagnostics.dispcnt = control.dispcnt;
  diagnostics.forced_blank = (control.dispcnt & 0x0080U) != 0U;
  diagnostics.bg_enabled_mask =
      static_cast<std::uint8_t>((control.dispcnt >> 8U) & 0x1FU);

  std::vector<std::uint32_t> color_counts(1U << 16, 0U);
  std::uint32_t max_count = 0;
  std::uint16_t dominant_color = 0;
  for (const std::uint16_t pixel : framebuffer) {
    if (pixel != 0U) {
      ++diagnostics.non_zero_pixel_count;
    }
    const std::uint32_t index = pixel;
    const std::uint32_t next = color_counts.at(index) + 1U;
    color_counts.at(index) = next;
    if (next > max_count) {
      max_count = next;
      dominant_color = pixel;
    }
  }
  diagnostics.dominant_color_rgb565 = dominant_color;

  std::uint32_t unique = 0;
  for (const std::uint32_t count : color_counts) {
    if (count > 0U) {
      ++unique;
    }
  }
  diagnostics.unique_color_count = unique;
  diagnostics.dominant_color_ratio =
      framebuffer.empty()
          ? 0.0F
          : static_cast<float>(max_count) /
                static_cast<float>(PpuRenderer::kFramebufferPixels);

  constexpr std::size_t kCenterX = PpuRenderer::kScreenWidth / 2U;
  constexpr std::size_t kCenterY = PpuRenderer::kScreenHeight / 2U;
  const std::size_t center_index =
      static_cast<std::size_t>(kCenterY) * PpuRenderer::kScreenWidth + kCenterX;
  diagnostics.sample_rgb565 = framebuffer.at(center_index);
  diagnostics.framebuffer_crc32 = crc32_rgb565_framebuffer(framebuffer);
  diagnostics.uniform_backdrop =
      diagnostics.unique_color_count <= 1U ||
      (diagnostics.dominant_color_ratio >= 0.99F &&
       diagnostics.dominant_color_rgb565 == diagnostics.sample_rgb565);
  return diagnostics;
}

}  // namespace

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
  const std::optional<GamePakSaveType> save_type =
      session_.memory().detect_game_pak_save_type();
  const GamePakSaveType configured_save =
      save_type.value_or(GamePakSaveType::none);
  if (!session_.memory().configure_game_pak_save(configured_save)) {
    session_.reset();
    return AndroidRuntimeStatus::rom_rejected;
  }
  session_.configure_for_game_boot();
  renderer_.clear();
  last_audio_batch_.clear();
  render_control_ = session_.ppu().render_control();
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

namespace {

void record_fetch_trace(AndroidRuntimeUnsupportedDump* dump,
                        const CoreSchedulerFetchStepResult& fetch) {
  if (dump == nullptr || !fetch.instruction.has_value()) {
    return;
  }
  AndroidRuntimeFetchTraceEntry entry{};
  entry.pc = fetch.fetch_address;
  entry.thumb = fetch.instruction_set == CoreInstructionSet::thumb;
  entry.insn = fetch.instruction.value();
  dump->recent_fetches.at(dump->fetch_trace_count % dump->recent_fetches.size()) = entry;
  ++dump->fetch_trace_count;
}

void fill_unsupported_dump(AndroidRuntimeUnsupportedDump& dump, const CoreSession& session) {
  const Arm7tdmi& cpu = session.cpu();
  const MemoryBus& memory = session.memory();
  const CoreSchedulerState scheduler_state = session.scheduler().save_state();
  dump.final_pc = cpu.register_value(Arm7tdmi::kPc);
  dump.thumb = cpu.thumb_state();
  dump.cpu_mode = static_cast<std::uint8_t>(cpu.current_mode());
  dump.cpsr = cpu.cpsr();
  dump.irq_spsr = cpu.spsr();
  dump.hle_irq_return_lr_set = scheduler_state.hle_irq_return_lr.has_value();
  dump.hle_irq_return_lr =
      scheduler_state.hle_irq_return_lr.value_or(0);
  dump.user_irq_handler = memory.read32(BiosHleConstants::kUserIrqHandlerPointer);
  dump.ime = session.interrupts().ime();
  dump.ie = session.interrupts().interrupt_enable();
  dump.interrupt_flags = session.interrupts().interrupt_flags();
  const std::optional<std::uint16_t> dispcnt = memory.read16(IoRegisters::kDispcnt);
  dump.dispcnt = dispcnt.value_or(0);
  if (dump.thumb) {
    const std::optional<std::uint16_t> halfword = memory.read16(dump.final_pc);
    dump.raw_insn =
        halfword.has_value() ? static_cast<std::uint32_t>(halfword.value()) : 0xFFFFFFFFU;
  } else {
    dump.raw_insn = memory.read32(dump.final_pc).value_or(0xFFFFFFFFU);
  }
}

}  // namespace

AndroidRuntimeFrameResult AndroidRuntime::step_frame_with_fetch_trace(
    std::uint32_t max_steps, AndroidRuntimeUnsupportedDump* dump_on_unsupported) {
  if (dump_on_unsupported != nullptr) {
    dump_on_unsupported->fetch_trace_count = 0;
  }
  if (max_steps == 0) {
    return {AndroidRuntimeStatus::invalid_argument};
  }

  AndroidRuntimeFrameResult result{};
  const std::uint64_t scheduler_cycles = session_.scheduler().scheduler_cycles();
  const std::uint64_t frame_index =
      scheduler_cycles / static_cast<std::uint64_t>(PpuTiming::kCyclesPerFrame);
  const std::uint64_t cycle_start =
      frame_index * static_cast<std::uint64_t>(PpuTiming::kCyclesPerFrame);
  const std::uint64_t cycle_target =
      cycle_start + static_cast<std::uint64_t>(PpuTiming::kCyclesPerFrame);

  result.run.requested_steps = max_steps;
  result.run.stop_reason = CoreRunStopReason::max_steps;

  while (session_.scheduler().scheduler_cycles() < cycle_target &&
         result.run.executed_steps < max_steps) {
    const CoreSchedulerFetchStepResult fetch = session_.step();
    record_fetch_trace(dump_on_unsupported, fetch);
    if (fetch.fetch_failed) {
      ++result.run.fetch_failures;
      result.run.stop_reason = CoreRunStopReason::fetch_failed;
      break;
    }
    ++result.run.attempted_steps;
    if (!fetch.instruction.has_value()) {
      ++result.run.skipped_steps;
      continue;
    }
    if (fetch.step.has_value() &&
        fetch.step->cpu_step.status == ExecuteStatus::unsupported) {
      ++result.run.unsupported_steps;
      result.run.stop_reason = CoreRunStopReason::unsupported_instruction;
      if (dump_on_unsupported != nullptr) {
        fill_unsupported_dump(*dump_on_unsupported, session_);
      }
      break;
    }
    ++result.run.executed_steps;
  }

  result.run.scheduler_cycles = session_.scheduler().scheduler_cycles();
  result.scheduler_cycles_delta = result.run.scheduler_cycles - cycle_start;
  result.run.final_pc = session_.cpu().register_value(Arm7tdmi::kPc);

  const bool frame_cycle_budget_met = result.run.scheduler_cycles >= cycle_target;
  const bool abnormal_stop =
      result.run.stop_reason == CoreRunStopReason::fetch_failed ||
      result.run.stop_reason == CoreRunStopReason::unsupported_instruction;
  if (frame_cycle_budget_met && !abnormal_stop) {
    const PpuRenderControl control = session_.ppu().render_control();
    render_control_ = control;
    for (std::uint16_t y = 0; y < PpuRenderer::kScreenHeight; ++y) {
      const PpuRenderStats stats =
          renderer_.render_scanline(session_.memory(), control, y);
      if (stats.supported_mode) {
        ++result.rendered_scanlines;
      }
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
  render_control_ = session_.ppu().render_control();
  return result;
}

AndroidRuntimeFrameResult AndroidRuntime::step_frame(std::uint32_t max_steps) {
  return step_frame_with_fetch_trace(max_steps, nullptr);
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

AndroidRuntimeVideoDiagnostics AndroidRuntime::video_diagnostics() const {
  return analyze_framebuffer(renderer_.framebuffer(), render_control_);
}

const std::vector<ApuMixedSample>& AndroidRuntime::last_audio_batch() const {
  return last_audio_batch_;
}

}  // namespace gba::core
