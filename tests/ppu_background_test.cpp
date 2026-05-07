#include "gba/core/memory_bus.hpp"
#include "gba/core/ppu_background.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>

namespace {

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void expect_pixel(const std::optional<gba::core::BgPixel>& pixel, std::uint8_t color_index,
                  std::uint16_t color, std::string_view message) {
  expect(pixel.has_value(), message);
  expect(pixel->color_index == color_index, message);
  expect(pixel->color == color, message);
}

}  // namespace

int main() {
  using gba::core::BgColorMode;
  using gba::core::MemoryBus;
  using gba::core::PpuBackgroundFetcher;

  MemoryBus memory;

  const gba::core::BgControl bg0 = PpuBackgroundFetcher::decode_control(8U << 8);
  expect(bg0.priority == 0, "BG priority decodes");
  expect(bg0.charblock == 0, "BG charblock decodes");
  expect(bg0.screenblock == 8, "BG screenblock decodes");
  expect(bg0.color_mode == BgColorMode::bpp4, "BG defaults to 4bpp");
  expect(PpuBackgroundFetcher::width_pixels(bg0) == 256, "BG size 0 width is 256");
  expect(PpuBackgroundFetcher::height_pixels(bg0) == 256, "BG size 0 height is 256");

  expect(memory.write16(0x06004000, static_cast<std::uint16_t>(1U | (2U << 12))),
         "seed BG map entry");
  expect(memory.write8(0x06000020, 0x05), "seed BG 4bpp tile byte");
  expect(memory.write16(0x0500004A, 0x03E0), "seed BG palette color");
  const std::optional<gba::core::BgPixel> bg_pixel =
      PpuBackgroundFetcher::fetch_text_pixel(memory, bg0, 0, 0);
  expect_pixel(bg_pixel, 5, 0x03E0, "BG 4bpp pixel fetches color");
  expect(bg_pixel->palette_bank == 2, "BG 4bpp palette bank decodes");
  expect(bg_pixel->tile_id == 1, "BG tile id decodes");
  expect(!bg_pixel->transparent, "BG nonzero color is opaque");

  expect(memory.write16(0x06004002,
                        static_cast<std::uint16_t>(2U | 0x0400U | 0x0800U | (3U << 12))),
         "seed flipped BG map entry");
  expect(memory.write8(0x06000040 + 31, 0xA0), "seed flipped BG tile byte");
  expect(memory.write16(0x05000000 + 3U * 32U + 10U * 2U, 0x7C00),
         "seed flipped BG palette color");
  const std::optional<gba::core::BgPixel> flipped =
      PpuBackgroundFetcher::fetch_text_pixel(memory, bg0, 8, 0);
  expect_pixel(flipped, 10, 0x7C00, "BG hflip/vflip selects mirrored pixel");
  expect(flipped->hflip, "BG hflip bit decodes");
  expect(flipped->vflip, "BG vflip bit decodes");

  const gba::core::BgControl bg8 =
      PpuBackgroundFetcher::decode_control(static_cast<std::uint16_t>((1U << 2) |
                                                                      0x0080U |
                                                                      (12U << 8)));
  expect(bg8.charblock == 1, "BG 8bpp charblock decodes");
  expect(bg8.color_mode == BgColorMode::bpp8, "BG 8bpp mode decodes");
  expect(memory.write16(0x06006000, 3), "seed BG 8bpp map entry");
  expect(memory.write8(0x06004000 + 3U * 64U + 2U * 8U + 3U, 0x44),
         "seed BG 8bpp tile byte");
  expect(memory.write16(0x05000000 + 0x44U * 2U, 0x4210), "seed BG 8bpp palette color");
  const std::optional<gba::core::BgPixel> bg8_pixel =
      PpuBackgroundFetcher::fetch_text_pixel(memory, bg8, 3, 2);
  expect_pixel(bg8_pixel, 0x44, 0x4210, "BG 8bpp pixel fetches color");
  expect(bg8_pixel->palette_bank == 0, "BG 8bpp ignores map palette bank");

  const gba::core::BgControl wide =
      PpuBackgroundFetcher::decode_control(static_cast<std::uint16_t>((10U << 8) |
                                                                      (1U << 14)));
  expect(PpuBackgroundFetcher::width_pixels(wide) == 512, "BG size 1 width is 512");
  expect(PpuBackgroundFetcher::height_pixels(wide) == 256, "BG size 1 height is 256");
  expect(memory.write16(0x06005800, static_cast<std::uint16_t>(4U | (1U << 12))),
         "seed BG second screenblock entry");
  expect(memory.write8(0x06000080, 0x07), "seed BG second screenblock tile");
  expect(memory.write16(0x05000000 + 1U * 32U + 7U * 2U, 0x001F),
         "seed BG second screenblock palette");
  expect_pixel(PpuBackgroundFetcher::fetch_text_pixel(memory, wide, 256, 0), 7, 0x001F,
               "BG size 1 fetch crosses into next screenblock");
  expect_pixel(PpuBackgroundFetcher::fetch_text_pixel(memory, wide, 768, 0), 7, 0x001F,
               "BG fetch wraps X coordinate");

  expect(memory.write16(0x06004004, 5), "seed transparent BG map entry");
  expect(memory.write8(0x060000A0, 0x00), "seed transparent BG tile byte");
  expect(memory.write16(0x05000000, 0x1234), "seed transparent palette color zero");
  const std::optional<gba::core::BgPixel> transparent =
      PpuBackgroundFetcher::fetch_text_pixel(memory, bg0, 16, 0);
  expect_pixel(transparent, 0, 0x1234, "BG color zero fetches palette zero");
  expect(transparent->transparent, "BG color zero is transparent");

  std::cout << "ppu_background_test: PASS\n";
  return 0;
}
