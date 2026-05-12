#include "gba/core/memory_bus.hpp"
#include "gba/core/state_hash.hpp"
#include "gba/core/wait_state_control.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace gba::core {
namespace {

constexpr std::uint32_t kBiosStart = 0x00000000;
constexpr std::uint32_t kBiosEnd = 0x00003FFF;
constexpr std::uint32_t kBiosProtectedEnd = 0x01FFFFFF;
constexpr std::uint32_t kEwramStart = 0x02000000;
constexpr std::uint32_t kEwramEnd = 0x0203FFFF;
constexpr std::uint32_t kIwramStart = 0x03000000;
constexpr std::uint32_t kIwramEnd = 0x03007FFF;
constexpr std::uint32_t kIoStart = 0x04000000;
constexpr std::uint32_t kIoEnd = 0x040003FE;
constexpr std::uint32_t kPaletteStart = 0x05000000;
constexpr std::uint32_t kPaletteEnd = 0x050003FF;
constexpr std::uint32_t kVramStart = 0x06000000;
constexpr std::uint32_t kVramEnd = 0x06017FFF;
constexpr std::uint32_t kOamStart = 0x07000000;
constexpr std::uint32_t kOamEnd = 0x070003FF;
constexpr std::uint32_t kGamePakRomWait0Start = 0x08000000;
constexpr std::uint32_t kGamePakRomWait0End = 0x09FFFFFF;
constexpr std::uint32_t kGamePakRomWait1Start = 0x0A000000;
constexpr std::uint32_t kGamePakRomWait1End = 0x0BFFFFFF;
constexpr std::uint32_t kGamePakRomWait2Start = 0x0C000000;
constexpr std::uint32_t kGamePakRomWait2End = 0x0DFFFFFF;
constexpr std::uint32_t kGamePakEepromLargeRomStart = 0x0DFFFF00;
constexpr std::uint32_t kGamePakSaveStart = 0x0E000000;
constexpr std::uint32_t kGamePakSaveEnd = 0x0FFFFFFF;
constexpr std::uint32_t kMgbaDebugEnable = 0x04FFF780;
constexpr std::uint16_t kMgbaDebugEnableMagic = 0x1DEA;
constexpr std::uint32_t kMgbaDebugStringStart = 0x04FFF600;
constexpr std::uint32_t kMgbaDebugStringEnd = 0x04FFF6FF;
constexpr std::uint32_t kMgbaDebugFlags = 0x04FFF700;
constexpr std::uint32_t kNoCashGbaDebugIdStart = 0x04FFFA00;
constexpr std::string_view kNoCashGbaDebugId = "no$gba ";
constexpr std::uint32_t kNoCashGbaDebugOut = 0x04FFFA10;
constexpr std::uint32_t kNoCashGbaDebugChar = 0x04FFFA1C;
constexpr std::uint32_t kBankMask = 0xFF000000;
constexpr std::uint32_t kBankOffsetMask = 0x00FFFFFF;
constexpr std::uint32_t kVramMirrorStep = 128 * 1024;
constexpr std::uint32_t kVramObjMirrorStart = 96 * 1024;
constexpr std::uint32_t kVramObjMirrorDelta = 32 * 1024;
constexpr std::uint8_t kGamePakRomBusWidthBits = 16;
constexpr std::uint8_t kGamePakSaveBusWidthBits = 8;
constexpr std::uint8_t kGamePakEepromBusWidthBits = 1;
constexpr std::size_t kCartridgeTitleOffset = 0xA0;
constexpr std::size_t kCartridgeTitleSize = 12;
constexpr std::size_t kCartridgeGameCodeOffset = 0xAC;
constexpr std::size_t kCartridgeGameCodeSize = 4;
constexpr std::size_t kCartridgeMakerCodeOffset = 0xB0;
constexpr std::size_t kCartridgeMakerCodeSize = 2;
constexpr std::size_t kCartridgeFixedValueOffset = 0xB2;
constexpr std::size_t kCartridgeUnitCodeOffset = 0xB3;
constexpr std::size_t kCartridgeDeviceTypeOffset = 0xB4;
constexpr std::size_t kCartridgeVersionOffset = 0xBC;
constexpr std::size_t kCartridgeComplementCheckOffset = 0xBD;
constexpr std::size_t kMinimumCartridgeHeaderSize = kCartridgeComplementCheckOffset + 1;
constexpr std::uint8_t kCartridgeExpectedFixedValue = 0x96;
constexpr std::uint32_t kFlashUnlockAddress1 = 0x5555;
constexpr std::uint32_t kFlashUnlockAddress2 = 0x2AAA;
constexpr std::uint32_t kFlashBankSelectAddress = 0x0000;
constexpr std::size_t kFlashBankSize = 64 * 1024;
constexpr std::size_t kFlashSectorSize = 4 * 1024;
constexpr std::uint8_t kFlashManufacturerId = 0xC2;
constexpr std::uint8_t kFlash64DeviceId = 0x1C;
constexpr std::uint8_t kFlash128DeviceId = 0x09;
constexpr std::array<std::uint8_t, 16> kHleBiosVectorBytes{
    0x04, 0x20, 0xA0, 0xE3, 0x01, 0x02, 0x03, 0x04,
    0x05, 0x20, 0xA0, 0xE3, 0x07, 0x20, 0xA0, 0xE3,
};

[[nodiscard]] bool in_range(std::uint32_t value, std::uint32_t start, std::uint32_t end) {
  return value >= start && value <= end;
}

[[nodiscard]] bool is_word(AccessWidth width) {
  return width == AccessWidth::word;
}

[[nodiscard]] std::uint32_t bank_offset(std::uint32_t address) {
  return address & kBankOffsetMask;
}

[[nodiscard]] bool bank_matches(std::uint32_t address, std::uint32_t start) {
  return (address & kBankMask) == (start & kBankMask);
}

[[nodiscard]] std::uint32_t align_word(std::uint32_t address) {
  return address & ~0x3U;
}

[[nodiscard]] std::uint32_t rotate_right(std::uint32_t value, std::uint8_t amount) {
  const std::uint8_t shift = static_cast<std::uint8_t>(amount % 32U);
  if (shift == 0) {
    return value;
  }
  return (value >> shift) | (value << (32U - shift));
}

[[nodiscard]] constexpr std::uint16_t repeat_byte16(std::uint8_t value) {
  return static_cast<std::uint16_t>(value) |
         (static_cast<std::uint16_t>(value) << 8U);
}

[[nodiscard]] constexpr std::uint32_t repeat_byte32(std::uint8_t value) {
  return static_cast<std::uint32_t>(value) |
         (static_cast<std::uint32_t>(value) << 8U) |
         (static_cast<std::uint32_t>(value) << 16U) |
         (static_cast<std::uint32_t>(value) << 24U);
}

[[nodiscard]] std::uint32_t game_pak_open_bus_aligned_word(std::uint32_t address) {
  const std::uint32_t aligned = align_word(address);
  const std::uint16_t low =
      static_cast<std::uint16_t>((aligned >> 1U) & 0xFFFFU);
  const std::uint16_t high = static_cast<std::uint16_t>(low + 1U);
  return static_cast<std::uint32_t>(low) | (static_cast<std::uint32_t>(high) << 16U);
}

[[nodiscard]] std::uint32_t game_pak_open_bus_word(std::uint32_t address) {
  const std::uint32_t aligned_value = game_pak_open_bus_aligned_word(address);
  return rotate_right(aligned_value, static_cast<std::uint8_t>((address & 0x3U) * 8U));
}

[[nodiscard]] std::uint32_t mirrored_offset(std::uint32_t address, std::uint32_t size) {
  return bank_offset(address) % size;
}

[[nodiscard]] std::uint32_t vram_offset(std::uint32_t address) {
  std::uint32_t offset = bank_offset(address) % kVramMirrorStep;
  if (offset >= kVramObjMirrorStart) {
    offset -= kVramObjMirrorDelta;
  }
  return offset;
}

[[nodiscard]] CartridgeAddressInfo cartridge_none() {
  return {CartridgeWindow::none, CartridgeSaveKind::none, 0, 0, 0,
          false, false, false, false, false};
}

[[nodiscard]] CartridgeAddressInfo cartridge_rom_info(std::uint32_t address,
                                                      std::uint32_t window_start,
                                                      CartridgeWindow window,
                                                      bool mirrored) {
  const std::uint32_t offset = address - window_start;
  const bool eeprom_candidate = address >= kGamePakEepromLargeRomStart;
  return {window,
          eeprom_candidate ? CartridgeSaveKind::eeprom_serial : CartridgeSaveKind::none,
          offset,
          static_cast<std::uint32_t>(MemoryBus::kGamePakRomWindowSize),
          eeprom_candidate ? kGamePakEepromBusWidthBits : kGamePakRomBusWidthBits,
          false,
          false,
          true,
          eeprom_candidate,
          mirrored};
}

[[nodiscard]] bool contains_ascii_marker(const std::vector<std::uint8_t>& data,
                                         std::string_view marker) {
  return std::search(data.begin(), data.end(), marker.begin(), marker.end()) != data.end();
}

}  // namespace

