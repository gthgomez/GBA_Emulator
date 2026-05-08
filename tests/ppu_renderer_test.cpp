#include "gba/core/memory_bus.hpp"
#include "gba/core/ppu_renderer.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

gba::core::PpuRenderControl control(std::uint16_t dispcnt, std::uint16_t bg0_control) {
  return {
      dispcnt,
      {bg0_control, 0, 0, 0},
      {0, 0, 0, 0},
      {0, 0, 0, 0},
  };
}

bool write_vram_byte(gba::core::MemoryBus& memory, std::uint32_t address,
                     std::uint8_t value) {
  const std::uint32_t aligned = address & ~1U;
  const std::uint16_t halfword =
      (address & 1U) != 0 ? static_cast<std::uint16_t>(value << 8U) : value;
  return memory.write16(aligned, halfword);
}

}  // namespace

int main() {
  using gba::core::MemoryBus;
  using gba::core::PpuRenderer;

  constexpr std::uint16_t kBg0Enable = 0x0100;
  constexpr std::uint16_t kObjEnable = 0x1000;
  constexpr std::uint16_t kBackdrop = 0x1111;
  constexpr std::uint16_t kBgGreen = 0x03E0;
  constexpr std::uint16_t kBgBlue = 0x001F;
  constexpr std::uint16_t kObjRed = 0x7C00;

  MemoryBus memory;
  PpuRenderer renderer;
  renderer.clear(0x2222);
  expect(renderer.pixel(0, 0) == 0x2222, "renderer clear fills framebuffer");
  expect(renderer.pixel(PpuRenderer::kScreenWidth, 0) == 0,
         "renderer out-of-bounds pixel reads zero");

  expect(memory.write16(0x05000000, kBackdrop), "seed backdrop color");
  const gba::core::PpuRenderStats backdrop_stats =
      renderer.render_scanline(memory, control(0, 8U << 8), 0);
  expect(backdrop_stats.supported_mode, "mode 0 is supported");
  expect(backdrop_stats.bg_pixels == 0, "disabled BG renders no BG pixels");
  expect(backdrop_stats.obj_pixels == 0, "disabled OBJ renders no OBJ pixels");
  expect(renderer.pixel(0, 0) == kBackdrop, "disabled layers render backdrop");

  expect(memory.write16(0x06004000, static_cast<std::uint16_t>(1U | (2U << 12))),
         "seed renderer BG map entry");
  expect(write_vram_byte(memory, 0x06000020, 0x05), "seed renderer BG tile pixel");
  expect(memory.write16(0x05000000 + 2U * 32U + 5U * 2U, kBgGreen),
         "seed renderer BG palette");
  const gba::core::PpuRenderStats bg_stats =
      renderer.render_scanline(memory, control(kBg0Enable, static_cast<std::uint16_t>(8U << 8)),
                               0);
  expect(bg_stats.bg_pixels == 1, "renderer counts one opaque BG pixel");
  expect(renderer.pixel(0, 0) == kBgGreen, "renderer draws BG0 text pixel");
  expect(renderer.pixel(1, 0) == kBackdrop, "transparent BG pixel leaves backdrop");

  expect(memory.write16(0x07000000, 0), "seed renderer OBJ attr0");
  expect(memory.write16(0x07000002, 0), "seed renderer OBJ attr1");
  expect(memory.write16(0x07000004, 10), "seed renderer OBJ attr2");
  expect(write_vram_byte(memory, 0x06010000 + 10U * 32U, 0x09),
         "seed renderer OBJ tile pixel");
  expect(memory.write16(0x05000200 + 9U * 2U, kObjRed), "seed renderer OBJ palette");
  const gba::core::PpuRenderStats obj_stats = renderer.render_scanline(
      memory, control(static_cast<std::uint16_t>(kBg0Enable | kObjEnable),
                      static_cast<std::uint16_t>(1U | (8U << 8))),
      0);
  expect(obj_stats.bg_pixels == 1, "renderer still counts BG under OBJ");
  expect(obj_stats.obj_pixels == 1, "renderer counts one opaque OBJ pixel");
  expect(renderer.pixel(0, 0) == kObjRed, "higher-priority OBJ overlays BG");

  expect(memory.write16(0x07000004, static_cast<std::uint16_t>(10U | (1U << 10))),
         "seed lower-priority OBJ attr2");
  [[maybe_unused]] const gba::core::PpuRenderStats hidden_obj_stats =
      renderer.render_scanline(
          memory, control(static_cast<std::uint16_t>(kBg0Enable | kObjEnable),
                          static_cast<std::uint16_t>(8U << 8)),
          0);
  expect(renderer.pixel(0, 0) == kBgGreen, "lower-priority OBJ stays behind BG");

  expect(memory.write16(0x06004800, 2), "seed renderer BG1 map entry");
  expect(write_vram_byte(memory, 0x06000040, 0x06), "seed renderer BG1 tile pixel");
  expect(memory.write16(0x05000000 + 6U * 2U, kBgBlue), "seed renderer BG1 palette");
  gba::core::PpuRenderControl multi_bg_control =
      control(static_cast<std::uint16_t>(kBg0Enable | 0x0200U),
              static_cast<std::uint16_t>(1U | (8U << 8)));
  multi_bg_control.bg_control.at(1) = static_cast<std::uint16_t>(9U << 8);
  const gba::core::PpuRenderStats multi_bg_stats =
      renderer.render_scanline(memory, multi_bg_control, 0);
  (void)multi_bg_stats;
  expect(renderer.pixel(0, 0) == kBgBlue, "higher-priority BG1 overlays BG0");

  gba::core::PpuRenderControl window_control =
      control(static_cast<std::uint16_t>(kBg0Enable | 0x2000U),
              static_cast<std::uint16_t>(8U << 8));
  window_control.win0h = static_cast<std::uint16_t>((10U << 8) | 20U);
  window_control.win0v = static_cast<std::uint16_t>((0U << 8) | 1U);
  window_control.winin = 0x0001;
  const gba::core::PpuRenderStats window_stats =
      renderer.render_scanline(memory, window_control, 0);
  expect(window_stats.window_masked_pixels > 0, "WIN0 masks pixels outside window");
  expect(renderer.pixel(0, 0) == kBackdrop, "window-masked BG leaves backdrop");

  expect(memory.write16(0x06000000, 0x1234), "seed mode 3 bitmap pixel");
  const gba::core::PpuRenderStats mode3_stats =
      renderer.render_scanline(memory, control(3, 0), 0);
  expect(mode3_stats.supported_mode, "mode 3 is supported");
  expect(mode3_stats.bitmap_pixels == PpuRenderer::kScreenWidth,
         "mode 3 renders one bitmap scanline");
  expect(renderer.pixel(0, 0) == 0x1234, "mode 3 reads 16-bit framebuffer pixels");

  expect(write_vram_byte(memory, 0x06000000, 7), "seed mode 4 bitmap pixel index");
  expect(memory.write16(0x05000000 + 7U * 2U, 0x2345), "seed mode 4 palette color");
  const gba::core::PpuRenderStats mode4_stats =
      renderer.render_scanline(memory, control(4, 0), 0);
  expect(mode4_stats.bitmap_pixels >= 1, "mode 4 renders indexed bitmap pixels");
  expect(renderer.pixel(0, 0) == 0x2345, "mode 4 reads palette-indexed framebuffer pixels");

  expect(memory.write16(0x06000000 + 159U * 2U, 0x3456), "seed mode 5 edge pixel");
  const gba::core::PpuRenderStats mode5_stats =
      renderer.render_scanline(memory, control(5, 0), 0);
  expect(mode5_stats.supported_mode, "mode 5 is supported");
  expect(renderer.pixel(159, 0) == 0x3456, "mode 5 reads 160-wide bitmap pixels");
  expect(renderer.pixel(160, 0) == kBackdrop, "mode 5 outside bitmap uses backdrop");

  const gba::core::PpuRenderStats forced_blank =
      renderer.render_scanline(memory, control(0x0080, 0), 0);
  expect(forced_blank.forced_blank, "forced blank is reported");
  expect(renderer.pixel(0, 0) == 0x7FFF, "forced blank renders white");

  gba::core::PpuRenderControl blend_control = control(0, 0);
  blend_control.bldcnt = 0x0040;
  blend_control.bldy = 16;
  const gba::core::PpuRenderStats blend_stats =
      renderer.render_scanline(memory, blend_control, 0);
  expect(blend_stats.blend_pixels == PpuRenderer::kScreenWidth,
         "brightness blend applies across scanline");
  expect(renderer.pixel(0, 0) != kBackdrop, "brightness blend changes backdrop color");

  const gba::core::PpuRenderStats unsupported =
      renderer.render_scanline(memory, control(6, 8U << 8), 0);
  expect(!unsupported.supported_mode, "renderer reports unsupported display mode");
  expect(renderer.pixel(0, 0) == kBackdrop, "unsupported mode renders backdrop only");
  expect(!renderer.render_scanline(memory, control(0, 0), PpuRenderer::kScreenHeight)
              .supported_mode,
         "out-of-range scanline is rejected");

  std::cout << "ppu_renderer_test: PASS\n";
  return 0;
}
