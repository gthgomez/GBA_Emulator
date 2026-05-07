#include "gba/core/apu.hpp"
#include "gba/core/arm7tdmi.hpp"
#include "gba/core/core_scheduler.hpp"
#include "gba/core/core_session.hpp"
#include "gba/core/dma_controller.hpp"
#include "gba/core/interrupt_controller.hpp"
#include "gba/core/io_registers.hpp"
#include "gba/core/memory_bus.hpp"
#include "gba/core/ppu_background.hpp"
#include "gba/core/ppu_sprites.hpp"
#include "gba/core/ppu_timing.hpp"
#include "gba/core/timers.hpp"
#include "gba/core/wait_state_control.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct BenchmarkResult {
  std::string_view name;
  std::uint64_t operations;
  double elapsed_ms;
  double operations_per_second;
  double ns_per_operation;
  std::uint64_t checksum;
};

void fail(std::string_view message) {
  std::cerr << "core_benchmark: FAIL: " << message << '\n';
  std::exit(1);
}

void require(bool condition, std::string_view message) {
  if (!condition) {
    fail(message);
  }
}

template <typename Workload>
BenchmarkResult measure(std::string_view name, std::uint64_t operations,
                        Workload workload) {
  const Clock::time_point start = Clock::now();
  const std::uint64_t checksum = workload();
  const Clock::time_point end = Clock::now();
  const std::chrono::duration<double, std::milli> elapsed = end - start;
  const double elapsed_ms = elapsed.count();
  const double ops_per_second =
      elapsed_ms > 0.0 ? static_cast<double>(operations) * 1000.0 / elapsed_ms : 0.0;
  const double ns_per_operation =
      operations > 0 ? elapsed_ms * 1000000.0 / static_cast<double>(operations) : 0.0;
  return {name, operations, elapsed_ms, ops_per_second, ns_per_operation, checksum};
}

void print_result(const BenchmarkResult& result) {
  std::cout << result.name << ',' << result.operations << ',' << std::fixed
            << std::setprecision(3) << result.elapsed_ms << ',' << std::setprecision(2)
            << result.operations_per_second << ',' << std::setprecision(2)
            << result.ns_per_operation << ',' << result.checksum << '\n';
}

std::uint64_t benchmark_memory_bus_rw32() {
  constexpr std::uint32_t kIterations = 1000000;
  gba::core::MemoryBus memory;
  std::uint64_t checksum = 0;

  for (std::uint32_t i = 0; i < kIterations; ++i) {
    const std::uint32_t address = 0x03000000U + ((i * 4U) & 0x7FFCU);
    const std::uint32_t value = i ^ 0xA5A5A5A5U;
    require(memory.write32(address, value), "memory write32 failed");
    const std::optional<std::uint32_t> read = memory.read32(address);
    require(read.has_value(), "memory read32 failed");
    checksum += read.value() ^ i;
  }

  return checksum;
}

std::uint64_t benchmark_cpu_step_arm_add() {
  constexpr std::uint32_t kIterations = 1000000;
  constexpr std::uint32_t kAddR0R0Imm1 = 0xE2800001U;
  gba::core::Arm7tdmi cpu;
  std::uint64_t checksum = 0;

  for (std::uint32_t i = 0; i < kIterations; ++i) {
    const gba::core::ArmStepResult result = cpu.step_arm(kAddR0R0Imm1);
    require(result.status == gba::core::ExecuteStatus::executed,
            "ARM ADD step did not execute");
    checksum += cpu.register_value(0) + result.elapsed_cycles;
  }

  return checksum ^ cpu.elapsed_cycles();
}

