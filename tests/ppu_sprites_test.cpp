#include "gba/core/memory_bus.hpp"
#include "gba/core/ppu_sprites.hpp"

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

void expect_pixel(const std::optional<gba::core::SpritePixel>& pixel,
                  std::uint8_t color_index, std::uint16_t color,
                  std::string_view message) {
  expect(pixel.has_value(), message);
  expect(pixel->color_index == color_index, message);
  expect(pixel->color == color, message);
}

}  // namespace

int main() {
  using gba::core::MemoryBus;
  using gba::core::PpuSpriteFetcher;
  using gba::core::SpriteColorMode;

  MemoryBus memory;

  expect(!PpuSpriteFetcher::read_sprite(memory, PpuSpriteFetcher::kSpriteCount).has_value(),
         "OAM index outside 128 sprites is rejected");

  expect(memory.write16(0x07000000, 20), "seed OBJ0 attr0");
  expect(memory.write16(0x07000002, 10), "seed OBJ0 attr1");
  expect(memory.write16(0x07000004, static_cast<std::uint16_t>(5U | (1U << 10) |
                                                              (4U << 12))),
         "seed OBJ0 attr2");
  const std::optional<gba::core::SpriteAttributes> obj0 =
      PpuSpriteFetcher::read_sprite(memory, 0);
  expect(obj0.has_value(), "OBJ0 decodes");
  expect(obj0->x == 10, "OBJ0 X decodes");
  expect(obj0->y == 20, "OBJ0 Y decodes");
  expect(obj0->shape == 0, "OBJ0 square shape decodes");
  expect(obj0->size == 0, "OBJ0 size 0 decodes");
  expect(obj0->width == 8 && obj0->height == 8, "OBJ0 8x8 dimensions decode");
  expect(obj0->tile_id == 5, "OBJ0 tile id decodes");
  expect(obj0->priority == 1, "OBJ0 priority decodes");
  expect(obj0->palette_bank == 4, "OBJ0 palette bank decodes");
  expect(obj0->color_mode == SpriteColorMode::bpp4, "OBJ0 defaults to 4bpp");
  expect(!obj0->disabled, "OBJ0 is enabled");

  expect(memory.write8(0x06010000 + 5U * 32U, 0x09), "seed OBJ0 tile pixel");
  expect(memory.write16(0x05000200 + 4U * 32U + 9U * 2U, 0x7FFF),
         "seed OBJ0 palette color");
  const std::optional<gba::core::SpritePixel> obj0_pixel =
      PpuSpriteFetcher::fetch_sprite_pixel(memory, obj0.value(), 0, 0);
  expect_pixel(obj0_pixel, 9, 0x7FFF, "OBJ0 4bpp pixel fetches color");
  expect(!obj0_pixel->transparent, "OBJ0 nonzero color is opaque");

  expect(memory.write16(0x07000008, static_cast<std::uint16_t>(30U | 0x2000U)),
         "seed OBJ1 8bpp attr0");
  expect(memory.write16(0x0700000A, static_cast<std::uint16_t>(40U | (1U << 14))),
         "seed OBJ1 size attr1");
  expect(memory.write16(0x0700000C, 12), "seed OBJ1 attr2");
  const std::optional<gba::core::SpriteAttributes> obj1 =
      PpuSpriteFetcher::read_sprite(memory, 1);
  expect(obj1.has_value(), "OBJ1 decodes");
  expect(obj1->color_mode == SpriteColorMode::bpp8, "OBJ1 8bpp mode decodes");
  expect(obj1->width == 16 && obj1->height == 16, "OBJ1 16x16 dimensions decode");
  expect(memory.write8(0x06010000 + 12U * 32U + 2U * 8U + 3U, 0x55),
         "seed OBJ1 8bpp tile pixel");
  expect(memory.write16(0x05000200 + 0x55U * 2U, 0x03E0), "seed OBJ1 palette color");
  expect_pixel(PpuSpriteFetcher::fetch_sprite_pixel(memory, obj1.value(), 3, 2), 0x55,
               0x03E0, "OBJ1 8bpp pixel fetches color");

  expect(memory.write16(0x07000010, 50), "seed OBJ2 attr0");
  expect(memory.write16(0x07000012,
                        static_cast<std::uint16_t>(0x010U | 0x1000U | 0x2000U)),
         "seed OBJ2 flipped attr1");
  expect(memory.write16(0x07000014, static_cast<std::uint16_t>(20U | (2U << 12))),
         "seed OBJ2 attr2");
  const std::optional<gba::core::SpriteAttributes> obj2 =
      PpuSpriteFetcher::read_sprite(memory, 2);
  expect(obj2.has_value(), "OBJ2 decodes");
  expect(obj2->hflip, "OBJ2 hflip decodes");
  expect(obj2->vflip, "OBJ2 vflip decodes");
  expect(memory.write8(0x06010000 + 20U * 32U + 31U, 0xB0), "seed OBJ2 flipped pixel");
  expect(memory.write16(0x05000200 + 2U * 32U + 11U * 2U, 0x001F),
         "seed OBJ2 palette color");
  expect_pixel(PpuSpriteFetcher::fetch_sprite_pixel(memory, obj2.value(), 0, 0), 11,
               0x001F, "OBJ2 hflip/vflip selects mirrored pixel");

  expect(memory.write16(0x07000018, static_cast<std::uint16_t>(60U | 0x0200U)),
         "seed disabled OBJ3 attr0");
  expect(memory.write16(0x0700001A, 70), "seed disabled OBJ3 attr1");
  expect(memory.write16(0x0700001C, 30), "seed disabled OBJ3 attr2");
  const std::optional<gba::core::SpriteAttributes> obj3 =
      PpuSpriteFetcher::read_sprite(memory, 3);
  expect(obj3.has_value(), "OBJ3 decodes");
  expect(obj3->disabled, "OBJ3 disabled bit decodes for non-affine object");
  expect(!PpuSpriteFetcher::fetch_sprite_pixel(memory, obj3.value(), 0, 0).has_value(),
         "disabled OBJ does not fetch pixels");

  expect(memory.write16(0x07000020, static_cast<std::uint16_t>(70U | (1U << 14))),
         "seed wide OBJ4 attr0");
  expect(memory.write16(0x07000022, static_cast<std::uint16_t>(0x1F8U | (3U << 14))),
         "seed wide OBJ4 attr1");
  expect(memory.write16(0x07000024, 40), "seed wide OBJ4 attr2");
  const std::optional<gba::core::SpriteAttributes> obj4 =
      PpuSpriteFetcher::read_sprite(memory, 4);
  expect(obj4.has_value(), "OBJ4 decodes");
  expect(obj4->x == -8, "OBJ4 signed X wraps from 9-bit coordinate");
  expect(obj4->width == 64 && obj4->height == 32, "OBJ4 wide dimensions decode");

  expect(memory.write16(0x07000028, 80), "seed transparent OBJ5 attr0");
  expect(memory.write16(0x0700002A, 90), "seed transparent OBJ5 attr1");
  expect(memory.write16(0x0700002C, static_cast<std::uint16_t>(50U | (1U << 12))),
         "seed transparent OBJ5 attr2");
  const std::optional<gba::core::SpriteAttributes> obj5 =
      PpuSpriteFetcher::read_sprite(memory, 5);
  expect(obj5.has_value(), "OBJ5 decodes");
  expect(memory.write8(0x06010000 + 50U * 32U, 0x00), "seed transparent OBJ pixel");
  expect(memory.write16(0x05000200 + 1U * 32U, 0x2222), "seed OBJ transparent palette");
  const std::optional<gba::core::SpritePixel> transparent =
      PpuSpriteFetcher::fetch_sprite_pixel(memory, obj5.value(), 0, 0);
  expect_pixel(transparent, 0, 0x2222, "OBJ color zero fetches palette zero");
  expect(transparent->transparent, "OBJ color zero is transparent");

  std::cout << "ppu_sprites_test: PASS\n";
  return 0;
}
