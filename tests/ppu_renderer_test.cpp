#include "gba/core/memory_bus.hpp"
#include "gba/core/ppu_renderer.hpp"
#include "gba/core/ppu_sprites.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

#include "test_helpers.hpp"

namespace {

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

void obj_line_budget_test() {
  using gba::core::MemoryBus;
  using gba::core::PpuRenderer;
  auto memory = std::make_unique<MemoryBus>();
  auto renderer = std::make_unique<PpuRenderer>();
  constexpr std::uint16_t kColor = 0x001F;
  const auto scene = [&](unsigned consumers, bool affine, bool disabled,
                         bool different_line, int consumer_x = 0) {
    memory->reset();
    for (unsigned index = 0; index < 128; ++index) {
      expect(memory->write16(0x07000000U + index * 8U, 0x0200),
             "disable unused budget fixture objects");
    }
    expect(memory->write16(0x05000202, kColor), "seed budget fixture palette");
    // Transparent wide objects still consume rendering time. The final
    // object uses a separate solid tile set so its admission is observable.
    for (unsigned offset = 0; offset < 2048; offset += 2) {
      expect(memory->write16(0x06011000U + offset, 0x1111),
             "seed solid budget fixture tiles");
    }
    for (unsigned index = 0; index <= consumers; ++index) {
      const auto base = 0x07000000U + index * 8U;
      const bool target = index == consumers;
      std::uint16_t attr0 = affine ? 0x0300 : 0;
      if (!target && disabled) attr0 = 0x0200;
      if (!target && different_line) attr0 |= 64;
      expect(memory->write16(base, attr0), "seed budget fixture attr0");
      const auto x = static_cast<std::uint16_t>(consumer_x) & 0x01FFU;
      expect(memory->write16(base + 2, target ? 0xC080 : 0xC000U | x),
             "seed budget fixture attr1");
      expect(memory->write16(base + 4, target ? 128 : 0),
             "seed budget fixture tile index");
    }
    expect(memory->write16(0x07000006, 0x0100), "seed budget affine PA");
    expect(memory->write16(0x0700000E, 0), "seed budget affine PB");
    expect(memory->write16(0x07000016, 0), "seed budget affine PC");
    expect(memory->write16(0x0700001E, 0x0100), "seed budget affine PD");
  };
  scene(19, false, false, false);
  (void)renderer->render_scanline(*memory, control(0x1040, 0), 0);
  expect(renderer->pixel(128, 0) == 0,
         "nineteen transparent 64px objects exhaust the normal line budget");
  // These prefix endpoints verify the documented nominal model, not exact
  // hardware OAM/VRAM pipeline overlap or offscreen clipping behavior.
  scene(18, false, false, false);
  (void)renderer->render_scanline(*memory, control(0x1040, 0), 0);
  expect(renderer->pixel(185, 0) == kColor && renderer->pixel(186, 0) == 0,
         "normal budget retains the final object's nominal 58px prefix");
  scene(15, false, false, false);
  (void)renderer->render_scanline(*memory, control(0x1040, 0), 0);
  expect(renderer->pixel(128, 0) == kColor,
         "normal allowance admits the sixteenth wide object");
  (void)renderer->render_scanline(*memory, control(0x1060, 0), 0);
  expect(renderer->pixel(128, 0) == 0,
         "HBlank-free allowance is exhausted before the sixteenth wide object");
  scene(5, true, false, false);
  (void)renderer->render_scanline(*memory, control(0x1040, 0), 48);
  expect(renderer->pixel(176, 48) == 0,
         "five transparent double-area affine objects exhaust the line budget");
  scene(4, true, false, false);
  (void)renderer->render_scanline(*memory, control(0x1040, 0), 48);
  expect(renderer->pixel(195, 48) == kColor && renderer->pixel(196, 48) == 0,
         "affine budget retains the final object's nominal 68px drawing prefix");
  scene(19, false, true, false);
  (void)renderer->render_scanline(*memory, control(0x1060, 0), 0);
  expect(renderer->pixel(128, 0) == kColor,
         "disabled objects do not consume full pixel-rendering work");
  scene(19, false, false, true);
  (void)renderer->render_scanline(*memory, control(0x1060, 0), 0);
  expect(renderer->pixel(128, 0) == kColor,
         "objects on another line do not consume full pixel-rendering work");
  scene(19, false, false, false, -8);
  (void)renderer->render_scanline(*memory, control(0x1040, 0), 0);
  expect(renderer->pixel(191, 0) == kColor,
         "fully clipped normal pixel pairs do not consume rendering work");
  scene(19, false, false, false, -1);
  (void)renderer->render_scanline(*memory, control(0x1040, 0), 0);
  expect(renderer->pixel(128, 0) == 0,
         "a normal pair crossing the left edge still consumes work");
  scene(19, false, false, false, -2);
  (void)renderer->render_scanline(*memory, control(0x1040, 0), 0);
  expect(renderer->pixel(159, 0) == kColor && renderer->pixel(160, 0) == 0,
         "clipped normal pairs retain the nominal 32px sentinel prefix");
  scene(4, true, false, false, -16);
  (void)renderer->render_scanline(*memory, control(0x1040, 0), 48);
  expect(renderer->pixel(223, 48) == kColor,
         "clipped affine pixels preserve work for later objects");
}

void obj_window_texel_test() {
  using gba::core::MemoryBus;
  using gba::core::PpuRenderer;
  auto memory = std::make_unique<MemoryBus>();
  auto renderer = std::make_unique<PpuRenderer>();
  constexpr std::uint16_t kGreen = 0x03E0;
  constexpr std::uint32_t kWindowTile = 0x06011000;
  const auto scene = [&]() {
    memory->reset();
    for (unsigned index = 0; index < 128; ++index) {
      expect(memory->write16(0x07000000U + index * 8U, 0x0200),
             "disable unused OBJ-window fixture objects");
    }
    expect(memory->write16(0x05000002, kGreen), "seed window background palette");
    for (unsigned offset = 0; offset < 32; offset += 2) {
      expect(memory->write16(0x06000000U + offset, 0x1111),
             "seed opaque window background tile");
    }
    expect(memory->write16(0x07000000, 0x0800), "seed window object mode");
    expect(memory->write16(0x07000002, 0), "seed window object position");
    expect(memory->write16(0x07000004, 128), "seed window object tile");
    auto result = control(0x9140, 8U << 8U);
    result.winout = 0x0001;  // BG0 outside; no layers inside the OBJ window.
    return result;
  };
  auto c = scene();
  (void)renderer->render_scanline(*memory, c, 0);
  expect(renderer->pixel(0, 0) == kGreen && renderer->pixel(7, 0) == kGreen,
         "transparent OBJ-window texels do not mask their bounding rectangle");
  expect(memory->write16(kWindowTile, 0x0001), "seed one opaque window texel");
  const auto one_pixel = renderer->render_scanline(*memory, c, 0);
  expect(renderer->pixel(0, 0) == 0 && renderer->pixel(1, 0) == kGreen,
         "only nonzero OBJ-window texels select the inside mask");
  expect(one_pixel.window_masked_pixels == 1,
         "one opaque window texel masks exactly one pixel");
  // Palette entry 1 is black: coverage follows the index, not its RGB value.
  expect(memory->read16(0x05000202).value_or(1) == 0,
         "window fixture keeps its opaque palette color black");
  expect(memory->write16(0x07000000, 0x1800), "enable mosaic flag on window object");
  expect(gba::core::PpuSpriteFetcher::read_sprite(*memory, 0)->mosaic_enabled,
         "window fixture exercises the decoded mosaic flag");
  c.mosaic = 0x7700;
  (void)renderer->render_scanline(*memory, c, 0);
  expect(renderer->pixel(0, 0) == 0 && renderer->pixel(1, 0) == kGreen,
         "OBJ-window coverage bypasses horizontal color mosaic");
  (void)renderer->render_scanline(*memory, c, 1);
  expect(renderer->pixel(0, 1) == kGreen,
         "OBJ-window coverage bypasses vertical color mosaic");
  expect(memory->write16(0x05000202, 0x001F), "seed normal mosaic OBJ color");
  expect(memory->write16(0x07000000, 0x1000), "select normal mosaic object");
  c.dispcnt = 0x1140;
  (void)renderer->render_scanline(*memory, c, 0);
  (void)renderer->render_scanline(*memory, c, 1);
  expect(renderer->pixel(0, 0) == 0x001F && renderer->pixel(1, 0) == 0x001F &&
             renderer->pixel(0, 1) == 0x001F,
         "normal OBJ color repeats horizontally and vertically with mosaic bit 12");
  expect(memory->write16(0x07000000, 0x0040), "select Y64 normal object without mosaic");
  (void)renderer->render_scanline(*memory, c, 64);
  (void)renderer->render_scanline(*memory, c, 65);
  expect(renderer->pixel(0, 64) == 0x001F && renderer->pixel(1, 64) == kGreen &&
             renderer->pixel(0, 65) == kGreen,
         "Y bit 6 does not repeat normal OBJ texels");
  expect(memory->write16(0x07000000, 0x0800), "restore window object mode");
  c.dispcnt = 0x9140;
  c.mosaic = 0;
  c.dispcnt &= ~0x1000U;
  (void)renderer->render_scanline(*memory, c, 0);
  expect(renderer->pixel(0, 0) == kGreen,
         "OBJ master enable is required for OBJ-window coverage");
  c.dispcnt = 0x1140;
  (void)renderer->render_scanline(*memory, c, 0);
  expect(renderer->pixel(0, 0) == kGreen,
         "OBJ-window enable is required for OBJ-window coverage");
  c.dispcnt = 0x9140;
  expect(memory->write16(0x07000002, 0x1000), "flip window object horizontally");
  (void)renderer->render_scanline(*memory, c, 0);
  expect(renderer->pixel(0, 0) == kGreen && renderer->pixel(7, 0) == 0,
         "OBJ-window coverage follows flipped texture coordinates");
  expect(memory->write16(0x07000000, 0x2800), "select 8bpp window object");
  expect(memory->write16(0x07000002, 0), "restore window object position");
  (void)renderer->render_scanline(*memory, c, 0);
  expect(renderer->pixel(0, 0) == 0 && renderer->pixel(1, 0) == kGreen,
         "8bpp OBJ-window texels use index-zero transparency");
  expect(memory->write16(0x07000000, 0x0B00), "select affine double-area window");
  expect(memory->write16(0x07000006, 0x0100), "seed window affine PA");
  expect(memory->write16(0x0700000E, 0), "seed window affine PB");
  expect(memory->write16(0x07000016, 0), "seed window affine PC");
  expect(memory->write16(0x0700001E, 0x0100), "seed window affine PD");
  (void)renderer->render_scanline(*memory, c, 4);
  expect(renderer->pixel(3, 4) == kGreen && renderer->pixel(4, 4) == 0 &&
             renderer->pixel(5, 4) == kGreen,
         "affine OBJ-window coverage excludes transparent double-area margins");

  c = scene();
  for (unsigned index = 0; index < 18; ++index) {
    expect(memory->write16(0x07000000U + index * 8U, 0),
           "seed transparent window-budget consumer");
    expect(memory->write16(0x07000002U + index * 8U, 0xC000),
           "select 64px window-budget consumer");
    expect(memory->write16(0x07000004U + index * 8U, 0),
           "select transparent window-budget tile");
  }
  expect(memory->write16(0x07000090, 0x0800), "seed final budget-limited window");
  expect(memory->write16(0x07000092, 0xC080), "position final 64px window");
  expect(memory->write16(0x07000094, 128), "select final window tile set");
  for (unsigned offset = 0; offset < 2048; offset += 2) {
    expect(memory->write16(kWindowTile + offset, 0x1111),
           "seed opaque budget-limited window tiles");
  }
  (void)renderer->render_scanline(*memory, c, 0);
  expect(renderer->pixel(185, 0) == 0 && renderer->pixel(186, 0) == kGreen,
         "OBJ-window texel coverage retains the admitted nominal prefix");
}

}  // namespace