std::uint64_t benchmark_io_register_mix() {
  constexpr std::uint32_t kIterations = 250000;
  gba::core::InterruptController interrupts;
  gba::core::Timers timers;
  gba::core::DmaController dma;
  gba::core::PpuTiming ppu;
  gba::core::Apu apu;
  gba::core::WaitStateControl waitcnt;
  gba::core::IoRegisters io(interrupts, timers, dma, ppu, apu, waitcnt);
  std::uint64_t checksum = 0;

  require(io.write16(gba::core::IoRegisters::kSoundcntX, 0x0080),
          "APU master enable IO write failed");

  for (std::uint32_t i = 0; i < kIterations; ++i) {
    require(io.write16(gba::core::IoRegisters::kTimerBase,
                       static_cast<std::uint16_t>(i)),
            "timer reload IO write failed");
    require(io.write16(gba::core::IoRegisters::kTimerBase + 2U, 0x0080),
            "timer control IO write failed");
    require(io.write32(gba::core::IoRegisters::kDmaBase, 0x02000000U + (i & 0xFFU)),
            "DMA source IO write failed");
    require(io.write32(gba::core::IoRegisters::kDmaBase + 4U,
                       0x03000000U + ((i * 4U) & 0x3FFU)),
            "DMA destination IO write failed");
    require(io.write16(gba::core::IoRegisters::kIe,
                       static_cast<std::uint16_t>(i & 0x3FFFU)),
            "IE IO write failed");
    require(io.write32(gba::core::IoRegisters::kFifoA, 0x04030201U ^ i),
            "FIFO A IO write failed");

    const std::optional<std::uint16_t> ie = io.read16(gba::core::IoRegisters::kIe);
    const std::optional<std::uint16_t> tm0 =
        io.read16(gba::core::IoRegisters::kTimerBase + 2U);
    require(ie.has_value() && tm0.has_value(), "IO readback failed");
    checksum += ie.value() + tm0.value() + apu.fifo_size(gba::core::DirectSoundChannel::a);
  }

  return checksum;
}

std::uint64_t benchmark_waitcnt_timing_lookup() {
  constexpr std::uint32_t kIterations = 500000;
  gba::core::WaitStateControl waitcnt;
  std::uint64_t checksum = 0;

  for (std::uint32_t i = 0; i < kIterations; ++i) {
    if ((i & 0x3FFFU) == 0) {
      waitcnt.write_control(waitcnt.read_control() ^
                            gba::core::WaitStateControl::kStandardGamePakSetting);
    }

    const std::uint32_t address =
        (i % 3U) == 0 ? 0x08000000U : ((i % 3U) == 1 ? 0x0C000000U : 0x0E000000U);
    const gba::core::AccessWidth width =
        (i % 3U) == 2 ? gba::core::AccessWidth::byte : gba::core::AccessWidth::word;
    const gba::core::MemoryAccessTiming timing =
        gba::core::MemoryBus::timing(address, width, waitcnt);
    checksum += timing.nonsequential + timing.sequential + waitcnt.read_control();
  }

  return checksum;
}

std::uint64_t benchmark_cpu_waitcnt_elapsed_estimate() {
  constexpr std::uint32_t kIterations = 500000;
  constexpr std::uint32_t kLdrR5R6Plus4 = 0xE5965004U;
  gba::core::WaitStateControl waitcnt;
  std::uint64_t checksum = 0;

  for (std::uint32_t i = 0; i < kIterations; ++i) {
    if ((i & 0x3FFFU) == 0) {
      waitcnt.write_control(waitcnt.read_control() ^
                            gba::core::WaitStateControl::kStandardGamePakSetting);
    }

    const std::uint32_t address =
        (i % 3U) == 0 ? 0x08000004U : ((i % 3U) == 1 ? 0x0C000004U : 0x0E000004U);
    const std::optional<gba::core::ArmElapsedCycleEstimate> estimate =
        gba::core::Arm7tdmi::estimate_arm_elapsed_cycles(kLdrR5R6Plus4, address,
                                                         waitcnt);
    require(estimate.has_value(), "WAITCNT elapsed-cycle estimate failed");
    require(estimate->memory_timing_applied, "WAITCNT elapsed-cycle timing was not applied");
    checksum += estimate->cycles + waitcnt.read_control() +
                (estimate->data_dependent ? 1U : 0U);
  }

  return checksum;
}

