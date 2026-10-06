#pragma once

#include "gba/core/apu.hpp"
#include "gba/core/arm7tdmi.hpp"
#include "gba/core/bios.hpp"
#include "gba/core/dma_controller.hpp"
#include "gba/core/interrupt_controller.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace gba::core {

class MemoryBus;
class IoRegisters;
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

struct CoreDataAccessTrace {
  std::uint32_t address = 0;
  std::uint8_t width_bytes = 0;
  bool load = false;
  bool timer_io = false;
  std::uint32_t pre_access_cycles = 0;
  std::uint32_t timer_io_gap_cycles = 0;
};

struct CoreSchedulerStepResult {
  ArmStepResult cpu_step;
  CoreDeviceTickResult devices;
  DmaRunResult immediate_dma;
  bool irq_serviced;
  std::uint64_t scheduler_cycles;
  std::optional<CoreDataAccessTrace> data_access;
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
  bool suppress_next_game_pak_prefetch = false;
  bool recover_next_game_pak_data_fetch = false;
  bool suppress_next_thumb_prefetch_execute_bubble = false;
  bool previous_thumb_internal_load = false;
  std::optional<std::uint16_t> last_waitcnt_control;
  std::optional<std::uint32_t> hle_irq_return_lr;
  std::optional<std::array<std::uint32_t, 13>> hle_irq_saved_registers;
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
  std::uint32_t thumb_misfetch_recovery_count = 0;
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
  [[nodiscard]] std::uint32_t thumb_misfetch_recovery_count() const;
  void reset_scheduler_cycles();
  [[nodiscard]] CoreSchedulerState save_state() const;
  void load_state(const CoreSchedulerState& state);
  [[nodiscard]] std::uint64_t state_hash() const;
  [[nodiscard]] bool halted() const;
  void halt_until_interrupt();
  [[nodiscard]] bool wake_from_halt_if_irq_pending();
  void set_io_registers(IoRegisters& io);
  // Injectable diagnostics sink for deterministic-core tracing (SWI Halt
  // entry, etc.). Null by default; the core never reads environment state.
  void set_debug_trace_sink(std::function<void(const char*)> sink);

  [[nodiscard]] CoreDeviceTickResult advance_devices(std::uint32_t cycles);
  [[nodiscard]] DmaRunResult run_immediate_dma();
  [[nodiscard]] bool service_pending_irq(
      bool from_data_access = false,
      bool from_spaced_timer_io_access = false);
  [[nodiscard]] CoreSchedulerStepResult step_arm(
      std::uint32_t instruction,
      std::optional<ArmElapsedCycleEstimate> elapsed_override = std::nullopt);
  [[nodiscard]] CoreSchedulerStepResult step_thumb(
      std::uint16_t instruction, bool prefetch_internal_load_overlap = false,
      std::optional<ArmElapsedCycleEstimate> elapsed_override = std::nullopt);
  [[nodiscard]] CoreSchedulerFetchStepResult step_arm_from_pc();
  [[nodiscard]] CoreSchedulerFetchStepResult step_from_pc();
  [[nodiscard]] CoreSchedulerRunResult run_steps_from_pc(
      std::uint32_t max_steps,
      CoreSchedulerFetchStepResult (CoreScheduler::*step_fn)());
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
  IoRegisters* io_;
  std::uint64_t scheduler_cycles_;
  bool halted_;
  const WaitStateControl* waitcnt_;
  std::optional<std::uint16_t> last_waitcnt_control_;
  std::optional<std::uint32_t> last_fetch_address_;
  std::optional<std::uint8_t> last_fetch_width_bytes_;
  std::optional<std::uint8_t> last_fetch_window_;
  std::uint8_t prefetch_buffer_halfwords_;
  bool suppress_next_game_pak_prefetch_;
  bool recover_next_game_pak_data_fetch_;
  bool suppress_next_thumb_prefetch_execute_bubble_;
  bool previous_thumb_internal_load_;
  std::optional<std::uint32_t> hle_irq_return_lr_;
  std::optional<std::array<std::uint32_t, 13>> hle_irq_saved_registers_;
  bool hle_irq_reentry_dispatch_pending_;
  bool hle_irq_return_latency_pending_;
  bool hle_irq_post_return_latency_armed_;
  bool hle_irq_post_return_dispatch_pending_;
  bool hle_irq_chained_post_return_dispatch_pending_;
  bool hle_irq_chained_post_return_data_dispatch_pending_;
  bool hle_irq_chained_post_return_spaced_data_dispatch_pending_;
  bool hle_irq_long_timer_chained_return_pending_;
  bool hle_irq_slow_timer0_return_pending_;
  bool hle_irq_post_return_chain_active_;
  std::uint8_t hle_irq_chained_spaced_data_service_count_;
  bool auto_irq_line_high_;
  std::uint8_t auto_irq_latency_cycles_;
  std::uint32_t timer_io_access_gap_cycles_;
  std::uint32_t thumb_misfetch_recovery_count_;
  std::function<void(const char*)> debug_trace_sink_;

