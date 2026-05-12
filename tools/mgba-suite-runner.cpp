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
  bool hle_irq_post_return_chain_active = false;
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

gba::core::CoreSchedulerRunResult run_with_last_step(gba::core::CoreSession& session,
                                                     std::uint32_t max_steps,
                                                     RunnerLastStep& last_step,
                                                     const std::vector<InputEvent>& events,
                                                     std::string_view until_output,
                                                     RunnerStop& runner_stop,
                                                     std::deque<RunnerLastStep>& recent_steps,
                                                     std::vector<RunnerWatchChange>& watch_changes,
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
    last_step.hle_irq_post_return_chain_active =
        scheduler_state.hle_irq_post_return_chain_active;
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
      }
    }
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
    const gba::core::CoreSchedulerRunResult result =
        run_with_last_step(session, max_steps, last_step, input_events, until_output,
                           runner_stop, recent_steps, watch_changes,
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
