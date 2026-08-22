#pragma once

#include "gba/core/memory_bus.hpp"

#include <cstdint>
#include <optional>

namespace gba::core {

enum class SpriteColorMode : std::uint8_t {
  bpp4,
  bpp8,
};

struct SpriteAttributes {
  std::uint16_t attr0;
  std::uint16_t attr1;
  std::uint16_t attr2;
  std::int16_t x;
  std::int16_t y;
  std::uint8_t shape;
  std::uint8_t size;
  std::uint8_t width;
  std::uint8_t height;
  std::uint16_t tile_id;
  std::uint8_t priority;
  std::uint8_t palette_bank;
  SpriteColorMode color_mode;
  std::uint8_t affine_matrix_index;
  bool affine;
  bool double_size;
  bool disabled;
  bool hflip;
  bool vflip;
  bool semi_transparent;
  bool obj_window;
  bool mosaic_enabled;
};

struct SpritePixel {
  std::uint8_t color_index;
  std::uint8_t palette_bank;
  std::uint16_t color;
  bool transparent;
};

class PpuSpriteFetcher {
 public:
  static constexpr std::uint32_t kOamBase = 0x07000000;
  static constexpr std::uint32_t kObjTileBase = 0x06010000;
  static constexpr std::uint32_t kObjPaletteBase = 0x05000200;
  static constexpr std::uint16_t kSpriteCount = 128;

  [[nodiscard]] static std::optional<SpriteAttributes> read_sprite(
      const MemoryBus& memory, std::uint16_t index);
  [[nodiscard]] static std::optional<SpritePixel> fetch_sprite_pixel(
      const MemoryBus& memory, const SpriteAttributes& sprite, std::uint8_t local_x,
      std::uint8_t local_y, bool character_mapping_1d);
};

}  // namespace gba::core
