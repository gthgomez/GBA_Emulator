#include "gba/core/android_runtime.hpp"
#include "gba/core/arm7tdmi.hpp"
#include "gba/core/core_session.hpp"
#include "gba/core/core_scheduler.hpp"
#include "gba/core/memory_bus.hpp"
#include "gba/core/ppu_timing.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t kMaxRomBytes = 32U * 1024U * 1024U;
constexpr std::uint32_t kDefaultMaxStepsPerFrame = 500'000U;
constexpr std::uint32_t kDefaultFrames = 300U;

[[nodiscard]] const char* execute_status_name(gba::core::ExecuteStatus status) {
  using gba::core::ExecuteStatus;
  switch (status) {
    case ExecuteStatus::executed:
      return "executed";
    case ExecuteStatus::skipped_condition:
      return "skipped_condition";
    case ExecuteStatus::unsupported:
      return "unsupported";
  }
  return "unknown";
}

[[nodiscard]] std::string_view stop_reason_name(gba::core::CoreRunStopReason reason) {
  using gba::core::CoreRunStopReason;
  switch (reason) {
    case CoreRunStopReason::max_steps:
      return "max_steps";
    case CoreRunStopReason::fetch_failed:
      return "fetch_failed";
    case CoreRunStopReason::unsupported_instruction:
      return "unsupported_instruction";
  }
  return "unknown";
}

[[nodiscard]] bool header_is_valid(const gba::core::MemoryBus& memory) {
  return memory.cartridge_header_is_valid();
}

