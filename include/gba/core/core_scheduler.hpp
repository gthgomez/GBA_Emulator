#pragma once

#include "gba/core/apu.hpp"
#include "gba/core/arm7tdmi.hpp"
#include "gba/core/bios.hpp"
#include "gba/core/dma_controller.hpp"
#include "gba/core/interrupt_controller.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace gba::core {

class MemoryBus;
class PpuTiming;
class Timers;
class WaitStateControl;

struct CoreDeviceTickResult {
  std::uint32_t cycles = 0;
  std::optional<ApuFrameStep> apu_frame_step = std::nullopt;
  std::uint32_t apu_timer_events = 0;
  DirectSoundTimerResult last_direct_sound = {};
  DmaRunResult triggered_dma = {};
};

struct CoreSchedulerStepResult {
  ArmStepResult cpu_step;
  CoreDeviceTickResult devices;
  DmaRunResult immediate_dma;
  bool irq_serviced;
  std::uint64_t scheduler_cycles;
};

enum class CoreRunStopReason : std::uint8_t {
  max_steps,
  fetch_failed,
  unsupported_instruction,
};

enum class CoreInstructionSet : std::uint8_t {
  arm,
  thumb,
};

struct CoreSchedulerFetchStepResult {
  CoreInstructionSet instruction_set;
  std::uint8_t instruction_width_bytes;
  std::uint32_t fetch_address;
  std::optional<std::uint32_t> instruction;
  std::optional<CoreSchedulerStepResult> step;
  std::uint32_t fetch_cycles;
  bool fetch_failed;
  bool pc_advanced;
  bool fetch_timing_applied;
  bool fetch_sequential;
  bool prefetch_enabled;
  bool prefetch_hit;
  std::uint8_t prefetch_buffer_halfwords;
  bool boundary_forced_nonsequential;
};

struct CoreSchedulerRunResult {
  std::uint32_t requested_steps;
  std::uint32_t attempted_steps;
  std::uint32_t executed_steps;
  std::uint32_t skipped_steps;
  std::uint32_t unsupported_steps;
  std::uint32_t fetch_failures;
  CoreRunStopReason stop_reason;
  std::uint32_t final_pc;
  std::uint64_t scheduler_cycles;
};

struct CoreSchedulerState {
  std::uint64_t scheduler_cycles = 0;
  bool halted = false;
  std::optional<std::uint32_t> last_fetch_address;
  std::optional<std::uint8_t> last_fetch_width_bytes;
  std::optional<std::uint8_t> last_fetch_window;
  std::uint8_t prefetch_buffer_halfwords = 0;
  std::optional<std::uint32_t> hle_irq_return_lr;
  std::optional<std::array<std::uint32_t, 13>> hle_irq_saved_registers;
  bool auto_irq_line_high = false;
  std::uint8_t auto_irq_latency_cycles = 0;
};

class CoreScheduler {
 public:
  CoreScheduler(Arm7tdmi& cpu, MemoryBus& memory, InterruptController& interrupts,
                Timers& timers, DmaController& dma, PpuTiming& ppu, Apu& apu);
  CoreScheduler(Arm7tdmi& cpu, MemoryBus& memory, InterruptController& interrupts,
                Timers& timers, DmaController& dma, PpuTiming& ppu, Apu& apu,
                const WaitStateControl& waitcnt);
  CoreScheduler(Arm7tdmi& cpu, MemoryBus& memory, InterruptController& interrupts,
                Timers& timers, DmaController& dma, PpuTiming& ppu, Apu& apu,
                const WaitStateControl& waitcnt, BiosController& bios);

  [[nodiscard]] std::uint64_t scheduler_cycles() const;
  void reset_scheduler_cycles();
  [[nodiscard]] CoreSchedulerState save_state() const;
  void load_state(const CoreSchedulerState& state);
  [[nodiscard]] std::uint64_t state_hash() const;
  [[nodiscard]] bool halted() const;
  void halt_until_interrupt();
  [[nodiscard]] bool wake_from_halt_if_irq_pending();

  [[nodiscard]] CoreDeviceTickResult advance_devices(std::uint32_t cycles);
  [[nodiscard]] DmaRunResult run_immediate_dma();
  [[nodiscard]] bool service_pending_irq();
  [[nodiscard]] CoreSchedulerStepResult step_arm(std::uint32_t instruction);
  [[nodiscard]] CoreSchedulerStepResult step_thumb(std::uint16_t instruction);
  [[nodiscard]] CoreSchedulerFetchStepResult step_arm_from_pc();
  [[nodiscard]] CoreSchedulerFetchStepResult step_from_pc();
  [[nodiscard]] CoreSchedulerRunResult run_arm_from_pc(std::uint32_t max_steps);
  [[nodiscard]] CoreSchedulerRunResult run_from_pc(std::uint32_t max_steps);

 private:
  Arm7tdmi& cpu_;
  MemoryBus& memory_;
  InterruptController& interrupts_;
  Timers& timers_;
  DmaController& dma_;
  PpuTiming& ppu_;
  Apu& apu_;
  BiosController* bios_;
  std::uint64_t scheduler_cycles_;
  bool halted_;
  const WaitStateControl* waitcnt_;
  std::optional<std::uint32_t> last_fetch_address_;
  std::optional<std::uint8_t> last_fetch_width_bytes_;
  std::optional<std::uint8_t> last_fetch_window_;
  std::uint8_t prefetch_buffer_halfwords_;
  std::optional<std::uint32_t> hle_irq_return_lr_;
  std::optional<std::array<std::uint32_t, 13>> hle_irq_saved_registers_;
  bool auto_irq_line_high_;
  std::uint8_t auto_irq_latency_cycles_;

  [[nodiscard]] std::uint32_t apply_fetch_timing(std::uint32_t fetch_address,
                                                 std::uint8_t width_bytes,
                                                 bool& timing_applied,
                                                 bool& sequential,
                                                 bool& prefetch_enabled,
                                                 bool& prefetch_hit,
                                                 bool& boundary_forced_nonsequential);
  void refill_prefetch_after_step(std::uint32_t fetch_address, std::uint8_t width_bytes,
                                  const CoreSchedulerStepResult& step);
  void reset_fetch_timing_sequence();
  void update_auto_irq_latency(std::uint32_t elapsed_cycles);
  [[nodiscard]] bool auto_irq_ready() const;
  void reset_auto_irq_latency();
  [[nodiscard]] bool bios_hle_enabled() const;
  [[nodiscard]] std::optional<CoreSchedulerStepResult> dispatch_hle_irq_vector(
      std::uint32_t fetch_address);
  [[nodiscard]] std::optional<CoreSchedulerStepResult> dispatch_hle_irq_return(
      std::uint32_t fetch_address);
  [[nodiscard]] std::optional<ArmStepResult> execute_hle_arm_swi(
      std::uint32_t instruction);
  [[nodiscard]] std::optional<ArmStepResult> execute_hle_thumb_swi(
      std::uint16_t instruction);
  [[nodiscard]] ArmStepResult execute_hle_swi(BiosSwiCall call);
  [[nodiscard]] bool wait_for_interrupt_mask(std::uint16_t mask, bool discard_old_flags);
  [[nodiscard]] bool hle_cpu_set(bool fast);
};

}  // namespace gba::core