std::uint64_t benchmark_dma_immediate_copy() {
  constexpr std::uint32_t kIterations = 20000;
  constexpr std::uint16_t kWordsPerCopy = 32;
  gba::core::MemoryBus memory;
  gba::core::InterruptController interrupts;
  gba::core::DmaController dma;
  std::uint64_t checksum = 0;

  for (std::uint16_t word = 0; word < kWordsPerCopy; ++word) {
    require(memory.write32(0x02000000U + word * 4U, 0x12340000U + word),
            "seed DMA source failed");
  }

  for (std::uint32_t i = 0; i < kIterations; ++i) {
    const std::uint32_t destination = 0x03001000U + ((i & 0xFU) * kWordsPerCopy * 4U);
    dma.write_source(0, 0x02000000U);
    dma.write_destination(0, destination);
    dma.write_word_count(0, kWordsPerCopy);
    dma.write_control(0, 0x8400);
    const gba::core::DmaRunResult result = dma.run_immediate(memory, interrupts);
    require(result.channels_executed == 1, "DMA channel did not execute");
    require(result.units_transferred == kWordsPerCopy, "DMA unit count mismatch");
    const std::optional<std::uint32_t> copied = memory.read32(destination);
    require(copied.has_value(), "DMA destination read failed");
    checksum += copied.value() + result.units_transferred;
  }

  return checksum;
}

void seed_ppu_fetch_memory(gba::core::MemoryBus& memory) {
  for (std::uint32_t byte = 0; byte < 32; ++byte) {
    require(memory.write8(0x06000000U + byte, 0x11), "seed BG tile failed");
    require(memory.write8(0x06010000U + byte, 0x22), "seed OBJ tile failed");
  }
  for (std::uint32_t entry = 0; entry < 32 * 32; ++entry) {
    require(memory.write16(0x06004000U + entry * 2U, 0), "seed BG map failed");
  }
  require(memory.write16(0x05000002U, 0x001F), "seed BG palette failed");
  require(memory.write16(0x05000204U, 0x03E0), "seed OBJ palette failed");
  require(memory.write16(0x07000000U, 0), "seed OBJ attr0 failed");
  require(memory.write16(0x07000002U, 0), "seed OBJ attr1 failed");
  require(memory.write16(0x07000004U, 0), "seed OBJ attr2 failed");
}

std::uint64_t benchmark_ppu_fetchers() {
  constexpr std::uint32_t kIterations = 500000;
  gba::core::MemoryBus memory;
  seed_ppu_fetch_memory(memory);
  const gba::core::BgControl bg =
      gba::core::PpuBackgroundFetcher::decode_control(8U << 8);
  const std::optional<gba::core::SpriteAttributes> sprite =
      gba::core::PpuSpriteFetcher::read_sprite(memory, 0);
  require(sprite.has_value(), "sprite decode failed");
  std::uint64_t checksum = 0;

  for (std::uint32_t i = 0; i < kIterations; ++i) {
    const std::optional<gba::core::BgPixel> bg_pixel =
        gba::core::PpuBackgroundFetcher::fetch_text_pixel(
            memory, bg, static_cast<std::uint16_t>(i & 0xFFU),
            static_cast<std::uint16_t>((i >> 8U) & 0xFFU));
    const std::optional<gba::core::SpritePixel> obj_pixel =
        gba::core::PpuSpriteFetcher::fetch_sprite_pixel(
            memory, sprite.value(), static_cast<std::uint8_t>(i & 0x7U),
            static_cast<std::uint8_t>((i >> 3U) & 0x7U));
    require(bg_pixel.has_value() && obj_pixel.has_value(), "PPU fetch failed");
    checksum += bg_pixel->color + obj_pixel->color + bg_pixel->color_index +
                obj_pixel->color_index;
  }

  return checksum;
}

