#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gba::core {

class WaitStateControl;

enum class Region : std::uint8_t {
  bios,
  ewram,
  iwram,
  io,
  palette,
  vram,
  oam,
  game_pak_rom,
  game_pak_save,
  unknown,
};

enum class AccessWidth : std::uint8_t {
  byte,
  halfword,
  word,
};

enum class CartridgeWindow : std::uint8_t {
  none,
  rom_wait0,
  rom_wait1,
  rom_wait2,
  save,
};

enum class CartridgeSaveKind : std::uint8_t {
  none,
  sram_or_flash,
  eeprom_serial,
};

enum class GamePakSaveType : std::uint8_t {
  none,
  sram32k,
  flash64k,
  flash128k,
  eeprom512,
  eeprom8k,
};

enum class FlashCommandState : std::uint8_t {
  idle,
  unlock1,
  unlock2,
  program_data,
  erase_unlock1,
  erase_unlock2,
  erase_command,
  erase_confirm_unlock1,
  erase_confirm_unlock2,
  bank_select,
};

enum class MemoryReadFailure : std::uint8_t {
  none,
  protected_bios_open_bus_unmodeled,
  io_register_facade_required,
  external_data_required,
  unmapped_open_bus_unmodeled,
};

enum class VideoAccessWindow : std::uint8_t {
  not_video_memory,
  ppu_contention_unmodeled,
};

struct AddressInfo {
  Region region;
  std::uint32_t offset;
  bool writable;
  bool mirrored;
};

struct MemoryReadPolicy {
  Region region;
  bool readable;
  MemoryReadFailure failure;
};

struct VideoAccessPolicy {
  Region region;
  VideoAccessWindow window;
  bool access_allowed;
  bool contention_timing_modeled;
};

struct CartridgeAddressInfo {
  CartridgeWindow window;
  CartridgeSaveKind save_kind;
  std::uint32_t offset;
  std::uint32_t max_window_size;
  std::uint8_t bus_width_bits;
  bool readable;
  bool writable;
  bool requires_external_data;
  bool serial_protocol;
  bool mirrored;
};

struct MemoryAccessTiming {
  std::uint8_t nonsequential;
  std::uint8_t sequential;
  bool readable;
  bool writable;
};

struct CartridgeHeader {
  std::array<std::uint8_t, 12> title;
  std::array<std::uint8_t, 4> game_code;
  std::array<std::uint8_t, 2> maker_code;
  std::uint8_t fixed_value;
  std::uint8_t unit_code;
  std::uint8_t device_type;
  std::uint8_t version;
  std::uint8_t complement_check;
  bool fixed_value_valid;
};

struct FlashProtocolStatus {
  bool id_mode;
  std::uint8_t bank;
  FlashCommandState command_state;
};

struct MemoryBusIoCallbacks {
  void* context = nullptr;
  std::optional<std::uint16_t> (*read16)(void* context, std::uint32_t address) = nullptr;
  std::optional<std::uint32_t> (*read32)(void* context, std::uint32_t address) = nullptr;
  bool (*write16)(void* context, std::uint32_t address, std::uint16_t value) = nullptr;
  bool (*write32)(void* context, std::uint32_t address, std::uint32_t value) = nullptr;
};

class MemoryBus {
 public:
  static constexpr std::size_t kEwramSize = 256 * 1024;
  static constexpr std::size_t kIwramSize = 32 * 1024;
  static constexpr std::size_t kPaletteSize = 1024;
  static constexpr std::size_t kVramSize = 96 * 1024;
  static constexpr std::size_t kOamSize = 1024;
  static constexpr std::size_t kGamePakRomWindowSize = 32 * 1024 * 1024;
  static constexpr std::size_t kGamePakSaveWindowSize = 64 * 1024;
  static constexpr std::size_t kSram32kSize = 32 * 1024;
  static constexpr std::size_t kFlash64kSize = 64 * 1024;
  static constexpr std::size_t kFlash128kSize = 128 * 1024;
  static constexpr std::size_t kEeprom512Size = 512;
  static constexpr std::size_t kEeprom8kSize = 8 * 1024;

  MemoryBus();