int main() {
  obj_line_budget_test();
  obj_window_texel_test();
  using gba::core::MemoryBus;
  using gba::core::PpuRenderer;

  constexpr std::uint16_t kBg0Enable = 0x0100;
  constexpr std::uint16_t kObjEnable = 0x1000;
  constexpr std::uint16_t kBackdrop = 0x1111;
  constexpr std::uint16_t kBgGreen = 0x03E0;
  constexpr std::uint16_t kBgBlue = 0x001F;
  constexpr std::uint16_t kObjRed = 0x7C00;
  constexpr std::uint16_t kHighBit = 0x8000;

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

  expect(memory.write16(0x05000000, 0x9111), "seed high-bit backdrop color");
  [[maybe_unused]] const gba::core::PpuRenderStats masked_backdrop_stats =
      renderer.render_scanline(memory, control(0, 8U << 8), 0);
  expect(renderer.pixel(0, 0) == kBackdrop, "renderer masks backdrop color bit 15");
  expect(memory.write16(0x05000000, kBackdrop), "restore backdrop color");

  expect(memory.write16(0x06004000, static_cast<std::uint16_t>(1U | (2U << 12))),
         "seed renderer BG map entry");
  expect(write_vram_byte(memory, 0x06000020, 0x05), "seed renderer BG tile pixel");
  expect(memory.write16(0x05000000 + 2U * 32U + 5U * 2U,
                        static_cast<std::uint16_t>(kHighBit | kBgGreen)),
         "seed renderer high-bit BG palette");
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
  expect(memory.write16(0x05000200 + 9U * 2U,
                        static_cast<std::uint16_t>(kHighBit | kObjRed)),
         "seed renderer high-bit OBJ palette");
  const gba::core::PpuRenderStats obj_stats = renderer.render_scanline(
      memory, control(static_cast<std::uint16_t>(kBg0Enable | kObjEnable),
                      static_cast<std::uint16_t>(1U | (8U << 8))),
      0);
  expect(obj_stats.bg_pixels == 1, "renderer still counts BG under OBJ");
  expect(obj_stats.obj_pixels == 1, "renderer counts one opaque OBJ pixel");
  expect(renderer.pixel(0, 0) == kObjRed, "higher-priority OBJ overlays BG");

  expect(memory.write16(0x07000000, 0x0100), "seed affine renderer OBJ attr0");
  expect(memory.write16(0x07000002, 0), "seed affine renderer OBJ attr1");
  expect(memory.write16(0x07000004, 10), "seed affine renderer OBJ attr2");
  expect(memory.write16(0x07000006, 0x0100), "seed affine renderer PA");
  expect(memory.write16(0x0700000E, 0), "seed affine renderer PB");
  expect(memory.write16(0x07000016, 0), "seed affine renderer PC");
  expect(memory.write16(0x0700001E, 0x0100), "seed affine renderer PD");
  const gba::core::PpuRenderStats affine_obj_stats = renderer.render_scanline(
      memory, control(static_cast<std::uint16_t>(kBg0Enable | kObjEnable),
                      static_cast<std::uint16_t>(1U | (8U << 8))),
      0);
  expect(affine_obj_stats.obj_pixels == 1, "renderer counts one affine OBJ pixel");
  expect(renderer.pixel(0, 0) == kObjRed, "affine OBJ overlays BG");

  expect(memory.write16(0x07000000, 0x0300),
         "seed double-size affine renderer OBJ attr0");
  expect(write_vram_byte(memory, 0x06010000 + 10U * 32U + 4U * 4U + 2U, 0x09),
         "seed double-size affine OBJ expanded-area texture pixel");
  const gba::core::PpuRenderStats double_size_affine_obj_stats =
      renderer.render_scanline(
          memory, control(static_cast<std::uint16_t>(kBg0Enable | kObjEnable),
                          static_cast<std::uint16_t>(1U | (8U << 8))),
          8);
  expect(double_size_affine_obj_stats.obj_pixels == 1,
         "renderer counts one double-size affine OBJ expanded-area pixel");
  expect(renderer.pixel(8, 8) == kObjRed,
         "double-size affine OBJ renders beyond nominal sprite bounds");

  expect(memory.write16(0x07000000, 0), "restore regular renderer OBJ attr0");
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

  // Register changes between scanline renders must select the current map;
  // decoded background controls cannot be retained from a previous call.
  auto changed_map = control(kBg0Enable, static_cast<std::uint16_t>(9U << 8));
  (void)renderer.render_scanline(memory, changed_map, 0);
  expect(renderer.pixel(0, 0) == kBgBlue, "updated BG map selects blue pixel");
  changed_map.bg_control[0] = static_cast<std::uint16_t>(8U << 8);
  (void)renderer.render_scanline(memory, changed_map, 0);
  expect(renderer.pixel(0, 0) == kBgGreen, "subsequent render uses changed BG map");

  gba::core::PpuRenderControl window_control =
      control(static_cast<std::uint16_t>(kBg0Enable | 0x2000U),
              static_cast<std::uint16_t>(8U << 8));
  window_control.win0h = static_cast<std::uint16_t>((10U << 8) | 20U);
  window_control.win0v = static_cast<std::uint16_t>((0U << 8) | 1U);
  window_control.winin = 0x0001;
  window_control.winout = 0;
  const gba::core::PpuRenderStats window_stats =
      renderer.render_scanline(memory, window_control, 0);
  expect(window_stats.window_masked_pixels > 0, "WIN0 masks pixels outside window");
  expect(renderer.pixel(0, 0) == kBackdrop, "window-masked BG leaves backdrop");

  gba::core::PpuRenderControl win1_control =
      control(static_cast<std::uint16_t>(kBg0Enable | 0x4000U),
              static_cast<std::uint16_t>(8U << 8));
  win1_control.win1h = static_cast<std::uint16_t>((0U << 8) | 20U);
  win1_control.win1v = static_cast<std::uint16_t>((0U << 8) | 2U);
  win1_control.winin = static_cast<std::uint16_t>(0x0001U << 8U);
  win1_control.winout = 0;
  const gba::core::PpuRenderStats win1_stats =
      renderer.render_scanline(memory, win1_control, 0);
  expect(win1_stats.window_masked_pixels > 0, "WIN1 masks pixels outside window");
  expect(renderer.pixel(25, 0) == kBackdrop, "WIN1 outside region leaves backdrop");
  expect(renderer.pixel(0, 0) == kBgGreen, "WIN1 inside region draws enabled BG");

  // Regression (issue #15): WINOUT=0 with every window disabled in DISPCNT
  // must not mask layers — the window circuit is bypassed entirely
  // (mGBA-aligned). Emerald leaves WINOUT at 0 and renders only its backdrop
  // if WINOUT is applied unconditionally.
  const gba::core::PpuRenderStats windowless_stats =
      renderer.render_scanline(memory, control(kBg0Enable, static_cast<std::uint16_t>(8U << 8)),
                               0);
  expect(windowless_stats.window_masked_pixels == 0,
         "windowing disabled masks no pixels");
  expect(renderer.pixel(0, 0) == kBgGreen,
         "WINOUT ignored when no window is enabled");

  expect(memory.write16(0x06000000, 0xFFFF), "seed mode 3 high-bit bitmap pixel");
  const gba::core::PpuRenderStats mode3_stats =
      renderer.render_scanline(memory, control(3, 0), 0);
  expect(mode3_stats.supported_mode, "mode 3 is supported");
  expect(mode3_stats.bitmap_pixels == PpuRenderer::kScreenWidth,
         "mode 3 renders one bitmap scanline");
  expect(renderer.pixel(0, 0) == 0x7FFF, "mode 3 masks framebuffer color bit 15");

  expect(write_vram_byte(memory, 0x06000000, 7), "seed mode 4 bitmap pixel index");
  expect(memory.write16(0x05000000 + 7U * 2U, 0xA345),
         "seed mode 4 high-bit palette color");
  const gba::core::PpuRenderStats mode4_stats =
      renderer.render_scanline(memory, control(4, 0), 0);
  expect(mode4_stats.bitmap_pixels >= 1, "mode 4 renders indexed bitmap pixels");
  expect(renderer.pixel(0, 0) == 0x2345,
         "mode 4 masks palette-indexed framebuffer color bit 15");

  expect(memory.write16(0x06000000 + 159U * 2U, 0xB456),
         "seed mode 5 high-bit edge pixel");
  const gba::core::PpuRenderStats mode5_stats =
      renderer.render_scanline(memory, control(5, 0), 0);
  expect(mode5_stats.supported_mode, "mode 5 is supported");
  expect(renderer.pixel(159, 0) == 0x3456, "mode 5 masks 160-wide bitmap color bit 15");
  expect(renderer.pixel(160, 0) == 0x0000,
         "mode 5 outside the buffer scans adjacent VRAM instead of backdrop");

  const gba::core::PpuRenderStats forced_blank =
      renderer.render_scanline(memory, control(0x0080, 0), 0);
  expect(forced_blank.forced_blank, "forced blank is reported");
  expect(renderer.pixel(0, 0) == 0x7FFF, "forced blank renders white");

  gba::core::PpuRenderControl fade_control = control(0, 0);
  fade_control.bldcnt = 0x00E0;
  fade_control.bldy = 16;
  const gba::core::PpuRenderStats fade_stats =
      renderer.render_scanline(memory, fade_control, 0);
  expect(fade_stats.blend_pixels == PpuRenderer::kScreenWidth,
         "brightness-decrease applies across scanline");
  expect(renderer.pixel(0, 0) == 0x0000, "BLDCNT=0xC0 darkens the backdrop toward black");

  gba::core::PpuRenderControl brighten_control = control(0, 0);
  brighten_control.bldcnt = 0x00A0;
  brighten_control.bldy = 16;
  [[maybe_unused]] const gba::core::PpuRenderStats brighten_stats =
      renderer.render_scanline(memory, brighten_control, 0);
  expect(renderer.pixel(0, 0) != kBackdrop,
         "BLDCNT=0x80 brightens rather than blends");

  expect(write_vram_byte(memory, 0x06000000U, 0),
         "clear the stray mode-4 tile pixel before the alpha check");
  gba::core::PpuRenderControl alpha_control =
      control(static_cast<std::uint16_t>(kBg0Enable),
              static_cast<std::uint16_t>(8U << 8));
  alpha_control.bldcnt = 0x2041;
  alpha_control.bldalpha = static_cast<std::uint16_t>((8U << 8) | 8U);
  [[maybe_unused]] const gba::core::PpuRenderStats alpha_stats =
      renderer.render_scanline(memory, alpha_control, 0);
  expect(alpha_stats.blend_pixels == 1,
         "alpha blend counts only first-target pixels");
  expect(renderer.pixel(0, 0) == 0x0A68,
         "alpha blend mixes BG0 green with the backdrop at EVA=EVB=8");
  expect(renderer.pixel(1, 0) == kBackdrop,
         "non-first-target pixels skip alpha blending");
  alpha_control.bldcnt = 0x0041;
  [[maybe_unused]] const auto no_alpha_target_stats =
      renderer.render_scanline(memory, alpha_control, 0);
  expect(renderer.pixel(0, 0) == kBgGreen,
         "alpha first target stays opaque when backdrop is not a second target");

  expect(memory.write16(0x07000000, static_cast<std::uint16_t>(0x0400U)),
         "seed semi-transparent OBJ attr0");
  expect(memory.write16(0x07000002, 0), "seed semi-transparent OBJ attr1");
  expect(memory.write16(0x07000004, 10), "seed semi-transparent OBJ attr2");
  expect(memory.write16(0x06010000U + 10U * 32U, 0x9999),
         "seed semi-transparent OBJ first tile word");
  expect(memory.write16(0x06010000U + 10U * 32U + 2U, 0x9999),
         "seed semi-transparent OBJ second tile word");
  gba::core::PpuRenderControl semi_control =
      control(static_cast<std::uint16_t>(kObjEnable), 0);
  semi_control.bldalpha = static_cast<std::uint16_t>(12U | (4U << 8));
  [[maybe_unused]] const auto opaque_semi_stats =
      renderer.render_scanline(memory, semi_control, 0);
  expect(renderer.pixel(0, 0) == kObjRed,
         "semi-transparent OBJ remains opaque without a second target");
  expect(opaque_semi_stats.blend_pixels == 0,
         "semi-transparent OBJ does not blend without a second target");
  semi_control.bldcnt = 0x2000;
  const gba::core::PpuRenderStats semi_stats =
      renderer.render_scanline(memory, semi_control, 0);
  expect(semi_stats.blend_pixels == 8,
         "semi-transparent OBJ forces alpha only where it draws");
  expect(renderer.pixel(0, 0) == 0x6044,
         "semi-transparent OBJ blends against selected backdrop");

  expect(memory.write16(0x07000008, 0), "seed overlapping OBJ attr0");
  expect(memory.write16(0x0700000A, 0), "seed overlapping OBJ attr1");
  expect(memory.write16(0x0700000C, 10), "seed overlapping OBJ attr2");
  semi_control.bldcnt = 0x3000;
  [[maybe_unused]] const auto overlapping_obj_stats =
      renderer.render_scanline(memory, semi_control, 0);
  expect(renderer.pixel(0, 0) == 0x6044,
         "overlapping OBJs blend with the backdrop, not each other");
  expect(memory.write16(0x05000200 + 8U * 2U, kBgBlue),
         "seed alternate OBJ palette color");
  expect(memory.write16(0x06010000U + 11U * 32U, 0x8888),
         "seed alternate OBJ tile");
  expect(memory.write16(0x07000004, static_cast<std::uint16_t>(10U | (3U << 10))),
         "set lower-numbered OBJ to lower BG priority");
  expect(memory.write16(0x0700000C, 11), "select alternate higher-numbered OBJ tile");
  semi_control.bldcnt = 0;
  [[maybe_unused]] const auto obj_priority_stats =
      renderer.render_scanline(memory, semi_control, 0);
  expect(renderer.pixel(0, 0) == kObjRed,
         "lower-numbered OBJ wins overlap regardless of BG priority");
  gba::core::PpuRenderControl obj_behind_bg =
      control(static_cast<std::uint16_t>(kBg0Enable | kObjEnable),
              static_cast<std::uint16_t>(8U << 8));
  [[maybe_unused]] const auto obj_behind_bg_stats =
      renderer.render_scanline(memory, obj_behind_bg, 0);
  expect(renderer.pixel(0, 0) == kBgGreen,
         "OBJ behind BG still masks a higher-numbered OBJ in front of BG");
  expect(memory.write16(0x07000008, 0x0200), "disable overlapping OBJ");

  gba::core::PpuRenderControl affine_bg_control =
      control(static_cast<std::uint16_t>(2U | 0x0400U), 0);
  affine_bg_control.bg_control.at(2) = static_cast<std::uint16_t>(30U << 8);
  affine_bg_control.bg_affine_pa.at(2) = 0x0000;
  affine_bg_control.bg_affine_pb.at(2) = 0x0100;
  affine_bg_control.bg_affine_pc.at(2) = 0x0000;
  affine_bg_control.bg_affine_pd.at(2) = 0x0000;
  for (std::uint32_t entry = 0; entry < 15; ++entry) {
    expect(memory.write8(0x06000000U + 30U * 0x800U + entry, 3),
           "seed affine rotation map entry");
  }
  expect(memory.write16(0x060000C0, 0x0201),
         "seed affine rotation tile word zero");
  expect(memory.write16(0x060000C2, 0x0403),
         "seed affine rotation tile word one");
  expect(memory.write16(0x060000C4, 0x0504),
         "seed affine rotation tile word two");
  expect(memory.write16(0x060000C6, 0x0807),
         "seed affine rotation tile word three");
  expect(memory.write16(0x05000000 + 3U * 2U, kBgBlue), "seed affine palette three");
  expect(memory.write16(0x05000000 + 5U * 2U, kBgGreen), "seed affine palette five");
  const gba::core::PpuRenderStats rotate_stats =
      renderer.render_scanline(memory, affine_bg_control, 5);
  expect(rotate_stats.bg_pixels == PpuRenderer::kScreenWidth,
         "90-degree affine rotation fills the scanline");
  expect(renderer.pixel(0, 5) == kBgGreen,
         "PB=0x0100 rotates screen Y into texture X");
  expect(renderer.pixel(239, 5) == kBgGreen,
         "PA=0 keeps rotated texture coordinates constant along X");
  const gba::core::PpuRenderStats rotate_oob_stats =
      renderer.render_scanline(memory, affine_bg_control, 130);
  expect(rotate_oob_stats.bg_pixels == 0,
         "affine fetch past a non-wrapped map is transparent");
  expect(renderer.pixel(0, 130) == kBackdrop,
         "out-of-bounds affine pixels fall back to backdrop");
  affine_bg_control.bg_control.at(2) =
      static_cast<std::uint16_t>((30U << 8) | 0x2000U);
  [[maybe_unused]] const gba::core::PpuRenderStats rotate_wrap_stats =
      renderer.render_scanline(memory, affine_bg_control, 130);
  expect(renderer.pixel(0, 130) == kBgBlue,
         "BGxCNT bit13 wraps affine coordinates back into the map");

  gba::core::PpuRenderControl mosaic_control =
      control(static_cast<std::uint16_t>(kBg0Enable),
              static_cast<std::uint16_t>((8U << 8) | 0x0040U));
  mosaic_control.mosaic = static_cast<std::uint16_t>(2U | (2U << 4));
  expect(memory.write16(0x06004000, 0), "reseed mosaic BG map entry to tile 0");
  expect(memory.write16(0x06000000, 0x4321),
         "seed mosaic tile first pixel word");
  expect(memory.write16(0x06000002, 0x8765),
         "seed mosaic tile second pixel word");
  expect(memory.write16(0x05000000 + 1U * 2U, kBgBlue), "seed mosaic palette one");
  expect(memory.write16(0x05000000 + 4U * 2U, kBgGreen), "seed mosaic palette four");
  expect(memory.write16(0x05000000 + 7U * 2U, kObjRed), "seed mosaic palette seven");
  [[maybe_unused]] const gba::core::PpuRenderStats mosaic_stats =
      renderer.render_scanline(memory, mosaic_control, 0);
  [[maybe_unused]] const gba::core::PpuRenderStats mosaic_stats_row1 =
      renderer.render_scanline(memory, mosaic_control, 1);
  [[maybe_unused]] const gba::core::PpuRenderStats mosaic_stats_row2 =
      renderer.render_scanline(memory, mosaic_control, 2);
  expect(mosaic_stats.bg_pixels == PpuRenderer::kScreenWidth,
         "mosaic BG stays opaque across the scanline");
  expect(renderer.pixel(0, 0) == kBgBlue && renderer.pixel(1, 0) == kBgBlue &&
             renderer.pixel(2, 0) == kBgBlue,
         "mosaic horizontal block size 3 repeats the block-origin color");
  expect(renderer.pixel(3, 0) == kBgGreen && renderer.pixel(5, 0) == kBgGreen,
         "next mosaic horizontal block samples its own origin");
  expect(renderer.pixel(6, 0) == kObjRed, "third mosaic block advances again");
  expect(renderer.pixel(0, 1) == kBgBlue && renderer.pixel(0, 2) == kBgBlue,
         "mosaic vertical block size 3 collapses rows onto the block origin");

  expect(memory.write16(0x07000000, static_cast<std::uint16_t>(0x0800U)),
         "seed OBJ-window sprite attr0");
  expect(memory.write16(0x07000002, 0), "seed OBJ-window sprite attr1");
  expect(memory.write16(0x07000004, 20), "seed OBJ-window sprite attr2");
  expect(memory.write16(0x06010000 + 20U * 32U, 0x1111) &&
             memory.write16(0x06010000 + 20U * 32U + 2U, 0x1111),
         "seed eight opaque OBJ-window texels");
  expect(memory.write16(0x07000008, 0), "seed normal OBJ attr0 for window test");
  expect(memory.write16(0x0700000A, 10), "seed normal OBJ attr1 for window test");
  expect(memory.write16(0x0700000C, 11), "seed normal OBJ attr2 for window test");
  expect(write_vram_byte(memory, 0x06010000 + 11U * 32U, 0x09),
         "seed normal OBJ window-test tile pixel");
  expect(memory.write16(0x05000200 + 9U * 2U, kObjRed),
         "seed normal OBJ window-test palette");
  gba::core::PpuRenderControl objwin_control =
      control(static_cast<std::uint16_t>(kObjEnable | 0x8000U), 0);
  objwin_control.winout = 0x003F;
  const gba::core::PpuRenderStats objwin_stats =
      renderer.render_scanline(memory, objwin_control, 0);
  expect(objwin_stats.window_masked_pixels >= 8,
         "OBJ window contributes masked pixels to the scanline");
  expect(renderer.pixel(0, 0) == kBackdrop,
         "DISPCNT bit15 lets the window OBJ mask other layers");
  expect(renderer.pixel(10, 0) == kObjRed,
         "normal OBJ draws outside OBJ-window coverage");

  expect(memory.write16(0x07000010, 248), "seed wraparound OBJ attr0");
  expect(memory.write16(0x07000012,
                        static_cast<std::uint16_t>(500U | (3U << 14))),
         "seed wraparound OBJ attr1 at raw x 500");
  expect(memory.write16(0x07000014, 12), "seed wraparound OBJ attr2");
  expect(write_vram_byte(memory, 0x06010000 + 21U * 32U + 2U, 0x09),
         "seed wraparound OBJ wrapped texel");
  const gba::core::PpuRenderStats wrap_stats =
      renderer.render_scanline(
          memory,
          control(static_cast<std::uint16_t>(kObjEnable | 0x0040U), 0), 0);
  expect(wrap_stats.obj_pixels >= 1,
         "sprite at raw x=500/y=248 intersects via modulo extents");
  expect(renderer.pixel(0, 0) == kObjRed,
         "wrapped sprite renders through the left/top screen edges");
  expect(renderer.pixel(52, 0) == kBackdrop,
         "wrapped sprite stops after its screen-width extent");

  const gba::core::PpuRenderStats unsupported =
      renderer.render_scanline(memory, control(6, 8U << 8), 0);
  expect(!unsupported.supported_mode, "renderer reports unsupported display mode");
  expect(renderer.pixel(0, 0) == 0x7FFF,
         "undefined display modes output white instead of backdrop");
  expect(!renderer.render_scanline(memory, control(0, 0), PpuRenderer::kScreenHeight)
               .supported_mode,
         "out-of-range scanline is rejected");

  std::cout << "ppu_renderer_test: PASS\n";
  return 0;
}
