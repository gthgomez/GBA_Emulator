#include "gba/core/apu.hpp"
#include "gba/core/dma_controller.hpp"
#include "gba/core/interrupt_controller.hpp"
#include "gba/core/io_registers.hpp"
#include "gba/core/memory_bus.hpp"
#include "gba/core/ppu_timing.hpp"
#include "gba/core/timers.hpp"
#include "gba/core/wait_state_control.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>

namespace {

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void expect_read16(const gba::core::IoRegisters& io, std::uint32_t address,
                   std::uint16_t expected, std::string_view message) {
  const std::optional<std::uint16_t> value = io.read16(address);
  expect(value.has_value(), message);
  expect(value.value() == expected, message);
}

void expect_read32(const gba::core::IoRegisters& io, std::uint32_t address,
                   std::uint32_t expected, std::string_view message) {
  const std::optional<std::uint32_t> value = io.read32(address);
  expect(value.has_value(), message);
  expect(value.value() == expected, message);
}

void expect_memory_read32(const gba::core::MemoryBus& memory, std::uint32_t address,
                          std::uint32_t expected, std::string_view message) {
  const std::optional<std::uint32_t> value = memory.read32(address);
  expect(value.has_value(), message);
  expect(value.value() == expected, message);
}

constexpr std::uint16_t irq_bit(gba::core::InterruptSource source) {
  return static_cast<std::uint16_t>(1U << static_cast<std::uint8_t>(source));
}

}  // namespace