MemoryBus::MemoryBus()
    : mgba_debug_string_{},
      debug_output_(),
      game_pak_save_type_(GamePakSaveType::none),
      flash_command_state_(FlashCommandState::idle),
      flash_id_mode_(false),
      flash_bank_(0),
      io_callbacks_() {
  reset();
  clear_game_pak_save();
}

AddressInfo MemoryBus::describe(std::uint32_t address) {
  if (in_range(address, kBiosStart, kBiosEnd)) {
    return {Region::bios, address - kBiosStart, false, false};
  }
  if (address <= kBiosProtectedEnd) {
    return {Region::bios, address - kBiosStart, false, true};
  }
  if (bank_matches(address, kEwramStart)) {
    return {Region::ewram, mirrored_offset(address, kEwramSize), true,
            !in_range(address, kEwramStart, kEwramEnd)};
  }
  if (bank_matches(address, kIwramStart)) {
    return {Region::iwram, mirrored_offset(address, kIwramSize), true,
            !in_range(address, kIwramStart, kIwramEnd)};
  }
  if (in_range(address, kIoStart, kIoEnd)) {
    return {Region::io, address - kIoStart, false, false};
  }
  if (bank_matches(address, kPaletteStart)) {
    return {Region::palette, mirrored_offset(address, kPaletteSize), true,
            !in_range(address, kPaletteStart, kPaletteEnd)};
  }
  if (bank_matches(address, kVramStart)) {
    return {Region::vram, vram_offset(address), true,
            !in_range(address, kVramStart, kVramEnd)};
  }
  if (bank_matches(address, kOamStart)) {
    return {Region::oam, mirrored_offset(address, kOamSize), true,
            !in_range(address, kOamStart, kOamEnd)};
  }
  const CartridgeAddressInfo cartridge = describe_cartridge(address);
  if (cartridge.window == CartridgeWindow::rom_wait0 ||
      cartridge.window == CartridgeWindow::rom_wait1 ||
      cartridge.window == CartridgeWindow::rom_wait2) {
    return {Region::game_pak_rom, cartridge.offset, false, cartridge.mirrored};
  }
  if (cartridge.window == CartridgeWindow::save) {
    return {Region::game_pak_save, cartridge.offset, false, cartridge.mirrored};
  }
  return {Region::unknown, 0, false, false};
}

