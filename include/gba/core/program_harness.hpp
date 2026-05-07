#pragma once

#include "gba/core/core_scheduler.hpp"
#include "gba/core/wait_state_control.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace gba::core {

class CoreSession;

enum class ProgramHarnessStatus : std::uint8_t {
  passed,
  empty_rom,
  rom_load_rejected,
  unloaded_rom,
  invalid_header,
  stop_reason_mismatch,
  final_pc_mismatch,
  register_mismatch,
  state_hash_mismatch,
};

struct RegisterExpectation {
  std::uint8_t index = 0;
  std::uint32_t value = 0;
};

struct ProgramExpectedState {
  CoreRunStopReason stop_reason = CoreRunStopReason::max_steps;
  std::optional<std::uint32_t> final_pc = std::nullopt;
  std::vector<RegisterExpectation> registers;
  std::optional<std::uint64_t> state_hash = std::nullopt;
};

struct LoadedProgramRunSpec {
  std::uint32_t entry_pc = 0x08000000U;
  std::uint32_t max_steps = 1;
  bool require_valid_header = false;
  std::uint16_t waitcnt_control = WaitStateControl::kStandardGamePakSetting;
  ProgramExpectedState expected;
};

struct ProgramRunSpec {
  std::vector<std::uint8_t> rom;
  LoadedProgramRunSpec loaded;
};

struct ProgramHarnessResult {
  ProgramHarnessStatus status = ProgramHarnessStatus::passed;
  CoreRunStopReason stop_reason = CoreRunStopReason::max_steps;
  std::uint32_t attempted_steps = 0;
  std::uint32_t executed_steps = 0;
  std::uint32_t unsupported_steps = 0;
  std::uint32_t fetch_failures = 0;
  std::uint32_t final_pc = 0;
  std::uint64_t scheduler_cycles = 0;
  std::uint64_t state_hash = 0;
  std::optional<std::uint8_t> failed_register = std::nullopt;
  std::optional<std::uint32_t> expected_value = std::nullopt;
  std::optional<std::uint32_t> actual_value = std::nullopt;
};

[[nodiscard]] ProgramHarnessResult run_loaded_legal_program(
    CoreSession& session, const LoadedProgramRunSpec& spec);
[[nodiscard]] ProgramHarnessResult run_legal_program(CoreSession& session,
                                                     const ProgramRunSpec& spec);

}  // namespace gba::core
