// Focused verifier for the IntrWait serial horizon
// (CoreScheduler::intr_wait_serial_horizon_cycles, added alongside the S4
// batched device-advance path).
//
// Before the horizon existed every IntrWait device chunk advanced up to
// kIntrWaitCoarseBatchCycles (64) regardless of an in-flight serial transfer.
// A transfer whose completion edge fell inside that window was therefore
// observed up to 63 cycles late, because the serial IRQ is only requested
// inside IoRegisters::tick when the chunk reaches the edge. This verifier seeds
// SIOCNT so the completion edge falls inside a single batching window, runs the
// wait-for-interrupt path (SWI 4 / IntrWait), and asserts the scheduler wakes
// exactly on that edge rather than at the end of a coarse chunk.

#include "gba/core/apu.hpp"
#include "gba/core/arm7tdmi.hpp"
#include "gba/core/bios.hpp"
#include "gba/core/core_scheduler.hpp"
#include "gba/core/dma_controller.hpp"
#include "gba/core/interrupt_controller.hpp"
#include "gba/core/io_registers.hpp"
#include "gba/core/memory_bus.hpp"
#include "gba/core/ppu_timing.hpp"
#include "gba/core/timers.hpp"
#include "gba/core/wait_state_control.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

#include "test_helpers.hpp"

namespace {

constexpr std::uint32_t kGamePakProgramBase = 0x08000000U;
constexpr std::uint32_t kSiocnt = 0x04000128U;
// Normal 8-bit mode, internal clock, IRQ enabled, start bit set.
constexpr std::uint16_t kStartNormalEightBitTransfer = 0x4081U;
// 8 bits * 64 cycles/bit + 40 cycles completion latency.
constexpr std::uint32_t kNormalEightBitTransferCycles = 552U;
// Must match CoreScheduler's private kIntrWaitCoarseBatchCycles: the value a
// batched IntrWait would advance if the serial horizon were absent.
constexpr std::uint32_t kCoarseBatchCycles = 64U;
constexpr std::uint16_t kThumbSwi4 = 0xDF04U;

}  // namespace