  [[nodiscard]] static AddressInfo describe(std::uint32_t address);
  [[nodiscard]] static MemoryReadPolicy read_policy(std::uint32_t address);
  [[nodiscard]] static VideoAccessPolicy video_access_policy(std::uint32_t address);
  [[nodiscard]] static CartridgeAddressInfo describe_cartridge(std::uint32_t address);
  [[nodiscard]] static MemoryAccessTiming timing(std::uint32_t address, AccessWidth width);
  [[nodiscard]] static MemoryAccessTiming timing(std::uint32_t address, AccessWidth width,
                                                 const WaitStateControl& waitcnt);
  [[nodiscard]] std::optional<std::uint8_t> read8(std::uint32_t address) const;
  [[nodiscard]] std::optional<std::uint16_t> read16(std::uint32_t address) const;
  [[nodiscard]] std::optional<std::uint32_t> read32(std::uint32_t address) const;
  void drive_open_bus(std::uint32_t value);
  void clear_open_bus_latch();
  [[nodiscard]] std::optional<std::uint32_t> open_bus_latch() const;
  void set_io_callbacks(MemoryBusIoCallbacks callbacks);
  void clear_io_callbacks();
  [[nodiscard]] bool load_game_pak_rom(const std::vector<std::uint8_t>& data);
  void clear_game_pak_rom();
  [[nodiscard]] bool has_game_pak_rom() const;
  [[nodiscard]] std::size_t game_pak_rom_size() const;
  [[nodiscard]] std::vector<std::uint8_t> export_game_pak_rom() const;
  [[nodiscard]] std::optional<CartridgeHeader> game_pak_header() const;
  [[nodiscard]] static bool cartridge_complement_valid(const std::vector<std::uint8_t>& rom);
  [[nodiscard]] bool cartridge_header_complement_valid() const;
  [[nodiscard]] bool cartridge_header_is_valid() const;
  [[nodiscard]] std::optional<GamePakSaveType> detect_game_pak_save_type() const;
  [[nodiscard]] bool configure_game_pak_save(GamePakSaveType type);
  void clear_game_pak_save();
  [[nodiscard]] bool has_game_pak_save() const;
  [[nodiscard]] GamePakSaveType game_pak_save_type() const;
  [[nodiscard]] std::size_t game_pak_save_size() const;
  [[nodiscard]] FlashProtocolStatus flash_protocol_status() const;
  [[nodiscard]] std::uint16_t eeprom_block_count() const;
  [[nodiscard]] bool eeprom_write_block(std::uint16_t block_index,
                                        const std::array<std::uint8_t, 8>& data);
  [[nodiscard]] std::optional<std::array<std::uint8_t, 8>> eeprom_read_block(
      std::uint16_t block_index) const;
  [[nodiscard]] bool import_game_pak_save(GamePakSaveType type,
                                          const std::vector<std::uint8_t>& data);
  [[nodiscard]] std::vector<std::uint8_t> export_game_pak_save() const;
  [[nodiscard]] std::string debug_output() const;
  void clear_debug_output();
  [[nodiscard]] std::uint64_t state_hash() const;
  [[nodiscard]] bool write8(std::uint32_t address, std::uint8_t value);
  [[nodiscard]] bool write16(std::uint32_t address, std::uint16_t value);
  [[nodiscard]] bool write32(std::uint32_t address, std::uint32_t value);
  void reset();
  void soft_reset();
  void hard_reset();
  void register_ram_reset(std::uint32_t flags);

 private:
  std::array<std::uint8_t, kEwramSize> ewram_;
  std::array<std::uint8_t, kIwramSize> iwram_;
  std::array<std::uint8_t, kPaletteSize> palette_;
  std::array<std::uint8_t, kVramSize> vram_;
  std::array<std::uint8_t, kOamSize> oam_;
  std::vector<std::uint8_t> game_pak_rom_;
  std::vector<std::uint8_t> game_pak_save_;
  std::array<std::uint8_t, 256> mgba_debug_string_;
  std::string debug_output_;
  GamePakSaveType game_pak_save_type_;
  FlashCommandState flash_command_state_;
  bool flash_id_mode_;
  std::uint8_t flash_bank_;
  std::uint32_t open_bus_latch_;
  bool open_bus_latch_valid_;
  MemoryBusIoCallbacks io_callbacks_;

  [[nodiscard]] static std::size_t save_size_for_type(GamePakSaveType type);
  [[nodiscard]] bool game_pak_save_supports_memory_aperture() const;
  [[nodiscard]] bool is_flash_save() const;
  [[nodiscard]] std::size_t flash_effective_offset(std::uint32_t aperture_offset) const;
  [[nodiscard]] std::optional<std::uint8_t> read_game_pak_save_byte(
      std::uint32_t aperture_offset) const;
  [[nodiscard]] bool write_game_pak_save_byte(std::uint32_t aperture_offset,
                                              std::uint8_t value);
  [[nodiscard]] std::optional<std::uint8_t> read_flash_byte(
      std::uint32_t aperture_offset) const;
  [[nodiscard]] bool write_flash_byte(std::uint32_t aperture_offset, std::uint8_t value);
  [[nodiscard]] bool write_debug8(std::uint32_t address, std::uint8_t value);
  [[nodiscard]] bool write_debug16(std::uint32_t address, std::uint16_t value);
  [[nodiscard]] bool write_debug32(std::uint32_t address, std::uint32_t value);
  void flush_mgba_debug_string(std::uint16_t flags);
  void reset_flash_protocol();
};

}  // namespace gba::core
