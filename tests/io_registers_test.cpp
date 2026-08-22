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

#include "test_helpers.hpp"

namespace {

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

void expect_no_read32(const gba::core::IoRegisters& io, std::uint32_t address,
                      std::string_view message) {
  expect(!io.read32(address).has_value(), message);
}

void expect_memory_read32(const gba::core::MemoryBus& memory, std::uint32_t address,
                          std::uint32_t expected, std::string_view message) {
  const std::optional<std::uint32_t> value = memory.read32(address);
  expect(value.has_value(), message);
  expect(value.value() == expected, message);
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
  gba::core::Keypad keypad;
  IoRegisters io(interrupts, timers, dma, ppu, apu, waitcnt, keypad);

  expect(!io.read16(0x04000300).has_value(), "unmodeled IO register read is rejected");
  expect_read16(io, IoRegisters::kDispcnt, 0x0000, "DISPCNT defaults to zero");
  expect(io.write16(IoRegisters::kDispcnt, 0x0141), "DISPCNT write routes to LCD state");
  expect_read16(io, IoRegisters::kDispcnt, 0x0141, "DISPCNT readback routes");
  expect(io.write16(IoRegisters::kBg1Cnt, 0x0004), "BG1CNT write routes to LCD state");
  expect_read16(io, IoRegisters::kBg1Cnt, 0x0004, "BG1CNT readback routes");
  expect(io.write32(IoRegisters::kBg0Cnt, 0x00040003), "BG0/BG1CNT word write routes");
  expect_read32(io, IoRegisters::kBg0Cnt, 0x00040003, "BG0/BG1CNT read32 routes");
  expect(io.write16(IoRegisters::kBg0Cnt, 0xFFFF), "BG0CNT accepts suite mask probe");
  expect_read16(io, IoRegisters::kBg0Cnt, 0xDFFF, "BG0CNT masks unused overflow bit");
  expect(io.write16(IoRegisters::kBg1Cnt, 0xFFFF), "BG1CNT accepts suite mask probe");
  expect_read16(io, IoRegisters::kBg1Cnt, 0xDFFF, "BG1CNT masks unused overflow bit");
  expect(io.write16(0x04000048, 0xFFFF), "WININ accepts suite mask probe");
  expect_read16(io, 0x04000048, 0x3F3F, "WININ masks unused window bits");
  expect(io.write16(0x0400004A, 0xFFFF), "WINOUT accepts suite mask probe");
  expect_read16(io, 0x0400004A, 0x3F3F, "WINOUT masks unused window bits");
  expect(io.write16(0x04000050, 0xFFFF), "BLDCNT accepts suite mask probe");
  expect_read16(io, 0x04000050, 0x3FFF, "BLDCNT masks unused high bits");
  expect(io.write16(0x04000052, 0xFFFF), "BLDALPHA accepts suite mask probe");
  expect_read16(io, 0x04000052, 0x1F1F, "BLDALPHA masks coefficient fields");
  expect(!io.write16(IoRegisters::kVcount, 12), "VCOUNT write is rejected as read-only");
  expect(!io.write32(IoRegisters::kDispstat + 2U, 0x12345678),
         "misaligned word IO write is rejected");
  expect(io.write32(IoRegisters::kDispstat, 0x12345678),
         "half-open word IO write commits the writable DISPSTAT half");
  expect_read16(io, IoRegisters::kDispstat, 0x5638,
                "DISPSTAT low halfword committed through half-open word write");
  expect_read16(io, IoRegisters::kVcount, 0,
                "read-only VCOUNT half is ignored during half-open word write");
  expect(io.write32(IoRegisters::kWaitcnt, 0x12344317),
         "WAITCNT word write routes low halfword and ignores high halfword");
  expect_read16(io, IoRegisters::kWaitcnt, 0x4317,
                "WAITCNT word write updates low halfword");

  expect(io.write16(IoRegisters::kDispstat, 0x2F38), "DISPSTAT write routes to PPU");
  expect_read16(io, IoRegisters::kDispstat, 0x2F38,
                "DISPSTAT read includes PPU writable fields");
  expect_read16(io, IoRegisters::kVcount, 0, "VCOUNT read routes to PPU timing");

  expect(io.write16(IoRegisters::kIe, irq_bit(InterruptSource::timer0)),
         "IE write routes to interrupt controller");
  expect(io.write16(IoRegisters::kIme, 1), "IME write routes to interrupt controller");
  expect(io.write32(IoRegisters::kIme, 0), "IME word write routes low halfword");
  expect_read16(io, IoRegisters::kIme, 0, "IME word write updates low halfword");
  expect(io.write16(IoRegisters::kIme, 1), "IME restores after word-write check");
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
  expect_no_read32(io, IoRegisters::kDmaBase, "DMA0 source read32 returns open bus");
  expect_no_read32(io, IoRegisters::kDmaBase + 4U, "DMA0 destination read32 returns open bus");
  expect_read16(io, IoRegisters::kDmaBase + 8U, 0, "DMA0 word count reads as zero");
  expect(io.write16(IoRegisters::kDmaBase + 10U, 0xC400), "DMA0CNT_H write routes");
  expect(dma.enabled(0), "DMA0 enable bit routes through IO");
  expect(dma.transfer_32bit(0), "DMA0 32-bit mode routes through IO");

  constexpr std::uint32_t kDma3CntH = IoRegisters::kDmaBase + 3U * 12U + 10U;
  constexpr std::uint32_t kDma1CntH = IoRegisters::kDmaBase + 1U * 12U + 10U;
  expect(io.write16(kDma3CntH, 0xF800), "DMA3CNT_H gamepak DRQ write routes");
  expect(dma.control(3) == 0xF800, "DMA3 gamepak DRQ bit is storable through IO");
  expect_read16(io, kDma3CntH, 0xF800,
                "DMA3CNT_H readback round-trips the gamepak DRQ bit");
  dma.write_control(3, 0);
  expect(io.write16(kDma1CntH, 0x8800), "DMA1CNT_H gamepak DRQ probe write routes");
  expect(dma.control(1) == 0x8000,
         "gamepak DRQ bit is stripped for channels other than DMA3");
  expect_read16(io, kDma1CntH, 0x8000,
                "DMA1CNT_H readback does not fabricate the gamepak DRQ bit");
  dma.write_control(1, 0);

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
  expect_read16(io, IoRegisters::kKeyinput, 0x03FF, "KEYINPUT starts all released");
  keypad.press(gba::core::KeypadButton::a);
  keypad.press(gba::core::KeypadButton::start);
  expect_read16(io, IoRegisters::kKeyinput, 0x03F6, "KEYINPUT active-low buttons route");
  expect(!io.write16(IoRegisters::kKeyinput, 0), "KEYINPUT is read-only");
  expect(io.write16(IoRegisters::kKeycnt, 0xC009), "KEYCNT write routes");
  expect_read16(io, IoRegisters::kKeycnt, 0xC009, "KEYCNT readback routes");
  expect(interrupts.requested(InterruptSource::keypad),
         "KEYCNT AND condition requests keypad IRQ when selected keys are pressed");
  expect(io.write16(IoRegisters::kRcnt, 0x8000), "RCNT write routes to serial IO state");
  expect_read16(io, IoRegisters::kRcnt, 0x81FF, "RCNT read masks general-purpose mode");
  expect(io.write16(IoRegisters::kSoundcntH, 0x0300), "SOUNDCNT_H write routes");
  expect_read16(io, IoRegisters::kSoundcntH, 0x0300, "SOUNDCNT_H read routes");
  expect(io.write16(IoRegisters::kSoundbias, 0x02FF), "SOUNDBIAS write routes");
  expect_read16(io, IoRegisters::kSoundbias, 0x02FF, "SOUNDBIAS read routes");
  expect(io.write16(0x04000120, 0xFFFF), "SIODATA32_L write routes");
  expect_read16(io, 0x04000120, 0, "SIODATA32_L idle normal-8 read is zero");
  expect(io.write16(0x04000122, 0xFFFF), "SIODATA32_H write routes");
  expect_read16(io, 0x04000122, 0, "SIODATA32_H idle normal-8 read is zero");
  expect(io.write16(0x04000128, 0xDFFF), "SIOCNT normal-32 probe write routes");
  expect_read16(io, 0x04000128, 0x5F8F, "SIOCNT read masks idle status bits");
  expect(io.write16(0x04000134, 0xBFFF), "RCNT general-purpose probe write routes");
  expect_read16(io, 0x04000134, 0x81FF, "RCNT read masks general-purpose idle bits");
  expect(io.write16(0x04000140, 0xFFFF), "JOYCNT write is accepted");
  expect_read16(io, 0x04000140, 0x0040, "JOYCNT idle read exposes receive-ready bit");
  expect(io.write16(0x04000150, 0xFFFF), "JOY_RECV_L write is accepted");
  expect_read16(io, 0x04000150, 0, "JOY_RECV_L idle read is zero");
  expect(!interrupts.requested(InterruptSource::serial), "serial IRQ starts clear");
  expect(io.write16(0x04000128, 0x4081), "SIOCNT starts normal 8-bit transfer");
  io.tick(551);
  expect(!interrupts.requested(InterruptSource::serial),
         "SIO transfer waits for full bit-clock duration");
  io.tick(1);
  expect(interrupts.requested(InterruptSource::serial),
         "SIO transfer completion requests serial IRQ when enabled");
  interrupts.write_interrupt_flags(irq_bit(InterruptSource::serial));
  expect(io.write16(0x04000128, 0x0081), "SIOCNT starts transfer with IRQ disabled");
  io.tick(552);
  expect(!interrupts.requested(InterruptSource::serial),
         "SIO transfer completion does not request IRQ when disabled");
  expect(io.write16(0x04000128, 0x6081), "SIOCNT no-peer multiplayer start is accepted");
  io.tick(0x10000);
  expect(!interrupts.requested(InterruptSource::serial),
         "no-peer multiplayer transfer does not invent serial IRQ");
  expect(io.write32(IoRegisters::kFifoA, 0x04030201), "FIFO A word write routes");
  expect(apu.fifo_size(DirectSoundChannel::a) == 4, "FIFO A received four samples");
  expect(io.write32(IoRegisters::kFifoB, 0x08070605), "FIFO B word write routes");
  expect(apu.fifo_size(DirectSoundChannel::b) == 4, "FIFO B received four samples");

  std::cout << "io_registers_test: PASS\n";
  return 0;
}
