#include "gba/core/android_runtime.hpp"
#include "gba/core/memory_bus.hpp"
#include "gba/core/ppu_timing.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

constexpr std::size_t kMaxRomBytes = 32U * 1024U * 1024U;
constexpr std::uint32_t kDefaultMaxStepsPerFrame = 2'000'000U;
constexpr std::uint32_t kDefaultFrames = 600U;
constexpr std::uint32_t kCyclesPerFrame = gba::core::PpuTiming::kCyclesPerFrame;
constexpr std::uint16_t kMinScanlines = gba::core::PpuRenderer::kScreenHeight;

struct ThresholdRule {
  std::uint32_t frame = 0;
  std::uint32_t value = 0;
};

struct Options {
  std::string rom_path;
  std::uint32_t frames = kDefaultFrames;
  std::uint32_t max_steps_per_frame = kDefaultMaxStepsPerFrame;
  bool require_valid_header = false;
  bool json = false;
  bool require_frame_complete = false;
  std::uint32_t require_not_uniform_after_frame = 0;
  std::uint16_t require_scanlines = 0;
  std::vector<ThresholdRule> require_nonzero_after;
  std::vector<ThresholdRule> require_dispcnt_after;
  std::vector<ThresholdRule> require_unique_colors_after;
  std::unordered_set<std::uint32_t> check_frames;
};

[[nodiscard]] bool parse_threshold_list(const std::string& text,
                                        std::vector<ThresholdRule>& rules) {
  std::stringstream stream(text);
  std::string token;
  while (std::getline(stream, token, ',')) {
    const std::size_t colon = token.find(':');
    if (colon == std::string::npos) {
      return false;
    }
    ThresholdRule rule{};
    rule.frame = static_cast<std::uint32_t>(std::stoul(token.substr(0, colon), nullptr, 0));
    rule.value = static_cast<std::uint32_t>(std::stoul(token.substr(colon + 1), nullptr, 0));
    rules.push_back(rule);
  }
  return true;
}

[[nodiscard]] bool parse_frame_list(const std::string& text,
                                    std::unordered_set<std::uint32_t>& frames) {
  std::stringstream stream(text);
  std::string token;
  while (std::getline(stream, token, ',')) {
    frames.insert(static_cast<std::uint32_t>(std::stoul(token)));
  }
  return !frames.empty();
}

void print_usage() {
  std::cerr
      << "Usage: rom_video_smoke.exe --rom <path> [--frames N] [--max-steps-per-frame N]\n"
      << "       [--require-valid-header] [--require-frame-complete]\n"
      << "       [--require-scanlines 160] [--require-nonzero-after FRAME:MIN]\n"
      << "       [--require-dispcnt-after FRAME:MASK] [--require-unique-colors-after FRAME:MIN]\n"
      << "       [--require-not-uniform-after FRAME] [--check-frame 8,60,120] [--json]\n";
}

[[nodiscard]] bool parse_args(int argc, char** argv, Options& options) {
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--rom" && index + 1 < argc) {
      options.rom_path = argv[++index];
      continue;
    }
    if (arg == "--frames" && index + 1 < argc) {
      options.frames = static_cast<std::uint32_t>(std::stoul(argv[++index]));
      continue;
    }
    if (arg == "--max-steps-per-frame" && index + 1 < argc) {
      options.max_steps_per_frame = static_cast<std::uint32_t>(std::stoul(argv[++index]));
      continue;
    }
    if (arg == "--require-valid-header") {
      options.require_valid_header = true;
      continue;
    }
    if (arg == "--require-frame-complete") {
      options.require_frame_complete = true;
      continue;
    }
    if (arg == "--require-scanlines" && index + 1 < argc) {
      options.require_scanlines =
          static_cast<std::uint16_t>(std::stoul(argv[++index]));
      continue;
    }
    if (arg == "--require-not-uniform-after" && index + 1 < argc) {
      options.require_not_uniform_after_frame =
          static_cast<std::uint32_t>(std::stoul(argv[++index]));
      continue;
    }
    if (arg == "--require-nonzero-after" && index + 1 < argc) {
      if (!parse_threshold_list(argv[++index], options.require_nonzero_after)) {
        return false;
      }
      continue;
    }
    if (arg == "--require-dispcnt-after" && index + 1 < argc) {
      if (!parse_threshold_list(argv[++index], options.require_dispcnt_after)) {
        return false;
      }
      continue;
    }
    if (arg == "--require-unique-colors-after" && index + 1 < argc) {
      if (!parse_threshold_list(argv[++index], options.require_unique_colors_after)) {
        return false;
      }
      continue;
    }
    if (arg == "--check-frame" && index + 1 < argc) {
      if (!parse_frame_list(argv[++index], options.check_frames)) {
        return false;
      }
      continue;
    }
    if (arg == "--json") {
      options.json = true;
      continue;
    }
    if (arg == "--help" || arg == "-h") {
      print_usage();
      return false;
    }
    std::cerr << "Unknown argument: " << arg << '\n';
    print_usage();
    return false;
  }
  if (options.rom_path.empty()) {
    print_usage();
    return false;
  }
  if (options.check_frames.empty()) {
    options.check_frames = {1, 8, 60, 120};
  }
  return true;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> read_rom_file(
    const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  input.seekg(0, std::ios::end);
  const std::streamoff size = input.tellg();
  if (size <= 0 || static_cast<std::size_t>(size) > kMaxRomBytes) {
    return std::nullopt;
  }
  if ((static_cast<std::size_t>(size) & 0x1U) != 0U) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.seekg(0, std::ios::beg);
  if (!input.read(reinterpret_cast<char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()))) {
    return std::nullopt;
  }
  return bytes;
}

