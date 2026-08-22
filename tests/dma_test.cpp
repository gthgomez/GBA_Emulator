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

  // M3 regression: the completion IRQ fires exactly once per full block.
  // COUNT=16 words at four words per burst means four bursts, one IRQ.
  interrupts.reset();
  for (std::uint32_t word = 0; word < 16; ++word) {
    expect(memory.write32(0x02000200 + word * 4U, 0x01000000U + word),
           "seed FIFO block source word");
  }
  dma.write_source(1, 0x02000200);
  dma.write_destination(1, 0x040000A0);
  dma.write_word_count(1, 16);
  dma.write_control(1, 0xF640);  // enable | irq | special | 32-bit | repeat | dest fixed
  for (std::uint32_t burst = 1; burst <= 4; ++burst) {
    const gba::core::DmaRunResult burst_result =
        dma.run_sound_fifo(gba::core::DmaTrigger::fifo_a, memory, apu, interrupts);
    expect(burst_result.channels_executed == 1, "FIFO block burst runs");
    if (burst < 4U) {
      expect(!interrupts.requested(InterruptSource::dma1),
             "FIFO DMA holds the completion IRQ until count reaches zero");
    } else {
      expect(interrupts.requested(InterruptSource::dma1),
             "FIFO DMA requests completion IRQ exactly at count-zero");
    }
  }
  expect(dma.enabled(1), "repeat FIFO DMA re-arms after block completion");
  expect(dma.active_count(1) == 16, "FIFO DMA reloads full block count at count-zero");
  interrupts.reset();
  expect(dma.run_sound_fifo(gba::core::DmaTrigger::fifo_a, memory, apu, interrupts)
                 .channels_executed == 1,
         "next FIFO block starts after reload");
  expect(!interrupts.requested(InterruptSource::dma1),
         "reloaded FIFO block does not re-raise completion IRQ immediately");
  dma.write_control(1, 0);

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

  std::cout << "dma_test: PASS\n";
  return 0;
}
