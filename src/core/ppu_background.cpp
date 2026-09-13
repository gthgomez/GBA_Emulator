#include "gba/core/ppu_background.hpp"

namespace gba::core {
namespace {

[[nodiscard]] std::uint16_t text_map_width_tiles(const BgControl& control) {
  return (control.screen_size & 0x1U) != 0 ? 64 : 32;
}

[[nodiscard]] std::uint16_t text_map_height_tiles(const BgControl& control) {
  return (control.screen_size & 0x2U) != 0 ? 64 : 32;
}

[[nodiscard]] std::uint32_t screenblock_base(std::uint8_t screenblock) {
  return PpuBackgroundFetcher::kVramBase +
         static_cast<std::uint32_t>(screenblock & 0x1FU) *
             PpuBackgroundFetcher::kScreenblockBytes;
}

[[nodiscard]] std::uint32_t charblock_base(std::uint8_t charblock) {
  return PpuBackgroundFetcher::kVramBase +
         static_cast<std::uint32_t>(charblock & 0x3U) *
             PpuBackgroundFetcher::kCharblockBytes;
}

[[nodiscard]] std::uint8_t screenblock_for_tile(const BgControl& control,
                                                std::uint16_t tile_x,
                                                std::uint16_t tile_y) {
  const std::uint8_t block_x = static_cast<std::uint8_t>(tile_x / 32U);
  const std::uint8_t block_y = static_cast<std::uint8_t>(tile_y / 32U);
  const std::uint8_t block_offset =
      text_map_width_tiles(control) == 64
          ? static_cast<std::uint8_t>(block_x + (block_y * 2U))
          : block_y;
  return static_cast<std::uint8_t>((control.screenblock + block_offset) & 0x1FU);
}

}  // namespace

BgControl PpuBackgroundFetcher::decode_control(std::uint16_t control) {
  return {
      control,
      static_cast<std::uint8_t>(control & 0x3U),
      static_cast<std::uint8_t>((control >> 2) & 0x3U),
      static_cast<std::uint8_t>((control >> 8) & 0x1FU),
      (control & 0x0080U) != 0 ? BgColorMode::bpp8 : BgColorMode::bpp4,
      static_cast<std::uint8_t>((control >> 14) & 0x3U),
  };
}

std::uint16_t PpuBackgroundFetcher::text_width_pixels(const BgControl& control) {
  return static_cast<std::uint16_t>(text_map_width_tiles(control) * kTilePixels);
}

std::uint16_t PpuBackgroundFetcher::text_height_pixels(const BgControl& control) {
  return static_cast<std::uint16_t>(text_map_height_tiles(control) * kTilePixels);
}

std::uint16_t PpuBackgroundFetcher::width_pixels(const BgControl& control) {
  return text_width_pixels(control);
}

std::uint16_t PpuBackgroundFetcher::height_pixels(const BgControl& control) {
  return text_height_pixels(control);
}

// GBATEK affine BG screen sizes: entries 128/256/512/1024 px for size 0-3.
std::uint32_t PpuBackgroundFetcher::affine_map_pixels(const BgControl& control) {
  return 128U << (control.screen_size & 0x3U);
}

std::optional<BgPixel> PpuBackgroundFetcher::fetch_text_pixel(
    const MemoryBus& memory, const BgControl& control, std::uint16_t x,
    std::uint16_t y) {
  const std::uint16_t wrapped_x = x % text_width_pixels(control);
  const std::uint16_t wrapped_y = y % text_height_pixels(control);
  const std::uint16_t tile_x = wrapped_x / kTilePixels;
  const std::uint16_t tile_y = wrapped_y / kTilePixels;
  const std::uint8_t local_x = static_cast<std::uint8_t>(wrapped_x % kTilePixels);
  const std::uint8_t local_y = static_cast<std::uint8_t>(wrapped_y % kTilePixels);
  const std::uint8_t block = screenblock_for_tile(control, tile_x, tile_y);
  const std::uint16_t block_tile_x = tile_x % 32U;
  const std::uint16_t block_tile_y = tile_y % 32U;
  const std::uint32_t map_entry_address =
      screenblock_base(block) + ((block_tile_y * 32U + block_tile_x) * 2U);
  const std::optional<std::uint16_t> map_entry = memory.read16(map_entry_address);
  if (!map_entry.has_value()) {
    return std::nullopt;
  }

  const std::uint16_t entry = map_entry.value();
  const std::uint16_t tile_id = entry & 0x03FFU;
  const bool hflip = (entry & 0x0400U) != 0;
  const bool vflip = (entry & 0x0800U) != 0;
  const std::uint8_t palette_bank = static_cast<std::uint8_t>((entry >> 12) & 0xFU);
  const std::uint8_t pixel_x = hflip ? static_cast<std::uint8_t>(7U - local_x) : local_x;
  const std::uint8_t pixel_y = vflip ? static_cast<std::uint8_t>(7U - local_y) : local_y;
  const bool bpp8 = control.color_mode == BgColorMode::bpp8;
  const std::uint32_t tile_bytes = bpp8 ? 64U : 32U;
  const std::uint32_t tile_address =
      charblock_base(control.charblock) + static_cast<std::uint32_t>(tile_id) * tile_bytes;
  const std::uint32_t pixel_address =
      tile_address + (bpp8 ? pixel_y * 8U + pixel_x : pixel_y * 4U + pixel_x / 2U);
  const std::optional<std::uint8_t> packed_pixel = memory.read8(pixel_address);
  if (!packed_pixel.has_value()) {
    return std::nullopt;
  }

  const std::uint8_t color_index =
      bpp8 ? packed_pixel.value()
           : static_cast<std::uint8_t>((pixel_x & 1U) != 0 ? packed_pixel.value() >> 4
                                                          : packed_pixel.value() & 0xFU);
  const std::uint32_t palette_address =
      kPaletteBase + (bpp8 ? static_cast<std::uint32_t>(color_index) * 2U
                           : (static_cast<std::uint32_t>(palette_bank) * 32U +
                              static_cast<std::uint32_t>(color_index) * 2U));
  const std::optional<std::uint16_t> color = memory.read16(palette_address);
  if (!color.has_value()) {
    return std::nullopt;
  }

  return BgPixel{entry, tile_id, palette_bank, color_index, color.value(),
                 color_index == 0, hflip, vflip};
}

// GBATEK affine fetch: 1-byte map entries hold 8-bit tile indices, tiles are
// 64-byte 8bpp cells, and the whole map lives in one screenblock region.
std::optional<AffinePixel> PpuBackgroundFetcher::fetch_affine_pixel(
    const MemoryBus& memory, const BgControl& control, std::int32_t tex_x,
    std::int32_t tex_y) {
  const std::uint32_t map_pixels = affine_map_pixels(control);
  const std::uint32_t map_tiles = map_pixels / kTilePixels;
  const bool wraparound = (control.raw & 0x2000U) != 0;
  const bool outside = tex_x < 0 || tex_y < 0 ||
                       tex_x >= static_cast<std::int32_t>(map_pixels) ||
                       tex_y >= static_cast<std::int32_t>(map_pixels);
  if (outside && !wraparound) {
    return AffinePixel{0, 0, 0, true};
  }
  // Power-of-two masks keep negative wrapped coordinates positive.
  const std::uint32_t masked_x =
      static_cast<std::uint32_t>(tex_x) & (map_pixels - 1U);
  const std::uint32_t masked_y =
      static_cast<std::uint32_t>(tex_y) & (map_pixels - 1U);
  const std::uint32_t tile_index_address = screenblock_base(control.screenblock) +
                                           (masked_y / kTilePixels) * map_tiles +
                                           masked_x / kTilePixels;
  const std::optional<std::uint8_t> tile_index = memory.read8(tile_index_address);
  if (!tile_index.has_value()) {
    return std::nullopt;
  }
  const std::uint8_t in_tile_x =
      static_cast<std::uint8_t>(masked_x % kTilePixels);
  const std::uint8_t in_tile_y =
      static_cast<std::uint8_t>(masked_y % kTilePixels);
  const std::uint32_t pixel_address =
      charblock_base(control.charblock) +
      static_cast<std::uint32_t>(tile_index.value()) * 64U + in_tile_y * 8U +
      in_tile_x;
  const std::optional<std::uint8_t> packed_pixel = memory.read8(pixel_address);
  if (!packed_pixel.has_value()) {
    return std::nullopt;
  }
  const std::uint8_t color_index = packed_pixel.value();
  const std::optional<std::uint16_t> color =
      memory.read16(kPaletteBase + static_cast<std::uint32_t>(color_index) * 2U);
  if (!color.has_value()) {
    return std::nullopt;
  }
  return AffinePixel{tile_index.value(), color_index, color.value(),
                     color_index == 0};
}

}  // namespace gba::core
