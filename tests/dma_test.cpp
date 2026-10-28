#include "gba/core/dma_controller.hpp"
#include "gba/core/interrupt_controller.hpp"
#include "gba/core/memory_bus.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

#include "test_helpers.hpp"

namespace {

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

void put_rom_word(std::vector<std::uint8_t>& rom, std::size_t offset,
                  std::uint32_t value) {
  rom.at(offset + 0) = static_cast<std::uint8_t>(value & 0xFFU);
  rom.at(offset + 1) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  rom.at(offset + 2) = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  rom.at(offset + 3) = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
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
  expect(dma0_result.bus_cycles == 14, "DMA0 halfword transfer reports bus occupancy");
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
  expect(dma1_result.bus_cycles == 16, "DMA1 word transfer reports doubled bus occupancy");
  expect_read32(memory, 0x03000020, 0xAABBCCDD, "DMA1 copied first word to initial dest");
  expect_read32(memory, 0x0300001C, 0x11223344, "DMA1 decremented destination for second word");
  expect(!dma.enabled(1), "DMA1 one-shot transfer clears enable bit");

  std::vector<std::uint8_t> rom(256, 0);
  put_rom_word(rom, 0, 0xDEADBEEF);
  put_rom_word(rom, 0x0C, 0xDEADBEEF);
  put_rom_word(rom, 0x10, 0xDEADBEF0);
  put_rom_word(rom, 0x14, 0xDEADBEF1);
  put_rom_word(rom, 0x18, 0xDEADBEF2);
  expect(memory.load_game_pak_rom(rom), "DMA test ROM loads");
  dma.write_source(1, 0x08000001);
  dma.write_destination(1, 0x03000024);
  dma.write_word_count(1, 1);
  dma.write_control(1, 0x8400);
  const gba::core::DmaRunResult dma1_unaligned_source = dma.run_immediate(memory, interrupts);
  expect(dma1_unaligned_source.channels_executed == 1,
         "DMA1 unaligned word source still runs");
  expect_read32(memory, 0x03000024, 0xDEADBEEF,
                "DMA word source aligns instead of rotating like CPU LDR");

  dma.write_source(1, 0x0800000C);
  dma.write_destination(1, 0x03000028);
  dma.write_word_count(1, 4);
  dma.write_control(1, 0x8540);
  expect(dma.run_immediate(memory, interrupts).channels_executed == 1,
         "DMA1 fixed ROM word source transfer runs");
  expect_read32(memory, 0x03000028, 0xDEADBEF2,
                "DMA1 ROM word source streams forward even when fixed");

  dma.write_source(1, 0x0800000C);
  dma.write_destination(1, 0x0300002C);
  dma.write_word_count(1, 4);
  dma.write_control(1, 0x84C0);
  expect(dma.run_immediate(memory, interrupts).channels_executed == 1,
         "DMA1 decrement ROM word source transfer runs");
  expect_read32(memory, 0x0300002C, 0xDEADBEF2,
                "DMA1 ROM word source streams forward even when decrementing");

  expect(memory.write32(0x02000200, 0xFEEDFACE), "seed DMA0 latch source");
  dma.write_source(0, 0x02000200);
  dma.write_destination(0, 0x03000200);
  dma.write_word_count(0, 1);
  dma.write_control(0, 0x8400);
  expect(dma.run_immediate(memory, interrupts).channels_executed == 1,
         "DMA0 latch seed transfer runs");
  dma.write_source(0, 0x08000000);
  dma.write_destination(0, 0x03000204);
  dma.write_word_count(0, 1);
  dma.write_control(0, 0x8000);
  expect(dma.run_immediate(memory, interrupts).channels_executed == 1,
         "DMA0 masked ROM halfword transfer runs from BIOS latch");
  expect_read16(memory, 0x03000204, 0xFACE,
                "DMA0 source mask redirects ROM source into BIOS latch/open bus");
  dma.write_source(0, 0x00000000);
  dma.write_destination(0, 0x03000206);
  dma.write_word_count(0, 1);
  dma.write_control(0, 0x8400);
  expect(dma.run_immediate(memory, interrupts).channels_executed == 1,
         "DMA0 BIOS word transfer runs after latch halfword read");
  expect_read32(memory, 0x03000204, 0xFEEDFACE,
                "DMA halfword latch source read preserves full word latch");

  expect(memory.write32(0x02000208, 0xCAFEBABE), "seed DMA1 BIOS latch source");
  dma.write_source(1, 0x02000208);
  dma.write_destination(1, 0x03000208);
  dma.write_word_count(1, 4);
  dma.write_control(1, 0x8140);
  expect(dma.run_immediate(memory, interrupts).channels_executed == 1,
         "DMA1 halfword latch seed transfer runs");
  dma.write_source(1, 0x00000010);
  dma.write_destination(1, 0x0300020C);
  dma.write_word_count(1, 1);
  dma.write_control(1, 0x8400);
  expect(dma.run_immediate(memory, interrupts).channels_executed == 1,
         "DMA1 BIOS word source reuses latch");
  expect_read32(memory, 0x0300020C, 0xBABEBABE,
                "DMA1 BIOS word source uses duplicated halfword latch");

  dma.write_source(1, 0x0800000C);
  dma.write_destination(1, 0x03000210);
  dma.write_word_count(1, 4);
  dma.write_control(1, 0x8000);
  expect(dma.run_immediate(memory, interrupts).channels_executed == 1,
         "DMA1 ROM halfword latch seed transfer runs");
  dma.write_source(1, 0x0000001C);
  dma.write_destination(1, 0x03000214);
  dma.write_word_count(1, 4);
  dma.write_control(1, 0x8400);
  expect(dma.run_immediate(memory, interrupts).channels_executed == 1,
         "DMA1 BIOS word source after ROM seed transfer runs");
  expect_read32(memory, 0x03000214, 0xDEADDEAD,
                "DMA1 BIOS word source preserves duplicated ROM halfword latch");
  expect_read32(memory, 0x03000218, 0xDEADDEAD,
                "DMA1 BIOS word source writes duplicated latch while incrementing");

  dma.write_source(1, 0x02000005);
  dma.write_destination(1, 0x03000207);
  dma.write_word_count(1, 1);
  dma.write_control(1, 0x8000);
  expect(dma.run_immediate(memory, interrupts).channels_executed == 1,
         "DMA unaligned halfword destination still runs");
  expect_read16(memory, 0x03000206, 0x3333,
                "DMA halfword destination aligns down before write");
  dma.write_source(2, 0x02000000);
  dma.write_destination(2, 0x08000003);
  dma.write_word_count(2, 1);
  dma.write_control(2, 0x8400);
  const gba::core::DmaRunResult dma_to_rom = dma.run_immediate(memory, interrupts);
  expect(dma_to_rom.channels_executed == 1,
         "DMA to read-only/protected destination still completes");
  expect(!dma.enabled(2), "ignored DMA destination write clears one-shot enable");
  expect_read32(memory, 0x08000000, 0xDEADBEEF,
                "ignored DMA destination write does not mutate ROM");

  dma.write_word_count(0, 0);
  dma.write_control(0, 0x8000);
  expect(dma.active_count(0) == 0x4000, "DMA0 zero count normalizes to 0x4000");
  dma.write_control(0, 0);
  dma.write_word_count(3, 0);
  dma.write_control(3, 0x8000);
  expect(dma.active_count(3) == 0x10000, "DMA3 zero count normalizes to 0x10000");
  dma.write_control(3, 0);

  // M6 regression: DMA3CNT_H bit 11 (Game Pak DRQ) is storable only on ch3.
  dma.write_control(3, 0x8800);
  expect(dma.control(3) == 0x8800, "DMA3 gamepak DRQ bit is storable");
  dma.write_control(3, 0);
  dma.write_control(2, 0x8800);
  expect(dma.control(2) == 0x8000,
         "gamepak DRQ bit is stripped for channels other than DMA3");
  dma.write_control(2, 0);

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
  const gba::core::DmaRunResult vblank_result =
      dma.run_trigger(gba::core::DmaTrigger::vblank, memory, interrupts);
  expect(vblank_result.channels_executed == 1, "VBlank DMA runs on VBlank trigger");
  expect(vblank_result.units_transferred == 1, "VBlank DMA copies one unit");
  expect(vblank_result.bus_cycles == 6, "VBlank DMA reports bus occupancy");
  expect_read16(memory, 0x03000030, 0x4444, "VBlank DMA copied delayed value");
  dma.write_control(2, 0);

  expect(memory.write16(0x02000034, 0x7777), "seed DMA2 HBlank source");
  dma.write_source(2, 0x02000034);
  dma.write_destination(2, 0x03000034);
  dma.write_word_count(2, 1);
  dma.write_control(2, 0xA000);
  const gba::core::DmaRunResult hblank_result =
      dma.run_trigger(gba::core::DmaTrigger::hblank, memory, interrupts);
  expect(hblank_result.channels_executed == 1, "HBlank DMA runs on HBlank trigger");
  expect_read16(memory, 0x03000034, 0x7777, "HBlank DMA copied delayed value");
  expect(memory.open_bus_latch().has_value() &&
             memory.open_bus_latch().value() == 0x77777777,
         "triggered HBlank DMA drives the shared open-bus latch");
  dma.write_control(2, 0);

  expect(memory.write16(0x02000040, 0x5555), "seed DMA3 repeat source 0");
  expect(memory.write16(0x02000042, 0x6666), "seed DMA3 repeat source 1");
  dma.write_source(3, 0x02000040);
  dma.write_destination(3, 0x03000040);
  dma.write_word_count(3, 1);
  // Repeat re-arms only for recurring triggers, so this regression uses
  // VBlank start timing; the repeat+immediate termination case is covered
  // below.
  dma.write_control(3, 0x9260);
  const gba::core::DmaRunResult repeat_first =
      dma.run_trigger(gba::core::DmaTrigger::vblank, memory, interrupts);
  expect(repeat_first.channels_executed == 1, "repeat DMA3 first run executes");
  expect(dma.enabled(3), "repeat DMA3 stays enabled");
  expect(dma.active_count(3) == 1, "repeat DMA3 reloads active count");
  expect_read16(memory, 0x03000040, 0x5555, "repeat DMA3 writes first value");
  const gba::core::DmaRunResult repeat_second =
      dma.run_trigger(gba::core::DmaTrigger::vblank, memory, interrupts);
  expect(repeat_second.channels_executed == 1, "repeat DMA3 second run executes");
  expect_read16(memory, 0x03000040, 0x6666, "repeat DMA3 destination reload overwrites dest");
  dma.write_control(3, 0);

  // M2 regression: a repeat-enabled channel with immediate start timing must
  // terminate after exactly one block instead of staying armed forever.
  expect(memory.write32(0x02000044, 0x11111111), "seed repeat-immediate source word");
  dma.write_source(3, 0x02000044);
  dma.write_destination(3, 0x03000048);
  dma.write_word_count(3, 2);
  dma.write_control(3, 0x8200);  // enable | repeat | 32-bit | immediate | incrementing dest
  expect(dma.immediate_pending(), "repeat+immediate enable latches pending");
  const gba::core::DmaRunResult repeat_immediate = dma.run_immediate(memory, interrupts);
  expect(repeat_immediate.channels_executed == 1,
         "repeat+immediate DMA3 executes once");
  expect(repeat_immediate.units_transferred == 2,
         "repeat+immediate DMA3 transfers exactly word_count units");
  expect(!dma.enabled(3), "repeat+immediate DMA3 disables on completion");
  expect(dma.active_count(3) == 0, "repeat+immediate DMA3 leaves zero active count");
  expect(!dma.immediate_pending(),
         "repeat+immediate completion clears the immediate queue");
  expect_read32(memory, 0x03000048, 0x11111111,
                "repeat+immediate DMA3 copied its single block");

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

  gba::core::Apu apu;
  apu.write_soundcnt_x(0x0080);
  expect(memory.write32(0x02000100, 0x04030201), "seed FIFO DMA word 0");
  expect(memory.write32(0x02000104, 0x08070605), "seed FIFO DMA word 1");
  expect(memory.write32(0x02000108, 0x0C0B0A09), "seed FIFO DMA word 2");
  expect(memory.write32(0x0200010C, 0x100F0E0D), "seed FIFO DMA word 3");
  dma.write_source(1, 0x02000100);
  dma.write_destination(1, 0x040000A0);
  dma.write_word_count(1, 4);
  dma.write_control(1, 0xB640);
  const gba::core::DmaRunResult fifo_result =
      dma.run_sound_fifo(gba::core::DmaTrigger::fifo_a, memory, apu, interrupts);
  expect(fifo_result.channels_executed == 1, "FIFO DMA runs one sound channel");
  expect(fifo_result.units_transferred == 4, "FIFO DMA transfers four words");
  expect(fifo_result.bus_cycles == 28, "FIFO DMA reports word bus occupancy");
  expect(apu.fifo_size(gba::core::DirectSoundChannel::a) == 16,
         "FIFO DMA pushes sixteen bytes into Direct Sound FIFO A");
  expect(dma.enabled(1), "repeat FIFO DMA stays enabled for next refill");
  dma.write_control(1, 0);

  // Sound FIFO requests force a four-word, fixed-destination transfer even
  // when the programmed width and destination-control fields differ.
  for (const std::uint16_t destination_bits : {0x0000U, 0x0020U, 0x0040U, 0x0060U}) {
    for (const std::uint16_t width_bits : {0x0000U, 0x0400U}) {
      for (const std::size_t channel : {1U, 2U}) {
        DmaController sound_dma;
        gba::core::Apu sound_apu;
        gba::core::InterruptController sound_interrupts;
        sound_apu.write_soundcnt_x(0x0080);
        sound_apu.write_soundcnt_h(0x3304);
        const auto fifo = channel == 1 ? gba::core::DirectSoundChannel::a
                                       : gba::core::DirectSoundChannel::b;
        const auto trigger = channel == 1 ? gba::core::DmaTrigger::fifo_a
                                          : gba::core::DmaTrigger::fifo_b;
        sound_dma.write_source(channel, 0x02000100);
        sound_dma.write_destination(channel, channel == 1 ? 0x040000A0 : 0x040000A4);
        sound_dma.write_word_count(channel, 4);
        sound_dma.write_control(
            channel, static_cast<std::uint16_t>(0xB200U | destination_bits | width_bits));
        const auto burst = sound_dma.run_sound_fifo(trigger, memory, sound_apu, sound_interrupts);
        expect(!burst.unsupported_request && burst.channels_executed == 1 &&
                   burst.units_transferred == 4 && burst.bus_cycles == 28,
               "FIFO DMA forces word width and fixed destination");
        expect(sound_apu.fifo_size(fifo) == 16, "forced FIFO burst writes sixteen bytes");
        const auto popped = sound_apu.timer_overflow(0);
        const auto sample = channel == 1 ? popped.fifo_a : popped.fifo_b;
        expect(sample.produced && sample.sample == 1, "forced FIFO burst preserves byte order");
        expect(sound_dma.save_state().channels.at(channel).current_source == 0x02000110,
               "forced FIFO burst advances source by four words");
      }
    }
  }

  // FIFO requests complete after four words regardless of programmed COUNT.
  // Acknowledge each IRQ so a later burst must request a fresh interrupt.
  for (std::uint32_t word = 0; word < 16; ++word) {
    expect(memory.write32(0x02000200 + word * 4U, 0x01000000U + word),
           "seed FIFO block source word");
  }
  for (const std::size_t channel : {1U, 2U}) {
    for (const std::uint16_t count : {16U, 0U, 1U, 3U, 4U, 0x3FFFU}) {
      for (const bool repeating : {true, false}) {
        for (const bool irq_enabled : {true, false}) {
          DmaController sound_dma;
          gba::core::Apu sound_apu;
          gba::core::InterruptController sound_interrupts;
          const auto trigger = channel == 1 ? gba::core::DmaTrigger::fifo_a
                                            : gba::core::DmaTrigger::fifo_b;
          const auto irq = channel == 1 ? InterruptSource::dma1 : InterruptSource::dma2;
          sound_dma.write_source(channel, 0x02000200);
          sound_dma.write_destination(channel, channel == 1 ? 0x040000A0 : 0x040000A4);
          sound_dma.write_word_count(channel, count);
          sound_dma.write_control(
              channel, static_cast<std::uint16_t>(0xB440U |
                  (repeating ? 0x0200U : 0U) | (irq_enabled ? 0x4000U : 0U)));
          for (std::uint32_t burst = 1; burst <= 3; ++burst) {
            const auto result = sound_dma.run_sound_fifo(
                trigger, memory, sound_apu, sound_interrupts);
            expect(!result.unsupported_request && result.channels_executed == 1 &&
                       result.units_transferred == 4,
                   "FIFO DMA ignores programmed count and transfers four words");
            expect(sound_interrupts.requested(irq) == irq_enabled,
                   "each FIFO burst requests its enabled completion IRQ");
            sound_interrupts.write_interrupt_flags(
                static_cast<std::uint16_t>(1U << static_cast<unsigned>(irq)));
            expect(!sound_interrupts.requested(irq), "FIFO completion IRQ is acknowledged");
            expect(sound_dma.save_state().channels.at(channel).current_source ==
                       0x02000200U + burst * 16U,
                   "FIFO source continues across four-word completions");
            expect(sound_dma.word_count(channel) == count,
                   "FIFO completion preserves the programmed count register");
            expect(sound_dma.enabled(channel) == repeating,
                   "FIFO completion re-arms only a repeating channel");
            if (!repeating) {
              expect(sound_dma.active_count(channel) == 0,
                     "non-repeating FIFO completion clears active count");
              expect(sound_dma.run_sound_fifo(trigger, memory, sound_apu, sound_interrupts)
                         .channels_executed == 0,
                     "non-repeating FIFO DMA ignores later requests");
              break;
            }
          }
        }
      }
    }
  }

  // The documented FIFO stop sequence writes COUNT=4 and switches to
  // immediate word DMA while keeping Enable set. COUNT becomes effective
  // again, without restarting the source stream at its programmed address.
  for (const std::size_t channel : {1U, 2U}) {
    for (const std::uint16_t initial_count : {0U, 1U, 16U}) {
      DmaController sound_dma;
      gba::core::Apu sound_apu;
      gba::core::InterruptController sound_interrupts;
      sound_dma.write_source(channel, 0x02000200);
      sound_dma.write_destination(channel, channel == 1 ? 0x040000A0 : 0x040000A4);
      sound_dma.write_word_count(channel, initial_count);
      sound_dma.write_control(channel, 0xB600);
      expect(sound_dma.run_sound_fifo(channel == 1 ? gba::core::DmaTrigger::fifo_a
                                                   : gba::core::DmaTrigger::fifo_b,
                                     memory, sound_apu, sound_interrupts)
                     .units_transferred == 4,
             "seed an enabled FIFO stream before stopping");
      sound_dma.write_word_count(channel, 4);
      sound_dma.write_control(channel, 0x8440);
      expect(sound_dma.active_count(channel) == 4,
             "FIFO stop uses the newly programmed count in immediate mode");
      const auto stopped = sound_dma.run_immediate(memory, sound_interrupts);
      expect(!stopped.unsupported_request && stopped.channels_executed == 1 &&
                 stopped.units_transferred == 4 && stopped.bus_cycles == 30,
             "FIFO stop completes one four-word immediate transfer");
      expect(sound_dma.save_state().channels.at(channel).current_source == 0x02000220U,
             "FIFO stop continues from the current source address");
      expect(!sound_dma.enabled(channel) && !sound_dma.immediate_pending(),
             "FIFO stop completes and clears the immediate queue");
    }
  }

  // Save-state contract: symmetric snapshot across all channel registers,
  // latched internals, and the immediate queue.
  {
    DmaController snapshot_peer;
    snapshot_peer.write_source(0, 0x02345670);
    snapshot_peer.write_destination(0, 0x03000FF0);
    snapshot_peer.write_word_count(0, 21);
    snapshot_peer.write_control(0, 0x84A0);  // immediate | 32-bit | src fixed | dest dec
    const DmaController::State saved_dma_state = snapshot_peer.save_state();
    const std::uint64_t saved_hash = snapshot_peer.state_hash();
    expect(snapshot_peer.immediate_pending(), "armed immediate DMA reports pending");
    snapshot_peer.write_control(0, 0);
    snapshot_peer.write_source(1, 0xDEADBEEF);
    expect(snapshot_peer.state_hash() != saved_hash, "DMA hash tracks state mutations");
    expect(snapshot_peer.load_state(saved_dma_state),
           "DMA state restore accepts armed snapshot");
    expect(snapshot_peer.state_hash() == saved_hash,
           "DMA state restore returns exact pre-mutation hash");
    expect(snapshot_peer.immediate_pending(),
           "restored DMA recomputes the immediate queue");
    expect(snapshot_peer.source(0) == 0x02345670 &&
               snapshot_peer.destination(0) == 0x03000FF0 &&
               snapshot_peer.word_count(0) == 21 &&
               snapshot_peer.control(0) == 0x84A0 &&
               snapshot_peer.active_count(0) == 21 &&
               !snapshot_peer.enabled(1) && snapshot_peer.source(1) == 0,
           "restored DMA snapshot exposes all latched registers");

    DmaController reject_peer;
    DmaController::State bad_state{};
    bad_state.channels.at(0).control = 0xFFFF;
    expect(!reject_peer.load_state(bad_state),
           "DMA state restore rejects control bits outside the ch0 mask");
    bad_state.channels.at(0).control = 0;
    bad_state.channels.at(3).control = 0x8000;
    bad_state.channels.at(3).current_count = 0x20000;
    expect(!reject_peer.load_state(bad_state),
           "DMA state restore rejects active counts above the channel maximum");
    bad_state.channels.at(3).current_count = 7;
    bad_state.channels.at(3).control = 0;
    expect(!reject_peer.load_state(bad_state),
           "DMA state restore rejects disabled channels with active counts");
  }

  // Snapshot validity is independent of the receiving controller's registers.
  // COUNT can also change while enabled without replacing the latched count.
  for (const std::size_t channel : {0U, 1U, 2U, 3U}) {
    DmaController snapshot_source;
    DmaController snapshot_receiver;
    snapshot_source.write_source(channel, 0x02000100);
    snapshot_source.write_destination(channel, 0x02000200);
    snapshot_source.write_word_count(channel, 16);
    snapshot_source.write_control(channel, 0x8000);
    snapshot_receiver.write_word_count(channel, 1);
    snapshot_receiver.write_control(channel, 0x8000);
    expect(snapshot_receiver.load_state(snapshot_source.save_state()),
           "DMA snapshot restore ignores the receiver's previous count");
    expect(snapshot_receiver.state_hash() == snapshot_source.state_hash(),
           "DMA snapshot restore replaces all receiver state");

    snapshot_source.write_word_count(channel, 1);
    snapshot_receiver.write_word_count(channel, 1);
    expect(snapshot_source.active_count(channel) == 16,
           "programmed count writes preserve an enabled DMA's latched count");
    expect(snapshot_receiver.load_state(snapshot_source.save_state()),
           "DMA snapshot accepts latched count above programmed count");
    expect(snapshot_receiver.state_hash() == snapshot_source.state_hash(),
           "DMA snapshot preserves independent programmed and latched counts");

    snapshot_source.write_control(channel, 0);
    snapshot_source.write_word_count(channel, 0);
    snapshot_source.write_control(channel, 0x8000);
    const std::uint32_t maximum = channel == 3 ? 0x10000U : 0x4000U;
    expect(snapshot_source.active_count(channel) == maximum,
           "zero count latches the channel capacity");
    expect(snapshot_receiver.load_state(snapshot_source.save_state()),
           "DMA snapshot accepts the channel's full count capacity");
    auto excessive_count = snapshot_source.save_state();
    excessive_count.channels.at(channel).current_count = maximum + 1U;
    const auto receiver_hash = snapshot_receiver.state_hash();
    expect(!snapshot_receiver.load_state(excessive_count),
           "DMA snapshot rejects a count beyond the channel capacity");
    expect(snapshot_receiver.state_hash() == receiver_hash,
           "invalid DMA snapshot leaves the receiver unchanged");
  }

  std::cout << "dma_test: PASS\n";
  return 0;
}