MemoryReadPolicy MemoryBus::read_policy(std::uint32_t address) {
  const AddressInfo info = describe(address);
  switch (info.region) {
    case Region::ewram:
    case Region::iwram:
    case Region::palette:
    case Region::vram:
    case Region::oam:
      return {info.region, true, MemoryReadFailure::none};
    case Region::bios:
      return {info.region, info.offset <= kBiosEnd,
              info.offset <= kBiosEnd ? MemoryReadFailure::none
                                      : MemoryReadFailure::protected_bios_open_bus_unmodeled};
    case Region::io:
      return {info.region, false, MemoryReadFailure::io_register_facade_required};
    case Region::game_pak_rom:
    case Region::game_pak_save:
      return {info.region, false, MemoryReadFailure::external_data_required};
    case Region::unknown:
      return {info.region, false, MemoryReadFailure::unmapped_open_bus_unmodeled};
  }
  return {Region::unknown, false, MemoryReadFailure::unmapped_open_bus_unmodeled};
}

VideoAccessPolicy MemoryBus::video_access_policy(std::uint32_t address) {
  const AddressInfo info = describe(address);
  switch (info.region) {
    case Region::palette:
    case Region::vram:
    case Region::oam:
      return {info.region, VideoAccessWindow::ppu_contention_unmodeled, true, false};
    case Region::bios:
    case Region::ewram:
    case Region::iwram:
    case Region::io:
    case Region::game_pak_rom:
    case Region::game_pak_save:
      return {info.region, VideoAccessWindow::not_video_memory, true, false};
    case Region::unknown:
      return {info.region, VideoAccessWindow::not_video_memory, false, false};
  }
  return {Region::unknown, VideoAccessWindow::not_video_memory, false, false};
}

CartridgeAddressInfo MemoryBus::describe_cartridge(std::uint32_t address) {
  if (in_range(address, kGamePakRomWait0Start, kGamePakRomWait0End)) {
    return cartridge_rom_info(address, kGamePakRomWait0Start, CartridgeWindow::rom_wait0,
                              false);
  }
  if (in_range(address, kGamePakRomWait1Start, kGamePakRomWait1End)) {
    return cartridge_rom_info(address, kGamePakRomWait1Start, CartridgeWindow::rom_wait1,
                              true);
  }
  if (in_range(address, kGamePakRomWait2Start, kGamePakRomWait2End)) {
    return cartridge_rom_info(address, kGamePakRomWait2Start, CartridgeWindow::rom_wait2,
                              true);
  }
  if (in_range(address, kGamePakSaveStart, kGamePakSaveEnd)) {
    return {CartridgeWindow::save,
            CartridgeSaveKind::sram_or_flash,
            static_cast<std::uint32_t>(bank_offset(address) % kGamePakSaveWindowSize),
            static_cast<std::uint32_t>(kGamePakSaveWindowSize),
            kGamePakSaveBusWidthBits,
            false,
            false,
            true,
            false,
            address >= 0x0F000000 || bank_offset(address) >= kGamePakSaveWindowSize};
  }
  return cartridge_none();
}

MemoryAccessTiming MemoryBus::timing(std::uint32_t address, AccessWidth width) {
  const AddressInfo info = describe(address);
  switch (info.region) {
    case Region::bios:
      return {1, 1, info.offset <= kBiosEnd, false};
    case Region::ewram:
      return {static_cast<std::uint8_t>(is_word(width) ? 6U : 3U),
              static_cast<std::uint8_t>(is_word(width) ? 6U : 3U), true, true};
    case Region::iwram:
      return {1, 1, true, true};
    case Region::palette:
    case Region::vram:
    case Region::oam:
      return {static_cast<std::uint8_t>(is_word(width) ? 2U : 1U),
              static_cast<std::uint8_t>(is_word(width) ? 2U : 1U), true, true};
    case Region::io:
    case Region::game_pak_rom:
    case Region::game_pak_save:
      return {1, 1, false, false};
    case Region::unknown:
      return {0, 0, false, false};
  }
  return {0, 0, false, false};
}

MemoryAccessTiming MemoryBus::timing(std::uint32_t address, AccessWidth width,
                                     const WaitStateControl& waitcnt) {
  const AddressInfo info = describe(address);
  if (info.region == Region::game_pak_rom) {
    const CartridgeAddressInfo cartridge = describe_cartridge(address);
    const GamePakWaitStates wait_states = waitcnt.rom_wait_states(cartridge.window);
    return {wait_states.nonsequential, wait_states.sequential, false, false};
  }
  if (info.region == Region::game_pak_save) {
    const std::uint8_t wait_states = waitcnt.save_wait_states();
    return {wait_states, wait_states, false, false};
  }
  return timing(address, width);
}