std::uint64_t benchmark_device_ticks() {
  constexpr std::uint32_t kIterations = 500000;
  gba::core::InterruptController interrupts;
  gba::core::Timers timers;
  gba::core::PpuTiming ppu;
  gba::core::Apu apu;
  std::uint64_t checksum = 0;

  timers.write_reload(0, 0xFFF0);
  timers.write_control(0, 0x00C0);
  apu.write_soundcnt_x(0x0080);
  apu.write_soundcnt_h(0x0300);
  apu.write_fifo(gba::core::DirectSoundChannel::a, 0x04030201U);

  for (std::uint32_t i = 0; i < kIterations; ++i) {
    timers.tick(16, interrupts);
    ppu.tick(16, interrupts);
    const std::optional<gba::core::ApuFrameStep> step = apu.tick(16);
    if (timers.counter(0) == 0xFFF0) {
      const gba::core::DirectSoundTimerResult sample = apu.timer_overflow(0);
      checksum += sample.fifo_a.produced ? 1U : 0U;
    }
    checksum += timers.counter(0) + ppu.vcount() + (step.has_value() ? step->step : 0U);
  }

  return checksum;
}

std::uint64_t benchmark_scheduler_step_arm_add() {
  constexpr std::uint32_t kIterations = 500000;
  constexpr std::uint32_t kAddR0R0Imm1 = 0xE2800001U;
  gba::core::Arm7tdmi cpu;
  gba::core::MemoryBus memory;
  gba::core::InterruptController interrupts;
  gba::core::Timers timers;
  gba::core::DmaController dma;
  gba::core::PpuTiming ppu;
  gba::core::Apu apu;
  gba::core::CoreScheduler scheduler(cpu, memory, interrupts, timers, dma, ppu, apu);
  std::uint64_t checksum = 0;

  timers.write_reload(0, 0xFFF0);
  timers.write_control(0, 0x0080);
  apu.write_soundcnt_x(0x0080);

  for (std::uint32_t i = 0; i < kIterations; ++i) {
    const gba::core::CoreSchedulerStepResult result = scheduler.step_arm(kAddR0R0Imm1);
    require(result.cpu_step.status == gba::core::ExecuteStatus::executed,
            "scheduler ARM ADD step did not execute");
    require(result.devices.cycles == result.cpu_step.elapsed_cycles,
            "scheduler device cycles mismatch");
    checksum += cpu.register_value(0) + timers.counter(0) + ppu.line_cycle() +
                result.scheduler_cycles;
  }

  return checksum ^ cpu.elapsed_cycles() ^ scheduler.scheduler_cycles();
}

std::uint64_t benchmark_scheduler_fetch_loop_arm_add_branch() {
  constexpr std::uint32_t kIterations = 500000;
  constexpr std::uint32_t kProgramBase = 0x03000000U;
  constexpr std::uint32_t kAddR0R0Imm1 = 0xE2800001U;
  constexpr std::uint32_t kBranchBackOneInstruction = 0xEAFFFFFDU;
  gba::core::Arm7tdmi cpu;
  gba::core::MemoryBus memory;
  gba::core::InterruptController interrupts;
  gba::core::Timers timers;
  gba::core::DmaController dma;
  gba::core::PpuTiming ppu;
  gba::core::Apu apu;
  gba::core::CoreScheduler scheduler(cpu, memory, interrupts, timers, dma, ppu, apu);

  require(memory.write32(kProgramBase, kAddR0R0Imm1), "seed fetch-loop ADD failed");
  require(memory.write32(kProgramBase + 4U, kBranchBackOneInstruction),
          "seed fetch-loop branch failed");
  cpu.set_register(gba::core::Arm7tdmi::kPc, kProgramBase);
  timers.write_reload(0, 0xFFF0);
  timers.write_control(0, 0x0080);

  const gba::core::CoreSchedulerRunResult result =
      scheduler.run_arm_from_pc(kIterations);
  require(result.stop_reason == gba::core::CoreRunStopReason::max_steps,
          "fetch loop did not stop at max steps");
  require(result.attempted_steps == kIterations, "fetch loop attempted count mismatch");
  require(result.executed_steps == kIterations, "fetch loop executed count mismatch");

  return static_cast<std::uint64_t>(cpu.register_value(0)) + result.final_pc +
         result.scheduler_cycles + ppu.line_cycle() + timers.counter(0);
}

