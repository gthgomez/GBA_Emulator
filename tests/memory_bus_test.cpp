#include "gba/core/interrupt_controller.hpp"
#include "gba/core/io_registers.hpp"
#include "gba/core/memory_bus.hpp"
#include "gba/core/ppu_timing.hpp"
#include "gba/core/timers.hpp"
#include "gba/core/wait_state_control.hpp"
#include "gba/core/dma_controller.hpp"
#include "gba/core/apu.hpp"
#include "gba/core/keypad.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

#include "test_helpers.hpp"

namespace {

void expect_read(const gba::core::MemoryBus& bus, std::uint32_t address, std::uint8_t expected,
                 std::string_view message) {
  const std::optional<std::uint8_t> value = bus.read8(address);
  expect(value.has_value(), message);
  expect(value.value() == expected, message);
}

void expect_read32(const gba::core::MemoryBus& bus, std::uint32_t address, std::uint32_t expected,
                   std::string_view message) {
  const std::optional<std::uint32_t> value = bus.read32(address);
  expect(value.has_value(), message);
  expect(value.value() == expected, message);
}

void expect_read16(const gba::core::MemoryBus& bus, std::uint32_t address, std::uint16_t expected,
                   std::string_view message) {
  const std::optional<std::uint16_t> value = bus.read16(address);
  expect(value.has_value(), message);
  expect(value.value() == expected, message);
}

void expect_timing(const gba::core::MemoryAccessTiming& timing, std::uint8_t nonsequential,
                   std::uint8_t sequential, bool readable, bool writable,
                   std::string_view message) {
  expect(timing.nonsequential == nonsequential, message);
  expect(timing.sequential == sequential, message);
  expect(timing.readable == readable, message);
  expect(timing.writable == writable, message);
}

void expect_address(const gba::core::AddressInfo& info, gba::core::Region region,
                    std::uint32_t offset, bool writable, bool mirrored,
                    std::string_view message) {
  expect(info.region == region, message);
  expect(info.offset == offset, message);
  expect(info.writable == writable, message);
  expect(info.mirrored == mirrored, message);
}

void expect_cartridge(const gba::core::CartridgeAddressInfo& info,
                      gba::core::CartridgeWindow window,
                      gba::core::CartridgeSaveKind save_kind, std::uint32_t offset,
                      std::uint32_t max_window_size, std::uint8_t bus_width_bits,
                      bool serial_protocol, bool mirrored, std::string_view message) {
  expect(info.window == window, message);
  expect(info.save_kind == save_kind, message);
  expect(info.offset == offset, message);
  expect(info.max_window_size == max_window_size, message);
  expect(info.bus_width_bits == bus_width_bits, message);
  expect(!info.readable, message);
  expect(!info.writable, message);
  expect(info.requires_external_data, message);
  expect(info.serial_protocol == serial_protocol, message);
  expect(info.mirrored == mirrored, message);
}

void flash_unlock(gba::core::MemoryBus& bus) {
  expect(bus.write8(0x0E005555, 0xAA), "Flash command unlock first byte");
  expect(bus.write8(0x0E002AAA, 0x55), "Flash command unlock second byte");
}

void flash_program(gba::core::MemoryBus& bus, std::uint32_t address, std::uint8_t value) {
  flash_unlock(bus);
  expect(bus.write8(0x0E005555, 0xA0), "Flash command enters program mode");
  expect(bus.write8(address, value), "Flash command programs selected byte");
}

void flash_chip_erase(gba::core::MemoryBus& bus) {
  flash_unlock(bus);
  expect(bus.write8(0x0E005555, 0x80), "Flash command enters erase mode");
  flash_unlock(bus);
  expect(bus.write8(0x0E005555, 0x10), "Flash command chip erase");
}

void flash_sector_erase(gba::core::MemoryBus& bus, std::uint32_t sector_address) {
  flash_unlock(bus);
  expect(bus.write8(0x0E005555, 0x80), "Flash command enters sector erase mode");
  flash_unlock(bus);
  expect(bus.write8(sector_address, 0x30), "Flash command sector erase");
}

void flash_switch_bank(gba::core::MemoryBus& bus, std::uint8_t bank) {
  flash_unlock(bus);
  expect(bus.write8(0x0E005555, 0xB0), "Flash command enters bank-select mode");
  expect(bus.write8(0x0E000000, bank), "Flash command selects bank");
}

void flash_exit_id(gba::core::MemoryBus& bus) {
  flash_unlock(bus);
  expect(bus.write8(0x0E005555, 0xF0), "Flash command exits ID mode");
}

}  // namespace