int main() {
  using gba::core::Apu;
  using gba::core::Arm7tdmi;
  using gba::core::BiosController;
  using gba::core::BiosExecutionMode;
  using gba::core::CoreScheduler;
  using gba::core::CoreSchedulerFetchStepResult;
  using gba::core::CoreSchedulerState;
  using gba::core::DmaController;
  using gba::core::ExecuteStatus;
  using gba::core::InterruptController;
  using gba::core::InterruptSource;
  using gba::core::IoRegisters;
  using gba::core::MemoryBus;
  using gba::core::PpuTiming;
  using gba::core::Timers;
  using gba::core::WaitStateControl;

  Arm7tdmi cpu;
  MemoryBus memory;
  InterruptController interrupts;
  Timers timers;
  DmaController dma;
  PpuTiming ppu;
  Apu apu;
  WaitStateControl waitcnt;
  gba::core::Keypad keypad;
  IoRegisters io(interrupts, timers, dma, ppu, apu, waitcnt, keypad);
  BiosController bios;
  bios.set_mode(BiosExecutionMode::hle);
  CoreScheduler scheduler(cpu, memory, interrupts, timers, dma, ppu, apu, waitcnt,
                          bios);
  scheduler.set_io_registers(io);

  std::vector<std::uint8_t> intr_wait_rom(256);
  intr_wait_rom.at(0x20U) = static_cast<std::uint8_t>(kThumbSwi4 & 0xFFU);
  intr_wait_rom.at(0x21U) = static_cast<std::uint8_t>((kThumbSwi4 >> 8U) & 0xFFU);

  auto seed = [&](bool preset_serial_flag) {
    cpu.reset();
    memory.reset();
    timers.reset();
    ppu.reset();
    apu.reset();
    dma.reset();
    interrupts.reset();
    io.reset();
    scheduler.reset_scheduler_cycles();
    scheduler.load_state(CoreSchedulerState{});
    expect(memory.load_game_pak_rom(intr_wait_rom), "serial-horizon ROM loads");
    expect(cpu.set_cpsr(0x0000001FU | 0x20U),
           "serial-horizon enters Thumb system mode");
    cpu.set_register(Arm7tdmi::kPc, kGamePakProgramBase + 0x20U);
    cpu.set_register(0, 0);  // r0: do not discard old flags
    cpu.set_register(1, irq_bit(InterruptSource::serial));  // r1: wait mask
    interrupts.write_interrupt_enable(irq_bit(InterruptSource::serial));
    interrupts.write_ime(0);  // keep the wake deterministic (no IRQ vector)
    if (preset_serial_flag) {
      interrupts.request(InterruptSource::serial);
    }
  };

  // Control: the serial flag is pre-set, so the wait returns without spinning.
  // Its cost is exactly the SWI fetch device cycles plus the IntrWait return
  // device cycles, which calibrates the runs below without hardcoding either.
  seed(true);
  const std::uint64_t control_before = scheduler.scheduler_cycles();
  [[maybe_unused]] const CoreSchedulerFetchStepResult control_step =
      scheduler.step_from_pc();
  const std::uint64_t control_delta =
      scheduler.scheduler_cycles() - control_before;

  // Seeds an in-flight normal 8-bit serial transfer whose completion edge sits
  // `remaining` cycles into the wait, pre-ticking away the rest so the edge
  // lands inside a single 64-cycle batching window.
  auto run_serial = [&](std::uint32_t remaining) {
    expect(remaining < kCoarseBatchCycles,
           "serial edge must fall inside one batching window");
    seed(false);
    expect(io.write16(kSiocnt, kStartNormalEightBitTransfer),
           "SIOCNT starts a normal 8-bit internal-clock transfer");
    expect(io.sio_transfer_active(), "serial transfer is active after start");
    io.tick(kNormalEightBitTransferCycles - remaining);
    expect(io.sio_transfer_cycles_remaining() == remaining,
           "serial transfer is pre-ticked to the intended remaining cycles");
    expect(io.sio_transfer_active(),
           "serial transfer is still in flight when IntrWait begins");

    const std::uint64_t before = scheduler.scheduler_cycles();
    const CoreSchedulerFetchStepResult result = scheduler.step_from_pc();
    const std::uint64_t delta = scheduler.scheduler_cycles() - before;

    expect(result.step.has_value() &&
               result.step->cpu_step.status == ExecuteStatus::executed,
           "serial-horizon IntrWait executes");
    expect(interrupts.requested(InterruptSource::serial),
           "serial transfer requests its IRQ exactly when it completes");
    expect(!io.sio_transfer_active() && io.sio_transfer_cycles_remaining() == 0,
           "serial transfer has fully completed by the time IntrWait wakes");
    return delta;
  };

  constexpr std::uint32_t kEdgeA = 40U;
  constexpr std::uint32_t kEdgeB = 50U;
  constexpr std::uint32_t kEdgeC = 60U;

  const std::uint64_t delta_a = run_serial(kEdgeA);
  const std::uint64_t delta_b = run_serial(kEdgeB);
  const std::uint64_t delta_c = run_serial(kEdgeC);

  // The wait's spin is the run cost minus the calibrated fetch+return cost. It
  // must equal the distance from the (post-fetch) transfer state to the
  // completion edge: strictly positive (the edge was not crossed by the fetch)
  // and strictly shorter than a coarse chunk (the horizon bounded it).
  const std::uint64_t spin_a = delta_a - control_delta;
  expect(spin_a > 0U, "IntrWait actually spins to reach the serial edge");
  expect(spin_a < kCoarseBatchCycles,
         "serial horizon bounds the chunk below the coarse batching window");
  expect(spin_a < kEdgeA,
         "wake occurs at the completion edge, not after overrunning it");

  // Exactness is self-calibrating: shifting the edge deeper into the window must
  // move the wake by exactly the same number of cycles. Coarse-only batching
  // would wake both runs at the same 64-cycle boundary and report a zero delta.
  expect(delta_b - delta_a == kEdgeB - kEdgeA,
         "serial IntrWait wake tracks the completion edge exactly");
  expect(delta_c - delta_a == kEdgeC - kEdgeA,
         "serial IntrWait wake tracks a second completion edge exactly");

  // Determinism guard: an identical reseed reproduces the wake bit-for-bit.
  const std::uint64_t delta_a_repeat = run_serial(kEdgeA);
  expect(delta_a_repeat == delta_a,
         "serial IntrWait wake is deterministic across reseeds");

  std::cout << "intr_wait_serial_horizon_test: PASS\n";
  return 0;
}
