#include "gba/core/memory_bus.hpp"
#include "gba/core/ppu_renderer.hpp"

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

}  // namespace

int main() {
  using gba::core::MemoryBus;
  using gba::core::PpuRenderer;

  constexpr std::uint16_t kBg0Enable = 0x0100;
  constexpr std::uint16_t kObjEnable = 0x1000;
  constexpr std::uint16_t kBackdrop = 0x1111;
  constexpr std::uint16_t kBgGreen = 0x03E0;
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
  expect(memory.write8(0x06000020, 0x05), "seed renderer BG tile pixel");
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
  expect(memory.write8(0x06010000 + 10U * 32U, 0x09), "seed renderer OBJ tile pixel");
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

  const gba::core::PpuRenderStats unsupported =
      renderer.render_scanline(memory, control(3, 8U << 8), 0);
  expect(!unsupported.supported_mode, "renderer reports unsupported display mode");
  expect(renderer.pixel(0, 0) == kBackdrop, "unsupported mode renders backdrop only");
  expect(!renderer.render_scanline(memory, control(0, 0), PpuRenderer::kScreenHeight)
              .supported_mode,
         "out-of-range scanline is rejected");

  std::cout << "ppu_renderer_test: PASS\n";
  return 0;
}