[[nodiscard]] bool header_is_valid(const gba::core::MemoryBus& memory) {
  return memory.cartridge_header_is_valid();
}

[[nodiscard]] bool complement_is_valid(const std::vector<std::uint8_t>& rom) {
  return gba::core::MemoryBus::cartridge_complement_valid(rom);
}

[[nodiscard]] bool frame_complete(const gba::core::AndroidRuntimeFrameResult& frame) {
  if (frame.status != gba::core::AndroidRuntimeStatus::ok) {
    return false;
  }
  if (frame.run.stop_reason == gba::core::CoreRunStopReason::fetch_failed ||
      frame.run.stop_reason == gba::core::CoreRunStopReason::unsupported_instruction) {
    return false;
  }
  return frame.scheduler_cycles_delta >= kCyclesPerFrame;
}

void print_checkpoint(std::uint32_t frame_index,
                      const gba::core::AndroidRuntimeFrameResult& frame,
                      const gba::core::AndroidRuntimeVideoDiagnostics& video,
                      std::uint32_t palette_nonzero,
                      bool json) {
  if (json) {
    std::cout << "    {\n"
              << "      \"frame\": " << frame_index << ",\n"
              << "      \"frame_complete\": " << (frame_complete(frame) ? "true" : "false")
              << ",\n"
              << "      \"scheduler_cycles_delta\": " << frame.scheduler_cycles_delta << ",\n"
              << "      \"rendered_scanlines\": " << frame.rendered_scanlines << ",\n"
              << "      \"dispcnt\": " << video.dispcnt << ",\n"
              << "      \"forced_blank\": " << (video.forced_blank ? "true" : "false") << ",\n"
              << "      \"bg_enabled_mask\": " << static_cast<unsigned>(video.bg_enabled_mask)
              << ",\n"
              << "      \"non_zero_pixel_count\": " << video.non_zero_pixel_count << ",\n"
              << "      \"center_rgb565\": " << video.sample_rgb565 << ",\n"
              << "      \"unique_color_count\": " << video.unique_color_count << ",\n"
              << "      \"dominant_color_rgb565\": " << video.dominant_color_rgb565 << ",\n"
              << "      \"dominant_color_ratio\": " << video.dominant_color_ratio << ",\n"
              << "      \"uniform_backdrop\": " << (video.uniform_backdrop ? "true" : "false")
              << ",\n"
              << "      \"framebuffer_crc32\": " << video.framebuffer_crc32 << ",\n"
              << "      \"palette_nonzero_count\": " << palette_nonzero << ",\n"
              << "      \"final_pc\": \"0x" << std::hex << frame.run.final_pc << std::dec
              << "\"\n"
              << "    }";
    return;
  }
  std::cout << "checkpoint frame=" << frame_index
            << " complete=" << (frame_complete(frame) ? "yes" : "no")
            << " cycles_delta=" << frame.scheduler_cycles_delta
            << " steps=" << frame.run.executed_steps
            << " scanlines=" << frame.rendered_scanlines
            << " pc=0x" << std::hex << frame.run.final_pc << std::dec
            << " dispcnt=0x" << std::hex << video.dispcnt << std::dec
            << " unique=" << video.unique_color_count
            << " dominant=0x" << std::hex << video.dominant_color_rgb565 << std::dec
            << " ratio=" << video.dominant_color_ratio
            << " crc=0x" << std::hex << video.framebuffer_crc32 << std::dec << '\n';
}