int main() {
  using gba::core::Apu;
  using gba::core::DirectSoundChannel;
  using gba::core::DmaController;
  using gba::core::InterruptController;
  using gba::core::InterruptSource;
  using gba::core::IoRegisters;
  using gba::core::MemoryBus;
  using gba::core::PpuTiming;
  using gba::core::Timers;
  using gba::core::WaitStateControl;

  InterruptController interrupts;
  Timers timers;
  DmaController dma;
  PpuTiming ppu;
  Apu apu;
  WaitStateControl waitcnt;
  IoRegisters io(interrupts, timers, dma, ppu, apu, waitcnt);

  expect(!io.read16(0x04000000).has_value(), "unmodeled IO register read is rejected");
  expect(!io.write16(IoRegisters::kVcount, 12), "VCOUNT write is rejected as read-only");
  expect(!io.write32(IoRegisters::kDispstat + 2U, 0x12345678),
         "unaligned word IO write is rejected");
  expect(!io.write32(IoRegisters::kDispstat, 0x12345678),
         "word IO write with read-only high half is rejected");
  expect_read16(io, IoRegisters::kDispstat, 0x0004,
                "rejected word IO write does not partially mutate DISPSTAT");
  expect(!io.write32(IoRegisters::kWaitcnt, 0x12344317),
         "WAITCNT word write with unmodeled high half is rejected");
  expect_read16(io, IoRegisters::kWaitcnt, 0x0000,
                "rejected WAITCNT word write does not partially mutate WAITCNT");

  expect(io.write16(IoRegisters::kDispstat, 0x2F38), "DISPSTAT write routes to PPU");
  expect_read16(io, IoRegisters::kDispstat, 0x2F38,
                "DISPSTAT read includes PPU writable fields");
  expect_read16(io, IoRegisters::kVcount, 0, "VCOUNT read routes to PPU timing");

  expect(io.write16(IoRegisters::kIe, irq_bit(InterruptSource::timer0)),
         "IE write routes to interrupt controller");
  expect(io.write16(IoRegisters::kIme, 1), "IME write routes to interrupt controller");
  interrupts.request(InterruptSource::timer0);
  expect_read16(io, IoRegisters::kIe, irq_bit(InterruptSource::timer0), "IE read routes");
  expect_read16(io, IoRegisters::kIf, irq_bit(InterruptSource::timer0), "IF read routes");
  expect_read16(io, IoRegisters::kIme, 1, "IME read routes");
  expect(interrupts.irq_line(), "routed IE/IME writes expose a pending IRQ line");
  expect(io.write16(IoRegisters::kIf, irq_bit(InterruptSource::timer0)),
         "IF write acknowledges requested IRQ bits");
  expect_read16(io, IoRegisters::kIf, 0, "IF acknowledge cleared timer0 bit");

  expect(io.write16(IoRegisters::kWaitcnt, WaitStateControl::kStandardGamePakSetting),
         "WAITCNT write routes to wait-state control");
  expect_read16(io, IoRegisters::kWaitcnt, WaitStateControl::kStandardGamePakSetting,
                "WAITCNT read routes to wait-state control");
  expect(waitcnt.prefetch_enabled(), "WAITCNT IO route exposes prefetch metadata");
  expect(waitcnt.rom_wait_states(gba::core::CartridgeWindow::rom_wait0).sequential == 1,
         "WAITCNT IO route updates Game Pak wait-state metadata");
  expect(io.write16(IoRegisters::kWaitcnt, 0xFFFF), "WAITCNT write masks read-only bit");
  expect_read16(io, IoRegisters::kWaitcnt, 0x7FFF,
                "WAITCNT read masks read-only game-pak type bit");

  expect(io.write16(IoRegisters::kTimerBase, 0xFFFE), "TM0 reload write routes");
  expect(io.write16(IoRegisters::kTimerBase + 2U, 0x00C0), "TM0 control write routes");
  expect_read16(io, IoRegisters::kTimerBase + 2U, 0x00C0, "TM0 control read routes");
  timers.tick(2, interrupts);
  expect(interrupts.requested(InterruptSource::timer0),
         "timer routed through IO can request overflow IRQ");
  expect_read16(io, IoRegisters::kTimerBase, 0xFFFE, "TM0 counter read routes");

  expect(io.write32(IoRegisters::kDmaBase, 0x02000010), "DMA0SAD word write routes");
  expect(io.write32(IoRegisters::kDmaBase + 4U, 0x03000010), "DMA0DAD word write routes");
  expect(io.write16(IoRegisters::kDmaBase + 8U, 1), "DMA0CNT_L write routes");
  expect_read32(io, IoRegisters::kDmaBase, 0x02000010, "DMA0 source read32 routes");
  expect_read32(io, IoRegisters::kDmaBase + 4U, 0x03000010, "DMA0 destination read32 routes");
  expect_read16(io, IoRegisters::kDmaBase + 8U, 1, "DMA0 word count read routes");
  expect(io.write16(IoRegisters::kDmaBase + 10U, 0xC400), "DMA0CNT_H write routes");
  expect(dma.enabled(0), "DMA0 enable bit routes through IO");
  expect(dma.transfer_32bit(0), "DMA0 32-bit mode routes through IO");

  MemoryBus memory;
  interrupts.write_interrupt_flags(InterruptController::kSupportedMask);
  interrupts.write_interrupt_enable(irq_bit(InterruptSource::dma0));
  expect(memory.write32(0x02000010, 0xAABBCCDD), "seed DMA source through memory");
  const gba::core::DmaRunResult dma_result = dma.run_immediate(memory, interrupts);
  expect(dma_result.channels_executed == 1, "IO-programmed DMA executes");
  expect(dma_result.units_transferred == 1, "IO-programmed DMA copies one word");
  expect_memory_read32(memory, 0x03000010, 0xAABBCCDD, "IO-programmed DMA copied data");
  expect(interrupts.requested(InterruptSource::dma0), "IO-programmed DMA requested IRQ");

  expect(io.write16(IoRegisters::kSoundcntX, 0x0080), "SOUNDCNT_X write enables APU");
  expect(apu.master_enabled(), "APU master enable routed");
  expect(io.write16(IoRegisters::kSoundcntH, 0x0300), "SOUNDCNT_H write routes");
  expect_read16(io, IoRegisters::kSoundcntH, 0x0300, "SOUNDCNT_H read routes");
  expect(io.write16(IoRegisters::kSoundbias, 0x02FF), "SOUNDBIAS write routes");
  expect_read16(io, IoRegisters::kSoundbias, 0x02FF, "SOUNDBIAS read routes");
  expect(io.write32(IoRegisters::kFifoA, 0x04030201), "FIFO A word write routes");
  expect(apu.fifo_size(DirectSoundChannel::a) == 4, "FIFO A received four samples");
  expect(io.write32(IoRegisters::kFifoB, 0x08070605), "FIFO B word write routes");
  expect(apu.fifo_size(DirectSoundChannel::b) == 4, "FIFO B received four samples");

  std::cout << "io_registers_test: PASS\n";
  return 0;
}
