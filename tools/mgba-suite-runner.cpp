#include "gba/core/arm7tdmi.hpp"
#include "gba/core/bios.hpp"
#include "gba/core/core_session.hpp"
#include "gba/core/memory_bus.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("failed to open ROM path");
  }
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
                                   std::istreambuf_iterator<char>());
}

const char* stop_reason_name(gba::core::CoreRunStopReason reason) {
  switch (reason) {
    case gba::core::CoreRunStopReason::max_steps:
      return "max_steps";
    case gba::core::CoreRunStopReason::fetch_failed:
      return "fetch_failed";
    case gba::core::CoreRunStopReason::unsupported_instruction:
      return "unsupported_instruction";
  }
  return "unknown";
}

const char* execute_status_name(gba::core::ExecuteStatus status) {
  switch (status) {
    case gba::core::ExecuteStatus::executed:
      return "executed";
    case gba::core::ExecuteStatus::skipped_condition:
      return "skipped_condition";
    case gba::core::ExecuteStatus::unsupported:
      return "unsupported";
  }
  return "unknown";
}

const char* cpu_mode_name(gba::core::CpuMode mode) {
  switch (mode) {
    case gba::core::CpuMode::user:
      return "user";
    case gba::core::CpuMode::fiq:
      return "fiq";
    case gba::core::CpuMode::irq:
      return "irq";
    case gba::core::CpuMode::supervisor:
      return "supervisor";
    case gba::core::CpuMode::abort:
      return "abort";
    case gba::core::CpuMode::undefined:
      return "undefined";
    case gba::core::CpuMode::system:
      return "system";
  }
  return "unknown";
}

const char* save_type_name(gba::core::GamePakSaveType type) {
  switch (type) {
    case gba::core::GamePakSaveType::none:
      return "none";
    case gba::core::GamePakSaveType::sram32k:
      return "sram32k";
    case gba::core::GamePakSaveType::flash64k:
      return "flash64k";
    case gba::core::GamePakSaveType::flash128k:
      return "flash128k";
    case gba::core::GamePakSaveType::eeprom512:
      return "eeprom512";
    case gba::core::GamePakSaveType::eeprom8k:
      return "eeprom8k";
  }
  return "unknown";
}

struct RunnerLastStep {
  std::uint32_t index = 0;
  gba::core::CoreInstructionSet instruction_set = gba::core::CoreInstructionSet::arm;
  std::uint32_t fetch_address = 0;
  std::optional<std::uint32_t> instruction = std::nullopt;
  bool fetch_failed = false;
  std::optional<gba::core::ExecuteStatus> status = std::nullopt;
  gba::core::CpuMode cpu_mode = gba::core::CpuMode::system;
  std::uint32_t pc = 0;
  std::array<std::uint32_t, 8> low_registers{};
  std::uint32_t sp = 0;
  std::uint32_t lr = 0;
  std::uint32_t locale_wctomb = 0;
  std::int32_t active_test = -1;
  std::int32_t active_subtest = -1;
  bool thumb_state = false;
  std::uint32_t fetch_cycles = 0;
  bool fetch_timing_applied = false;
  bool fetch_sequential = false;
  bool prefetch_enabled = false;
  bool prefetch_hit = false;
  std::uint8_t prefetch_buffer_halfwords = 0;
  bool boundary_forced_nonsequential = false;
  std::uint32_t cpu_elapsed_cycles = 0;
  std::uint16_t pre_ppu_line = 0;
  std::uint16_t pre_ppu_line_cycle = 0;
  std::uint16_t pre_dispstat = 0;
  std::uint16_t pre_timer0 = 0;
  std::uint16_t pre_interrupt_flags = 0;
  std::uint16_t sample_ppu_line = 0;
  std::uint16_t sample_ppu_line_cycle = 0;
  std::uint16_t sample_dispstat = 0;
  std::uint16_t sample_timer0 = 0;
  std::uint64_t scheduler_cycles = 0;
  std::uint32_t device_cycles = 0;
  std::uint32_t immediate_dma_bus_cycles = 0;
  std::uint32_t triggered_dma_bus_cycles = 0;
  bool irq_serviced = false;
  bool hle_irq_reentry_dispatch_pending = false;
  bool hle_irq_return_latency_pending = false;
  bool hle_irq_post_return_latency_armed = false;
  bool hle_irq_post_return_dispatch_pending = false;
  bool hle_irq_chained_post_return_dispatch_pending = false;
  bool hle_irq_chained_post_return_data_dispatch_pending = false;
  bool hle_irq_chained_post_return_spaced_data_dispatch_pending = false;
  bool hle_irq_long_timer_chained_return_pending = false;
  bool hle_irq_slow_timer0_return_pending = false;
  bool hle_irq_post_return_chain_active = false;
  std::uint8_t hle_irq_chained_spaced_data_service_count = 0;
  bool auto_irq_line_high = false;
  std::uint8_t auto_irq_latency_cycles = 0;
  std::uint32_t timer_io_access_gap_cycles = 0;
  std::uint16_t interrupt_enable = 0;
  std::uint16_t interrupt_flags = 0;
  std::uint16_t ime = 0;
  std::array<std::uint16_t, gba::core::Timers::kTimerCount> timer_counters{};
  std::array<std::uint16_t, gba::core::Timers::kTimerCount> timer_controls{};
  std::array<std::uint16_t, gba::core::Timers::kTimerCount> timer_enable_phases{};
  std::array<std::uint32_t, gba::core::Timers::kTimerCount> timer_next_ticks{};
  bool data_access_present = false;
  std::uint32_t data_access_address = 0;
  std::uint8_t data_access_width_bytes = 0;
  bool data_access_load = false;
  bool data_access_timer_io = false;
  std::uint32_t data_access_pre_cycles = 0;
  std::uint32_t data_access_timer_io_gap_cycles = 0;
  std::optional<std::uint32_t> sample_data_word = std::nullopt;
};

struct RunnerStop {
  std::string reason = "max_steps";
  bool until_output_matched = false;
};

struct RunnerWatchChange {
  std::uint32_t index = 0;
  std::uint32_t pc = 0;
  std::string kind;
  std::uint32_t previous = 0;
  std::uint32_t current = 0;
  std::int32_t active_test = -1;
  std::int32_t active_subtest = -1;
};