int main() {
  using gba::core::AccessWidth;
  using gba::core::CartridgeSaveKind;
  using gba::core::CartridgeWindow;
  using gba::core::GamePakSaveType;
  using gba::core::MemoryBus;
  using gba::core::MemoryReadFailure;
  using gba::core::Region;
  using gba::core::VideoAccessWindow;
  using gba::core::WaitStateControl;

  expect(MemoryBus::describe(0x02000000).region == Region::ewram, "EWRAM start maps");
  expect(MemoryBus::describe(0x0203FFFF).region == Region::ewram, "EWRAM end maps");
  expect(MemoryBus::describe(0x03000000).region == Region::iwram, "IWRAM start maps");
  expect(MemoryBus::describe(0x05000000).region == Region::palette, "palette start maps");
  expect(MemoryBus::describe(0x06017FFF).region == Region::vram, "VRAM end maps");
  expect(MemoryBus::describe(0x070003FF).region == Region::oam, "OAM end maps");
  expect_address(MemoryBus::describe(0x08000000), Region::game_pak_rom, 0, false, false,
                 "Game Pak ROM wait-state 0 window maps as metadata only");
  expect_address(MemoryBus::describe(0x0A000000), Region::game_pak_rom, 0, false, true,
                 "Game Pak ROM wait-state 1 mirror maps as metadata only");
  expect_address(MemoryBus::describe(0x0C000000), Region::game_pak_rom, 0, false, true,
                 "Game Pak ROM wait-state 2 mirror maps as metadata only");
  expect_address(MemoryBus::describe(0x0E000000), Region::game_pak_save, 0, false, false,
                 "Game Pak SRAM/Flash save aperture maps as metadata only");
  expect_address(MemoryBus::describe(0x0F000000), Region::game_pak_save, 0, false, true,
                 "Game Pak save mirror maps as metadata only");
  expect_cartridge(MemoryBus::describe_cartridge(0x08000000), CartridgeWindow::rom_wait0,
                   CartridgeSaveKind::none, 0, 32 * 1024 * 1024, 16, false, false,
                   "cartridge wait0 metadata");
  expect_cartridge(MemoryBus::describe_cartridge(0x0A123456), CartridgeWindow::rom_wait1,
                   CartridgeSaveKind::none, 0x123456, 32 * 1024 * 1024, 16, false, true,
                   "cartridge wait1 metadata");
  expect_cartridge(MemoryBus::describe_cartridge(0x0DFFFF00), CartridgeWindow::rom_wait2,
                   CartridgeSaveKind::eeprom_serial, 0x1FFFF00, 32 * 1024 * 1024, 1, true,
                   true, "large-ROM EEPROM candidate metadata");
  // M8: candidacy window widened to every ROM-bus access >= 0x0D000000.
  expect_cartridge(MemoryBus::describe_cartridge(0x0D000000), CartridgeWindow::rom_wait2,
                   CartridgeSaveKind::eeprom_serial, 0x1000000, 32 * 1024 * 1024, 1, true,
                   true, "widened EEPROM candidacy starts at the 0x0D000000 boundary");
  expect_cartridge(MemoryBus::describe_cartridge(0x0DFFFFFF), CartridgeWindow::rom_wait2,
                   CartridgeSaveKind::eeprom_serial, 0x1FFFFFF, 32 * 1024 * 1024, 1, true,
                   true, "widened EEPROM candidacy covers upper wait2 mirror addresses");
  expect_cartridge(MemoryBus::describe_cartridge(0x0CFFFFFF), CartridgeWindow::rom_wait2,
                   CartridgeSaveKind::none, 0xFFFFFF, 32 * 1024 * 1024, 16, false,
                   true, "addresses below the widened EEPROM window keep parallel metadata");
  expect_cartridge(MemoryBus::describe_cartridge(0x0E00FFFF), CartridgeWindow::save,
                   CartridgeSaveKind::sram_or_flash, 0xFFFF, 64 * 1024, 8, false, false,
                   "SRAM/Flash save metadata");
  expect_cartridge(MemoryBus::describe_cartridge(0x0E010000), CartridgeWindow::save,
                   CartridgeSaveKind::sram_or_flash, 0, 64 * 1024, 8, false, true,
                   "SRAM/Flash save mirror metadata");
  expect(MemoryBus::describe_cartridge(0x02000000).window == CartridgeWindow::none,
         "non-cartridge address has no cartridge metadata");
  expect_address(MemoryBus::describe(0x02040000), Region::ewram, 0, true, true,
                 "EWRAM mirror wraps at 256 KiB");
  expect_address(MemoryBus::describe(0x02FFFFFF), Region::ewram, 0x3FFFF, true, true,
                 "EWRAM mirrors through 02xxxxxx bank");
  expect_address(MemoryBus::describe(0x03008000), Region::iwram, 0, true, true,
                 "IWRAM mirror wraps at 32 KiB");
  expect_address(MemoryBus::describe(0x05FFFFFF), Region::palette, 0x3FF, true, true,
                 "palette mirrors through 05xxxxxx bank");
  expect_address(MemoryBus::describe(0x06018000), Region::vram, 0x10000, true, true,
                 "VRAM OBJ mirror folds 06018000 to OBJ base");
  expect_address(MemoryBus::describe(0x06020000), Region::vram, 0, true, true,
                 "VRAM mirror repeats every 128 KiB");
  expect_address(MemoryBus::describe(0x07000400), Region::oam, 0, true, true,
                 "OAM mirror wraps at 1 KiB");
  expect(MemoryBus::describe(0x04000400).region == Region::unknown,
         "unimplemented IO mirrors remain out of scope");
  expect(MemoryBus::describe(0x12000000).region == Region::unknown,
         "addresses outside the GBA internal banks stay out of scope");
  expect(MemoryBus::read_policy(0x02000000).readable,
         "read policy marks modeled RAM readable");
  expect(MemoryBus::read_policy(0x00000000).readable,
         "read policy exposes deterministic HLE BIOS vector bytes");
  expect(MemoryBus::read_policy(0x00010000).failure ==
             MemoryReadFailure::protected_bios_open_bus_unmodeled,
         "read policy reports protected BIOS open-bus region");
  expect(MemoryBus::read_policy(0x04000000).failure ==
             MemoryReadFailure::io_register_facade_required,
         "read policy routes IO reads to the IO facade");
  expect(MemoryBus::read_policy(0x12000000).failure ==
             MemoryReadFailure::unmapped_open_bus_unmodeled,
         "read policy rejects unmapped open-bus reads without fake data");
  expect(MemoryBus::video_access_policy(0x06000000).window ==
             VideoAccessWindow::ppu_contention_unmodeled,
         "VRAM exposes PPU access-window policy hook");
  expect(MemoryBus::video_access_policy(0x06000000).access_allowed,
         "VRAM remains CPU-accessible while contention timing is unmodeled");
  expect(!MemoryBus::video_access_policy(0x06000000).contention_timing_modeled,
         "VRAM contention timing is not silently claimed");
  expect(MemoryBus::video_access_policy(0x02000000).window ==
             VideoAccessWindow::not_video_memory,
         "non-video RAM reports no video access-window policy");
  expect(!MemoryBus::video_access_policy(0x12000000).access_allowed,
         "unmapped addresses are not exposed as video-accessible");

  expect_timing(MemoryBus::timing(0x02000000, AccessWidth::halfword), 3, 3, true, true,
                "EWRAM halfword timing metadata");
  expect_timing(MemoryBus::timing(0x02000000, AccessWidth::word), 6, 6, true, true,
                "EWRAM word timing metadata");
  expect_timing(MemoryBus::timing(0x03000000, AccessWidth::word), 1, 1, true, true,
                "IWRAM word timing metadata");
  expect_timing(MemoryBus::timing(0x06000000, AccessWidth::word), 2, 2, true, true,
                "VRAM word timing metadata");
  expect_timing(MemoryBus::timing(0x00000000, AccessWidth::word), 1, 1, true, false,
                "BIOS timing metadata covers HLE vector reads");
  expect_timing(MemoryBus::timing(0x08000000, AccessWidth::word), 1, 1, false, false,
                "cartridge timing remains inaccessible until ROM data and WAITCNT exist");
  expect_timing(MemoryBus::timing(0x0E000000, AccessWidth::byte), 1, 1, false, false,
                "save timing remains inaccessible until save storage exists");
  expect_timing(MemoryBus::timing(0x02040000, AccessWidth::word), 6, 6, true, true,
                "EWRAM mirror word timing metadata");
  expect_timing(MemoryBus::timing(0x06018000, AccessWidth::word), 2, 2, true, true,
                "VRAM OBJ mirror word timing metadata");

  WaitStateControl waitcnt;
  expect_timing(MemoryBus::timing(0x08000000, AccessWidth::word, waitcnt), 4, 2,
                false, false, "WAITCNT default wait0 timing metadata");
  expect_timing(MemoryBus::timing(0x0A000000, AccessWidth::word, waitcnt), 4, 4,
                false, false, "WAITCNT default wait1 timing metadata");
  expect_timing(MemoryBus::timing(0x0C000000, AccessWidth::word, waitcnt), 4, 8,
                false, false, "WAITCNT default wait2 timing metadata");
  expect_timing(MemoryBus::timing(0x0E000000, AccessWidth::byte, waitcnt), 4, 4,
                false, false, "WAITCNT default save timing metadata");
  waitcnt.write_control(WaitStateControl::kStandardGamePakSetting);
  expect_timing(MemoryBus::timing(0x08000000, AccessWidth::word, waitcnt), 3, 1,
                false, false, "WAITCNT standard wait0 timing metadata");
  expect_timing(MemoryBus::timing(0x0C000000, AccessWidth::word, waitcnt), 8, 8,
                false, false, "WAITCNT standard wait2 timing metadata");
  expect_timing(MemoryBus::timing(0x0DFFFF00, AccessWidth::halfword, waitcnt), 8, 8,
                false, false, "WAITCNT standard EEPROM candidate timing metadata");
  expect_timing(MemoryBus::timing(0x0E000000, AccessWidth::byte, waitcnt), 8, 8,
                false, false, "WAITCNT standard save timing metadata");
  expect_timing(MemoryBus::timing(0x03000000, AccessWidth::word, waitcnt), 1, 1,
                true, true, "WAITCNT-aware timing preserves internal RAM timing");

  MemoryBus bus;
  expect(bus.write8(0x02000000, 0x12), "write EWRAM start");
  expect(bus.write8(0x0203FFFF, 0x34), "write EWRAM end");
  expect(bus.write8(0x03000000, 0x56), "write IWRAM start");
  expect(bus.write8(0x05000000, 0x78), "write palette start");
  expect(bus.write8(0x06000000, 0x9A), "write VRAM start");
  expect(bus.write16(0x07000000, 0x00BC), "write OAM start");

  expect_read(bus, 0x02000000, 0x12, "read EWRAM start");
  expect_read(bus, 0x0203FFFF, 0x34, "read EWRAM end");
  expect_read(bus, 0x03000000, 0x56, "read IWRAM start");
  expect_read(bus, 0x05000000, 0x78, "read palette start");
  expect_read(bus, 0x06000000, 0x9A, "read VRAM start");
  expect_read(bus, 0x07000000, 0xBC, "read OAM start");

  expect(bus.write8(0x02040000, 0xCA), "write EWRAM mirror");
  expect_read(bus, 0x02000000, 0xCA, "EWRAM mirror writes canonical byte");
  expect(bus.write8(0x03008000, 0xCB), "write IWRAM mirror");
  expect_read(bus, 0x03000000, 0xCB, "IWRAM mirror writes canonical byte");
  expect(bus.write16(0x05000400, 0x1357), "write palette mirror");
  expect_read16(bus, 0x05000000, 0x1357, "palette mirror writes canonical halfword");
  expect(bus.write16(0x06018000, 0x00D1), "write VRAM OBJ mirror");
  expect_read(bus, 0x06010000, 0xD1, "VRAM OBJ mirror writes canonical byte");
  expect(bus.write8(0x06020000, 0xD2), "write VRAM repeat mirror");
  expect_read(bus, 0x06000000, 0xD2, "VRAM repeat mirror writes canonical byte");
  expect(bus.write16(0x07000400, 0x00D3), "write OAM mirror");
  expect_read(bus, 0x07000000, 0xD3, "OAM mirror writes canonical byte");
  expect(bus.write8(0x05000002, 0xD8), "palette byte write duplicates into halfword");
  expect_read16(bus, 0x05000002, 0xD8D8, "palette byte-store quirk duplicates byte");
  expect(bus.write8(0x06000002, 0xD8), "VRAM BG byte write duplicates into halfword");
  expect_read16(bus, 0x06000002, 0xD8D8, "VRAM BG byte-store quirk duplicates byte");
  expect(bus.write8(0x06010000, 0xEE), "VRAM OBJ byte write is ignored");
  expect_read(bus, 0x06010000, 0xD1, "VRAM OBJ byte-store quirk preserves data");
  expect(bus.write8(0x07000000, 0xEE), "OAM byte write is ignored");
  expect_read(bus, 0x07000000, 0xD3, "OAM byte-store quirk preserves data");

  expect(bus.write32(0x02000004, 0x12345678), "write32 EWRAM aligned");
  expect_read32(bus, 0x02000004, 0x12345678, "read32 EWRAM aligned");
  expect_read32(bus, 0x02000005, 0x78123456,
                "unaligned read32 rotates addressed byte to low byte at +1");
  expect_read32(bus, 0x02000006, 0x56781234,
                "unaligned read32 rotates addressed byte to low byte at +2");
  expect_read32(bus, 0x02000007, 0x34567812,
                "unaligned read32 rotates addressed byte to low byte at +3");
  expect_read(bus, 0x02000004, 0x78, "read32 byte 0 little-endian");
  expect_read(bus, 0x02000005, 0x56, "read32 byte 1 little-endian");
  expect_read(bus, 0x02000006, 0x34, "read32 byte 2 little-endian");
  expect_read(bus, 0x02000007, 0x12, "read32 byte 3 little-endian");

  expect(bus.write16(0x02000008, 0xBEEF), "write16 EWRAM aligned");
  expect_read16(bus, 0x02000008, 0xBEEF, "read16 EWRAM aligned");
  expect_read(bus, 0x02000008, 0xEF, "read16 byte 0 little-endian");
  expect_read(bus, 0x02000009, 0xBE, "read16 byte 1 little-endian");

  // M5: unaligned halfword/word stores split into per-lane byte stores.
  expect(bus.write16(0x02000003, 0xABCD), "unaligned write16 splits into byte lanes");
  expect_read(bus, 0x02000003, 0xCD, "unaligned halfword low lane stored");
  expect_read(bus, 0x02000004, 0xAB, "unaligned halfword high lane stored");
  expect(!bus.read16(0x02000003).has_value(),
         "read16 rejects odd address as unpredictable/unsupported");
  expect(bus.write32(0x02000002, 0xAABBCCDD), "unaligned write32 splits into byte lanes");
  expect_read(bus, 0x02000002, 0xDD, "unaligned word lane 0 stored");
  expect_read(bus, 0x02000003, 0xCC, "unaligned word lane 1 stored");
  expect_read(bus, 0x02000004, 0xBB, "unaligned word lane 2 stored");
  expect_read(bus, 0x02000005, 0xAA, "unaligned word lane 3 stored");
  expect_read32(bus, 0x02000002, 0x00CACCDD,
                "unaligned write32 lanes read back rotated like the read path");

  // M5 regression: unaligned IWRAM word store lands each byte in the lane a
  // read of the same address fetches it back from.
  expect(bus.write32(0x03000001, 0xAABBCCDD), "unaligned IWRAM word store splits across lanes");
  expect_read(bus, 0x03000001, 0xDD, "IWRAM unaligned lane 1 stored");
  expect_read(bus, 0x03000002, 0xCC, "IWRAM unaligned lane 2 stored");
  expect_read(bus, 0x03000003, 0xBB, "IWRAM unaligned lane 3 stored");
  expect_read(bus, 0x03000004, 0xAA, "IWRAM unaligned lane 4 stored");
  expect_read32(bus, 0x03000001, 0xCBBBCCDD,
                "unaligned IWRAM word store round-trips through the read rotation");
  expect(!bus.write8(0x00000000, 0xFF), "BIOS range is not writable");
  expect(!bus.write16(0x00000000, 0xFFFF), "write16 BIOS range is not writable");
  expect(!bus.write32(0x00000000, 0xFFFFFFFF), "write32 BIOS range is not writable");
  expect_read(bus, 0x00000000, 0x04, "HLE BIOS vector byte 0 is readable");
  expect_read16(bus, 0x00000000, 0x2004, "HLE BIOS vector halfword is readable");
  expect_read32(bus, 0x00000000, 0xE3A02004, "HLE BIOS vector word is readable");
  expect(!bus.read8(0x00010000).has_value(),
         "protected BIOS region does not fabricate bus data directly");
  expect(!bus.write8(0x04000000, 0xFF), "IO registers not implemented in Phase 1");
  expect(bus.write16(0x04FFF780, 0xC0DE),
         "unmapped address writes are ignored without mutating modeled memory");
  expect_read16(bus, 0x04FFF780, 0x1DEA, "mGBA debug-enable probe read is modeled");
  expect(bus.write8(0x04FFF600, 'O'), "mGBA debug string byte writes are captured");
  expect(bus.write8(0x04FFF601, 'K'), "mGBA debug string second byte writes are captured");
  expect(bus.write8(0x04FFF602, 0), "mGBA debug string terminator writes are captured");
  expect(bus.write16(0x04FFF700, 0x0103), "mGBA debug flags flush captured string");
  expect(bus.debug_output() == "OK\n", "mGBA debug flush appends string output");
  bus.clear_debug_output();
  expect(bus.debug_output().empty(), "debug output clear resets captured text");
  expect_read32(bus, 0x04FFFA00, 0x67246F6E,
                "no$gba debug ID prefix is readable");
  expect_read16(bus, 0x04FFFA04, 0x6162, "no$gba debug ID middle is readable");
  expect(!bus.read16(0x04FFF782).has_value(),
         "unmapped address reads remain open-bus-unmodeled");
  expect(!bus.read8(0x08000000).has_value(), "cartridge ROM has no loaded data");
  expect(!bus.write8(0x08000000, 0xFF), "cartridge ROM remains read-only");
  expect(!bus.has_game_pak_rom(), "cartridge ROM starts unloaded");
  expect(bus.game_pak_rom_size() == 0, "unloaded cartridge ROM reports zero size");
  expect(!bus.game_pak_header().has_value(), "unloaded cartridge header is unavailable");

  std::vector<std::uint8_t> empty_rom;
  expect(!bus.load_game_pak_rom(empty_rom), "empty cartridge ROM blob is rejected");
  std::vector<std::uint8_t> short_rom(0xBD);
  expect(bus.load_game_pak_rom(short_rom), "short explicit cartridge ROM blob can load");
  expect(!bus.game_pak_header().has_value(),
         "too-small loaded cartridge blob has no header metadata");
  std::vector<std::uint8_t> invalid_header_rom(256);
  invalid_header_rom.at(0xB2) = 0x00;
  expect(bus.load_game_pak_rom(invalid_header_rom), "invalid-fixed-byte ROM blob loads");
  expect(bus.game_pak_header().has_value(), "invalid-fixed-byte ROM still parses header");
  expect(!bus.game_pak_header()->fixed_value_valid,
         "invalid cartridge fixed-value byte is reported");
  std::vector<std::uint8_t> tiny_rom(256);
  tiny_rom.at(0) = 0x78;
  tiny_rom.at(1) = 0x56;
  tiny_rom.at(2) = 0x34;
  tiny_rom.at(3) = 0x12;
  tiny_rom.at(0xA0) = 'T';
  tiny_rom.at(0xA1) = 'I';
  tiny_rom.at(0xA2) = 'N';
  tiny_rom.at(0xA3) = 'Y';
  tiny_rom.at(0xA4) = 'T';
  tiny_rom.at(0xA5) = 'E';
  tiny_rom.at(0xA6) = 'S';
  tiny_rom.at(0xA7) = 'T';
  tiny_rom.at(0xA8) = 'R';
  tiny_rom.at(0xA9) = 'O';
  tiny_rom.at(0xAA) = 'M';
  tiny_rom.at(0xAB) = ' ';
  tiny_rom.at(0xAC) = 'A';
  tiny_rom.at(0xAD) = 'B';
  tiny_rom.at(0xAE) = 'C';
  tiny_rom.at(0xAF) = 'D';
  tiny_rom.at(0xB0) = '0';
  tiny_rom.at(0xB1) = '1';
  tiny_rom.at(0xB2) = 0x96;
  tiny_rom.at(0xB3) = 0x00;
  tiny_rom.at(0xB4) = 0x00;
  tiny_rom.at(0xBC) = 0x02;
  tiny_rom.at(0xBD) = 0xF0;
  expect(bus.load_game_pak_rom(tiny_rom), "explicit cartridge ROM blob loads");
  expect(bus.has_game_pak_rom(), "loaded cartridge ROM reports present");
  expect(bus.game_pak_rom_size() == tiny_rom.size(), "loaded cartridge ROM reports size");
  expect_read32(bus, 0x08000000, 0x12345678, "loaded cartridge ROM reads little-endian word");
  expect_read16(bus, 0x0A000000, 0x5678, "wait1 ROM mirror reads loaded data");
  // M8: widened EEPROM candidacy must not misroute parallel ROM data reads
  // for addresses inside the new window. Candidate addresses start at wait2
  // offset 0x1000000, so a temporary wide fixture puts real bytes there.
  {
    std::vector<std::uint8_t> wide_rom(0x1000100U, 0);
    wide_rom.at(0x1000000U) = 0x78;
    wide_rom.at(0x1000001U) = 0x56;
    wide_rom.at(0x1000002U) = 0x34;
    wide_rom.at(0x1000003U) = 0x12;
    expect(bus.load_game_pak_rom(wide_rom), "wide EEPROM-window fixture ROM loads");
    expect_read16(bus, 0x0D000000, 0x5678,
                  "EEPROM-candidate address still reads parallel ROM halfword data");
    expect_read32(bus, 0x0D000000, 0x12345678,
                  "EEPROM-candidate word read keeps little-endian ROM data");
    expect(bus.load_game_pak_rom(tiny_rom),
           "tiny ROM restored after EEPROM-window read checks");
  }
  expect_read(bus, 0x0C000003, 0x12, "wait2 ROM mirror reads loaded data");
  expect_read(bus, 0x08000100, 0x80, "ROM out-of-bounds byte reads use open bus");
  expect_read(bus, 0x0A000101, 0x00, "ROM mirror out-of-bounds byte reads use open bus");
  expect_read(bus, 0x092468AC, 0x56, "ROM out-of-bounds byte reads address bus low byte");
  expect_read16(bus, 0x092468AC, 0x3456,
                "ROM out-of-bounds halfword reads address bus halfword");
  expect_read32(bus, 0x092468AC, 0x34573456,
                "ROM out-of-bounds word reads adjacent address-bus halfwords");
  expect_read32(bus, 0x092468AD, 0x56345734,
                "ROM out-of-bounds unaligned word reads rotate open bus value");
  expect(bus.write8(0x08000000, 0xFF), "loaded cartridge ROM accepts ignored byte writes");
  expect(bus.write16(0x08000000, 0xFFFF),
         "loaded cartridge ROM accepts ignored halfword writes");
  expect(bus.write32(0x08000000, 0xFFFFFFFF),
         "loaded cartridge ROM accepts ignored word writes");
  expect(bus.write16(0x08000001, 0xFFFF),
         "loaded cartridge ROM accepts ignored unaligned halfword writes");
  expect(bus.write32(0x08000001, 0xFFFFFFFF),
         "loaded cartridge ROM accepts ignored unaligned word writes");
  expect_read32(bus, 0x08000000, 0x12345678, "ignored cartridge ROM writes do not mutate bytes");
  std::vector<std::uint8_t> oversized_rom(MemoryBus::kGamePakRomWindowSize + 1U);
  expect(!bus.load_game_pak_rom(oversized_rom), "oversized cartridge ROM blob is rejected");
  expect(bus.game_pak_rom_size() == tiny_rom.size(),
         "oversized ROM rejection preserves previous loaded ROM");
  expect_read32(bus, 0x08000000, 0x12345678,
                "oversized ROM rejection preserves previous ROM bytes");
  const std::optional<gba::core::CartridgeHeader> header = bus.game_pak_header();
  expect(header.has_value(), "loaded cartridge header parses");
  expect(header->title.at(0) == 'T' && header->title.at(10) == 'M',
         "cartridge header title bytes parse");
  expect(header->game_code.at(0) == 'A' && header->game_code.at(3) == 'D',
         "cartridge header game code parses");
  expect(header->maker_code.at(0) == '0' && header->maker_code.at(1) == '1',
         "cartridge header maker code parses");
  expect(header->fixed_value == 0x96 && header->fixed_value_valid,
         "cartridge fixed-value header byte validates");
  expect(header->version == 0x02, "cartridge header version parses");
  expect(header->complement_check == 0xF0, "cartridge complement byte parses");
  expect(!gba::core::MemoryBus::cartridge_complement_valid(tiny_rom),
         "tiny test ROM complement is intentionally invalid in fixture");
  {
    std::uint32_t sum = 0x19U;
    for (std::size_t offset = 0xA0U; offset <= 0xBCU; ++offset) {
      sum = (sum + tiny_rom.at(offset)) & 0xFFU;
    }
    tiny_rom.at(0xBD) = static_cast<std::uint8_t>((0x100U - sum) & 0xFFU);
  }
  expect(gba::core::MemoryBus::cartridge_complement_valid(tiny_rom),
         "cartridge complement validates when checksum byte corrected");
  expect(!bus.detect_game_pak_save_type().has_value(),
         "cartridge save-type detector returns nullopt when no marker is present");
  auto put_marker = [](std::vector<std::uint8_t>& rom, std::size_t offset,
                       std::string_view marker) {
    for (std::size_t index = 0; index < marker.size(); ++index) {
      rom.at(offset + index) = static_cast<std::uint8_t>(marker.at(index));
    }
  };
  std::vector<std::uint8_t> sram_marker_rom = tiny_rom;
  put_marker(sram_marker_rom, 0xC0, "SRAM_V");
  expect(bus.load_game_pak_rom(sram_marker_rom), "SRAM marker ROM reloads");
  expect(bus.detect_game_pak_save_type() == GamePakSaveType::sram32k,
         "cartridge save-type detector maps SRAM marker conservatively");
  expect(!bus.has_game_pak_save(),
         "cartridge save-type detection does not mutate configured save backing");
  std::vector<std::uint8_t> flash_marker_rom = tiny_rom;
  put_marker(flash_marker_rom, 0xC0, "FLASH1M_V");
  expect(bus.load_game_pak_rom(flash_marker_rom), "Flash marker ROM reloads");
  expect(bus.detect_game_pak_save_type() == GamePakSaveType::flash128k,
         "cartridge save-type detector maps FLASH1M marker to Flash128K");
  std::vector<std::uint8_t> eeprom_marker_rom = tiny_rom;
  put_marker(eeprom_marker_rom, 0xC0, "EEPROM_V");
  expect(bus.load_game_pak_rom(eeprom_marker_rom), "EEPROM marker ROM reloads");
  expect(bus.detect_game_pak_save_type() == GamePakSaveType::eeprom8k,
         "cartridge save-type detector maps EEPROM marker to conservative max size");
  std::vector<std::uint8_t> conflicting_marker_rom = tiny_rom;
  put_marker(conflicting_marker_rom, 0xC0, "SRAM_V");
  put_marker(conflicting_marker_rom, 0xD0, "EEPROM_V");
  expect(bus.load_game_pak_rom(conflicting_marker_rom), "conflicting marker ROM reloads");
  expect(!bus.detect_game_pak_save_type().has_value(),
         "cartridge save-type detector rejects conflicting markers");
  expect(bus.load_game_pak_rom(tiny_rom), "restore tiny ROM after save-type detection tests");
  bus.clear_game_pak_rom();
  expect(!bus.has_game_pak_rom(), "cartridge ROM clear removes explicit blob");
  expect(!bus.read8(0x08000000).has_value(), "cleared cartridge ROM stops reads");
  expect(!bus.read8(0x0E000000).has_value(), "save RAM has no backing storage");
  expect(!bus.write8(0x0E000000, 0xFF), "save RAM is metadata-only");

  expect(!bus.has_game_pak_save(), "save backing starts unconfigured");
  expect(bus.game_pak_save_type() == GamePakSaveType::none,
         "save backing type starts none");
  expect(bus.game_pak_save_size() == 0, "save backing starts empty");
  expect(bus.export_game_pak_save().empty(), "empty save backing exports empty data");
  expect(bus.configure_game_pak_save(GamePakSaveType::sram32k),
         "SRAM32K save backing configures");
  expect(bus.has_game_pak_save(), "configured SRAM save backing reports present");
  expect(bus.game_pak_save_type() == GamePakSaveType::sram32k,
         "configured SRAM save type is reported");
  expect(bus.game_pak_save_size() == MemoryBus::kSram32kSize,
         "configured SRAM save size is reported");
  expect_read(bus, 0x0E000000, 0xFF, "SRAM save backing initializes erased");
  expect(bus.write8(0x0E000000, 0x12), "SRAM save backing accepts byte write");
  expect_read(bus, 0x0E000000, 0x12, "SRAM save backing reads written byte");
  expect(bus.write8(0x0E008000, 0x34), "SRAM save backing mirrors at 32 KiB");
  expect_read(bus, 0x0E000000, 0x34, "SRAM save mirror writes canonical byte");
  expect(bus.write8(0x0F000001, 0x56), "SRAM save backing accepts mirrored bank byte");
  expect_read(bus, 0x0E000001, 0x56, "SRAM save mirrored bank maps canonical byte");
  expect_read16(bus, 0x0E000001, 0x5656, "SRAM halfword reads duplicate addressed byte");
  expect_read32(bus, 0x0E000001, 0x56565656, "SRAM word reads duplicate addressed byte");
  const std::vector<std::uint8_t> exported_sram = bus.export_game_pak_save();
  expect(exported_sram.size() == MemoryBus::kSram32kSize,
         "SRAM save export exposes full backing bytes");
  expect(exported_sram.at(0) == 0x34 && exported_sram.at(1) == 0x56,
         "SRAM save export preserves written bytes");
  expect(bus.write16(0x0E000000, 0xBEEF),
         "SRAM save backing accepts low-byte halfword writes");
  expect(bus.write32(0x0E000004, 0x11223344),
         "SRAM save backing accepts low-byte word writes");
  expect_read(bus, 0x0E000000, 0xEF, "SRAM halfword write stores only low byte");
  expect_read(bus, 0x0E000004, 0x44, "SRAM save word write stores low byte");
  expect_read(bus, 0x0E000007, 0xFF, "SRAM save word write leaves high byte erased");
  expect_read16(bus, 0x0E000000, 0xEFEF,
                "SRAM save aperture duplicates low-byte halfword write");

  std::vector<std::uint8_t> imported_flash(MemoryBus::kFlash64kSize, 0xA5);
  imported_flash.at(0x1234) = 0x5A;
  expect(bus.import_game_pak_save(GamePakSaveType::flash64k, imported_flash),
         "Flash64K save backing imports exact-size bytes");
  expect(bus.game_pak_save_type() == GamePakSaveType::flash64k,
         "imported Flash64K save type is reported");
  expect(bus.game_pak_save_size() == MemoryBus::kFlash64kSize,
         "imported Flash64K save size is reported");
  expect_read(bus, 0x0E001234, 0x5A, "imported Flash64K byte reads through save aperture");
  expect(!bus.write8(0x0E001234, 0x00),
         "Flash64K direct byte write is rejected without command protocol");
  expect_read(bus, 0x0E001234, 0x5A, "rejected direct Flash64K write preserves data");
  flash_unlock(bus);
  expect(bus.write8(0x0E005555, 0x90), "Flash64K command enters ID mode");
  expect(bus.flash_protocol_status().id_mode, "Flash64K protocol status reports ID mode");
  expect_read(bus, 0x0E000000, 0xC2, "Flash64K ID mode reports manufacturer ID");
  expect_read(bus, 0x0E000001, 0x1C, "Flash64K ID mode reports device ID");
  flash_exit_id(bus);
  expect(!bus.flash_protocol_status().id_mode,
         "Flash64K protocol status clears ID mode after reset command");
  flash_program(bus, 0x0E001234, 0xF0);
  expect_read(bus, 0x0E001234, 0x50,
              "Flash64K program command only clears programmed bits");
  flash_program(bus, 0x0E002000, 0x00);
  expect_read(bus, 0x0E002000, 0x00, "Flash64K program command writes in second sector");
  flash_sector_erase(bus, 0x0E001000);
  expect_read(bus, 0x0E001234, 0xFF, "Flash64K sector erase clears selected sector");
  expect_read(bus, 0x0E002000, 0x00,
              "Flash64K sector erase preserves bytes outside selected sector");
  flash_chip_erase(bus);
  expect_read(bus, 0x0E002000, 0xFF, "Flash64K chip erase clears entire chip");
  expect(!bus.write8(0x0E001234, 0x12),
         "Flash64K unsupported command/data write rejects cleanly");
  std::vector<std::uint8_t> oversized_flash(MemoryBus::kFlash64kSize + 1U, 0);
  expect(!bus.import_game_pak_save(GamePakSaveType::flash64k, oversized_flash),
         "oversized Flash64K import is rejected");
  expect(bus.game_pak_save_type() == GamePakSaveType::flash64k,
         "failed save import preserves prior type");
  expect_read(bus, 0x0E001234, 0xFF, "failed save import preserves prior data");

  expect(bus.configure_game_pak_save(GamePakSaveType::flash128k),
         "Flash128K save backing configures");
  expect(bus.game_pak_save_size() == MemoryBus::kFlash128kSize,
         "Flash128K save backing reports full raw size");
  flash_program(bus, 0x0E000010, 0x9C);
  expect_read(bus, 0x0E000010, 0x9C,
              "Flash128K bank 0 program command writes selected byte");
  flash_switch_bank(bus, 1);
  expect(bus.flash_protocol_status().bank == 1, "Flash128K protocol status reports bank 1");
  expect_read(bus, 0x0E000010, 0xFF, "Flash128K bank 1 starts erased");
  flash_program(bus, 0x0E000010, 0x34);
  expect_read(bus, 0x0E000010, 0x34,
              "Flash128K bank 1 program command writes selected byte");
  flash_switch_bank(bus, 0);
  expect_read(bus, 0x0E000010, 0x9C,
              "Flash128K bank switch restores bank 0 byte view");
  flash_unlock(bus);
  expect(bus.write8(0x0E005555, 0x90), "Flash128K command enters ID mode");
  expect_read(bus, 0x0E000001, 0x09, "Flash128K ID mode reports device ID");
  flash_exit_id(bus);

  // M9: the F0 terminate command is accepted from any address while idle,
  // and bank selection survives leaving ID mode.
  flash_switch_bank(bus, 1);
  expect(bus.flash_protocol_status().bank == 1,
         "Flash128K selects bank 1 before the ID exit check");
  flash_program(bus, 0x0E000020, 0x5A);
  expect_read(bus, 0x0E000020, 0x5A,
              "Flash128K bank 1 byte programmed before the ID exit check");
  flash_unlock(bus);
  expect(bus.write8(0x0E005555, 0x90), "Flash128K re-enters ID mode for exit test");
  expect(bus.flash_protocol_status().id_mode, "ID mode active before arbitrary-offset F0");
  expect(bus.write8(0x0E001234, 0xF0),
         "Flash terminate command accepted from a data offset");
  const gba::core::FlashProtocolStatus after_id_exit = bus.flash_protocol_status();
  expect(!after_id_exit.id_mode && after_id_exit.bank == 1,
         "bank selection preserved across ID-mode exit");
  expect_read(bus, 0x0E000020, 0x5A, "bank 1 view intact after ID-mode exit");

  std::vector<std::uint8_t> imported_eeprom(MemoryBus::kEeprom512Size, 0xC3);
  expect(bus.import_game_pak_save(GamePakSaveType::eeprom512, imported_eeprom),
         "EEPROM512 raw backing imports exact-size bytes");
  expect(bus.has_game_pak_save(), "EEPROM raw backing reports present");
  expect(bus.game_pak_save_type() == GamePakSaveType::eeprom512,
         "EEPROM raw save type is reported");
  expect(bus.game_pak_save_size() == MemoryBus::kEeprom512Size,
         "EEPROM raw save size is reported");
  expect(!bus.read8(0x0E000000).has_value(),
         "EEPROM raw backing is not exposed through byte save aperture");
  expect(!bus.write8(0x0E000000, 0xAA),
         "EEPROM raw backing rejects byte aperture writes without serial protocol");
  expect(bus.export_game_pak_save().at(0) == 0xC3,
         "EEPROM raw backing exports imported bytes");
  expect(bus.eeprom_block_count() == 64, "EEPROM512 exposes 64 explicit 8-byte blocks");
  const std::array<std::uint8_t, 8> eeprom_block{0x00, 0x11, 0x22, 0x33,
                                                 0x44, 0x55, 0x66, 0x77};
  expect(bus.eeprom_write_block(2, eeprom_block), "EEPROM explicit block write accepts range");
  const std::optional<std::array<std::uint8_t, 8>> read_eeprom_block =
      bus.eeprom_read_block(2);
  expect(read_eeprom_block.has_value(), "EEPROM explicit block read accepts range");
  expect(read_eeprom_block.value() == eeprom_block,
         "EEPROM explicit block read returns written block");
  const std::vector<std::uint8_t> eeprom_before_reject = bus.export_game_pak_save();
  expect(!bus.eeprom_write_block(64, eeprom_block),
         "EEPROM explicit block write rejects out-of-range block");
  expect(!bus.eeprom_read_block(64).has_value(),
         "EEPROM explicit block read rejects out-of-range block");
  expect(bus.export_game_pak_save() == eeprom_before_reject,
         "EEPROM rejected block write preserves backing bytes");
  expect(bus.configure_game_pak_save(GamePakSaveType::sram32k),
         "SRAM reconfigures before EEPROM wrong-type negative test");
  expect(bus.eeprom_block_count() == 0, "non-EEPROM save exposes zero EEPROM blocks");
  expect(!bus.eeprom_write_block(0, eeprom_block),
         "non-EEPROM save rejects EEPROM block writes");
  expect(!bus.eeprom_read_block(0).has_value(),
         "non-EEPROM save rejects EEPROM block reads");
  bus.clear_game_pak_save();
  expect(!bus.has_game_pak_save(), "save backing clear removes configured save");
  expect(bus.game_pak_save_type() == GamePakSaveType::none,
         "save backing clear resets type");
  expect(!bus.read8(0x0E000000).has_value(), "cleared save backing stops reads");

  expect(bus.load_game_pak_rom(tiny_rom), "reload ROM before reset policy tests");
  expect(bus.configure_game_pak_save(GamePakSaveType::sram32k),
         "reload save before reset policy tests");
  expect(bus.write8(0x02000000, 0x88), "seed EWRAM before soft reset");
  expect(bus.write8(0x0E000000, 0x44), "seed save before soft reset");
  bus.soft_reset();
  expect_read(bus, 0x02000000, 0x00, "soft reset clears RAM");
  expect(bus.has_game_pak_rom(), "soft reset preserves explicit ROM backing");
  expect(bus.has_game_pak_save(), "soft reset preserves explicit save backing");
  expect_read(bus, 0x0E000000, 0x44, "soft reset preserves save data");
  bus.hard_reset();
  expect(!bus.has_game_pak_rom(), "hard reset clears explicit ROM backing");
  expect(!bus.has_game_pak_save(), "hard reset clears explicit save backing");

  expect(bus.write8(0x02000000, 0x66), "seed EWRAM before compatibility reset");
  bus.reset();
  expect_read(bus, 0x02000000, 0x00, "reset clears EWRAM");
  expect_read(bus, 0x03000000, 0x00, "reset clears IWRAM");

  {
    // Save-state contract: complete MemoryBus snapshot round-trip.
    MemoryBus snapshot_bus;
    expect(snapshot_bus.load_game_pak_rom(tiny_rom), "snapshot fixture loads ROM");
    expect(snapshot_bus.configure_game_pak_save(GamePakSaveType::flash128k),
           "snapshot fixture configures Flash128K backing");
    flash_switch_bank(snapshot_bus, 1);
    flash_unlock(snapshot_bus);
    expect(snapshot_bus.write8(0x0E005555, 0x90), "snapshot fixture enters flash ID mode");
    expect(snapshot_bus.write8(0x02000100, 0x77), "snapshot seeds EWRAM");
    expect(snapshot_bus.write16(0x06000102, 0x5A5A), "snapshot seeds VRAM");
    snapshot_bus.drive_open_bus(0xDEADBEEF);
    expect(snapshot_bus.write8(0x04FFF600, 'H'), "snapshot seeds mGBA debug byte 0");
    expect(snapshot_bus.write8(0x04FFF601, 'I'), "snapshot seeds mGBA debug byte 1");

    const MemoryBus::State saved_bus_state = snapshot_bus.save_state();
    const std::uint64_t saved_bus_hash = snapshot_bus.state_hash();
    snapshot_bus.hard_reset();
    expect(snapshot_bus.state_hash() != saved_bus_hash,
           "hard reset perturbs the snapshotted bus state");
    expect(snapshot_bus.load_state(saved_bus_state),
           "bus state restore accepts a full snapshot");
    expect(snapshot_bus.state_hash() == saved_bus_hash,
           "bus state restore returns the exact pre-reset hash");
    expect_read16(snapshot_bus, 0x08000000, 0x5678, "restored ROM backing reads data");
    expect_read(snapshot_bus, 0x02000100, 0x77, "restored EWRAM byte round-trips");
    expect_read16(snapshot_bus, 0x06000102, 0x5A5A, "restored VRAM halfword round-trips");
    const gba::core::FlashProtocolStatus restored_flash =
        snapshot_bus.flash_protocol_status();
    expect(restored_flash.bank == 1 && restored_flash.id_mode,
           "restored flash FSM keeps bank selection and ID mode");
    expect(snapshot_bus.open_bus_latch().has_value() &&
               snapshot_bus.open_bus_latch().value() == 0xDEADBEEF,
           "restored open-bus latch value round-trips");
    expect(snapshot_bus.game_pak_save_type() == GamePakSaveType::flash128k &&
               snapshot_bus.game_pak_save_size() == MemoryBus::kFlash128kSize,
           "restored save backing keeps type and size");

    MemoryBus::State bad_bus_state = saved_bus_state;
    bad_bus_state.flash_bank = 2U;
    expect(!snapshot_bus.load_state(bad_bus_state),
           "bus state restore rejects out-of-range flash banks");
    bad_bus_state = saved_bus_state;
    bad_bus_state.game_pak_save_type = GamePakSaveType::sram32k;
    expect(!snapshot_bus.load_state(bad_bus_state),
           "bus state restore rejects save type/backing size mismatches");
    bad_bus_state = saved_bus_state;
    bad_bus_state.flash_command_state = static_cast<gba::core::FlashCommandState>(200);
    expect(!snapshot_bus.load_state(bad_bus_state),
           "bus state restore rejects unknown flash command states");
  }

  {
    using gba::core::Apu;
    using gba::core::DmaController;
    using gba::core::InterruptController;
    using gba::core::IoRegisters;
    using gba::core::PpuTiming;
    using gba::core::Timers;

    InterruptController interrupts;
    Timers timers;
    DmaController dma;
    PpuTiming ppu;
    Apu apu;
    WaitStateControl waitcnt;
    gba::core::Keypad keypad;
    IoRegisters io(interrupts, timers, dma, ppu, apu, waitcnt, keypad);
    MemoryBus io_bus;
    io_bus.set_io_callbacks(
        {&io,
         [](void* ctx, std::uint32_t address) {
           return static_cast<IoRegisters*>(ctx)->read16(address);
         },
         [](void* ctx, std::uint32_t address) {
           return static_cast<IoRegisters*>(ctx)->read32(address);
         },
         [](void* ctx, std::uint32_t address, std::uint16_t value) {
           return static_cast<IoRegisters*>(ctx)->write16(address, value);
         },
         [](void* ctx, std::uint32_t address, std::uint32_t value) {
           return static_cast<IoRegisters*>(ctx)->write32(address, value);
         }});

    constexpr std::uint32_t kLine159Cycles =
        static_cast<std::uint32_t>(PpuTiming::kCyclesPerLine) * 159U;
    [[maybe_unused]] const auto events = ppu.tick(kLine159Cycles, interrupts);
    expect_read(io_bus, 0x04000006, 159, "IO VCOUNT low byte is readable via read8");
    expect_read16(io_bus, 0x04000006, 159, "IO VCOUNT halfword read matches byte lane");

    // M4: byte stores to IO merge into the addressed lane of the aligned
    // halfword, preserving the sibling byte.
    expect(io.write16(IoRegisters::kIe, 0x1234), "IO facade seeds the IE halfword");
    expect(io_bus.write8(0x04000201, 0x0A), "odd-lane IO byte store is accepted");
    expect_read16(io_bus, IoRegisters::kIe, 0x0A34,
                  "odd-lane byte store preserves the sibling low byte");
    expect(io_bus.write8(0x04000200, 0xC5), "even-lane IO byte store is accepted");
    expect_read16(io_bus, IoRegisters::kIe, 0x0AC5,
                  "even-lane byte store preserves the sibling high byte");
    expect(io.write16(IoRegisters::kDispstat, 0x0038),
           "IO facade seeds DISPSTAT before read-only guard test");
    expect(io_bus.write8(0x04000007, 0xFF),
           "byte store targeting a read-only register still reports a bus cycle");
    expect_read16(io_bus, IoRegisters::kDispstat, 0x0038,
                  "byte store to read-only VCOUNT lane leaves DISPSTAT untouched");
    expect_read16(io_bus, IoRegisters::kVcount, 159,
                  "read-only VCOUNT keeps its live value after merged write rejection");
  }

  std::cout << "memory_bus_test: PASS\n";
  return 0;
}