std::optional<std::uint8_t> MemoryBus::read8(std::uint32_t address) const {
  if (address >= kNoCashGbaDebugIdStart &&
      address < kNoCashGbaDebugIdStart + kNoCashGbaDebugId.size()) {
    return static_cast<std::uint8_t>(
        kNoCashGbaDebugId.at(address - kNoCashGbaDebugIdStart));
  }
  if (address >= kMgbaDebugStringStart && address <= kMgbaDebugStringEnd) {
    return mgba_debug_string_.at(address - kMgbaDebugStringStart);
  }

  const AddressInfo info = describe(address);
  switch (info.region) {
    case Region::ewram:
      return ewram_.at(info.offset);
    case Region::iwram:
      return iwram_.at(info.offset);
    case Region::palette:
      return palette_.at(info.offset);
    case Region::vram:
      return vram_.at(info.offset);
    case Region::oam:
      return oam_.at(info.offset);
    case Region::bios:
      if (info.offset > kBiosEnd) {
        return std::nullopt;
      }
      if (info.offset < kHleBiosVectorBytes.size()) {
        return kHleBiosVectorBytes.at(info.offset);
      }
      return 0;
    case Region::game_pak_rom:
      if (game_pak_rom_.empty()) {
        return std::nullopt;
      }
      if (info.offset >= game_pak_rom_.size()) {
        const std::uint32_t open_bus = game_pak_open_bus_aligned_word(address);
        return static_cast<std::uint8_t>((open_bus >> ((address & 0x3U) * 8U)) & 0xFFU);
      }
      return game_pak_rom_.at(info.offset);
    case Region::game_pak_save:
      return read_game_pak_save_byte(info.offset);
    case Region::io:
    case Region::unknown:
      return std::nullopt;
  }
  return std::nullopt;
}

std::optional<std::uint16_t> MemoryBus::read16(std::uint32_t address) const {
  const AddressInfo info = describe(address);
  if (info.region == Region::game_pak_save) {
    const std::optional<std::uint8_t> byte = read_game_pak_save_byte(info.offset);
    if (!byte.has_value()) {
      return std::nullopt;
    }
    return repeat_byte16(byte.value());
  }

  if ((address % 2) != 0) {
    return std::nullopt;
  }

  if (info.region == Region::io && io_callbacks_.read16 != nullptr) {
    return io_callbacks_.read16(io_callbacks_.context, address);
  }
  if (address == kMgbaDebugEnable) {
    return kMgbaDebugEnableMagic;
  }

  const std::optional<std::uint8_t> b0 = read8(address);
  const std::optional<std::uint8_t> b1 = read8(address + 1);
  if (!b0.has_value() || !b1.has_value()) {
    return std::nullopt;
  }

  return static_cast<std::uint16_t>(b0.value()) |
         static_cast<std::uint16_t>(b1.value() << 8);
}

std::optional<std::uint32_t> MemoryBus::read32(std::uint32_t address) const {
  const AddressInfo direct_info = describe(address);
  if (direct_info.region == Region::game_pak_save) {
    const std::optional<std::uint8_t> byte = read_game_pak_save_byte(direct_info.offset);
    if (!byte.has_value()) {
      return std::nullopt;
    }
    return repeat_byte32(byte.value());
  }
  if (direct_info.region == Region::game_pak_rom && !game_pak_rom_.empty() &&
      direct_info.offset >= game_pak_rom_.size()) {
    return game_pak_open_bus_word(address);
  }

  if ((address % 4) == 0) {
    if (direct_info.region == Region::io && io_callbacks_.read32 != nullptr) {
      return io_callbacks_.read32(io_callbacks_.context, address);
    }
  }

  const std::uint32_t aligned_address = align_word(address);
  const std::optional<std::uint8_t> b0 = read8(aligned_address);
  const std::optional<std::uint8_t> b1 = read8(aligned_address + 1);
  const std::optional<std::uint8_t> b2 = read8(aligned_address + 2);
  const std::optional<std::uint8_t> b3 = read8(aligned_address + 3);
  if (!b0.has_value() || !b1.has_value() || !b2.has_value() || !b3.has_value()) {
    return std::nullopt;
  }

  const std::uint32_t aligned_value =
      static_cast<std::uint32_t>(b0.value()) |
      (static_cast<std::uint32_t>(b1.value()) << 8) |
      (static_cast<std::uint32_t>(b2.value()) << 16) |
      (static_cast<std::uint32_t>(b3.value()) << 24);
  return rotate_right(aligned_value, static_cast<std::uint8_t>((address & 0x3U) * 8U));
}

void MemoryBus::set_io_callbacks(MemoryBusIoCallbacks callbacks) {
  io_callbacks_ = callbacks;
}

void MemoryBus::clear_io_callbacks() {
  io_callbacks_ = {};
}

bool MemoryBus::load_game_pak_rom(const std::vector<std::uint8_t>& data) {
  if (data.empty() || data.size() > kGamePakRomWindowSize) {
    return false;
  }

  game_pak_rom_ = data;
  return true;
}

void MemoryBus::clear_game_pak_rom() {
  game_pak_rom_.clear();
}

bool MemoryBus::has_game_pak_rom() const {
  return !game_pak_rom_.empty();
}

std::size_t MemoryBus::game_pak_rom_size() const {
  return game_pak_rom_.size();
}

std::vector<std::uint8_t> MemoryBus::export_game_pak_rom() const {
  return game_pak_rom_;
}

