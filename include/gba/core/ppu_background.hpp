#pragma once

#include "gba/core/memory_bus.hpp"

#include <cstdint>
#include <optional>

namespace gba::core {

enum class BgColorMode : std::uint8_t {
  bpp4,
  bpp8,
};

struct BgControl {
  std::uint16_t raw;
  std::uint8_t priority;
  std::uint8_t charblock;
  std::uint8_t screenblock;
  BgColorMode color_mode;
  std::uint8_t screen_size;
};

struct BgPixel {
  std::uint16_t map_entry;
  std::uint16_t tile_id;
  std::uint8_t palette_bank;
  std::uint8_t color_index;
  std::uint16_t color;
  bool transparent;
  bool hflip;
  bool vflip;
};

class PpuBackgroundFetcher {
 public:
  static constexpr std::uint32_t kVramBase = 0x06000000;
  static constexpr std::uint32_t kPaletteBase = 0x05000000;
  static constexpr std::uint16_t kTilePixels = 8;
  static constexpr std::uint16_t kScreenblockBytes = 0x800;
  static constexpr std::uint16_t kCharblockBytes = 0x4000;

  [[nodiscard]] static BgControl decode_control(std::uint16_t control);
  [[nodiscard]] static std::uint16_t width_pixels(const BgControl& control);
  [[nodiscard]] static std::uint16_t height_pixels(const BgControl& control);
  [[nodiscard]] static std::optional<BgPixel> fetch_text_pixel(
      const MemoryBus& memory, const BgControl& control, std::uint16_t x,
      std::uint16_t y);
};

}  // namespace gba::core