std::uint64_t benchmark_scheduler_dispatch_thumb_add() {
  constexpr std::uint32_t kIterations = 500000;
  constexpr std::uint32_t kProgramBase = 0x03000000U;
  constexpr std::uint16_t kThumbAddR0Imm1 = 0x3001U;
  gba::core::Arm7tdmi cpu;
  gba::core::MemoryBus memory;
  gba::core::InterruptController interrupts;
  gba::core::Timers timers;
  gba::core::DmaController dma;
  gba::core::PpuTiming ppu;
  gba::core::Apu apu;
  gba::core::CoreScheduler scheduler(cpu, memory, interrupts, timers, dma, ppu, apu);
  std::uint64_t checksum = 0;

  require(cpu.set_cpsr(0x00000033U), "enter Thumb state for dispatch benchmark");
  require(memory.write16(kProgramBase, kThumbAddR0Imm1), "seed Thumb ADD 0 failed");
  require(memory.write16(kProgramBase + 2U, kThumbAddR0Imm1), "seed Thumb ADD 1 failed");
  cpu.set_register(gba::core::Arm7tdmi::kPc, kProgramBase);
  timers.write_reload(0, 0xFFF0);
  timers.write_control(0, 0x0080);

  for (std::uint32_t i = 0; i < kIterations; ++i) {
    if (cpu.register_value(gba::core::Arm7tdmi::kPc) >= kProgramBase + 4U) {
      cpu.set_register(gba::core::Arm7tdmi::kPc, kProgramBase);
    }
    const gba::core::CoreSchedulerFetchStepResult fetched = scheduler.step_from_pc();
    require(!fetched.fetch_failed, "Thumb dispatch fetch failed");
    require(fetched.instruction_set == gba::core::CoreInstructionSet::thumb,
            "Thumb dispatch used wrong instruction set");
    require(fetched.step->cpu_step.status == gba::core::ExecuteStatus::executed,
            "Thumb dispatch step did not execute");
    checksum += cpu.register_value(0) + fetched.fetch_address + scheduler.scheduler_cycles();
  }

  return checksum ^ cpu.elapsed_cycles() ^ scheduler.scheduler_cycles() ^ ppu.line_cycle();
}