std::optional<CartridgeHeader> MemoryBus::game_pak_header() const {
  if (game_pak_rom_.size() < kMinimumCartridgeHeaderSize) {
    return std::nullopt;
  }

  CartridgeHeader header{};
  std::copy_n(game_pak_rom_.begin() + kCartridgeTitleOffset, kCartridgeTitleSize,
              header.title.begin());
  std::copy_n(game_pak_rom_.begin() + kCartridgeGameCodeOffset, kCartridgeGameCodeSize,
              header.game_code.begin());
  std::copy_n(game_pak_rom_.begin() + kCartridgeMakerCodeOffset, kCartridgeMakerCodeSize,
              header.maker_code.begin());
  header.fixed_value = game_pak_rom_.at(kCartridgeFixedValueOffset);
  header.unit_code = game_pak_rom_.at(kCartridgeUnitCodeOffset);
  header.device_type = game_pak_rom_.at(kCartridgeDeviceTypeOffset);
  header.version = game_pak_rom_.at(kCartridgeVersionOffset);
  header.complement_check = game_pak_rom_.at(kCartridgeComplementCheckOffset);
  header.fixed_value_valid = header.fixed_value == kCartridgeExpectedFixedValue;
  return header;
}

std::optional<GamePakSaveType> MemoryBus::detect_game_pak_save_type() const {
  if (game_pak_rom_.empty()) {
    return std::nullopt;
  }

  const bool has_sram = contains_ascii_marker(game_pak_rom_, "SRAM_V");
  const bool has_flash128 = contains_ascii_marker(game_pak_rom_, "FLASH1M_V");
  const bool has_flash64 = contains_ascii_marker(game_pak_rom_, "FLASH512_V") ||
                           contains_ascii_marker(game_pak_rom_, "FLASH_V");
  const bool has_eeprom = contains_ascii_marker(game_pak_rom_, "EEPROM_V");
  const int found_kinds = static_cast<int>(has_sram) + static_cast<int>(has_flash128) +
                          static_cast<int>(has_flash64) + static_cast<int>(has_eeprom);
  if (found_kinds != 1) {
    return std::nullopt;
  }
  if (has_sram) {
    return GamePakSaveType::sram32k;
  }
  if (has_flash128) {
    return GamePakSaveType::flash128k;
  }
  if (has_flash64) {
    return GamePakSaveType::flash64k;
  }
  return GamePakSaveType::eeprom8k;
}

bool MemoryBus::configure_game_pak_save(GamePakSaveType type) {
  const std::size_t size = save_size_for_type(type);
  if (size == 0) {
    clear_game_pak_save();
    return type == GamePakSaveType::none;
  }

  game_pak_save_type_ = type;
  game_pak_save_.assign(size, 0xFF);
  reset_flash_protocol();
  return true;
}

void MemoryBus::clear_game_pak_save() {
  game_pak_save_.clear();
  game_pak_save_type_ = GamePakSaveType::none;
  reset_flash_protocol();
}

bool MemoryBus::has_game_pak_save() const {
  return game_pak_save_type_ != GamePakSaveType::none && !game_pak_save_.empty();
}

GamePakSaveType MemoryBus::game_pak_save_type() const {
  return game_pak_save_type_;
}

std::size_t MemoryBus::game_pak_save_size() const {
  return game_pak_save_.size();
}

FlashProtocolStatus MemoryBus::flash_protocol_status() const {
  return {flash_id_mode_, flash_bank_, flash_command_state_};
}

std::uint16_t MemoryBus::eeprom_block_count() const {
  if (game_pak_save_type_ != GamePakSaveType::eeprom512 &&
      game_pak_save_type_ != GamePakSaveType::eeprom8k) {
    return 0;
  }
  return static_cast<std::uint16_t>(game_pak_save_.size() / 8U);
}

bool MemoryBus::eeprom_write_block(std::uint16_t block_index,
                                   const std::array<std::uint8_t, 8>& data) {
  const std::uint16_t block_count = eeprom_block_count();
  if (block_count == 0 || block_index >= block_count) {
    return false;
  }

  std::copy(data.begin(), data.end(), game_pak_save_.begin() + (block_index * 8U));
  return true;
}

std::optional<std::array<std::uint8_t, 8>> MemoryBus::eeprom_read_block(
    std::uint16_t block_index) const {
  const std::uint16_t block_count = eeprom_block_count();
  if (block_count == 0 || block_index >= block_count) {
    return std::nullopt;
  }

  std::array<std::uint8_t, 8> data{};
  std::copy_n(game_pak_save_.begin() + (block_index * 8U), data.size(), data.begin());
  return data;
}

bool MemoryBus::import_game_pak_save(GamePakSaveType type,
                                     const std::vector<std::uint8_t>& data) {
  const std::size_t size = save_size_for_type(type);
  if (size == 0 || data.size() != size) {
    return false;
  }

  game_pak_save_type_ = type;
  game_pak_save_ = data;
  reset_flash_protocol();
  return true;
}

std::vector<std::uint8_t> MemoryBus::export_game_pak_save() const {
  return game_pak_save_;
}

std::string MemoryBus::debug_output() const {
  return debug_output_;
}

void MemoryBus::clear_debug_output() {
  debug_output_.clear();
  mgba_debug_string_.fill(0);
}

std::uint64_t MemoryBus::state_hash() const {
  StateHasher hasher;
  hasher.add_bytes(ewram_);
  hasher.add_bytes(iwram_);
  hasher.add_bytes(palette_);
  hasher.add_bytes(vram_);
  hasher.add_bytes(oam_);
  hasher.add_bytes(game_pak_rom_);
  hasher.add_u8(static_cast<std::uint8_t>(game_pak_save_type_));
  hasher.add_u8(static_cast<std::uint8_t>(flash_command_state_));
  hasher.add_u8(flash_id_mode_ ? 1U : 0U);
  hasher.add_u8(flash_bank_);
  hasher.add_bytes(game_pak_save_);
  hasher.add_bytes(mgba_debug_string_);
  hasher.add_bytes(debug_output_);
  return hasher.value();
}