  [[nodiscard]] std::uint32_t apply_fetch_timing(std::uint32_t fetch_address,
                                                 std::uint8_t width_bytes,
                                                 bool& timing_applied,
                                                 bool& sequential,
                                                 bool& prefetch_enabled,
                                                 bool& prefetch_hit,
                                                 bool& boundary_forced_nonsequential);
  void refill_prefetch_after_step(std::uint32_t fetch_address, std::uint8_t width_bytes,
                                  const CoreSchedulerStepResult& step,
                                  std::uint32_t extra_refill_cycles = 0);
  void reset_fetch_timing_sequence();
  // Fast path shared by the duplicate simple-instruction blocks inside
  // step_arm_from_pc: executes the instruction, advances devices, and
  // services an auto-IRQ when the PC stayed sequential.
  [[nodiscard]] CoreSchedulerStepResult step_simple_arm_fast_path(
      std::uint32_t instruction, std::uint32_t fetch_address);
  // Distance in cycles until the next interrupt source covered by `mask`
  // could newly assert, so IntrWait can advance devices in coarse batches
  // without overshooting a wake edge. Always >= 1.
  [[nodiscard]] std::uint32_t intr_wait_batch_cycles(std::uint16_t mask) const;
  [[nodiscard]] std::uint32_t intr_wait_timer_horizon_cycles(std::uint16_t mask) const;
  [[nodiscard]] std::uint32_t intr_wait_ppu_horizon_cycles(std::uint16_t mask) const;
  [[nodiscard]] std::uint32_t intr_wait_serial_horizon_cycles(std::uint16_t mask) const;
  void update_auto_irq_latency(std::uint32_t elapsed_cycles);
  [[nodiscard]] bool auto_irq_ready() const;
  void reset_auto_irq_latency();
  [[nodiscard]] bool bios_hle_enabled() const;
  [[nodiscard]] std::optional<CoreSchedulerStepResult> dispatch_hle_irq_vector(
      std::uint32_t fetch_address);
  [[nodiscard]] std::optional<CoreSchedulerStepResult> dispatch_hle_irq_return(
      std::uint32_t fetch_address);
  [[nodiscard]] std::optional<ArmStepResult> execute_hle_arm_swi(
      std::uint32_t instruction, std::uint32_t fetch_address);
  [[nodiscard]] std::optional<ArmStepResult> execute_hle_thumb_swi(
      std::uint16_t instruction, std::uint32_t fetch_address);
  [[nodiscard]] ArmStepResult execute_hle_swi(BiosSwiCall call,
                                              std::uint32_t fetch_address);
  [[nodiscard]] bool wait_for_interrupt_mask(std::uint16_t mask, bool discard_old_flags);
  // Returns nullopt on failure, otherwise the device cycles already consumed
  // by chunked advancement during the copy (deducted from the SWI's charged
  // cycles to preserve final-cycle totals).
  [[nodiscard]] std::optional<std::uint32_t> hle_cpu_set(bool fast);
  [[nodiscard]] bool hle_lz77_decompress_from_source(std::uint32_t source,
                                                     std::vector<std::uint8_t>& output);
  [[nodiscard]] bool hle_rle_decompress_from_source(std::uint8_t expected_type,
                                                    std::uint32_t source,
                                                    std::vector<std::uint8_t>& output);
  [[nodiscard]] bool hle_diff8_decompress_from_source(std::uint8_t expected_type,
                                                        std::uint32_t source,
                                                        std::vector<std::uint8_t>& output);
  [[nodiscard]] bool hle_diff16_decompress_from_source(std::uint8_t expected_type,
                                                         std::uint32_t source,
                                                         std::vector<std::uint8_t>& output);
  [[nodiscard]] bool hle_write_decompressed_wram(const std::vector<std::uint8_t>& output);
  [[nodiscard]] bool hle_write_decompressed_vram(const std::vector<std::uint8_t>& output);
  [[nodiscard]] bool hle_lz77_uncomp_vram();
  [[nodiscard]] bool hle_lz77_uncomp_wram();
  [[nodiscard]] bool hle_rl_uncomp_wram();
  [[nodiscard]] bool hle_rl_uncomp_vram();
  [[nodiscard]] bool hle_diff8_unfilter_wram();
  [[nodiscard]] bool hle_diff8_unfilter_vram();
  [[nodiscard]] bool hle_diff16_unfilter();
  [[nodiscard]] bool hle_bit_unpack();
  [[nodiscard]] bool hle_bg_affine_set();
  [[nodiscard]] bool hle_obj_affine_set();
};

}  // namespace gba::core
