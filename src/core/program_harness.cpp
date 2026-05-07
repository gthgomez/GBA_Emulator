#include "gba/core/program_harness.hpp"

#include "gba/core/core_session.hpp"
#include "gba/core/memory_bus.hpp"

namespace gba::core {

namespace {

[[nodiscard]] ProgramHarnessResult make_status(ProgramHarnessStatus status) {
  ProgramHarnessResult result{};
  result.status = status;
  return result;
}

[[nodiscard]] bool header_is_valid(const CoreSession& session) {
  const std::optional<CartridgeHeader> header = session.memory().game_pak_header();
  return header.has_value() && header->fixed_value_valid;
}

[[nodiscard]] ProgramHarnessResult evaluate_expected(
    CoreSession& session, const LoadedProgramRunSpec& spec,
    const CoreSchedulerRunResult& run_result) {
  ProgramHarnessResult result{};
  result.stop_reason = run_result.stop_reason;
  result.attempted_steps = run_result.attempted_steps;
  result.executed_steps = run_result.executed_steps;
  result.unsupported_steps = run_result.unsupported_steps;
  result.fetch_failures = run_result.fetch_failures;
  result.final_pc = run_result.final_pc;
  result.scheduler_cycles = run_result.scheduler_cycles;
  result.state_hash = session.state_hash();

  if (run_result.stop_reason != spec.expected.stop_reason) {
    result.status = ProgramHarnessStatus::stop_reason_mismatch;
    return result;
  }

  if (spec.expected.final_pc.has_value() &&
      run_result.final_pc != spec.expected.final_pc.value()) {
    result.status = ProgramHarnessStatus::final_pc_mismatch;
    result.expected_value = spec.expected.final_pc.value();
    result.actual_value = run_result.final_pc;
    return result;
  }

  for (const RegisterExpectation& expected : spec.expected.registers) {
    const std::uint32_t actual = session.cpu().register_value(expected.index);
    if (actual != expected.value) {
      result.status = ProgramHarnessStatus::register_mismatch;
      result.failed_register = expected.index;
      result.expected_value = expected.value;
      result.actual_value = actual;
      return result;
    }
  }

  if (spec.expected.state_hash.has_value() &&
      result.state_hash != spec.expected.state_hash.value()) {
    result.status = ProgramHarnessStatus::state_hash_mismatch;
    return result;
  }

  result.status = ProgramHarnessStatus::passed;
  return result;
}

}  // namespace

ProgramHarnessResult run_loaded_legal_program(CoreSession& session,
                                              const LoadedProgramRunSpec& spec) {
  if (!session.memory().has_game_pak_rom()) {
    return make_status(ProgramHarnessStatus::unloaded_rom);
  }

  if (!session.memory().read16(spec.entry_pc).has_value()) {
    return make_status(ProgramHarnessStatus::unloaded_rom);
  }

  if (spec.require_valid_header && !header_is_valid(session)) {
    return make_status(ProgramHarnessStatus::invalid_header);
  }

  session.waitcnt().write_control(spec.waitcnt_control);
  session.cpu().set_register(Arm7tdmi::kPc, spec.entry_pc);
  const CoreSchedulerRunResult run_result = session.run(spec.max_steps);
  return evaluate_expected(session, spec, run_result);
}

ProgramHarnessResult run_legal_program(CoreSession& session,
                                       const ProgramRunSpec& spec) {
  if (spec.rom.empty()) {
    return make_status(ProgramHarnessStatus::empty_rom);
  }

  session.reset();
  if (!session.memory().load_game_pak_rom(spec.rom)) {
    return make_status(ProgramHarnessStatus::rom_load_rejected);
  }

  return run_loaded_legal_program(session, spec.loaded);
}

}  // namespace gba::core