[[nodiscard]] std::optional<std::string> evaluate_thresholds(
    std::uint32_t frame_index,
    const gba::core::AndroidRuntimeFrameResult& frame,
    const gba::core::AndroidRuntimeVideoDiagnostics& video,
    const Options& options) {
  if (options.require_frame_complete && !frame_complete(frame)) {
    return "frame " + std::to_string(frame_index) + ": frame incomplete";
  }
  if (options.require_scanlines != 0 &&
      frame.rendered_scanlines < options.require_scanlines) {
    return "frame " + std::to_string(frame_index) + ": rendered_scanlines=" +
           std::to_string(frame.rendered_scanlines);
  }
  if (options.require_not_uniform_after_frame != 0 &&
      frame_index >= options.require_not_uniform_after_frame && video.uniform_backdrop) {
    return "frame " + std::to_string(frame_index) + ": uniform backdrop";
  }
  for (const ThresholdRule& rule : options.require_nonzero_after) {
    if (frame_index >= rule.frame &&
        video.non_zero_pixel_count < rule.value) {
      return "frame " + std::to_string(frame_index) + ": non_zero_pixel_count=" +
             std::to_string(video.non_zero_pixel_count);
    }
  }
  for (const ThresholdRule& rule : options.require_dispcnt_after) {
    if (frame_index == rule.frame &&
        (video.dispcnt & static_cast<std::uint16_t>(rule.value)) == 0) {
      return "frame " + std::to_string(frame_index) + ": dispcnt=0x" +
             [&]() {
               std::ostringstream out;
               out << std::hex << video.dispcnt;
               return out.str();
             }();
    }
  }
  for (const ThresholdRule& rule : options.require_unique_colors_after) {
    if (frame_index >= rule.frame && video.unique_color_count < rule.value) {
      return "frame " + std::to_string(frame_index) + ": unique_color_count=" +
             std::to_string(video.unique_color_count);
    }
  }
  return std::nullopt;
}

void print_unsupported_dump(std::uint32_t frame_index,
                            const gba::core::AndroidRuntimeUnsupportedDump& dump) {
  std::cerr << "unsupported_instruction dump (frame " << frame_index << "):\n"
            << "  final_pc=0x" << std::hex << dump.final_pc << std::dec
            << " thumb=" << (dump.thumb ? "1" : "0") << " mode=0x" << std::hex
            << static_cast<unsigned>(dump.cpu_mode) << std::dec << " cpsr=0x" << std::hex
            << dump.cpsr << std::dec << '\n'
            << "  raw_insn=0x" << std::hex << dump.raw_insn << std::dec << " irq_spsr="
            << (dump.irq_spsr.has_value()
                    ? [&]() {
                        std::ostringstream out;
                        out << "0x" << std::hex << dump.irq_spsr.value();
                        return out.str();
                      }()
                    : "none")
            << " hle_irq_return_lr="
            << (dump.hle_irq_return_lr_set
                    ? [&]() {
                        std::ostringstream out;
                        out << "0x" << std::hex << dump.hle_irq_return_lr;
                        return out.str();
                      }()
                    : "unset")
            << std::dec << '\n'
            << "  user_irq_handler@0x03007FFC=0x"
            << (dump.user_irq_handler.has_value()
                    ? [&]() {
                        std::ostringstream out;
                        out << std::hex << dump.user_irq_handler.value();
                        return out.str();
                      }()
                    : "read_fail")
            << std::dec << '\n'
            << "  IME=0x" << dump.ime << " IE=0x" << std::hex << dump.ie << " IF=0x"
            << dump.interrupt_flags << std::dec << " DISPCNT=0x" << std::hex << dump.dispcnt
            << std::dec << '\n'
            << "  last_fetch_trace (pc, thumb, insn):\n";

  const std::size_t count =
      std::min(dump.fetch_trace_count, dump.recent_fetches.size());
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t slot =
        (dump.fetch_trace_count - count + index) % dump.recent_fetches.size();
    const gba::core::AndroidRuntimeFetchTraceEntry& entry = dump.recent_fetches.at(slot);
    std::cerr << "    0x" << std::hex << entry.pc << std::dec << " T=" << (entry.thumb ? 1 : 0)
              << " 0x" << std::hex << entry.insn << std::dec << '\n';
  }
}