struct RunnerDiagnostic {
  std::uint32_t index = 0;
  std::string kind;
  std::int32_t active_test = -1;
  std::int32_t active_subtest = -1;
  std::uint32_t fetch_address = 0;
  std::optional<std::uint32_t> instruction = std::nullopt;
  gba::core::CpuMode cpu_mode = gba::core::CpuMode::system;
  std::uint64_t scheduler_cycles = 0;
  std::uint32_t fetch_cycles = 0;
  std::uint32_t cpu_elapsed_cycles = 0;
  std::uint32_t device_cycles = 0;
  bool fetch_timing_applied = false;
  bool fetch_sequential = false;
  bool prefetch_enabled = false;
  bool prefetch_hit = false;
  std::uint8_t prefetch_buffer_halfwords = 0;
  bool irq_serviced = false;
  bool hle_irq_reentry_dispatch_pending = false;
  bool hle_irq_return_latency_pending = false;
  bool hle_irq_post_return_latency_armed = false;
  bool hle_irq_post_return_dispatch_pending = false;
  bool hle_irq_chained_post_return_dispatch_pending = false;
  bool hle_irq_chained_post_return_data_dispatch_pending = false;
  bool hle_irq_chained_post_return_spaced_data_dispatch_pending = false;
  bool hle_irq_long_timer_chained_return_pending = false;
  bool hle_irq_slow_timer0_return_pending = false;
  bool hle_irq_post_return_chain_active = false;
  std::uint8_t hle_irq_chained_spaced_data_service_count = 0;
  bool auto_irq_line_high = false;
  std::uint8_t auto_irq_latency_cycles = 0;
  std::uint16_t interrupt_enable = 0;
  std::uint16_t interrupt_flags = 0;
  std::uint16_t ime = 0;
  std::array<std::uint16_t, gba::core::Timers::kTimerCount> timer_controls{};
  std::array<std::uint16_t, gba::core::Timers::kTimerCount> timer_enable_phases{};
  std::array<std::uint32_t, gba::core::Timers::kTimerCount> timer_next_ticks{};
  std::uint16_t pre_ppu_line = 0;
  std::uint16_t pre_ppu_line_cycle = 0;
  std::uint16_t pre_dispstat = 0;
  std::uint16_t pre_timer0 = 0;
  std::uint16_t pre_interrupt_flags = 0;
  std::uint16_t sample_ppu_line = 0;
  std::uint16_t sample_ppu_line_cycle = 0;
  std::uint16_t sample_dispstat = 0;
  std::uint16_t sample_timer0 = 0;
  std::uint16_t ppu_line = 0;
  std::uint16_t ppu_line_cycle = 0;
  std::uint32_t sp = 0;
  std::uint32_t r3 = 0;
  std::array<std::uint32_t, 8> low_registers{};
  std::uint32_t data_address = 0;
  std::uint8_t data_width = 0;
  bool data_load = false;
  std::uint32_t data_pre_cycles = 0;
  std::uint32_t data_timer_io_gap_cycles = 0;
  std::uint16_t dispstat = 0;
  std::uint16_t timer0 = 0;
  std::optional<std::uint32_t> sample_data_word = std::nullopt;
  std::optional<std::uint32_t> data_word = std::nullopt;
  std::uint32_t dma3_source = 0;
  std::uint32_t dma3_destination = 0;
  std::uint16_t dma3_count = 0;
  std::uint16_t dma3_control = 0;
  std::uint32_t dma3_active_count = 0;
  std::optional<std::uint32_t> dma3_source_word = std::nullopt;
  std::optional<std::uint32_t> dma3_destination_word = std::nullopt;
  std::optional<std::uint32_t> open_bus = std::nullopt;
  std::optional<std::uint16_t> pipeline_halfword = std::nullopt;
};

struct InputEvent {
  std::uint32_t step = 0;
  std::uint16_t pressed_mask = 0;
};

std::uint16_t button_mask(std::string_view name) {
  if (name == "none" || name == "release") {
    return 0;
  }
  if (name == "A") {
    return static_cast<std::uint16_t>(gba::core::KeypadButton::a);
  }
  if (name == "B") {
    return static_cast<std::uint16_t>(gba::core::KeypadButton::b);
  }
  if (name == "START") {
    return static_cast<std::uint16_t>(gba::core::KeypadButton::start);
  }
  if (name == "SELECT") {
    return static_cast<std::uint16_t>(gba::core::KeypadButton::select);
  }
  if (name == "UP") {
    return static_cast<std::uint16_t>(gba::core::KeypadButton::up);
  }
  if (name == "DOWN") {
    return static_cast<std::uint16_t>(gba::core::KeypadButton::down);
  }
  if (name == "LEFT") {
    return static_cast<std::uint16_t>(gba::core::KeypadButton::left);
  }
  if (name == "RIGHT") {
    return static_cast<std::uint16_t>(gba::core::KeypadButton::right);
  }
  if (name == "L") {
    return static_cast<std::uint16_t>(gba::core::KeypadButton::l);
  }
  if (name == "R") {
    return static_cast<std::uint16_t>(gba::core::KeypadButton::r);
  }
  return 0;
}