bool MemoryBus::write8(std::uint32_t address, std::uint8_t value) {
  if (write_debug8(address, value)) {
    return true;
  }

  const AddressInfo info = describe(address);
  if (info.region == Region::unknown) {
    return true;
  }
  if (info.region == Region::game_pak_save) {
    return write_game_pak_save_byte(info.offset, value);
  }
  if (info.region == Region::game_pak_rom) {
    return has_game_pak_rom();
  }

  if (!info.writable) {
    return false;
  }

  switch (info.region) {
    case Region::ewram:
      ewram_.at(info.offset) = value;
      return true;
    case Region::iwram:
      iwram_.at(info.offset) = value;
      return true;
    case Region::palette:
      palette_.at(info.offset & ~1U) = value;
      palette_.at((info.offset & ~1U) + 1U) = value;
      return true;
    case Region::vram:
      if (info.offset >= 0x10000U) {
        return true;
      }
      vram_.at(info.offset & ~1U) = value;
      vram_.at((info.offset & ~1U) + 1U) = value;
      return true;
    case Region::oam:
      return true;
    case Region::bios:
    case Region::io:
    case Region::game_pak_rom:
    case Region::game_pak_save:
    case Region::unknown:
      return false;
  }
  return false;
}

bool MemoryBus::write16(std::uint32_t address, std::uint16_t value) {
  const AddressInfo direct_info = describe(address);
  if (direct_info.region == Region::game_pak_rom) {
    return has_game_pak_rom();
  }
  if (direct_info.region == Region::game_pak_save) {
    return write_game_pak_save_byte(direct_info.offset,
                                    static_cast<std::uint8_t>(value & 0xFFU));
  }
  if ((address % 2) != 0) {
    return false;
  }
  if (write_debug16(address, value)) {
    return true;
  }

  const AddressInfo info = describe(address);
  if (info.region == Region::io && io_callbacks_.write16 != nullptr) {
    return io_callbacks_.write16(io_callbacks_.context, address, value);
  }
  if (info.region == Region::unknown) {
    return true;
  }
  if (info.region == Region::game_pak_save) {
    return write_game_pak_save_byte(info.offset, static_cast<std::uint8_t>(value & 0xFFU));
  }
  if (info.region == Region::game_pak_rom) {
    return has_game_pak_rom() && describe(address + 1).region == Region::game_pak_rom;
  }

  const AddressInfo b0 = describe(address);
  const AddressInfo b1 = describe(address + 1);
  if (!b0.writable || !b1.writable) {
    return false;
  }

  if (b0.region == Region::palette && b1.region == Region::palette) {
    palette_.at(b0.offset) = static_cast<std::uint8_t>(value & 0xFFU);
    palette_.at(b1.offset) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    return true;
  }
  if (b0.region == Region::vram && b1.region == Region::vram) {
    vram_.at(b0.offset) = static_cast<std::uint8_t>(value & 0xFFU);
    vram_.at(b1.offset) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    return true;
  }
  if (b0.region == Region::oam && b1.region == Region::oam) {
    oam_.at(b0.offset) = static_cast<std::uint8_t>(value & 0xFFU);
    oam_.at(b1.offset) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    return true;
  }

  return write8(address, static_cast<std::uint8_t>(value & 0xFFU)) &&
         write8(address + 1, static_cast<std::uint8_t>((value >> 8) & 0xFFU));
}

bool MemoryBus::write32(std::uint32_t address, std::uint32_t value) {
  const AddressInfo direct_info = describe(address);
  if (direct_info.region == Region::game_pak_rom) {
    return has_game_pak_rom();
  }
  if (direct_info.region == Region::game_pak_save) {
    return write_game_pak_save_byte(direct_info.offset,
                                    static_cast<std::uint8_t>(value & 0xFFU));
  }
  if ((address % 4) != 0) {
    return false;
  }
  if (write_debug32(address, value)) {
    return true;
  }

  const AddressInfo info = describe(address);
  if (info.region == Region::io && io_callbacks_.write32 != nullptr) {
    return io_callbacks_.write32(io_callbacks_.context, address, value);
  }
  if (info.region == Region::unknown) {
    return true;
  }
  if (info.region == Region::game_pak_save) {
    return write_game_pak_save_byte(info.offset, static_cast<std::uint8_t>(value & 0xFFU));
  }
  if (info.region == Region::game_pak_rom) {
    return has_game_pak_rom() && describe(address + 1).region == Region::game_pak_rom &&
           describe(address + 2).region == Region::game_pak_rom &&
           describe(address + 3).region == Region::game_pak_rom;
  }

  const AddressInfo b0 = describe(address);
  const AddressInfo b1 = describe(address + 1);
  const AddressInfo b2 = describe(address + 2);
  const AddressInfo b3 = describe(address + 3);
  if (!b0.writable || !b1.writable || !b2.writable || !b3.writable) {
    return false;
  }

  if (b0.region == Region::palette && b1.region == Region::palette &&
      b2.region == Region::palette && b3.region == Region::palette) {
    palette_.at(b0.offset) = static_cast<std::uint8_t>(value & 0xFFU);
    palette_.at(b1.offset) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    palette_.at(b2.offset) = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    palette_.at(b3.offset) = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    return true;
  }
  if (b0.region == Region::vram && b1.region == Region::vram &&
      b2.region == Region::vram && b3.region == Region::vram) {
    vram_.at(b0.offset) = static_cast<std::uint8_t>(value & 0xFFU);
    vram_.at(b1.offset) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    vram_.at(b2.offset) = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    vram_.at(b3.offset) = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    return true;
  }
  if (b0.region == Region::oam && b1.region == Region::oam &&
      b2.region == Region::oam && b3.region == Region::oam) {
    oam_.at(b0.offset) = static_cast<std::uint8_t>(value & 0xFFU);
    oam_.at(b1.offset) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    oam_.at(b2.offset) = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    oam_.at(b3.offset) = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    return true;
  }

  return write8(address, static_cast<std::uint8_t>(value & 0xFFU)) &&
         write8(address + 1, static_cast<std::uint8_t>((value >> 8) & 0xFFU)) &&
         write8(address + 2, static_cast<std::uint8_t>((value >> 16) & 0xFFU)) &&
         write8(address + 3, static_cast<std::uint8_t>((value >> 24) & 0xFFU));
}