[[nodiscard]] std::uint32_t count_palette_nonzero(const gba::core::MemoryBus& memory) {
  std::uint32_t count = 0;
  for (std::uint32_t offset = 0; offset < 512U; offset += 2U) {
    const std::optional<std::uint16_t> entry = memory.read16(0x05000000U + offset);
    if (entry.has_value() && entry.value() != 0U) {
      ++count;
    }
  }
  return count;
}

}  // namespace

int main(int argc, char** argv) {
  Options options{};
  if (!parse_args(argc, argv, options)) {
    return 2;
  }

  const std::optional<std::vector<std::uint8_t>> rom = read_rom_file(options.rom_path);
  if (!rom.has_value() || rom->empty()) {
    std::cerr << "FAIL: could not read ROM\n";
    return 1;
  }
  if (options.require_valid_header && !complement_is_valid(rom.value())) {
    std::cerr << "FAIL: ROM header complement invalid\n";
    return 1;
  }

  gba::core::AndroidRuntime runtime;
  if (runtime.load_rom(rom.value()) != gba::core::AndroidRuntimeStatus::ok) {
    std::cerr << "FAIL: load_rom rejected\n";
    return 1;
  }
  if (options.require_valid_header && !header_is_valid(runtime.session().memory())) {
    std::cerr << "FAIL: cartridge header invalid\n";
    return 1;
  }

  if (options.json) {
    std::cout << "{\n"
              << "  \"rom_path\": \"" << options.rom_path << "\",\n"
              << "  \"frames_requested\": " << options.frames << ",\n"
              << "  \"max_steps_per_frame\": " << options.max_steps_per_frame << ",\n"
              << "  \"checkpoints\": [\n";
  } else {
    std::cout << "rom_video_smoke: " << options.rom_path << " frames=" << options.frames
              << '\n';
  }

  bool first_checkpoint = true;
  std::string first_failure;
  bool unsupported_dump_printed = false;

  for (std::uint32_t frame_index = 0; frame_index < options.frames; ++frame_index) {
    gba::core::AndroidRuntimeUnsupportedDump unsupported_dump{};
    const gba::core::AndroidRuntimeFrameResult frame =
        runtime.step_frame_with_fetch_trace(options.max_steps_per_frame,
                                            unsupported_dump_printed ? nullptr
                                                                     : &unsupported_dump);
    if (frame.status != gba::core::AndroidRuntimeStatus::ok) {
      std::cerr << "FAIL: step_frame status error on frame " << frame_index << '\n';
      return 1;
    }
    if (frame_index == 0 &&
        frame.run.stop_reason != gba::core::CoreRunStopReason::max_steps) {
      std::cerr << "FAIL: abnormal stop on frame 0\n";
      return 1;
    }
    if (!unsupported_dump_printed &&
        frame.run.stop_reason ==
            gba::core::CoreRunStopReason::unsupported_instruction) {
      print_unsupported_dump(frame_index, unsupported_dump);
      unsupported_dump_printed = true;
    }

    const gba::core::AndroidRuntimeVideoDiagnostics video = runtime.video_diagnostics();
    if (options.check_frames.count(frame_index) != 0) {
      if (options.json && !first_checkpoint) {
        std::cout << ",\n";
      }
      print_checkpoint(frame_index, frame, video,
                       count_palette_nonzero(runtime.session().memory()), options.json);
      first_checkpoint = false;
    }

    if (const std::optional<std::string> failure =
            evaluate_thresholds(frame_index, frame, video, options);
        failure.has_value() && first_failure.empty()) {
      first_failure = failure.value();
    }
  }

  if (options.json) {
    std::cout << "\n  ],\n"
              << "  \"result\": \"" << (first_failure.empty() ? "pass" : "fail") << "\",\n"
              << "  \"failure\": \"" << first_failure << "\"\n"
              << "}\n";
  } else if (!first_failure.empty()) {
    std::cerr << "FAIL: " << first_failure << '\n';
  } else {
    std::cout << "rom_video_smoke: PASS\n";
  }

  return first_failure.empty() ? 0 : 1;
}
