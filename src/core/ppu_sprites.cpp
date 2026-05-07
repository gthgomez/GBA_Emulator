#include "gba/core/ppu_sprites.hpp"

#include <array>

namespace gba::core {
namespace {

struct SpriteSize {
  std::uint8_t width;
  std::uint8_t height;
};

constexpr std::array<std::array<SpriteSize, 4>, 4> kSpriteSizes{{
    {{{8, 8}, {16, 16}, {32, 32}, {64, 64}}},
    {{{16, 8}, {32, 8}, {32, 16}, {64, 32}}},
    {{{8, 16}, {8, 32}, {16, 32}, {32, 64}}},
    {{{0, 0}, {0, 0}, {0, 0}, {0, 0}}},
}};

[[nodiscard]] std::int16_t signed_obj_x(std::uint16_t attr1) {
  const std::uint16_t raw = attr1 & 0x01FFU;
  return raw >= 256U ? static_cast<std::int16_t>(raw - 512U)
                     : static_cast<std::int16_t>(raw);
}

[[nodiscard]] std::uint16_t obj_tile_row_stride(const SpriteAttributes& sprite,
                                                bool bpp8) {
  return static_cast<std::uint16_t>(sprite.width / 8U * (bpp8 ? 2U : 1U));
}

}  // namespace

std::optional<SpriteAttributes> PpuSpriteFetcher::read_sprite(const MemoryBus& memory,
                                                              std::uint16_t index) {
  if (index >= kSpriteCount) {
    return std::nullopt;
  }

  const std::uint32_t base = kOamBase + static_cast<std::uint32_t>(index) * 8U;
  const std::optional<std::uint16_t> attr0 = memory.read16(base);
  const std::optional<std::uint16_t> attr1 = memory.read16(base + 2U);
  const std::optional<std::uint16_t> attr2 = memory.read16(base + 4U);
  if (!attr0.has_value() || !attr1.has_value() || !attr2.has_value()) {
    return std::nullopt;
  }

  const std::uint8_t shape = static_cast<std::uint8_t>((attr0.value() >> 14) & 0x3U);
  const std::uint8_t size = static_cast<std::uint8_t>((attr1.value() >> 14) & 0x3U);
  const SpriteSize dimensions = kSpriteSizes.at(shape).at(size);
  const bool affine = (attr0.value() & 0x0100U) != 0;
  const bool object_disabled = !affine && (attr0.value() & 0x0200U) != 0;

  return SpriteAttributes{
      attr0.value(),
      attr1.value(),
      attr2.value(),
      signed_obj_x(attr1.value()),
      static_cast<std::int16_t>(attr0.value() & 0x00FFU),
      shape,
      size,
      dimensions.width,
      dimensions.height,
      static_cast<std::uint16_t>(attr2.value() & 0x03FFU),
      static_cast<std::uint8_t>((attr2.value() >> 10) & 0x3U),
      static_cast<std::uint8_t>((attr2.value() >> 12) & 0xFU),
      (attr0.value() & 0x2000U) != 0 ? SpriteColorMode::bpp8 : SpriteColorMode::bpp4,
      affine,
      object_disabled || dimensions.width == 0 || dimensions.height == 0,
      !affine && (attr1.value() & 0x1000U) != 0,
      !affine && (attr1.value() & 0x2000U) != 0,
  };
}

std::optional<SpritePixel> PpuSpriteFetcher::fetch_sprite_pixel(
    const MemoryBus& memory, const SpriteAttributes& sprite, std::uint8_t local_x,
    std::uint8_t local_y) {
  if (sprite.disabled || sprite.affine || local_x >= sprite.width ||
      local_y >= sprite.height) {
    return std::nullopt;
  }

  const bool bpp8 = sprite.color_mode == SpriteColorMode::bpp8;
  const std::uint8_t pixel_x =
      sprite.hflip ? static_cast<std::uint8_t>(sprite.width - 1U - local_x) : local_x;
  const std::uint8_t pixel_y =
      sprite.vflip ? static_cast<std::uint8_t>(sprite.height - 1U - local_y) : local_y;
  const std::uint16_t tile_x = pixel_x / 8U;
  const std::uint16_t tile_y = pixel_y / 8U;
  const std::uint8_t in_tile_x = static_cast<std::uint8_t>(pixel_x % 8U);
  const std::uint8_t in_tile_y = static_cast<std::uint8_t>(pixel_y % 8U);
  const std::uint16_t tile_number = static_cast<std::uint16_t>(
      sprite.tile_id + tile_y * obj_tile_row_stride(sprite, bpp8) + tile_x * (bpp8 ? 2U : 1U));
  const std::uint32_t tile_address =
      kObjTileBase + static_cast<std::uint32_t>(tile_number) * 32U;
  const std::uint32_t pixel_address =
      tile_address + (bpp8 ? in_tile_y * 8U + in_tile_x : in_tile_y * 4U + in_tile_x / 2U);
  const std::optional<std::uint8_t> packed_pixel = memory.read8(pixel_address);
  if (!packed_pixel.has_value()) {
    return std::nullopt;
  }

  const std::uint8_t color_index =
      bpp8 ? packed_pixel.value()
           : static_cast<std::uint8_t>((in_tile_x & 1U) != 0 ? packed_pixel.value() >> 4
                                                            : packed_pixel.value() & 0xFU);
  const std::uint32_t palette_address =
      kObjPaletteBase + (bpp8 ? static_cast<std::uint32_t>(color_index) * 2U
                              : (static_cast<std::uint32_t>(sprite.palette_bank) * 32U +
                                 static_cast<std::uint32_t>(color_index) * 2U));
  const std::optional<std::uint16_t> color = memory.read16(palette_address);
  if (!color.has_value()) {
    return std::nullopt;
  }

  return SpritePixel{color_index, sprite.palette_bank, color.value(), color_index == 0};
}

}  // namespace gba::core
