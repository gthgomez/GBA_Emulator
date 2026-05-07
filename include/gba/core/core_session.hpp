#pragma once

#include "gba/core/apu.hpp"
#include "gba/core/arm7tdmi.hpp"
#include "gba/core/core_scheduler.hpp"
#include "gba/core/dma_controller.hpp"
#include "gba/core/interrupt_controller.hpp"
#include "gba/core/memory_bus.hpp"
#include "gba/core/ppu_timing.hpp"
#include "gba/core/timers.hpp"
#include "gba/core/wait_state_control.hpp"

#include <cstdint>

namespace gba::core {

struct CoreSessionState {
  Arm7tdmi cpu;
  MemoryBus memory;
  InterruptController interrupts;
  Timers timers;
  DmaController dma;
  PpuTiming ppu;
  Apu apu;
  WaitStateControl waitcnt;
  CoreSchedulerState scheduler;
};

class CoreSession {
 public:
  CoreSession();

  void reset();

  [[nodiscard]] Arm7tdmi& cpu();
  [[nodiscard]] const Arm7tdmi& cpu() const;
  [[nodiscard]] MemoryBus& memory();
  [[nodiscard]] const MemoryBus& memory() const;
  [[nodiscard]] InterruptController& interrupts();
  [[nodiscard]] Timers& timers();
  [[nodiscard]] DmaController& dma();
  [[nodiscard]] PpuTiming& ppu();
  [[nodiscard]] Apu& apu();
  [[nodiscard]] WaitStateControl& waitcnt();
  [[nodiscard]] CoreScheduler& scheduler();

  [[nodiscard]] CoreSchedulerFetchStepResult step();
  [[nodiscard]] CoreSchedulerRunResult run(std::uint32_t max_steps);

  [[nodiscard]] CoreSessionState save_state() const;
  void load_state(const CoreSessionState& state);
  [[nodiscard]] std::uint64_t state_hash() const;

 private:
  Arm7tdmi cpu_;
  MemoryBus memory_;
  InterruptController interrupts_;
  Timers timers_;
  DmaController dma_;
  PpuTiming ppu_;
  Apu apu_;
  WaitStateControl waitcnt_;
  CoreScheduler scheduler_;
};

}  // namespace gba::core
