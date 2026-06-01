#include "gba/core/ppu_sprites.hpp"

#include <array>
#include <utility>

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

[[nodiscard]] std::int16_t signed_oam_param(std::uint16_t value) {
  return static_cast<std::int16_t>(value);
}

[[nodiscard]] std::uint16_t affine_param_address(std::uint8_t matrix_index,
                                                 std::uint8_t component) {
  return static_cast<std::uint16_t>(matrix_index * 32U + 6U + component * 8U);
}

[[nodiscard]] std::optional<std::int16_t> read_affine_param(const MemoryBus& memory,
                                                            std::uint8_t matrix_index,
                                                            std::uint8_t component) {
  const std::optional<std::uint16_t> raw = memory.read16(
      PpuSpriteFetcher::kOamBase + affine_param_address(matrix_index, component));
  if (!raw.has_value()) {
    return std::nullopt;
  }
  return signed_oam_param(raw.value());
}

[[nodiscard]] std::optional<std::pair<std::uint8_t, std::uint8_t>>
affine_texture_coordinates(const MemoryBus& memory, const SpriteAttributes& sprite,
                           std::uint8_t local_x, std::uint8_t local_y) {
  const std::optional<std::int16_t> pa =
      read_affine_param(memory, sprite.affine_matrix_index, 0);
  const std::optional<std::int16_t> pb =
      read_affine_param(memory, sprite.affine_matrix_index, 1);
  const std::optional<std::int16_t> pc =
      read_affine_param(memory, sprite.affine_matrix_index, 2);
  const std::optional<std::int16_t> pd =
      read_affine_param(memory, sprite.affine_matrix_index, 3);
  if (!pa.has_value() || !pb.has_value() || !pc.has_value() || !pd.has_value()) {
    return std::nullopt;
  }

  const std::int32_t texture_center_x = sprite.width / 2;
  const std::int32_t texture_center_y = sprite.height / 2;
  const std::int32_t screen_width = sprite.double_size ? sprite.width * 2 : sprite.width;
  const std::int32_t screen_height = sprite.double_size ? sprite.height * 2 : sprite.height;
  const std::int32_t dx = static_cast<std::int32_t>(local_x) - screen_width / 2;
  const std::int32_t dy = static_cast<std::int32_t>(local_y) - screen_height / 2;
  const std::int32_t texture_x =
      texture_center_x + ((pa.value() * dx + pb.value() * dy) >> 8);
  const std::int32_t texture_y =
      texture_center_y + ((pc.value() * dx + pd.value() * dy) >> 8);
  if (texture_x < 0 || texture_x >= sprite.width || texture_y < 0 ||
      texture_y >= sprite.height) {
    return std::nullopt;
  }

  return std::pair<std::uint8_t, std::uint8_t>{
      static_cast<std::uint8_t>(texture_x), static_cast<std::uint8_t>(texture_y)};
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
  const bool double_size = affine && (attr0.value() & 0x0200U) != 0;
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
      static_cast<std::uint8_t>((attr1.value() >> 9) & 0x1FU),
      affine,
      double_size,
      object_disabled || dimensions.width == 0 || dimensions.height == 0,
      !affine && (attr1.value() & 0x1000U) != 0,
      !affine && (attr1.value() & 0x2000U) != 0,
  };
}

std::optional<SpritePixel> PpuSpriteFetcher::fetch_sprite_pixel(
    const MemoryBus& memory, const SpriteAttributes& sprite, std::uint8_t local_x,
    std::uint8_t local_y) {
  const std::uint8_t screen_width = sprite.double_size
                                        ? static_cast<std::uint8_t>(sprite.width * 2U)
                                        : sprite.width;
  const std::uint8_t screen_height = sprite.double_size
                                         ? static_cast<std::uint8_t>(sprite.height * 2U)
                                         : sprite.height;
  if (sprite.disabled || local_x >= screen_width || local_y >= screen_height) {
    return std::nullopt;
  }

  const bool bpp8 = sprite.color_mode == SpriteColorMode::bpp8;
  std::uint8_t pixel_x = local_x;
  std::uint8_t pixel_y = local_y;
  if (sprite.affine) {
    const std::optional<std::pair<std::uint8_t, std::uint8_t>> transformed =
        affine_texture_coordinates(memory, sprite, local_x, local_y);
    if (!transformed.has_value()) {
      return std::nullopt;
    }
    pixel_x = transformed->first;
    pixel_y = transformed->second;
  } else {
    pixel_x =
        sprite.hflip ? static_cast<std::uint8_t>(sprite.width - 1U - local_x) : local_x;
    pixel_y =
        sprite.vflip ? static_cast<std::uint8_t>(sprite.height - 1U - local_y) : local_y;
  }
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
