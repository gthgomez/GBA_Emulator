#include "gba/core/ppu_renderer.hpp"

namespace gba::core {
namespace {

constexpr std::uint16_t kBgEnableBase = 0x0100;
constexpr std::uint16_t kObjEnable = 0x1000;
constexpr std::uint16_t kWin0Enable = 0x2000;
constexpr std::uint16_t kForcedBlank = 0x0080;
constexpr std::uint16_t kDisplayFrameSelect = 0x0010;
constexpr std::uint16_t kModeMask = 0x0007;
constexpr std::uint32_t kBackdropColorAddress = 0x05000000;
constexpr std::uint32_t kBitmapBaseAddress = 0x06000000;
constexpr std::uint32_t kBitmapPageSize = 0xA000;
constexpr std::uint16_t kMode5Width = 160;
constexpr std::uint16_t kMode5Height = 128;
constexpr std::uint16_t kBrightnessEffectMask = 0x00C0;
constexpr std::uint16_t kBrightnessIncrease = 0x0040;
constexpr std::uint16_t kBrightnessDecrease = 0x0080;

[[nodiscard]] bool line_intersects_sprite(const SpriteAttributes& sprite,
                                          std::uint16_t scanline) {
  const std::int16_t line = static_cast<std::int16_t>(scanline);
  return line >= sprite.y && line < static_cast<std::int16_t>(sprite.y + sprite.height);
}

[[nodiscard]] bool x_intersects_sprite(const SpriteAttributes& sprite, std::uint16_t x) {
  return static_cast<std::int16_t>(x) >= sprite.x &&
         static_cast<std::int16_t>(x) < static_cast<std::int16_t>(sprite.x + sprite.width);
}

[[nodiscard]] bool bg_enabled(std::uint16_t dispcnt, std::uint8_t bg_index) {
  return (dispcnt & static_cast<std::uint16_t>(kBgEnableBase << bg_index)) != 0;
}

[[nodiscard]] bool bg_available_in_mode(std::uint8_t mode, std::uint8_t bg_index) {
  if (mode == 0) {
    return bg_index < 4;
  }
  if (mode == 1) {
    return bg_index < 3;
  }
  if (mode == 2) {
    return bg_index >= 2 && bg_index < 4;
  }
  return false;
}

[[nodiscard]] bool inside_win0(const PpuRenderControl& control, std::uint16_t x,
                               std::uint16_t y) {
  if ((control.dispcnt & kWin0Enable) == 0) {
    return true;
  }
  const std::uint8_t x1 = static_cast<std::uint8_t>((control.win0h >> 8) & 0xFFU);
  const std::uint8_t x2 = static_cast<std::uint8_t>(control.win0h & 0xFFU);
  const std::uint8_t y1 = static_cast<std::uint8_t>((control.win0v >> 8) & 0xFFU);
  const std::uint8_t y2 = static_cast<std::uint8_t>(control.win0v & 0xFFU);
  const bool x_inside = x1 <= x2 ? x >= x1 && x < x2 : x >= x1 || x < x2;
  const bool y_inside = y1 <= y2 ? y >= y1 && y < y2 : y >= y1 || y < y2;
  return x_inside && y_inside;
}

[[nodiscard]] bool layer_allowed_by_window(const PpuRenderControl& control,
                                           std::uint8_t layer_bit) {
  if ((control.dispcnt & kWin0Enable) == 0) {
    return true;
  }
  return (control.winin & static_cast<std::uint16_t>(1U << layer_bit)) != 0;
}

[[nodiscard]] std::uint16_t brightness_blend(std::uint16_t color,
                                             const PpuRenderControl& control,
                                             bool& blended) {
  blended = false;
  const std::uint16_t effect =
      static_cast<std::uint16_t>(control.bldcnt & kBrightnessEffectMask);
  if (effect != kBrightnessIncrease && effect != kBrightnessDecrease) {
    return color;
  }

  const std::uint16_t amount = static_cast<std::uint16_t>(control.bldy & 0x1FU);
  if (amount == 0) {
    return color;
  }

  std::uint16_t red = static_cast<std::uint16_t>(color & 0x1FU);
  std::uint16_t green = static_cast<std::uint16_t>((color >> 5) & 0x1FU);
  std::uint16_t blue = static_cast<std::uint16_t>((color >> 10) & 0x1FU);
  const auto apply = [&](std::uint16_t channel) -> std::uint16_t {
    if (effect == kBrightnessIncrease) {
      return static_cast<std::uint16_t>(channel + (((31U - channel) * amount) >> 4));
    }
    return static_cast<std::uint16_t>(channel - ((channel * amount) >> 4));
  };
  red = apply(red);
  green = apply(green);
  blue = apply(blue);
  blended = true;
  return static_cast<std::uint16_t>(red | (green << 5) | (blue << 10));
}

[[nodiscard]] std::optional<std::uint16_t> fetch_bitmap_pixel(
    const MemoryBus& memory, const PpuRenderControl& control, std::uint8_t mode,
    std::uint16_t x, std::uint16_t y) {
  const std::uint32_t page =
      (control.dispcnt & kDisplayFrameSelect) != 0 ? kBitmapPageSize : 0;
  if (mode == 3) {
    return memory.read16(kBitmapBaseAddress + (static_cast<std::uint32_t>(y) * 240U + x) * 2U);
  }
  if (mode == 4) {
    const std::optional<std::uint8_t> color_index =
        memory.read8(kBitmapBaseAddress + page + static_cast<std::uint32_t>(y) * 240U + x);
    if (!color_index.has_value() || color_index.value() == 0) {
      return std::nullopt;
    }
    return memory.read16(kBackdropColorAddress + static_cast<std::uint32_t>(color_index.value()) * 2U);
  }
  if (mode == 5 && x < kMode5Width && y < kMode5Height) {
    return memory.read16(kBitmapBaseAddress + page + (static_cast<std::uint32_t>(y) * kMode5Width + x) * 2U);
  }
  return std::nullopt;
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
  const bool supported_mode = mode <= 5;
  const bool obj_enabled = supported_mode && (control.dispcnt & kObjEnable) != 0;
  const std::uint16_t backdrop = memory.read16(kBackdropColorAddress).value_or(0);
  std::uint16_t bg_pixels = 0;
  std::uint16_t obj_pixels = 0;
  std::uint16_t bitmap_pixels = 0;
  std::uint16_t window_masked_pixels = 0;
  std::uint16_t blend_pixels = 0;

  if ((control.dispcnt & kForcedBlank) != 0) {
    for (std::uint16_t x = 0; x < kScreenWidth; ++x) {
      framebuffer_.at(static_cast<std::uint32_t>(scanline) * kScreenWidth + x) = 0x7FFF;
    }
    return {scanline, 0, 0, supported_mode, true};
  }

  for (std::uint16_t x = 0; x < kScreenWidth; ++x) {
    std::uint16_t color = backdrop;
    std::uint8_t bg_priority = 4;
    const bool inside_window = inside_win0(control, x, scanline);

    if (!inside_window) {
      ++window_masked_pixels;
    }

    if (mode <= 2 && inside_window) {
      for (std::uint8_t bg_index = 0; bg_index < control.bg_control.size(); ++bg_index) {
        if (!bg_enabled(control.dispcnt, bg_index) ||
            !bg_available_in_mode(mode, bg_index) ||
            !layer_allowed_by_window(control, bg_index)) {
          continue;
        }
        const BgControl bg = PpuBackgroundFetcher::decode_control(
            control.bg_control.at(bg_index));
        const std::uint16_t mosaic_x =
            (bg.raw & 0x0040U) != 0 ? static_cast<std::uint16_t>(x & ~0x1U) : x;
        const std::uint16_t mosaic_y =
            (bg.raw & 0x0040U) != 0 ? static_cast<std::uint16_t>(scanline & ~0x1U)
                                    : scanline;
        const std::optional<BgPixel> bg_pixel = PpuBackgroundFetcher::fetch_text_pixel(
            memory, bg,
            static_cast<std::uint16_t>(mosaic_x + control.bg_scroll_x.at(bg_index)),
            static_cast<std::uint16_t>(mosaic_y + control.bg_scroll_y.at(bg_index)));
        if (bg_pixel.has_value() && !bg_pixel->transparent &&
            bg.priority <= bg_priority) {
          color = bg_pixel->color;
          bg_priority = bg.priority;
          ++bg_pixels;
        }
      }
    } else if (mode >= 3 && inside_window && layer_allowed_by_window(control, 2)) {
      const std::optional<std::uint16_t> bitmap =
          fetch_bitmap_pixel(memory, control, mode, x, scanline);
      if (bitmap.has_value()) {
        color = bitmap.value();
        bg_priority = 0;
        ++bitmap_pixels;
      }
    }

    if (obj_enabled && inside_window && layer_allowed_by_window(control, 4)) {
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

    bool blended = false;
    color = brightness_blend(color, control, blended);
    if (blended) {
      ++blend_pixels;
    }
    framebuffer_.at(static_cast<std::uint32_t>(scanline) * kScreenWidth + x) = color;
  }

  return {scanline, bg_pixels, obj_pixels, supported_mode, false, bitmap_pixels,
          window_masked_pixels, blend_pixels};
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