[[nodiscard]] bool complement_is_valid(const std::vector<std::uint8_t>& rom) {
  return gba::core::MemoryBus::cartridge_complement_valid(rom);
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

void print_usage() {
  std::cerr
      << "Usage: rom_smoke.exe --rom <path> [--frames N] [--max-steps-per-frame N]\n"
      << "       [--trace-steps N] [--require-valid-header] [--fail-on-unsupported]\n"
      << "       [--json]\n";
}

void print_trace_steps(gba::core::CoreSession& session, std::uint32_t trace_steps) {
  for (std::uint32_t index = 0; index < trace_steps; ++index) {
    const gba::core::CoreSchedulerFetchStepResult step = session.step();
    std::cout << "rom_smoke_trace: index=" << index
              << " set=" << (step.instruction_set == gba::core::CoreInstructionSet::arm ? "arm"
                                                                                        : "thumb")
              << " pc=0x" << std::hex << step.fetch_address;
    if (step.instruction.has_value()) {
      std::cout << " instruction=0x" << step.instruction.value();
    } else {
      std::cout << " instruction=null";
    }
    std::cout << std::dec << " fetch_failed=" << (step.fetch_failed ? "true" : "false");
    if (step.step.has_value()) {
      std::cout << " status=" << execute_status_name(step.step->cpu_step.status)
                << " elapsed=" << step.step->cpu_step.elapsed_cycles << " next_pc=0x"
                << std::hex << session.cpu().register_value(gba::core::Arm7tdmi::kPc) << std::dec;
    }
    std::cout << '\n';
    if (step.fetch_failed || !step.step.has_value() ||
        step.step->cpu_step.status == gba::core::ExecuteStatus::unsupported) {
      break;
    }
  }
}

struct Options {
  std::string rom_path;
  std::uint32_t frames = kDefaultFrames;
  std::uint32_t max_steps_per_frame = kDefaultMaxStepsPerFrame;
  std::uint32_t trace_steps = 0;
  bool require_valid_header = false;
  bool fail_on_unsupported = false;
  bool json = false;
};

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
    if (arg == "--trace-steps" && index + 1 < argc) {
      options.trace_steps = static_cast<std::uint32_t>(std::stoul(argv[++index]));
      continue;
    }
    if (arg == "--require-valid-header") {
      options.require_valid_header = true;
      continue;
    }
    if (arg == "--fail-on-unsupported") {
      options.fail_on_unsupported = true;
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
  return true;
}

void print_frame_line(std::uint32_t frame_index,
                      const gba::core::AndroidRuntimeFrameResult& frame,
                      bool json) {
  if (json) {
    std::cout << "    {\n"
              << "      \"frame\": " << frame_index << ",\n"
              << "      \"status\": \"ok\",\n"
              << "      \"executed_steps\": " << frame.run.executed_steps << ",\n"
              << "      \"scheduler_cycles\": " << frame.run.scheduler_cycles << ",\n"
              << "      \"rendered_scanlines\": " << frame.rendered_scanlines << ",\n"
              << "      \"stop_reason\": \""
              << stop_reason_name(frame.run.stop_reason) << "\",\n"
              << "      \"final_pc\": \"0x" << std::hex << frame.run.final_pc << std::dec
              << "\",\n"
              << "      \"state_hash\": " << frame.state_hash << ",\n"
              << "      \"audio_samples\": " << frame.audio_samples << "\n"
              << "    }";
    return;
  }
  std::cout << "frame=" << frame_index << " steps=" << frame.run.executed_steps
            << " cycles=" << frame.run.scheduler_cycles
            << " scanlines=" << frame.rendered_scanlines << " stop="
            << stop_reason_name(frame.run.stop_reason) << " pc=0x" << std::hex
            << frame.run.final_pc << std::dec << " hash=" << frame.state_hash
            << " audio=" << frame.audio_samples << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  Options options{};
  if (!parse_args(argc, argv, options)) {
    return 2;
  }

  const std::optional<std::vector<std::uint8_t>> rom = read_rom_file(options.rom_path);
  if (!rom.has_value() || rom->empty()) {
    std::cerr << "FAIL: could not read ROM (empty, missing, odd size, or > 32 MiB): "
              << options.rom_path << '\n';
    return 1;
  }

  if (options.require_valid_header) {
    if (!complement_is_valid(rom.value())) {
      std::cerr << "FAIL: ROM header complement check failed\n";
      return 1;
    }
  }

  gba::core::AndroidRuntime runtime;
  if (runtime.load_rom(rom.value()) != gba::core::AndroidRuntimeStatus::ok) {
    std::cerr << "FAIL: AndroidRuntime::load_rom rejected ROM\n";
    return 1;
  }

  if (options.require_valid_header && !header_is_valid(runtime.session().memory())) {
    std::cerr << "FAIL: ROM cartridge header fixed value invalid\n";
    return 1;
  }

  const std::optional<gba::core::GamePakSaveType> save_type =
      runtime.session().memory().detect_game_pak_save_type();
  const std::optional<gba::core::CartridgeHeader> header =
      runtime.session().memory().game_pak_header();

  if (options.json) {
    std::cout << "{\n"
              << "  \"rom_path\": \"" << options.rom_path << "\",\n"
              << "  \"rom_bytes\": " << rom->size() << ",\n"
              << "  \"frames_requested\": " << options.frames << ",\n"
              << "  \"max_steps_per_frame\": " << options.max_steps_per_frame << ",\n"
              << "  \"header_valid\": "
              << (header.has_value() && header->fixed_value_valid ? "true" : "false")
              << ",\n"
              << "  \"save_type_detected\": "
              << (save_type.has_value() ? "true" : "false") << ",\n"
              << "  \"frames\": [\n";
  } else {
    std::cout << "rom_smoke: " << options.rom_path << " bytes=" << rom->size()
              << " frames=" << options.frames;
    if (options.trace_steps > 0) {
      std::cout << " trace_steps=" << options.trace_steps;
    }
    std::cout << '\n';
  }

  if (options.trace_steps > 0) {
    print_trace_steps(runtime.session(), options.trace_steps);
  }

  std::uint32_t completed_frames = 0;
  bool abnormal_stop = false;
  gba::core::CoreRunStopReason first_abnormal = gba::core::CoreRunStopReason::max_steps;

  for (std::uint32_t frame_index = 0; frame_index < options.frames; ++frame_index) {
    const gba::core::AndroidRuntimeFrameResult frame =
        runtime.step_frame(options.max_steps_per_frame);
    if (frame.status != gba::core::AndroidRuntimeStatus::ok) {
      std::cerr << "FAIL: step_frame returned invalid status on frame " << frame_index
                << '\n';
      return 1;
    }

    if (options.json && frame_index > 0) {
      std::cout << ",\n";
    }
    print_frame_line(frame_index, frame, options.json);

    ++completed_frames;
    if (frame.run.stop_reason != gba::core::CoreRunStopReason::max_steps) {
      abnormal_stop = true;
      first_abnormal = frame.run.stop_reason;
      if (frame_index == 0) {
        if (options.json) {
          std::cout << "\n  ],\n"
                    << "  \"completed_frames\": " << completed_frames << ",\n"
                    << "  \"abnormal_stop\": true,\n"
                    << "  \"first_abnormal_stop_reason\": \""
                    << stop_reason_name(first_abnormal) << "\",\n"
                    << "  \"result\": \"fail_frame_0\"\n"
                    << "}\n";
        }
        std::cerr << "FAIL: abnormal stop on frame 0: "
                  << stop_reason_name(first_abnormal) << '\n';
        return 1;
      }
      if (options.fail_on_unsupported) {
        if (options.json) {
          std::cout << "\n  ],\n"
                    << "  \"completed_frames\": " << completed_frames << ",\n"
                    << "  \"abnormal_stop\": true,\n"
                    << "  \"first_abnormal_stop_reason\": \""
                    << stop_reason_name(first_abnormal) << "\",\n"
                    << "  \"result\": \"fail_on_unsupported\"\n"
                    << "}\n";
        }
        std::cerr << "FAIL: abnormal stop at frame " << frame_index << ": "
                  << stop_reason_name(first_abnormal) << '\n';
        return 1;
      }
      break;
    }
  }

  if (options.json) {
    std::cout << "\n  ],\n"
              << "  \"completed_frames\": " << completed_frames << ",\n"
              << "  \"abnormal_stop\": " << (abnormal_stop ? "true" : "false") << ",\n"
              << "  \"first_abnormal_stop_reason\": \""
              << stop_reason_name(first_abnormal) << "\",\n"
              << "  \"result\": \"pass\"\n"
              << "}\n";
  } else {
    std::cout << "rom_smoke: PASS frames=" << completed_frames
              << " abnormal=" << (abnormal_stop ? "yes" : "no") << '\n';
  }

  return 0;
}