std::uint64_t benchmark_scheduler_prefetch_cart_loop() {
  constexpr std::uint32_t kIterations = 100000;
  constexpr std::uint32_t kProgramBase = 0x08000000U;
  constexpr std::uint32_t kLdrR1FromR2Plus0x10 = 0xE5921010U;
  constexpr std::uint32_t kAddR0R0Imm1 = 0xE2800001U;
  constexpr std::uint32_t kDataWord = 0xCAFEBABEU;
  gba::core::Arm7tdmi cpu;
  gba::core::MemoryBus memory;
  gba::core::InterruptController interrupts;
  gba::core::Timers timers;
  gba::core::DmaController dma;
  gba::core::PpuTiming ppu;
  gba::core::Apu apu;
  gba::core::WaitStateControl waitcnt;
  waitcnt.write_control(gba::core::WaitStateControl::kStandardGamePakSetting);
  gba::core::CoreScheduler scheduler(cpu, memory, interrupts, timers, dma, ppu, apu,
                                     waitcnt);
  std::vector<std::uint8_t> rom(64);
  rom.at(0) = static_cast<std::uint8_t>(kLdrR1FromR2Plus0x10 & 0xFFU);
  rom.at(1) = static_cast<std::uint8_t>((kLdrR1FromR2Plus0x10 >> 8U) & 0xFFU);
  rom.at(2) = static_cast<std::uint8_t>((kLdrR1FromR2Plus0x10 >> 16U) & 0xFFU);
  rom.at(3) = static_cast<std::uint8_t>((kLdrR1FromR2Plus0x10 >> 24U) & 0xFFU);
  rom.at(4) = static_cast<std::uint8_t>(kAddR0R0Imm1 & 0xFFU);
  rom.at(5) = static_cast<std::uint8_t>((kAddR0R0Imm1 >> 8U) & 0xFFU);
  rom.at(6) = static_cast<std::uint8_t>((kAddR0R0Imm1 >> 16U) & 0xFFU);
  rom.at(7) = static_cast<std::uint8_t>((kAddR0R0Imm1 >> 24U) & 0xFFU);
  rom.at(0x10) = static_cast<std::uint8_t>(kDataWord & 0xFFU);
  rom.at(0x11) = static_cast<std::uint8_t>((kDataWord >> 8U) & 0xFFU);
  rom.at(0x12) = static_cast<std::uint8_t>((kDataWord >> 16U) & 0xFFU);
  rom.at(0x13) = static_cast<std::uint8_t>((kDataWord >> 24U) & 0xFFU);
  require(memory.load_game_pak_rom(rom), "prefetch benchmark ROM load failed");
  std::uint64_t checksum = 0;

  for (std::uint32_t i = 0; i < kIterations; ++i) {
    cpu.set_register(gba::core::Arm7tdmi::kPc, kProgramBase);
    cpu.set_register(2, kProgramBase);
    scheduler.reset_scheduler_cycles();
    const gba::core::CoreSchedulerFetchStepResult ldr = scheduler.step_from_pc();
    require(ldr.step.has_value() &&
                ldr.step->cpu_step.status == gba::core::ExecuteStatus::executed,
            "prefetch benchmark LDR did not execute");
    const gba::core::CoreSchedulerFetchStepResult add = scheduler.step_from_pc();
    require(add.prefetch_hit, "prefetch benchmark ADD was not a prefetch hit");
    require(add.step.has_value() &&
                add.step->cpu_step.status == gba::core::ExecuteStatus::executed,
            "prefetch benchmark ADD did not execute");
    checksum += cpu.register_value(0) + cpu.register_value(1) + add.fetch_cycles +
                add.prefetch_buffer_halfwords + scheduler.scheduler_cycles();
  }

  return checksum ^ cpu.elapsed_cycles() ^ ppu.line_cycle();
}

std::uint64_t benchmark_core_session_state_hash() {
  constexpr std::uint32_t kIterations = 10000;
  constexpr std::uint32_t kProgramBase = 0x08000000U;
  constexpr std::uint32_t kAddR0R0Imm1 = 0xE2800001U;
  constexpr std::uint32_t kBranchBackOneInstruction = 0xEAFFFFFDU;
  gba::core::CoreSession session;
  std::vector<std::uint8_t> rom(256);
  rom.at(0) = static_cast<std::uint8_t>(kAddR0R0Imm1 & 0xFFU);
  rom.at(1) = static_cast<std::uint8_t>((kAddR0R0Imm1 >> 8U) & 0xFFU);
  rom.at(2) = static_cast<std::uint8_t>((kAddR0R0Imm1 >> 16U) & 0xFFU);
  rom.at(3) = static_cast<std::uint8_t>((kAddR0R0Imm1 >> 24U) & 0xFFU);
  rom.at(4) = static_cast<std::uint8_t>(kBranchBackOneInstruction & 0xFFU);
  rom.at(5) = static_cast<std::uint8_t>((kBranchBackOneInstruction >> 8U) & 0xFFU);
  rom.at(6) = static_cast<std::uint8_t>((kBranchBackOneInstruction >> 16U) & 0xFFU);
  rom.at(7) = static_cast<std::uint8_t>((kBranchBackOneInstruction >> 24U) & 0xFFU);
  require(session.memory().load_game_pak_rom(rom), "session ROM load failed");
  require(session.memory().configure_game_pak_save(gba::core::GamePakSaveType::sram32k),
          "session save backing failed");
  session.waitcnt().write_control(gba::core::WaitStateControl::kStandardGamePakSetting);
  session.cpu().set_register(gba::core::Arm7tdmi::kPc, kProgramBase);
  std::uint64_t checksum = 0;

  for (std::uint32_t i = 0; i < kIterations; ++i) {
    if ((i & 0x3FU) == 0) {
      const gba::core::CoreSchedulerFetchStepResult step = session.step();
      require(!step.fetch_failed, "session benchmark step fetch failed");
      require(step.step->cpu_step.status == gba::core::ExecuteStatus::executed,
              "session benchmark step did not execute");
    }
    if ((i & 0x7FU) == 0) {
      require(session.memory().write8(0x0E000000U + (i & 0x7FFFU),
                                      static_cast<std::uint8_t>(i)),
              "session benchmark save write failed");
    }
    checksum ^= session.state_hash() + i;
  }

  return checksum;
}

}  // namespace

