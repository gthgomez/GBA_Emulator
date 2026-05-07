#include "gba/core/ppu_renderer.hpp"

namespace gba::core {
namespace {

constexpr std::uint16_t kBg0Enable = 0x0100;
constexpr std::uint16_t kObjEnable = 0x1000;
constexpr std::uint16_t kModeMask = 0x0007;
constexpr std::uint32_t kBackdropColorAddress = 0x05000000;

[[nodiscard]] bool line_intersects_sprite(const SpriteAttributes& sprite,
                                          std::uint16_t scanline) {
  const std::int16_t line = static_cast<std::int16_t>(scanline);
  return line >= sprite.y && line < static_cast<std::int16_t>(sprite.y + sprite.height);
}

[[nodiscard]] bool x_intersects_sprite(const SpriteAttributes& sprite, std::uint16_t x) {
  return static_cast<std::int16_t>(x) >= sprite.x &&
         static_cast<std::int16_t>(x) < static_cast<std::int16_t>(sprite.x + sprite.width);
}

}  // namespace

PpuRenderer::PpuRenderer() {
  clear();
}

void PpuRenderer::clear(std::uint16_t color) {
  framebuffer_.fill(color);
}

PpuRenderStats PpuRenderer::render_scanline(const MemoryBus& memory,
                                            const PpuRenderControl& control,
                                            std::uint16_t scanline) {
  if (scanline >= kScreenHeight) {
    return {scanline, 0, 0, false};
  }

  const std::uint8_t mode = static_cast<std::uint8_t>(control.dispcnt & kModeMask);
  const bool supported_mode = mode <= 2;
  const bool bg0_enabled = supported_mode && (control.dispcnt & kBg0Enable) != 0;
  const bool obj_enabled = supported_mode && (control.dispcnt & kObjEnable) != 0;
  const std::uint16_t backdrop = memory.read16(kBackdropColorAddress).value_or(0);
  const BgControl bg0 = PpuBackgroundFetcher::decode_control(control.bg_control.at(0));
  std::uint16_t bg_pixels = 0;
  std::uint16_t obj_pixels = 0;

  for (std::uint16_t x = 0; x < kScreenWidth; ++x) {
    std::uint16_t color = backdrop;
    std::uint8_t bg_priority = 4;

    if (bg0_enabled) {
      const std::optional<BgPixel> bg_pixel = PpuBackgroundFetcher::fetch_text_pixel(
          memory, bg0, static_cast<std::uint16_t>(x + control.bg_scroll_x.at(0)),
          static_cast<std::uint16_t>(scanline + control.bg_scroll_y.at(0)));
      if (bg_pixel.has_value() && !bg_pixel->transparent) {
        color = bg_pixel->color;
        bg_priority = bg0.priority;
        ++bg_pixels;
      }
    }

    if (obj_enabled) {
      std::uint8_t best_obj_priority = 4;
      std::uint16_t best_obj_color = color;
      bool has_obj_pixel = false;
      for (std::int16_t index = PpuSpriteFetcher::kSpriteCount - 1; index >= 0; --index) {
        const std::optional<SpriteAttributes> sprite =
            PpuSpriteFetcher::read_sprite(memory, static_cast<std::uint16_t>(index));
        if (!sprite.has_value() || sprite->disabled ||
            !line_intersects_sprite(sprite.value(), scanline) ||
            !x_intersects_sprite(sprite.value(), x) || sprite->priority > bg_priority ||
            sprite->priority > best_obj_priority) {
          continue;
        }

        const std::optional<SpritePixel> obj_pixel = PpuSpriteFetcher::fetch_sprite_pixel(
            memory, sprite.value(), static_cast<std::uint8_t>(x - sprite->x),
            static_cast<std::uint8_t>(scanline - sprite->y));
        if (!obj_pixel.has_value() || obj_pixel->transparent) {
          continue;
        }

        best_obj_priority = sprite->priority;
        best_obj_color = obj_pixel->color;
        has_obj_pixel = true;
      }
      if (has_obj_pixel) {
        color = best_obj_color;
        ++obj_pixels;
      }
    }

    framebuffer_.at(static_cast<std::uint32_t>(scanline) * kScreenWidth + x) = color;
  }

  return {scanline, bg_pixels, obj_pixels, supported_mode};
}

std::uint16_t PpuRenderer::pixel(std::uint16_t x, std::uint16_t y) const {
  if (x >= kScreenWidth || y >= kScreenHeight) {
    return 0;
  }
  return framebuffer_.at(static_cast<std::uint32_t>(y) * kScreenWidth + x);
}

const PpuRenderer::Framebuffer& PpuRenderer::framebuffer() const {
  return framebuffer_;
}

}  // namespace gba::core