std::vector<InputEvent> parse_input_script(std::string script) {
  std::vector<InputEvent> events;
  std::replace(script.begin(), script.end(), ';', ',');
  std::size_t start = 0;
  while (start < script.size()) {
    const std::size_t end = script.find(',', start);
    const std::string token =
        script.substr(start, end == std::string::npos ? std::string::npos : end - start);
    const std::size_t colon = token.find(':');
    if (colon != std::string::npos) {
      std::string button = token.substr(colon + 1);
      std::transform(button.begin(), button.end(), button.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
      });
      events.push_back({static_cast<std::uint32_t>(std::stoul(token.substr(0, colon))),
                        button_mask(button)});
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  std::sort(events.begin(), events.end(), [](const InputEvent& left, const InputEvent& right) {
    return left.step < right.step;
  });
  return events;
}

std::string printable_save_text(const std::vector<std::uint8_t>& save) {
  std::string text;
  for (const std::uint8_t byte : save) {
    if (byte == 0xFF || byte == 0) {
      continue;
    }
    if (byte == '\n' || byte == '\r' || byte == '\t' ||
        (byte >= 0x20 && byte <= 0x7E)) {
      text.push_back(static_cast<char>(byte));
    }
  }
  return text;
}

std::int32_t read_i32_or(const gba::core::MemoryBus& memory, std::uint32_t address,
                         std::int32_t fallback) {
  const std::optional<std::uint32_t> value = memory.read32(address);
  if (!value.has_value()) {
    return fallback;
  }
  return static_cast<std::int32_t>(value.value());
}

std::int32_t read_u16_or(const gba::core::MemoryBus& memory, std::uint32_t address,
                         std::int32_t fallback) {
  const std::optional<std::uint16_t> value = memory.read16(address);
  if (!value.has_value()) {
    return fallback;
  }
  return value.value();
}

std::int32_t read_u8_or(const gba::core::MemoryBus& memory, std::uint32_t address,
                        std::int32_t fallback) {
  const std::optional<std::uint8_t> value = memory.read8(address);
  if (!value.has_value()) {
    return fallback;
  }
  return value.value();
}

std::optional<std::uint32_t> diagnostic_word(const gba::core::MemoryBus& memory,
                                             std::uint32_t address) {
  return memory.read32(address & ~0x3U);
}

std::optional<std::uint32_t> sampled_io_word(
    std::uint32_t address, std::uint8_t width,
    const gba::core::PpuTiming& ppu, const gba::core::Timers& timers) {
  constexpr std::uint32_t kDispstat = 0x04000004U;
  constexpr std::uint32_t kTimerBase = 0x04000100U;
  constexpr std::uint32_t kTimerEnd = 0x04000110U;
  if (address <= kDispstat && address + width > kDispstat) {
    return ppu.dispstat();
  }
  if (address >= kTimerBase && address < kTimerEnd) {
    const std::uint32_t relative = address - kTimerBase;
    const std::size_t timer = relative / 4U;
    const std::uint32_t timer_offset = relative % 4U;
    if (timer >= gba::core::Timers::kTimerCount) {
      return std::nullopt;
    }
    if (timer_offset == 0) {
      return timers.counter(timer);
    }
    if (timer_offset == 2) {
      return timers.control(timer);
    }
  }
  return std::nullopt;
}

void maybe_add_misc_edge_diagnostic(gba::core::CoreSession& session,
                                    const gba::core::CoreSchedulerFetchStepResult& step,
                                    const RunnerLastStep& last_step,
                                    std::vector<RunnerDiagnostic>& diagnostics) {
  const bool misc_edge_diag_enabled =
      std::getenv("GBA_MISC_EDGE_DIAG") != nullptr;
  const bool timer_diag_enabled = std::getenv("GBA_TIMERS_DIAG") != nullptr;
  const bool timer_diag_target =
      timer_diag_enabled && (last_step.active_test == 3 || last_step.active_test == 11);
  const bool timer_diag_row =
      timer_diag_target &&
      ((step.step.has_value() && step.step->data_access.has_value() &&
        step.step->data_access->timer_io) ||
       last_step.cpu_mode == gba::core::CpuMode::irq ||
       (step.step.has_value() && step.step->irq_serviced));

  if ((!misc_edge_diag_enabled ||
       (last_step.active_test != 0 && last_step.active_test != 1)) &&
      !timer_diag_row) {
    return;
  }

  const bool dma3_hblank = step.step.has_value() &&
                           misc_edge_diag_enabled &&
                           last_step.active_test == 0 &&
                           step.step->devices.triggered_dma.channels_executed != 0;
  const bool dma3_source_stack_access =
      misc_edge_diag_enabled &&
      last_step.active_test == 0 && step.step.has_value() &&
      step.step->data_access.has_value() &&
      step.step->data_access->address >= 0x03007000U &&
      step.step->data_access->address < 0x03008000U;
  const bool unmapped_thumb_ldmia =
      misc_edge_diag_enabled &&
      last_step.active_test == 0 &&
      step.instruction_set == gba::core::CoreInstructionSet::thumb &&
      step.instruction.has_value() && step.instruction.value() == 0xCB04U &&
      last_step.low_registers.at(3) >= 0x10000000U &&
      last_step.low_registers.at(3) < 0x20000000U;
  const bool hle_vblank_intr_wait =
      misc_edge_diag_enabled &&
      last_step.active_test == 0 &&
      step.instruction_set == gba::core::CoreInstructionSet::thumb &&
      step.instruction.has_value() && step.instruction.value() == 0xDF05U;
  const bool hblank_halt =
      misc_edge_diag_enabled &&
      last_step.active_test == 1 &&
      step.instruction_set == gba::core::CoreInstructionSet::thumb &&
      step.instruction.has_value() && step.instruction.value() == 0xDF02U;
  const bool hblank_io_access =
      misc_edge_diag_enabled &&
      last_step.active_test == 1 && step.step.has_value() &&
      step.step->data_access.has_value() &&
      (step.step->data_access->address == 0x04000004U ||
       step.step->data_access->address == 0x04000100U);
  const bool hblank_bit_iwram =
      std::getenv("GBA_MISC_EDGE_HBLANK_VERBOSE") != nullptr &&
      last_step.active_test == 1 &&
      step.fetch_address >= 0x03000100U &&
      step.fetch_address < 0x030001E0U;
  const bool hblank_irq_window =
      std::getenv("GBA_MISC_EDGE_IRQ_VERBOSE") != nullptr &&
      last_step.active_test == 1 &&
      (last_step.cpu_mode == gba::core::CpuMode::irq ||
       step.fetch_address == 0x0FFFFF00U ||
       (step.step.has_value() && step.step->irq_serviced));

  if (!dma3_hblank && !dma3_source_stack_access && !unmapped_thumb_ldmia &&
      !hle_vblank_intr_wait && !hblank_halt && !hblank_io_access &&
      !hblank_bit_iwram && !hblank_irq_window && !timer_diag_row) {
    return;
  }

  RunnerDiagnostic diagnostic{};
  diagnostic.index = last_step.index;
  diagnostic.kind = dma3_hblank ? "hblank_dma3"
                    : timer_diag_row ? "timer_window"
                    : unmapped_thumb_ldmia ? "unmapped_thumb_ldmia"
                    : hle_vblank_intr_wait ? "hle_vblank_intr_wait"
                    : hblank_halt ? "hblank_halt"
                    : hblank_io_access ? "hblank_io_access"
                    : hblank_irq_window ? "hblank_irq_window"
                    : hblank_bit_iwram ? "hblank_bit_iwram"
                                           : "stack_access";
  diagnostic.active_test = last_step.active_test;
  diagnostic.active_subtest = last_step.active_subtest;
  diagnostic.fetch_address = step.fetch_address;
  diagnostic.instruction = step.instruction;
  diagnostic.cpu_mode = last_step.cpu_mode;
  diagnostic.scheduler_cycles = last_step.scheduler_cycles;
  diagnostic.fetch_cycles = last_step.fetch_cycles;
  diagnostic.cpu_elapsed_cycles = last_step.cpu_elapsed_cycles;
  diagnostic.device_cycles = last_step.device_cycles;
  diagnostic.fetch_timing_applied = last_step.fetch_timing_applied;
  diagnostic.fetch_sequential = last_step.fetch_sequential;
  diagnostic.prefetch_enabled = last_step.prefetch_enabled;
  diagnostic.prefetch_hit = last_step.prefetch_hit;
  diagnostic.prefetch_buffer_halfwords = last_step.prefetch_buffer_halfwords;
  diagnostic.irq_serviced = last_step.irq_serviced;
  diagnostic.hle_irq_reentry_dispatch_pending =
      last_step.hle_irq_reentry_dispatch_pending;
  diagnostic.hle_irq_return_latency_pending =
      last_step.hle_irq_return_latency_pending;
  diagnostic.hle_irq_post_return_latency_armed =
      last_step.hle_irq_post_return_latency_armed;
  diagnostic.hle_irq_post_return_dispatch_pending =
      last_step.hle_irq_post_return_dispatch_pending;
  diagnostic.hle_irq_chained_post_return_dispatch_pending =
      last_step.hle_irq_chained_post_return_dispatch_pending;
  diagnostic.hle_irq_chained_post_return_data_dispatch_pending =
      last_step.hle_irq_chained_post_return_data_dispatch_pending;
  diagnostic.hle_irq_chained_post_return_spaced_data_dispatch_pending =
      last_step.hle_irq_chained_post_return_spaced_data_dispatch_pending;
  diagnostic.hle_irq_long_timer_chained_return_pending =
      last_step.hle_irq_long_timer_chained_return_pending;
  diagnostic.hle_irq_slow_timer0_return_pending =
      last_step.hle_irq_slow_timer0_return_pending;
  diagnostic.hle_irq_post_return_chain_active =
      last_step.hle_irq_post_return_chain_active;
  diagnostic.hle_irq_chained_spaced_data_service_count =
      last_step.hle_irq_chained_spaced_data_service_count;
  diagnostic.auto_irq_line_high = last_step.auto_irq_line_high;
  diagnostic.auto_irq_latency_cycles = last_step.auto_irq_latency_cycles;
  diagnostic.interrupt_enable = last_step.interrupt_enable;
  diagnostic.interrupt_flags = last_step.interrupt_flags;
  diagnostic.ime = last_step.ime;
  diagnostic.timer_controls = last_step.timer_controls;
  diagnostic.timer_enable_phases = last_step.timer_enable_phases;
  diagnostic.timer_next_ticks = last_step.timer_next_ticks;
  diagnostic.pre_ppu_line = last_step.pre_ppu_line;
  diagnostic.pre_ppu_line_cycle = last_step.pre_ppu_line_cycle;
  diagnostic.pre_dispstat = last_step.pre_dispstat;
  diagnostic.pre_timer0 = last_step.pre_timer0;
  diagnostic.pre_interrupt_flags = last_step.pre_interrupt_flags;
  diagnostic.sample_ppu_line = last_step.sample_ppu_line;
  diagnostic.sample_ppu_line_cycle = last_step.sample_ppu_line_cycle;
  diagnostic.sample_dispstat = last_step.sample_dispstat;
  diagnostic.sample_timer0 = last_step.sample_timer0;
  diagnostic.ppu_line = session.ppu().vcount();
  diagnostic.ppu_line_cycle = session.ppu().line_cycle();
  diagnostic.sp = last_step.sp;
  diagnostic.r3 = last_step.low_registers.at(3);
  diagnostic.low_registers = last_step.low_registers;
  diagnostic.dispstat = session.ppu().dispstat();
  diagnostic.timer0 = session.timers().counter(0);
  diagnostic.sample_data_word = last_step.sample_data_word;
  diagnostic.dma3_source = session.dma().source(3);
  diagnostic.dma3_destination = session.dma().destination(3);
  diagnostic.dma3_count = session.dma().word_count(3);
  diagnostic.dma3_control = session.dma().control(3);
  diagnostic.dma3_active_count = session.dma().active_count(3);
  diagnostic.dma3_source_word =
      diagnostic_word(session.memory(), diagnostic.dma3_source);
  diagnostic.dma3_destination_word =
      diagnostic_word(session.memory(), diagnostic.dma3_destination);
  diagnostic.open_bus = session.memory().open_bus_latch();
  diagnostic.pipeline_halfword = session.memory().read16(step.fetch_address + 4U);
  if (step.step.has_value() && step.step->data_access.has_value()) {
    diagnostic.data_address = step.step->data_access->address;
    diagnostic.data_width = step.step->data_access->width_bytes;
    diagnostic.data_load = step.step->data_access->load;
    diagnostic.data_pre_cycles = step.step->data_access->pre_access_cycles;
    diagnostic.data_timer_io_gap_cycles =
        step.step->data_access->timer_io_gap_cycles;
    diagnostic.data_word = diagnostic_word(session.memory(), diagnostic.data_address);
  }
  diagnostics.push_back(diagnostic);
}

gba::core::CoreSchedulerRunResult run_with_last_step(gba::core::CoreSession& session,
                                                     std::uint32_t max_steps,
                                                     RunnerLastStep& last_step,
                                                     const std::vector<InputEvent>& events,
                                                     std::string_view until_output,
                                                     RunnerStop& runner_stop,
                                                     std::deque<RunnerLastStep>& recent_steps,
                                                     std::vector<RunnerWatchChange>& watch_changes,
                                                     std::vector<RunnerDiagnostic>& diagnostics,
                                                     std::size_t recent_step_limit) {
  gba::core::CoreSchedulerRunResult result{
      max_steps,
      0,
      0,
      0,
      0,
      0,
      gba::core::CoreRunStopReason::max_steps,
      session.cpu().register_value(gba::core::Arm7tdmi::kPc),
      session.scheduler().scheduler_cycles(),
  };
  runner_stop = {};

  std::size_t next_event = 0;
  std::uint32_t previous_locale_wctomb =
      static_cast<std::uint32_t>(read_i32_or(session.memory(), 0x0300377CU, 0));
  std::int32_t previous_active_test = read_u8_or(session.memory(), 0x030000B2U, -1);
  std::int32_t previous_active_subtest = read_u16_or(session.memory(), 0x030000B0U, -1);
  constexpr std::uint32_t kUntilOutputGraceSteps = 4096;
  bool until_output_seen = false;
  std::uint32_t until_output_grace_remaining = 0;
  for (std::uint32_t index = 0; index < max_steps; ++index) {
    while (next_event < events.size() && events.at(next_event).step == index) {
      [[maybe_unused]] const bool input_applied =
          session.keypad().set_pressed_mask(events.at(next_event).pressed_mask);
      session.keypad().poll_interrupt(session.interrupts());
      ++next_event;
    }
    const std::uint16_t pre_ppu_line = session.ppu().vcount();
    const std::uint16_t pre_ppu_line_cycle = session.ppu().line_cycle();
    const std::uint16_t pre_dispstat = session.ppu().dispstat();
    const std::uint16_t pre_timer0 = session.timers().counter(0);
    const std::uint16_t pre_interrupt_flags =
        session.interrupts().interrupt_flags();
    const gba::core::PpuTiming pre_access_ppu = session.ppu();
    const gba::core::Timers pre_access_timers = session.timers();
    const gba::core::InterruptController pre_access_interrupts =
        session.interrupts();
    const gba::core::CoreSchedulerFetchStepResult step = session.step();
    last_step = RunnerLastStep{};
    last_step.index = index;
    last_step.instruction_set = step.instruction_set;
    last_step.fetch_address = step.fetch_address;
    last_step.instruction = step.instruction;
    last_step.fetch_failed = step.fetch_failed;
    last_step.status = step.step.has_value()
                           ? std::optional<gba::core::ExecuteStatus>(step.step->cpu_step.status)
                           : std::nullopt;
    last_step.cpu_mode = session.cpu().current_mode();
    last_step.pc = session.cpu().register_value(gba::core::Arm7tdmi::kPc);
    for (std::uint8_t reg = 0; reg < last_step.low_registers.size(); ++reg) {
      last_step.low_registers.at(reg) = session.cpu().register_value(reg);
    }
    last_step.sp = session.cpu().register_value(13);
    last_step.lr = session.cpu().register_value(gba::core::Arm7tdmi::kLinkRegister);
    last_step.locale_wctomb =
        static_cast<std::uint32_t>(read_i32_or(session.memory(), 0x0300377CU, 0));
    last_step.active_test = read_u8_or(session.memory(), 0x030000B2U, -1);
    last_step.active_subtest = read_u16_or(session.memory(), 0x030000B0U, -1);
    last_step.fetch_cycles = step.fetch_cycles;
    last_step.fetch_timing_applied = step.fetch_timing_applied;
    last_step.fetch_sequential = step.fetch_sequential;
    last_step.prefetch_enabled = step.prefetch_enabled;
    last_step.prefetch_hit = step.prefetch_hit;
    last_step.prefetch_buffer_halfwords = step.prefetch_buffer_halfwords;
    last_step.boundary_forced_nonsequential = step.boundary_forced_nonsequential;
    last_step.pre_ppu_line = pre_ppu_line;
    last_step.pre_ppu_line_cycle = pre_ppu_line_cycle;
    last_step.pre_dispstat = pre_dispstat;
    last_step.pre_timer0 = pre_timer0;
    last_step.pre_interrupt_flags = pre_interrupt_flags;
    last_step.scheduler_cycles = session.scheduler().scheduler_cycles();
    const gba::core::CoreSchedulerState scheduler_state =
        session.scheduler().save_state();
    last_step.hle_irq_reentry_dispatch_pending =
        scheduler_state.hle_irq_reentry_dispatch_pending;
    last_step.hle_irq_return_latency_pending =
        scheduler_state.hle_irq_return_latency_pending;
    last_step.hle_irq_post_return_latency_armed =
        scheduler_state.hle_irq_post_return_latency_armed;
    last_step.hle_irq_post_return_dispatch_pending =
        scheduler_state.hle_irq_post_return_dispatch_pending;
    last_step.hle_irq_chained_post_return_dispatch_pending =
        scheduler_state.hle_irq_chained_post_return_dispatch_pending;
    last_step.hle_irq_chained_post_return_data_dispatch_pending =
        scheduler_state.hle_irq_chained_post_return_data_dispatch_pending;
    last_step.hle_irq_chained_post_return_spaced_data_dispatch_pending =
        scheduler_state.hle_irq_chained_post_return_spaced_data_dispatch_pending;
    last_step.hle_irq_long_timer_chained_return_pending =
        scheduler_state.hle_irq_long_timer_chained_return_pending;
    last_step.hle_irq_slow_timer0_return_pending =
        scheduler_state.hle_irq_slow_timer0_return_pending;
    last_step.hle_irq_post_return_chain_active =
        scheduler_state.hle_irq_post_return_chain_active;
    last_step.hle_irq_chained_spaced_data_service_count =
        scheduler_state.hle_irq_chained_spaced_data_service_count;
    last_step.auto_irq_line_high = scheduler_state.auto_irq_line_high;
    last_step.auto_irq_latency_cycles =
        scheduler_state.auto_irq_latency_cycles;
    last_step.timer_io_access_gap_cycles =
        scheduler_state.timer_io_access_gap_cycles;
    last_step.interrupt_enable = session.interrupts().interrupt_enable();
    last_step.interrupt_flags = session.interrupts().interrupt_flags();
    last_step.ime = session.interrupts().ime();
    for (std::size_t timer = 0; timer < gba::core::Timers::kTimerCount; ++timer) {
      last_step.timer_counters.at(timer) = session.timers().counter(timer);
      last_step.timer_controls.at(timer) = session.timers().control(timer);
      last_step.timer_enable_phases.at(timer) =
          session.timers().last_enable_phase(timer);
      last_step.timer_next_ticks.at(timer) =
          session.timers().cycles_until_next_prescaler_tick(timer);
    }
    if (step.step.has_value()) {
      last_step.cpu_elapsed_cycles = step.step->cpu_step.elapsed_cycles;
      last_step.device_cycles = step.step->devices.cycles;
      last_step.immediate_dma_bus_cycles = step.step->immediate_dma.bus_cycles;
      last_step.triggered_dma_bus_cycles =
          step.step->devices.triggered_dma.bus_cycles;
      last_step.irq_serviced = step.step->irq_serviced;
      if (step.step->data_access.has_value()) {
        last_step.data_access_present = true;
        last_step.data_access_address = step.step->data_access->address;
        last_step.data_access_width_bytes = step.step->data_access->width_bytes;
        last_step.data_access_load = step.step->data_access->load;
        last_step.data_access_timer_io = step.step->data_access->timer_io;
        last_step.data_access_pre_cycles =
            step.step->data_access->pre_access_cycles;
        last_step.data_access_timer_io_gap_cycles =
            step.step->data_access->timer_io_gap_cycles;
        gba::core::PpuTiming sample_ppu = pre_access_ppu;
        gba::core::Timers sample_timers = pre_access_timers;
        gba::core::InterruptController sample_interrupts =
            pre_access_interrupts;
        if (last_step.data_access_pre_cycles != 0) {
          [[maybe_unused]] const gba::core::PpuTickEvents ppu_events =
              sample_ppu.tick(last_step.data_access_pre_cycles,
                              sample_interrupts);
          [[maybe_unused]] const gba::core::Timers::TickResult timer_events =
              sample_timers.tick(last_step.data_access_pre_cycles,
                                 sample_interrupts);
        }
        last_step.sample_ppu_line = sample_ppu.vcount();
        last_step.sample_ppu_line_cycle = sample_ppu.line_cycle();
        last_step.sample_dispstat = sample_ppu.dispstat();
        last_step.sample_timer0 = sample_timers.counter(0);
        last_step.sample_data_word = sampled_io_word(
            last_step.data_access_address, last_step.data_access_width_bytes,
            sample_ppu, sample_timers);
      }
    }
    maybe_add_misc_edge_diagnostic(session, step, last_step, diagnostics);
    if (last_step.locale_wctomb != previous_locale_wctomb) {
      watch_changes.push_back({index, last_step.pc, "locale_wctomb",
                               previous_locale_wctomb,
                               last_step.locale_wctomb, last_step.active_test,
                               last_step.active_subtest});
      previous_locale_wctomb = last_step.locale_wctomb;
    }
    if (last_step.active_test != previous_active_test) {
      watch_changes.push_back(
          {index, last_step.pc, "active_test",
           static_cast<std::uint32_t>(previous_active_test),
           static_cast<std::uint32_t>(last_step.active_test), last_step.active_test,
           last_step.active_subtest});
      previous_active_test = last_step.active_test;
    }
    if (last_step.active_subtest != previous_active_subtest) {
      watch_changes.push_back(
          {index, last_step.pc, "active_subtest",
           static_cast<std::uint32_t>(previous_active_subtest),
           static_cast<std::uint32_t>(last_step.active_subtest), last_step.active_test,
           last_step.active_subtest});
      previous_active_subtest = last_step.active_subtest;
    }
    last_step.thumb_state = session.cpu().thumb_state();
    recent_steps.push_back(last_step);
    if (recent_step_limit != 0 && recent_steps.size() > recent_step_limit) {
      recent_steps.pop_front();
    }

    if (step.fetch_failed) {
      ++result.fetch_failures;
      result.stop_reason = gba::core::CoreRunStopReason::fetch_failed;
      runner_stop.reason = "fetch_failed";
      break;
    }

    ++result.attempted_steps;
    if (step.step.has_value() &&
        step.step->cpu_step.status == gba::core::ExecuteStatus::executed) {
      ++result.executed_steps;
    } else if (step.step.has_value() &&
               step.step->cpu_step.status == gba::core::ExecuteStatus::skipped_condition) {
      ++result.skipped_steps;
    } else {
      ++result.unsupported_steps;
      result.stop_reason = gba::core::CoreRunStopReason::unsupported_instruction;
      runner_stop.reason = "unsupported_instruction";
      break;
    }

    if (!until_output.empty() && !until_output_seen &&
        session.memory().debug_output().find(until_output) != std::string::npos) {
      runner_stop.reason = "until_output";
      runner_stop.until_output_matched = true;
      until_output_seen = true;
      until_output_grace_remaining = kUntilOutputGraceSteps;
    }
    if (until_output_seen) {
      if (until_output_grace_remaining == 0) {
        break;
      }
      --until_output_grace_remaining;
    }
  }

  result.final_pc = session.cpu().register_value(gba::core::Arm7tdmi::kPc);
  result.scheduler_cycles = session.scheduler().scheduler_cycles();
  return result;
}

std::string header_text(const std::array<std::uint8_t, 12>& bytes) {
  std::string text;
  for (const std::uint8_t byte : bytes) {
    if (byte == 0) {
      break;
    }
    text.push_back(static_cast<char>(byte));
  }
  return text;
}

void print_recent_trace(const std::deque<RunnerLastStep>& recent_steps) {
  std::cout << "suite_recent_trace_begin\n";
  for (const RunnerLastStep& step : recent_steps) {
    std::cout << "suite_recent_trace: index=" << step.index
              << " set=" << (step.instruction_set == gba::core::CoreInstructionSet::arm
                                  ? "arm"
                                  : "thumb")
              << " fetch_pc=0x" << std::hex << step.fetch_address;
    if (step.instruction.has_value()) {
      std::cout << " instruction=0x" << step.instruction.value();
    } else {
      std::cout << " instruction=null";
    }
    std::cout << " runtime_pc=0x" << step.pc << " lr=0x" << step.lr
              << " sp=0x" << step.sp << " r0=0x" << step.low_registers.at(0)
              << " r1=0x" << step.low_registers.at(1)
              << " r2=0x" << step.low_registers.at(2)
              << " r3=0x" << step.low_registers.at(3)
              << " r4=0x" << step.low_registers.at(4)
              << " r5=0x" << step.low_registers.at(5)
              << " r6=0x" << step.low_registers.at(6)
              << " r7=0x" << step.low_registers.at(7)
              << " locale_wctomb=0x" << step.locale_wctomb << std::dec
              << " active_test=" << step.active_test
              << " active_subtest=" << step.active_subtest
              << " thumb=" << (step.thumb_state ? "true" : "false")
              << " fetch_failed=" << (step.fetch_failed ? "true" : "false")
              << " fetch_cycles=" << step.fetch_cycles
              << " cpu_elapsed=" << step.cpu_elapsed_cycles
              << " device_cycles=" << step.device_cycles
              << " scheduler_cycles=" << step.scheduler_cycles
              << " fetch_timing=" << (step.fetch_timing_applied ? "true" : "false")
              << " fetch_sequential=" << (step.fetch_sequential ? "true" : "false")
              << " prefetch_enabled=" << (step.prefetch_enabled ? "true" : "false")
              << " prefetch_hit=" << (step.prefetch_hit ? "true" : "false")
              << " prefetch_buffer="
              << static_cast<unsigned>(step.prefetch_buffer_halfwords)
              << " boundary_nonseq="
              << (step.boundary_forced_nonsequential ? "true" : "false")
              << " irq_serviced=" << (step.irq_serviced ? "true" : "false")
              << " hle_reentry="
              << (step.hle_irq_reentry_dispatch_pending ? "true" : "false")
              << " hle_return_latency="
              << (step.hle_irq_return_latency_pending ? "true" : "false")
              << " hle_post_return_armed="
              << (step.hle_irq_post_return_latency_armed ? "true" : "false")
              << " hle_post_return_dispatch="
              << (step.hle_irq_post_return_dispatch_pending ? "true" : "false")
              << " hle_chained_dispatch="
              << (step.hle_irq_chained_post_return_dispatch_pending ? "true" : "false")
              << " hle_chained_data_dispatch="
              << (step.hle_irq_chained_post_return_data_dispatch_pending ? "true" : "false")
              << " hle_chained_spaced_data_dispatch="
              << (step.hle_irq_chained_post_return_spaced_data_dispatch_pending ? "true" : "false")
              << " hle_long_chained_return="
              << (step.hle_irq_long_timer_chained_return_pending ? "true" : "false")
              << " hle_chain="
              << (step.hle_irq_post_return_chain_active ? "true" : "false")
              << " auto_irq_line="
              << (step.auto_irq_line_high ? "true" : "false")
              << " auto_irq_latency="
              << static_cast<unsigned>(step.auto_irq_latency_cycles)
              << " timer_io_gap=" << step.timer_io_access_gap_cycles
              << " ime=0x" << std::hex << step.ime
              << " ie=0x" << step.interrupt_enable
              << " if=0x" << step.interrupt_flags
              << " tm0=0x" << step.timer_counters.at(0)
              << "/0x" << step.timer_controls.at(0)
              << "/phase=" << std::dec << step.timer_enable_phases.at(0)
              << "/next=" << step.timer_next_ticks.at(0) << std::hex
              << " tm1=0x" << step.timer_counters.at(1)
              << "/0x" << step.timer_controls.at(1)
              << "/phase=" << std::dec << step.timer_enable_phases.at(1)
              << "/next=" << step.timer_next_ticks.at(1) << std::hex
              << " tm2=0x" << step.timer_counters.at(2)
              << "/0x" << step.timer_controls.at(2)
              << "/phase=" << std::dec << step.timer_enable_phases.at(2)
              << "/next=" << step.timer_next_ticks.at(2) << std::hex
              << " tm3=0x" << step.timer_counters.at(3)
              << "/0x" << step.timer_controls.at(3)
              << "/phase=" << std::dec << step.timer_enable_phases.at(3)
              << "/next=" << step.timer_next_ticks.at(3) << std::hex
              << " dma_bus=" << std::dec << step.immediate_dma_bus_cycles
              << " triggered_dma_bus=" << step.triggered_dma_bus_cycles;
    if (step.data_access_present) {
      std::cout << " data_addr=0x" << std::hex << step.data_access_address
                << std::dec
                << " data_width=" << static_cast<unsigned>(step.data_access_width_bytes)
                << " data_load=" << (step.data_access_load ? "true" : "false")
                << " data_timer_io="
                << (step.data_access_timer_io ? "true" : "false")
                << " data_pre_cycles=" << step.data_access_pre_cycles
                << " data_timer_io_gap="
                << step.data_access_timer_io_gap_cycles;
    } else {
      std::cout << " data_addr=null";
    }
    if (step.status.has_value()) {
      std::cout << " status=" << execute_status_name(step.status.value());
    } else {
      std::cout << " status=null";
    }
    std::cout << '\n';
  }
  std::cout << "suite_recent_trace_end\n";
}

void print_watch_changes(const std::vector<RunnerWatchChange>& watch_changes) {
  std::cout << "suite_watch_changes_begin\n";
  for (const RunnerWatchChange& change : watch_changes) {
    std::cout << "suite_watch_change: index=" << change.index << " pc=0x" << std::hex
              << change.pc << std::dec << " kind=" << change.kind << std::hex
              << " old=0x" << change.previous << " new=0x" << change.current
              << std::dec
              << " active_test=" << change.active_test
              << " active_subtest=" << change.active_subtest << '\n';
  }
  std::cout << "suite_watch_changes_end\n";
}

void print_diagnostics(const std::vector<RunnerDiagnostic>& diagnostics) {
  if (std::getenv("GBA_MISC_EDGE_DIAG") == nullptr &&
      std::getenv("GBA_TIMERS_DIAG") == nullptr) {
    return;
  }
  std::cout << "suite_diagnostics_begin\n";
  for (const RunnerDiagnostic& diagnostic : diagnostics) {
    std::cout << "suite_diag: index=" << diagnostic.index
              << " kind=" << diagnostic.kind
              << " active_test=" << diagnostic.active_test
              << " active_subtest=" << diagnostic.active_subtest
              << " mode=" << cpu_mode_name(diagnostic.cpu_mode)
              << " fetch_pc=0x" << std::hex << diagnostic.fetch_address;
    if (diagnostic.instruction.has_value()) {
      std::cout << " instruction=0x" << diagnostic.instruction.value();
    } else {
      std::cout << " instruction=null";
    }
    std::cout << " scheduler_cycles=" << std::dec << diagnostic.scheduler_cycles
              << " fetch_cycles=" << diagnostic.fetch_cycles
              << " cpu_elapsed=" << diagnostic.cpu_elapsed_cycles
              << " device_cycles=" << diagnostic.device_cycles
              << " fetch_timing="
              << (diagnostic.fetch_timing_applied ? "true" : "false")
              << " fetch_sequential="
              << (diagnostic.fetch_sequential ? "true" : "false")
              << " prefetch_enabled="
              << (diagnostic.prefetch_enabled ? "true" : "false")
              << " prefetch_hit="
              << (diagnostic.prefetch_hit ? "true" : "false")
              << " prefetch_buffer="
              << static_cast<unsigned>(diagnostic.prefetch_buffer_halfwords)
              << " irq_serviced=" << (diagnostic.irq_serviced ? "true" : "false")
              << " hle_reentry="
              << (diagnostic.hle_irq_reentry_dispatch_pending ? "true" : "false")
              << " hle_return_latency="
              << (diagnostic.hle_irq_return_latency_pending ? "true" : "false")
              << " hle_post_return_armed="
              << (diagnostic.hle_irq_post_return_latency_armed ? "true" : "false")
              << " hle_post_return_dispatch="
              << (diagnostic.hle_irq_post_return_dispatch_pending ? "true" : "false")
              << " hle_chained_dispatch="
              << (diagnostic.hle_irq_chained_post_return_dispatch_pending ? "true" : "false")
              << " hle_chained_data_dispatch="
              << (diagnostic.hle_irq_chained_post_return_data_dispatch_pending ? "true"
                                                                               : "false")
              << " hle_chained_spaced_data_dispatch="
              << (diagnostic.hle_irq_chained_post_return_spaced_data_dispatch_pending
                      ? "true"
                      : "false")
              << " hle_long_chained_return="
              << (diagnostic.hle_irq_long_timer_chained_return_pending ? "true" : "false")
              << " hle_slow_timer0_return="
              << (diagnostic.hle_irq_slow_timer0_return_pending ? "true" : "false")
              << " hle_chain="
              << (diagnostic.hle_irq_post_return_chain_active ? "true" : "false")
              << " hle_chain_spaced_count="
              << static_cast<unsigned>(
                     diagnostic.hle_irq_chained_spaced_data_service_count)
              << " auto_irq_line="
              << (diagnostic.auto_irq_line_high ? "true" : "false")
              << " auto_irq_latency="
              << static_cast<unsigned>(diagnostic.auto_irq_latency_cycles)
              << " ime=0x" << std::hex << diagnostic.ime
              << " ie=0x" << diagnostic.interrupt_enable
              << " if=0x" << diagnostic.interrupt_flags
              << " tm0_ctrl=0x" << diagnostic.timer_controls.at(0)
              << std::dec
              << "/phase=" << diagnostic.timer_enable_phases.at(0)
              << "/next=" << diagnostic.timer_next_ticks.at(0)
              << " pre_ppu_line=" << diagnostic.pre_ppu_line
              << " pre_ppu_line_cycle=" << diagnostic.pre_ppu_line_cycle
              << " pre_dispstat=0x" << std::hex << diagnostic.pre_dispstat
              << " pre_timer0=0x" << diagnostic.pre_timer0
              << " pre_if=0x" << diagnostic.pre_interrupt_flags
              << std::dec
              << " sample_ppu_line=" << diagnostic.sample_ppu_line
              << " sample_ppu_line_cycle=" << diagnostic.sample_ppu_line_cycle
              << " sample_dispstat=0x" << std::hex << diagnostic.sample_dispstat
              << " sample_timer0=0x" << diagnostic.sample_timer0
              << std::dec
              << " ppu_line=" << diagnostic.ppu_line
              << " ppu_line_cycle=" << diagnostic.ppu_line_cycle
              << " sp=0x" << std::hex << diagnostic.sp
              << " r0=0x" << diagnostic.low_registers.at(0)
              << " r1=0x" << diagnostic.low_registers.at(1)
              << " r2=0x" << diagnostic.low_registers.at(2)
              << " r3=0x" << diagnostic.low_registers.at(3)
              << " r4=0x" << diagnostic.low_registers.at(4)
              << " r5=0x" << diagnostic.low_registers.at(5)
              << " r6=0x" << diagnostic.low_registers.at(6)
              << " r7=0x" << diagnostic.low_registers.at(7)
              << " dispstat=0x" << diagnostic.dispstat
              << " timer0=0x" << diagnostic.timer0
              << " data_addr=0x" << diagnostic.data_address
              << std::dec << " data_width=" << static_cast<unsigned>(diagnostic.data_width)
              << " data_load=" << (diagnostic.data_load ? "true" : "false")
              << " data_pre_cycles=" << diagnostic.data_pre_cycles
              << " data_timer_io_gap=" << diagnostic.data_timer_io_gap_cycles;
    if (diagnostic.sample_data_word.has_value()) {
      std::cout << " sample_data_word=0x" << std::hex
                << diagnostic.sample_data_word.value();
    } else {
      std::cout << " sample_data_word=null";
    }
    if (diagnostic.data_word.has_value()) {
      std::cout << " data_word=0x" << std::hex << diagnostic.data_word.value();
    } else {
      std::cout << " data_word=null";
    }
    std::cout << " dma3_src=0x" << std::hex << diagnostic.dma3_source
              << " dma3_dst=0x" << diagnostic.dma3_destination
              << " dma3_count=0x" << diagnostic.dma3_count
              << " dma3_ctrl=0x" << diagnostic.dma3_control
              << std::dec << " dma3_active_count=" << diagnostic.dma3_active_count;
    if (diagnostic.dma3_source_word.has_value()) {
      std::cout << " dma3_src_word=0x" << std::hex
                << diagnostic.dma3_source_word.value();
    } else {
      std::cout << " dma3_src_word=null";
    }
    if (diagnostic.dma3_destination_word.has_value()) {
      std::cout << " dma3_dst_word=0x" << std::hex
                << diagnostic.dma3_destination_word.value();
    } else {
      std::cout << " dma3_dst_word=null";
    }
    if (diagnostic.open_bus.has_value()) {
      std::cout << " open_bus=0x" << std::hex << diagnostic.open_bus.value();
    } else {
      std::cout << " open_bus=null";
    }
    if (diagnostic.pipeline_halfword.has_value()) {
      std::cout << " pipeline_halfword=0x" << std::hex
                << diagnostic.pipeline_halfword.value();
    } else {
      std::cout << " pipeline_halfword=null";
    }
    std::cout << std::dec << '\n';
  }
  std::cout << "suite_diagnostics_end\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 7) {
    std::cerr << "usage: mgba-suite-runner <suite.gba> [max_steps] [trace_steps]"
                 " [input_script] [until_output] [recent_trace_limit]\n";
    return 2;
  }

  const std::filesystem::path rom_path(argv[1]);
  const std::uint32_t max_steps =
      argc >= 3 ? static_cast<std::uint32_t>(std::stoul(argv[2])) : 100000U;
  const std::uint32_t trace_steps =
      argc >= 4 ? static_cast<std::uint32_t>(std::stoul(argv[3])) : 0U;
  const std::vector<InputEvent> input_events = argc >= 5 ? parse_input_script(argv[4])
                                                         : std::vector<InputEvent>{};
  const std::string until_output = argc >= 6 ? argv[5] : "";
  const std::size_t recent_trace_limit =
      argc >= 7 ? static_cast<std::size_t>(std::stoul(argv[6])) : 32U;

  try {
    const std::vector<std::uint8_t> rom = read_file(rom_path);
    gba::core::CoreSession session;
    session.bios().set_mode(gba::core::BiosExecutionMode::hle);
    const bool loaded = session.memory().load_game_pak_rom(rom);
    std::cout << "suite_runner: rom_path=" << rom_path.string() << '\n';
    std::cout << "suite_runner: rom_bytes=" << rom.size() << '\n';
    std::cout << "suite_runner: load_game_pak_rom=" << (loaded ? "true" : "false")
              << '\n';
    if (!loaded) {
      return 1;
    }

    const std::optional<gba::core::GamePakSaveType> save_type =
        session.memory().detect_game_pak_save_type();
    const bool save_configured =
        save_type.has_value() && session.memory().configure_game_pak_save(save_type.value());
    std::cout << "suite_runner: detected_save_type="
              << (save_type.has_value() ? save_type_name(save_type.value()) : "none")
              << '\n';
    std::cout << "suite_runner: configure_game_pak_save="
              << (save_configured ? "true" : "false") << '\n';
    std::cout << "suite_runner: save_bytes=" << session.memory().game_pak_save_size()
              << '\n';
    std::cout << "suite_runner: bios_mode=hle\n";
    std::cout << "suite_runner: input_events=" << input_events.size() << '\n';
    std::cout << "suite_runner: recent_trace_limit=" << recent_trace_limit << '\n';

    const std::optional<gba::core::CartridgeHeader> header =
        session.memory().game_pak_header();
    if (header.has_value()) {
      std::cout << "suite_runner: title=" << header_text(header->title) << '\n';
    }

    session.cpu().set_register(gba::core::Arm7tdmi::kPc, 0x08000000U);
    if (trace_steps > 0) {
      for (std::uint32_t index = 0; index < trace_steps; ++index) {
        const gba::core::CoreSchedulerFetchStepResult step = session.step();
        std::cout << "suite_trace: index=" << index
                  << " set=" << (step.instruction_set == gba::core::CoreInstructionSet::arm
                                      ? "arm"
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
                    << " elapsed=" << step.step->cpu_step.elapsed_cycles
                    << " next_pc=0x" << std::hex
                    << session.cpu().register_value(gba::core::Arm7tdmi::kPc)
                    << std::dec;
        }
        std::cout << '\n';
        if (step.fetch_failed || !step.step.has_value() ||
            step.step->cpu_step.status == gba::core::ExecuteStatus::unsupported) {
          break;
        }
      }
    }

    RunnerLastStep last_step;
    RunnerStop runner_stop;
    std::deque<RunnerLastStep> recent_steps;
    std::vector<RunnerWatchChange> watch_changes;
    std::vector<RunnerDiagnostic> diagnostics;
    const gba::core::CoreSchedulerRunResult result =
        run_with_last_step(session, max_steps, last_step, input_events, until_output,
                           runner_stop, recent_steps, watch_changes, diagnostics,
                           recent_trace_limit);

    std::cout << "suite_runner: requested_steps=" << result.requested_steps << '\n';
    std::cout << "suite_runner: attempted_steps=" << result.attempted_steps << '\n';
    std::cout << "suite_runner: executed_steps=" << result.executed_steps << '\n';
    std::cout << "suite_runner: skipped_steps=" << result.skipped_steps << '\n';
    std::cout << "suite_runner: unsupported_steps=" << result.unsupported_steps << '\n';
    std::cout << "suite_runner: fetch_failures=" << result.fetch_failures << '\n';
    std::cout << "suite_runner: stop_reason=" << stop_reason_name(result.stop_reason)
              << '\n';
    std::cout << "suite_runner: runner_stop_reason=" << runner_stop.reason << '\n';
    std::cout << "suite_runner: until_output=" << until_output << '\n';
    std::cout << "suite_runner: until_output_matched="
              << (runner_stop.until_output_matched ? "true" : "false") << '\n';
    std::cout << "suite_runner: final_pc=0x" << std::hex << result.final_pc << std::dec
              << '\n';
    std::cout << "suite_runner: scheduler_cycles=" << result.scheduler_cycles << '\n';
    std::cout << "suite_runner: state_hash=" << session.state_hash() << '\n';
    std::cout << "suite_runner: last_index=" << last_step.index << '\n';
    std::cout << "suite_runner: last_set="
              << (last_step.instruction_set == gba::core::CoreInstructionSet::arm ? "arm"
                                                                                   : "thumb")
              << '\n';
    std::cout << "suite_runner: last_fetch_pc=0x" << std::hex << last_step.fetch_address
              << '\n';
    if (last_step.instruction.has_value()) {
      std::cout << "suite_runner: last_instruction=0x" << last_step.instruction.value()
                << '\n';
    } else {
      std::cout << "suite_runner: last_instruction=null\n";
    }
    std::cout << "suite_runner: last_runtime_pc=0x" << last_step.pc << '\n';
    for (std::uint8_t reg = 0; reg < last_step.low_registers.size(); ++reg) {
      std::cout << "suite_runner: last_r" << std::dec << static_cast<int>(reg)
                << "=0x" << std::hex << last_step.low_registers.at(reg) << '\n';
    }
    std::cout << "suite_runner: last_sp=0x" << last_step.sp << '\n';
    std::cout << "suite_runner: last_lr=0x" << last_step.lr << '\n';
    std::cout << "suite_runner: last_locale_wctomb=0x" << last_step.locale_wctomb
              << '\n';
    std::cout << "suite_runner: last_active_test=" << std::dec << last_step.active_test
              << '\n';
    std::cout << "suite_runner: last_active_subtest=" << last_step.active_subtest
              << '\n';
    std::cout << "suite_runner: last_thumb_state="
              << (last_step.thumb_state ? "true" : "false") << '\n';
    std::cout << std::dec;
    std::cout << "suite_runner: last_fetch_failed="
              << (last_step.fetch_failed ? "true" : "false") << '\n';
    if (last_step.status.has_value()) {
      std::cout << "suite_runner: last_status="
                << execute_status_name(last_step.status.value()) << '\n';
    } else {
      std::cout << "suite_runner: last_status=null\n";
    }
    print_recent_trace(recent_steps);
    print_watch_changes(watch_changes);
    print_diagnostics(diagnostics);
    std::cout << "suite_runner: active_magic=0x" << std::hex
              << read_i32_or(session.memory(), 0x030000ACU, -1) << std::dec << '\n';
    std::cout << "suite_runner: active_suite_id="
              << read_u8_or(session.memory(), 0x030000B3U, -1) << '\n';
    std::cout << "suite_runner: active_test_id="
              << read_u8_or(session.memory(), 0x030000B2U, -1) << '\n';
    std::cout << "suite_runner: active_subtest_id="
              << read_u16_or(session.memory(), 0x030000B0U, -1) << '\n';
    const std::string debug = session.memory().debug_output();
    const std::string save_text = printable_save_text(session.memory().export_game_pak_save());
    std::cout << "suite_output_begin\n" << debug << "suite_output_end\n";
    std::cout << "suite_sram_text_begin\n" << save_text << "suite_sram_text_end\n";
    std::cout << "suite_summary_json={\"stop_reason\":\""
              << stop_reason_name(result.stop_reason) << "\",\"runner_stop_reason\":\""
              << runner_stop.reason << "\",\"until_output_matched\":"
              << (runner_stop.until_output_matched ? "true" : "false")
              << ",\"final_pc\":\"0x" << std::hex << result.final_pc << std::dec
              << "\",\"state_hash\":\"" << session.state_hash() << "\",\"debug_bytes\":"
              << debug.size() << ",\"sram_text_bytes\":" << save_text.size()
              << ",\"active_suite_id\":"
              << read_u8_or(session.memory(), 0x030000B3U, -1)
              << ",\"active_test_id\":"
              << read_u8_or(session.memory(), 0x030000B2U, -1)
              << ",\"active_subtest_id\":"
              << read_u16_or(session.memory(), 0x030000B0U, -1) << "}\n";
  } catch (const std::exception& error) {
    std::cerr << "suite_runner: error=" << error.what() << '\n';
    return 1;
  }

  return 0;
}