int main() {
  constexpr std::uint64_t kMemoryOps = 1000000;
  constexpr std::uint64_t kCpuOps = 1000000;
  constexpr std::uint64_t kIoOps = 250000ULL * 8ULL;
  constexpr std::uint64_t kDmaOps = 20000ULL * 32ULL;
  constexpr std::uint64_t kPpuOps = 500000ULL * 2ULL;
  constexpr std::uint64_t kDeviceTickOps = 500000ULL * 3ULL;
  constexpr std::uint64_t kSchedulerOps = 500000ULL * 4ULL;
  constexpr std::uint64_t kSchedulerFetchLoopOps = 500000ULL;
  constexpr std::uint64_t kSchedulerThumbDispatchOps = 500000ULL;
  constexpr std::uint64_t kSchedulerPrefetchCartOps = 100000ULL * 2ULL;
  constexpr std::uint64_t kWaitcntTimingOps = 500000ULL;
  constexpr std::uint64_t kWaitcntElapsedEstimateOps = 500000ULL;
  constexpr std::uint64_t kCoreSessionStateHashOps = 10000ULL;

  std::cout << "core_benchmark: synthetic_no_rom_no_bios\n";
  std::cout << "benchmark,operations,elapsed_ms,ops_per_second,ns_per_operation,checksum\n";

  print_result(measure("memory_bus_iwram_rw32_pair", kMemoryOps,
                       benchmark_memory_bus_rw32));
  print_result(measure("cpu_step_arm_add", kCpuOps, benchmark_cpu_step_arm_add));
  print_result(measure("io_register_mixed_access", kIoOps, benchmark_io_register_mix));
  print_result(measure("dma_immediate_word_copy_units", kDmaOps,
                       benchmark_dma_immediate_copy));
  print_result(measure("ppu_bg_obj_pixel_fetches", kPpuOps, benchmark_ppu_fetchers));
  print_result(measure("device_timer_ppu_apu_ticks", kDeviceTickOps,
                       benchmark_device_ticks));
  print_result(measure("scheduler_step_arm_add", kSchedulerOps,
                       benchmark_scheduler_step_arm_add));
  print_result(measure("scheduler_fetch_loop_arm_add_branch", kSchedulerFetchLoopOps,
                       benchmark_scheduler_fetch_loop_arm_add_branch));
  print_result(measure("scheduler_dispatch_thumb_add", kSchedulerThumbDispatchOps,
                       benchmark_scheduler_dispatch_thumb_add));
  print_result(measure("scheduler_prefetch_cart_loop", kSchedulerPrefetchCartOps,
                       benchmark_scheduler_prefetch_cart_loop));
  print_result(measure("memory_bus_waitcnt_timing_lookup", kWaitcntTimingOps,
                       benchmark_waitcnt_timing_lookup));
  print_result(measure("cpu_waitcnt_elapsed_estimate", kWaitcntElapsedEstimateOps,
                       benchmark_cpu_waitcnt_elapsed_estimate));
  print_result(measure("core_session_state_hash", kCoreSessionStateHashOps,
                       benchmark_core_session_state_hash));

  std::cout << "core_benchmark: PASS\n";
  return 0;
}
