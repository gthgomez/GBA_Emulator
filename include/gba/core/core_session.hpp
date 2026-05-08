#pragma once

#include "gba/core/apu.hpp"
#include "gba/core/arm7tdmi.hpp"
#include "gba/core/bios.hpp"
#include "gba/core/core_scheduler.hpp"
#include "gba/core/dma_controller.hpp"
#include "gba/core/interrupt_controller.hpp"
#include "gba/core/io_registers.hpp"
#include "gba/core/keypad.hpp"
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
  BiosController bios;
  Keypad keypad;
  WaitStateControl waitcnt;
  IoRegistersState io;
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
  [[nodiscard]] BiosController& bios();
  [[nodiscard]] const BiosController& bios() const;
  [[nodiscard]] Keypad& keypad();
  [[nodiscard]] const Keypad& keypad() const;
  [[nodiscard]] WaitStateControl& waitcnt();
  [[nodiscard]] const WaitStateControl& waitcnt() const;
  [[nodiscard]] IoRegisters& io();
  [[nodiscard]] const IoRegisters& io() const;
  [[nodiscard]] CoreScheduler& scheduler();
  [[nodiscard]] const CoreScheduler& scheduler() const;

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
  BiosController bios_;
  Keypad keypad_;
  WaitStateControl waitcnt_;
  IoRegisters io_;
  CoreScheduler scheduler_;
};

}  // namespace gba::core
