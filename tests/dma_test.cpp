#include "gba/core/dma_controller.hpp"
#include "gba/core/interrupt_controller.hpp"
#include "gba/core/memory_bus.hpp"

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

void expect_read16(const gba::core::MemoryBus& bus, std::uint32_t address,
                   std::uint16_t expected, std::string_view message) {
  const std::optional<std::uint16_t> value = bus.read16(address);
  expect(value.has_value(), message);
  expect(value.value() == expected, message);
}

void expect_read32(const gba::core::MemoryBus& bus, std::uint32_t address,
                   std::uint32_t expected, std::string_view message) {
  const std::optional<std::uint32_t> value = bus.read32(address);
  expect(value.has_value(), message);
  expect(value.value() == expected, message);
}

constexpr std::uint16_t irq_bit(gba::core::InterruptSource source) {
  return static_cast<std::uint16_t>(1U << static_cast<std::uint8_t>(source));
}

}  // namespace

int main() {
  using gba::core::DmaAddressControl;
  using gba::core::DmaController;
  using gba::core::DmaStartTiming;
  using gba::core::InterruptController;
  using gba::core::InterruptSource;
  using gba::core::MemoryBus;

  MemoryBus memory;
  InterruptController interrupts;
  DmaController dma;

  expect(!dma.enabled(0), "DMA0 resets disabled");
  expect(dma.source(0) == 0, "DMA0 source resets clear");
  expect(dma.destination(0) == 0, "DMA0 destination resets clear");
  expect(dma.word_count(0) == 0, "DMA0 word count resets clear");
  expect(dma.active_count(0) == 0, "DMA0 active count resets clear");
  expect(dma.destination_control(0) == DmaAddressControl::increment,
         "DMA0 destination increments by default");
  expect(dma.source_control(0) == DmaAddressControl::increment,
         "DMA0 source increments by default");
  expect(dma.start_timing(0) == DmaStartTiming::immediate,
         "DMA0 start timing defaults immediate");

  expect(memory.write16(0x02000000, 0x1111), "seed DMA0 source halfword 0");
  expect(memory.write16(0x02000002, 0x2222), "seed DMA0 source halfword 1");
  expect(memory.write16(0x02000004, 0x3333), "seed DMA0 source halfword 2");
  dma.write_source(0, 0x02000000);
  dma.write_destination(0, 0x03000000);
  dma.write_word_count(0, 3);
  dma.write_control(0, 0xC000);
  expect(dma.enabled(0), "DMA0 enable bit is latched");
  expect(dma.irq_on_completion(0), "DMA0 IRQ-on-completion bit is latched");
  expect(dma.active_count(0) == 3, "DMA0 active count loads on enable edge");
  const gba::core::DmaRunResult dma0_result = dma.run_immediate(memory, interrupts);
  expect(dma0_result.channels_executed == 1, "DMA0 immediate run executes one channel");
  expect(dma0_result.units_transferred == 3, "DMA0 immediate run copies three halfwords");
  expect(!dma0_result.unsupported_request, "DMA0 immediate run is supported");
  expect_read16(memory, 0x03000000, 0x1111, "DMA0 copied halfword 0");
  expect_read16(memory, 0x03000002, 0x2222, "DMA0 copied halfword 1");
  expect_read16(memory, 0x03000004, 0x3333, "DMA0 copied halfword 2");
  expect(!dma.enabled(0), "DMA0 one-shot transfer clears enable bit");
  expect(interrupts.requested(InterruptSource::dma0), "DMA0 completion requests IF bit");
  expect(interrupts.interrupt_flags() == irq_bit(InterruptSource::dma0),
         "DMA0 completion sets only DMA0 IF bit");

  expect(memory.write32(0x02000010, 0xAABBCCDD), "seed DMA1 source word 0");
  expect(memory.write32(0x02000014, 0x11223344), "seed DMA1 source word 1");
  dma.write_source(1, 0x02000010);
  dma.write_destination(1, 0x03000020);
  dma.write_word_count(1, 2);
  dma.write_control(1, 0x8420);
  expect(dma.transfer_32bit(1), "DMA1 word transfer bit is latched");
  expect(dma.destination_control(1) == DmaAddressControl::decrement,
         "DMA1 destination decrement mode is decoded");
  const gba::core::DmaRunResult dma1_result = dma.run_immediate(memory, interrupts);
  expect(dma1_result.channels_executed == 1, "DMA1 immediate run executes one channel");
  expect(dma1_result.units_transferred == 2, "DMA1 immediate run copies two words");
  expect_read32(memory, 0x03000020, 0xAABBCCDD, "DMA1 copied first word to initial dest");
  expect_read32(memory, 0x0300001C, 0x11223344, "DMA1 decremented destination for second word");
  expect(!dma.enabled(1), "DMA1 one-shot transfer clears enable bit");

  dma.write_word_count(0, 0);
  dma.write_control(0, 0x8000);
  expect(dma.active_count(0) == 0x4000, "DMA0 zero count normalizes to 0x4000");
  dma.write_control(0, 0);
  dma.write_word_count(3, 0);
  dma.write_control(3, 0x8000);
  expect(dma.active_count(3) == 0x10000, "DMA3 zero count normalizes to 0x10000");
  dma.write_control(3, 0);

  expect(memory.write16(0x02000030, 0x4444), "seed DMA2 delayed source");
  dma.write_source(2, 0x02000030);
  dma.write_destination(2, 0x03000030);
  dma.write_word_count(2, 1);
  dma.write_control(2, 0x9000);
  expect(dma.start_timing(2) == DmaStartTiming::vblank, "DMA2 VBlank timing is decoded");
  const gba::core::DmaRunResult delayed_result = dma.run_immediate(memory, interrupts);
  expect(delayed_result.channels_executed == 0, "non-immediate DMA does not run now");
  expect(delayed_result.units_transferred == 0, "non-immediate DMA copies no units");
  expect(dma.enabled(2), "non-immediate DMA remains enabled for a later trigger");
  expect(!memory.read16(0x03000030).has_value() ||
             memory.read16(0x03000030).value() != 0x4444,
         "non-immediate DMA leaves destination unchanged");
  dma.write_control(2, 0);

  expect(memory.write16(0x02000040, 0x5555), "seed DMA3 repeat source 0");
  expect(memory.write16(0x02000042, 0x6666), "seed DMA3 repeat source 1");
  dma.write_source(3, 0x02000040);
  dma.write_destination(3, 0x03000040);
  dma.write_word_count(3, 1);
  dma.write_control(3, 0x8260);
  const gba::core::DmaRunResult repeat_first = dma.run_immediate(memory, interrupts);
  expect(repeat_first.channels_executed == 1, "repeat DMA3 first run executes");
  expect(dma.enabled(3), "repeat DMA3 stays enabled");
  expect(dma.active_count(3) == 1, "repeat DMA3 reloads active count");
  expect_read16(memory, 0x03000040, 0x5555, "repeat DMA3 writes first value");
  const gba::core::DmaRunResult repeat_second = dma.run_immediate(memory, interrupts);
  expect(repeat_second.channels_executed == 1, "repeat DMA3 second run executes");
  expect_read16(memory, 0x03000040, 0x6666, "repeat DMA3 destination reload overwrites dest");
  dma.write_control(3, 0);

  dma.write_source(0, 0x02000000);
  dma.write_destination(0, 0x03000050);
  dma.write_word_count(0, 1);
  dma.write_control(0, 0x8180);
  expect(dma.source_control(0) == DmaAddressControl::increment_reload,
         "source increment/reload mode is decoded");
  const gba::core::DmaRunResult unsupported_result = dma.run_immediate(memory, interrupts);
  expect(unsupported_result.channels_executed == 0, "unsupported DMA does not count channel");
  expect(unsupported_result.units_transferred == 0, "unsupported DMA copies no units");
  expect(unsupported_result.unsupported_request, "unsupported DMA reports unsupported request");
  expect(dma.enabled(0), "unsupported DMA remains enabled for caller handling");

  std::cout << "dma_test: PASS\n";
  return 0;
}