void MemoryBus::reset() {
  soft_reset();
}

void MemoryBus::soft_reset() {
  ewram_.fill(0);
  iwram_.fill(0);
  palette_.fill(0);
  vram_.fill(0);
  oam_.fill(0);
  clear_debug_output();
  reset_flash_protocol();
}

void MemoryBus::hard_reset() {
  soft_reset();
  clear_game_pak_rom();
  clear_game_pak_save();
}

std::size_t MemoryBus::save_size_for_type(GamePakSaveType type) {
  switch (type) {
    case GamePakSaveType::none:
      return 0;
    case GamePakSaveType::sram32k:
      return kSram32kSize;
    case GamePakSaveType::flash64k:
      return kFlash64kSize;
    case GamePakSaveType::flash128k:
      return kFlash128kSize;
    case GamePakSaveType::eeprom512:
      return kEeprom512Size;
    case GamePakSaveType::eeprom8k:
      return kEeprom8kSize;
  }
  return 0;
}

bool MemoryBus::game_pak_save_supports_memory_aperture() const {
  return has_game_pak_save() &&
         (game_pak_save_type_ == GamePakSaveType::sram32k ||
          game_pak_save_type_ == GamePakSaveType::flash64k ||
          game_pak_save_type_ == GamePakSaveType::flash128k);
}

bool MemoryBus::is_flash_save() const {
  return game_pak_save_type_ == GamePakSaveType::flash64k ||
         game_pak_save_type_ == GamePakSaveType::flash128k;
}

std::size_t MemoryBus::flash_effective_offset(std::uint32_t aperture_offset) const {
  const std::size_t bank_base =
      game_pak_save_type_ == GamePakSaveType::flash128k ? flash_bank_ * kFlashBankSize : 0U;
  return bank_base + (aperture_offset % kFlashBankSize);
}

std::optional<std::uint8_t> MemoryBus::read_game_pak_save_byte(
    std::uint32_t aperture_offset) const {
  if (!game_pak_save_supports_memory_aperture()) {
    return std::nullopt;
  }
  if (is_flash_save()) {
    return read_flash_byte(aperture_offset);
  }
  return game_pak_save_.at(aperture_offset % game_pak_save_.size());
}

bool MemoryBus::write_game_pak_save_byte(std::uint32_t aperture_offset,
                                         std::uint8_t value) {
  if (!game_pak_save_supports_memory_aperture()) {
    return false;
  }
  if (is_flash_save()) {
    return write_flash_byte(aperture_offset, value);
  }
  game_pak_save_.at(aperture_offset % game_pak_save_.size()) = value;
  return true;
}

std::optional<std::uint8_t> MemoryBus::read_flash_byte(
    std::uint32_t aperture_offset) const {
  if (!has_game_pak_save() || !is_flash_save()) {
    return std::nullopt;
  }

  if (flash_id_mode_) {
    const std::uint32_t id_offset = aperture_offset % kFlashBankSize;
    if (id_offset == 0) {
      return kFlashManufacturerId;
    }
    if (id_offset == 1) {
      return game_pak_save_type_ == GamePakSaveType::flash128k ? kFlash128DeviceId
                                                               : kFlash64DeviceId;
    }
    return 0xFF;
  }

  return game_pak_save_.at(flash_effective_offset(aperture_offset));
}

bool MemoryBus::write_flash_byte(std::uint32_t aperture_offset, std::uint8_t value) {
  if (!has_game_pak_save() || !is_flash_save()) {
    return false;
  }

  const std::uint32_t command_offset = aperture_offset % kFlashBankSize;
  if (flash_command_state_ == FlashCommandState::idle && value == 0xF0) {
    reset_flash_protocol();
    return command_offset == kFlashUnlockAddress1 || command_offset == kFlashBankSelectAddress;
  }

  switch (flash_command_state_) {
    case FlashCommandState::idle:
      if (command_offset == kFlashUnlockAddress1 && value == 0xAA) {
        flash_command_state_ = FlashCommandState::unlock1;
        return true;
      }
      return false;
    case FlashCommandState::unlock1:
      if (command_offset == kFlashUnlockAddress2 && value == 0x55) {
        flash_command_state_ = FlashCommandState::unlock2;
        return true;
      }
      reset_flash_protocol();
      return false;
    case FlashCommandState::unlock2:
      if (command_offset != kFlashUnlockAddress1) {
        reset_flash_protocol();
        return false;
      }
      if (value == 0x90) {
        flash_id_mode_ = true;
        flash_command_state_ = FlashCommandState::idle;
        return true;
      }
      if (value == 0xA0) {
        flash_command_state_ = FlashCommandState::program_data;
        return true;
      }
      if (value == 0x80) {
        flash_command_state_ = FlashCommandState::erase_unlock1;
        return true;
      }
      if (value == 0xB0) {
        flash_command_state_ = FlashCommandState::bank_select;
        return true;
      }
      if (value == 0xF0) {
        reset_flash_protocol();
        return true;
      }
      reset_flash_protocol();
      return false;
    case FlashCommandState::program_data: {
      const std::size_t data_offset = flash_effective_offset(aperture_offset);
      game_pak_save_.at(data_offset) = static_cast<std::uint8_t>(game_pak_save_.at(data_offset) & value);
      flash_command_state_ = FlashCommandState::idle;
      return true;
    }
    case FlashCommandState::erase_unlock1:
      if (command_offset == kFlashUnlockAddress1 && value == 0xAA) {
        flash_command_state_ = FlashCommandState::erase_unlock2;
        return true;
      }
      reset_flash_protocol();
      return false;
    case FlashCommandState::erase_unlock2:
      if (command_offset == kFlashUnlockAddress2 && value == 0x55) {
        flash_command_state_ = FlashCommandState::erase_command;
        return true;
      }
      reset_flash_protocol();
      return false;
    case FlashCommandState::erase_command:
      if (command_offset == kFlashUnlockAddress1 && value == 0x10) {
        std::fill(game_pak_save_.begin(), game_pak_save_.end(), 0xFF);
        flash_command_state_ = FlashCommandState::idle;
        return true;
      }
      if (value == 0x30) {
        const std::size_t data_offset = flash_effective_offset(aperture_offset);
        const std::size_t sector_start = (data_offset / kFlashSectorSize) * kFlashSectorSize;
        std::fill_n(game_pak_save_.begin() + sector_start, kFlashSectorSize, 0xFF);
        flash_command_state_ = FlashCommandState::idle;
        return true;
      }
      reset_flash_protocol();
      return false;
    case FlashCommandState::bank_select:
      if (command_offset == kFlashBankSelectAddress) {
        flash_bank_ = game_pak_save_type_ == GamePakSaveType::flash128k
                          ? static_cast<std::uint8_t>(value & 0x01U)
                          : 0U;
        flash_command_state_ = FlashCommandState::idle;
        return true;
      }
      reset_flash_protocol();
      return false;
    case FlashCommandState::erase_confirm_unlock1:
    case FlashCommandState::erase_confirm_unlock2:
      reset_flash_protocol();
      return false;
  }
  reset_flash_protocol();
  return false;
}

bool MemoryBus::write_debug8(std::uint32_t address, std::uint8_t value) {
  if (address >= kMgbaDebugStringStart && address <= kMgbaDebugStringEnd) {
    mgba_debug_string_.at(address - kMgbaDebugStringStart) = value;
    return true;
  }
  if (address == kNoCashGbaDebugChar) {
    debug_output_.push_back(static_cast<char>(value));
    return true;
  }
  return false;
}

bool MemoryBus::write_debug16(std::uint32_t address, std::uint16_t value) {
  if (address == kMgbaDebugEnable) {
    return true;
  }
  if (address == kMgbaDebugFlags) {
    flush_mgba_debug_string(value);
    return true;
  }
  if (address >= kMgbaDebugStringStart && address < kMgbaDebugStringEnd) {
    return write_debug8(address, static_cast<std::uint8_t>(value & 0xFFU)) &&
           write_debug8(address + 1, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  }
  if (address == kNoCashGbaDebugChar) {
    debug_output_.push_back(static_cast<char>(value & 0xFFU));
    debug_output_.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    return true;
  }
  return false;
}

bool MemoryBus::write_debug32(std::uint32_t address, std::uint32_t value) {
  if (address >= kMgbaDebugStringStart && address <= kMgbaDebugStringEnd - 3U) {
    return write_debug8(address, static_cast<std::uint8_t>(value & 0xFFU)) &&
           write_debug8(address + 1, static_cast<std::uint8_t>((value >> 8U) & 0xFFU)) &&
           write_debug8(address + 2, static_cast<std::uint8_t>((value >> 16U) & 0xFFU)) &&
           write_debug8(address + 3, static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
  }
  if (address == kNoCashGbaDebugChar) {
    debug_output_.push_back(static_cast<char>(value & 0xFFU));
    debug_output_.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    debug_output_.push_back(static_cast<char>((value >> 16U) & 0xFFU));
    debug_output_.push_back(static_cast<char>((value >> 24U) & 0xFFU));
    return true;
  }
  if (address == kNoCashGbaDebugOut) {
    for (std::uint32_t offset = 0; offset < 256U; ++offset) {
      const std::optional<std::uint8_t> byte = read8(value + offset);
      if (!byte.has_value() || byte.value() == 0) {
        break;
      }
      debug_output_.push_back(static_cast<char>(byte.value()));
    }
    return true;
  }
  return false;
}

void MemoryBus::flush_mgba_debug_string(std::uint16_t flags) {
  if ((flags & 0x0100U) == 0) {
    return;
  }
  for (const std::uint8_t byte : mgba_debug_string_) {
    if (byte == 0) {
      break;
    }
    debug_output_.push_back(static_cast<char>(byte));
  }
  if (debug_output_.empty() || debug_output_.back() != '\n') {
    debug_output_.push_back('\n');
  }
}

void MemoryBus::reset_flash_protocol() {
  flash_command_state_ = FlashCommandState::idle;
  flash_id_mode_ = false;
  flash_bank_ = 0;
}

}  // namespace gba::core
