#include "gba/core/ppu_renderer.hpp"

#include <array>

namespace gba::core {
namespace {

constexpr std::uint16_t kBgEnableBase = 0x0100;
constexpr std::uint16_t kObjEnable = 0x1000;
constexpr std::uint16_t kWin0Enable = 0x2000;
constexpr std::uint16_t kWin1Enable = 0x4000;
constexpr std::uint16_t kObjWindowEnable = 0x8000;
constexpr std::uint16_t kCharacterMapping1D = 0x0040;
constexpr std::uint16_t kWindowLayerMask = 0x003FU;
constexpr std::uint16_t kWindowEffectsBit = 0x0020U;
constexpr std::uint8_t kBackdropLayerBit = 5;
constexpr std::uint16_t kForcedBlank = 0x0080;
constexpr std::uint16_t kDisplayFrameSelect = 0x0010;
constexpr std::uint16_t kModeMask = 0x0007;
constexpr std::uint16_t kBgMosaicEnable = 0x0040U;
constexpr std::uint32_t kBackdropColorAddress = 0x05000000;
constexpr std::uint32_t kBitmapBaseAddress = 0x06000000;
constexpr std::uint32_t kBitmapPageSize = 0xA000;
constexpr std::uint16_t kMode5Width = 160;
constexpr std::uint16_t kMode5Height = 128;
// BLDCNT bits 6-7 (GBATEK): 01 = alpha blend, 10 = brightness increase,
// 11 = brightness decrease.
constexpr std::uint16_t kEffectAlpha = 0x0040;
constexpr std::uint16_t kEffectBrightnessIncrease = 0x0080;
constexpr std::uint16_t kEffectBrightnessDecrease = 0x00C0;
constexpr std::uint16_t kColorMask = 0x7FFF;

[[nodiscard]] constexpr std::uint16_t normalize_color(std::uint16_t color) {
  return static_cast<std::uint16_t>(color & kColorMask);
}

[[nodiscard]] constexpr std::int16_t sprite_screen_width(
    const SpriteAttributes& sprite) {
  return static_cast<std::int16_t>(
      sprite.affine && sprite.double_size ? sprite.width * 2U : sprite.width);
}

[[nodiscard]] constexpr std::int16_t sprite_screen_height(
    const SpriteAttributes& sprite) {
  return static_cast<std::int16_t>(
      sprite.affine && sprite.double_size ? sprite.height * 2U : sprite.height);
}

// GBATEK OBJ geometry wraps modulo 512 horizontally / 256 vertically against
// the double-size-aware screen extents.
[[nodiscard]] bool line_intersects_sprite(const SpriteAttributes& sprite,
                                          std::uint16_t scanline) {
  const std::uint32_t delta =
      static_cast<std::uint32_t>(scanline - static_cast<std::uint16_t>(sprite.y));
  return (delta & 0xFFU) <
         static_cast<std::uint32_t>(sprite_screen_height(sprite));
}

[[nodiscard]] bool column_intersects_sprite(const SpriteAttributes& sprite,
                                            std::uint16_t x) {
  const std::uint32_t delta =
      static_cast<std::uint32_t>(static_cast<std::uint16_t>(x) -
                                 static_cast<std::uint16_t>(sprite.x));
  return (delta & 0x1FFU) <
         static_cast<std::uint32_t>(sprite_screen_width(sprite));
}

[[nodiscard]] bool bg_enabled(std::uint16_t dispcnt, std::uint8_t bg_index) {
  return (dispcnt & static_cast<std::uint16_t>(kBgEnableBase << bg_index)) != 0;
}

[[nodiscard]] bool text_bg_available_in_mode(std::uint8_t mode,
                                             std::uint8_t bg_index) {
  if (mode == 0) {
    return bg_index < 4;
  }
  if (mode == 1) {
    return bg_index < 2;
  }
  return false;
}

[[nodiscard]] bool affine_bg_available_in_mode(std::uint8_t mode,
                                               std::uint8_t bg_index) {
  if (mode == 1) {
    return bg_index == 2;
  }
  if (mode == 2) {
    return bg_index == 2 || bg_index == 3;
  }
  return false;
}

[[nodiscard]] bool inside_window_rect(std::uint16_t horizontal,
                                      std::uint16_t vertical, std::uint16_t x,
                                      std::uint16_t y) {
  const std::uint8_t x1 = static_cast<std::uint8_t>((horizontal >> 8) & 0xFFU);
  const std::uint8_t x2 = static_cast<std::uint8_t>(horizontal & 0xFFU);
  const std::uint8_t y1 = static_cast<std::uint8_t>((vertical >> 8) & 0xFFU);
  const std::uint8_t y2 = static_cast<std::uint8_t>(vertical & 0xFFU);
  const bool x_inside = x1 <= x2 ? x >= x1 && x < x2 : x >= x1 || x < x2;
  const bool y_inside = y1 <= y2 ? y >= y1 && y < y2 : y >= y1 || y < y2;
  return x_inside && y_inside;
}

[[nodiscard]] std::uint16_t window_layer_mask(const PpuRenderControl& control,
                                              std::uint16_t x, std::uint16_t y,
                                              bool obj_window_hit) {
  const bool win0_on = (control.dispcnt & kWin0Enable) != 0;
  const bool win1_on = (control.dispcnt & kWin1Enable) != 0;
  const bool obj_window_on = (control.dispcnt & kObjWindowEnable) != 0;
  if (win0_on && inside_window_rect(control.win0h, control.win0v, x, y)) {
    return static_cast<std::uint16_t>(control.winin & kWindowLayerMask);
  }
  if (win1_on && inside_window_rect(control.win1h, control.win1v, x, y)) {
    return static_cast<std::uint16_t>((control.winin >> 8) & kWindowLayerMask);
  }
  if (obj_window_on && obj_window_hit) {
    // WINOUT high byte controls the OBJ-window region.
    return static_cast<std::uint16_t>((control.winout >> 8) & kWindowLayerMask);
  }
  return static_cast<std::uint16_t>(control.winout & kWindowLayerMask);
}

[[nodiscard]] std::uint16_t mosaic_block(std::uint16_t coord, std::uint16_t size) {
  return size <= 1 ? coord : static_cast<std::uint16_t>((coord / size) * size);
}

[[nodiscard]] std::uint16_t alpha_blend_channel(std::uint16_t first,
                                                std::uint16_t second,
                                                std::uint16_t eva,
                                                std::uint16_t evb) {
  const std::uint32_t blended = (first * eva + second * evb) >> 4;
  return static_cast<std::uint16_t>(blended > 31U ? 31U : blended);
}

[[nodiscard]] std::uint16_t alpha_blend(std::uint16_t top, std::uint16_t bottom,
                                        std::uint16_t eva, std::uint16_t evb) {
  top = normalize_color(top);
  bottom = normalize_color(bottom);
  std::uint16_t result = 0;
  for (std::uint16_t shift = 0; shift <= 10; shift += 5) {
    const std::uint16_t first = static_cast<std::uint16_t>((top >> shift) & 0x1FU);
    const std::uint16_t second =
        static_cast<std::uint16_t>((bottom >> shift) & 0x1FU);
    result = static_cast<std::uint16_t>(
        result |
        (alpha_blend_channel(first, second, eva, evb) << shift));
  }
  return result;
}

[[nodiscard]] std::uint16_t brightness_blend(std::uint16_t color,
                                             std::uint16_t amount,
                                             bool increase) {
  color = normalize_color(color);
  std::uint16_t red = static_cast<std::uint16_t>(color & 0x1FU);
  std::uint16_t green = static_cast<std::uint16_t>((color >> 5) & 0x1FU);
  std::uint16_t blue = static_cast<std::uint16_t>((color >> 10) & 0x1FU);
  const auto apply = [&](std::uint16_t channel) -> std::uint16_t {
    if (increase) {
      return static_cast<std::uint16_t>(channel + (((31U - channel) * amount) >> 4));
    }
    return static_cast<std::uint16_t>(channel - ((channel * amount) >> 4));
  };
  red = apply(red);
  green = apply(green);
  blue = apply(blue);
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
  if (mode == 5) {
    // Hardware scans VRAM linearly past the 160x128 mode-5 buffer, so pixels
    // outside the buffer show whatever adjacent VRAM holds; reproduce that by
    // reading the equivalent unclamped offset instead of falling back.
    return memory.read16(kBitmapBaseAddress + page +
                         (static_cast<std::uint32_t>(y) * kMode5Width + x) * 2U);
  }
  return std::nullopt;
}

struct PixelCandidate {
  std::uint16_t color;
  std::uint8_t layer_bit;
  std::uint8_t priority;
};

struct AffineParams {
  bool present = false;
  std::int32_t pa = 0;
  std::int32_t pb = 0;
  std::int32_t pc = 0;
  std::int32_t pd = 0;
  std::int32_t reference_x = 0;
  std::int32_t reference_y = 0;
};

[[nodiscard]] std::int32_t sign_extend_reference(std::uint32_t raw28) {
  // BGxX/Y are 28-bit signed 8.8 accumulators.
  return static_cast<std::int32_t>(raw28 << 4) >> 4;
}

[[nodiscard]] std::int32_t sign_extend_affine_param(std::uint16_t raw) {
  return static_cast<std::int16_t>(raw);
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
  if (!supported_mode) {
    // Modes 6-7 do not exist on hardware; real units output white rather than
    // repeating the backdrop. (Deviation marker: exact open-circuit behavior
    // of the display matrix on illegal modes is UNVERIFIED-vs-hardware.)
    for (std::uint16_t x = 0; x < kScreenWidth; ++x) {
      framebuffer_.at(static_cast<std::uint32_t>(scanline) * kScreenWidth + x) = 0x7FFF;
    }
    return {scanline, 0, 0, false};
  }

  const bool obj_enabled = (control.dispcnt & kObjEnable) != 0;
  const bool obj_window_enabled = (control.dispcnt & kObjWindowEnable) != 0;
  const bool character_mapping_1d = (control.dispcnt & kCharacterMapping1D) != 0;
  const std::uint16_t backdrop =
      normalize_color(memory.read16(kBackdropColorAddress).value_or(0));
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

  std::array<SpriteAttributes, PpuSpriteFetcher::kSpriteCount> active_sprites{};
  std::size_t active_sprite_count = 0;
  if (obj_enabled || obj_window_enabled) {
    for (std::uint16_t index = 0; index < PpuSpriteFetcher::kSpriteCount; ++index) {
      const std::optional<SpriteAttributes> sprite =
          PpuSpriteFetcher::read_sprite(memory, index);
      if (!sprite.has_value() || sprite->disabled ||
          !line_intersects_sprite(sprite.value(), scanline)) {
        continue;
      }
      active_sprites.at(active_sprite_count++) = sprite.value();
    }
  }

  const std::uint16_t bg_mosaic_w = static_cast<std::uint16_t>((control.mosaic & 0xFU) + 1U);
  const std::uint16_t bg_mosaic_h =
      static_cast<std::uint16_t>(((control.mosaic >> 4) & 0xFU) + 1U);
  const std::uint16_t obj_mosaic_w =
      static_cast<std::uint16_t>(((control.mosaic >> 8) & 0xFU) + 1U);
  const std::uint16_t obj_mosaic_h =
      static_cast<std::uint16_t>(((control.mosaic >> 12) & 0xFU) + 1U);

  std::array<AffineParams, 4> affine_params{};
  if (mode == 1 || mode == 2) {
    for (std::uint8_t bg_index = 2; bg_index < 4; ++bg_index) {
      if (mode == 1 && bg_index != 2) {
        continue;
      }
      AffineParams params;
      params.present = true;
      params.pa = sign_extend_affine_param(control.bg_affine_pa.at(bg_index));
      params.pb = sign_extend_affine_param(control.bg_affine_pb.at(bg_index));
      params.pc = sign_extend_affine_param(control.bg_affine_pc.at(bg_index));
      params.pd = sign_extend_affine_param(control.bg_affine_pd.at(bg_index));
      params.reference_x = sign_extend_reference(control.bg_reference_x.at(bg_index));
      params.reference_y = sign_extend_reference(control.bg_reference_y.at(bg_index));
      affine_params.at(bg_index) = params;
    }
  }

  const auto obj_window_hit_at = [&](std::uint16_t x) {
    if (!obj_window_enabled) {
      return false;
    }
    for (std::size_t index = 0; index < active_sprite_count; ++index) {
      const SpriteAttributes& sprite = active_sprites.at(index);
      if (sprite.obj_window && column_intersects_sprite(sprite, x)) {
        return true;
      }
    }
    return false;
  };

  for (std::uint16_t x = 0; x < kScreenWidth; ++x) {
    const bool obj_window_hit = obj_window_hit_at(x);
    const std::uint16_t layer_mask =
        window_layer_mask(control, x, scanline, obj_window_hit);
    if (layer_mask == 0) {
      ++window_masked_pixels;
    }

    PixelCandidate best{backdrop, kBackdropLayerBit, 4};
    PixelCandidate second{backdrop, kBackdropLayerBit, 4};
    bool winner_is_semi_transparent = false;
    const auto consider = [&](PixelCandidate candidate) {
      if (candidate.priority <= best.priority) {
        second = best;
        best = candidate;
      } else if (candidate.priority <= second.priority) {
        second = candidate;
      }
    };

    if (layer_mask != 0) {
      if (text_bg_available_in_mode(mode, 0)) {
        for (std::uint8_t bg_index = 0; bg_index < 4; ++bg_index) {
          if (!text_bg_available_in_mode(mode, bg_index) ||
              !bg_enabled(control.dispcnt, bg_index) ||
              (layer_mask & static_cast<std::uint16_t>(1U << bg_index)) == 0) {
            continue;
          }
          const BgControl bg = PpuBackgroundFetcher::decode_control(
              control.bg_control.at(bg_index));
          std::uint16_t sample_x = x;
          std::uint16_t sample_y = scanline;
          if ((bg.raw & kBgMosaicEnable) != 0) {
            sample_x = mosaic_block(sample_x, bg_mosaic_w);
            sample_y = mosaic_block(sample_y, bg_mosaic_h);
          }
          const std::optional<BgPixel> bg_pixel = PpuBackgroundFetcher::fetch_text_pixel(
              memory, bg,
              static_cast<std::uint16_t>(sample_x + control.bg_scroll_x.at(bg_index)),
              static_cast<std::uint16_t>(sample_y + control.bg_scroll_y.at(bg_index)));
          if (!bg_pixel.has_value() || bg_pixel->transparent) {
            continue;
          }
          const PixelCandidate candidate{
              normalize_color(bg_pixel->color), bg_index, bg.priority};
          const std::uint16_t prior_best_priority = best.priority;
          consider(candidate);
          if (candidate.priority <= prior_best_priority) {
            ++bg_pixels;
          }
        }
      } else {
        for (std::uint8_t bg_index = 2; bg_index < 4; ++bg_index) {
          const AffineParams& params = affine_params.at(bg_index);
          if (!params.present ||
              !affine_bg_available_in_mode(mode, bg_index) ||
              !bg_enabled(control.dispcnt, bg_index) ||
              (layer_mask & static_cast<std::uint16_t>(1U << bg_index)) == 0) {
            continue;
          }
          const BgControl bg = PpuBackgroundFetcher::decode_control(
              control.bg_control.at(bg_index));
          // GBATEK rot/scale model (8.8 fixed point): reference point plus
          // matrix rows scaled by screen position, shifted back to integer.
          std::int64_t dx = x;
          std::int64_t dy = scanline;
          if ((bg.raw & kBgMosaicEnable) != 0) {
            dx = mosaic_block(static_cast<std::uint16_t>(dx), bg_mosaic_w);
            dy = mosaic_block(static_cast<std::uint16_t>(dy), bg_mosaic_h);
          }
          const std::int64_t tex_x_raw = static_cast<std::int64_t>(params.reference_x) +
                                         params.pa * dx + params.pb * dy;
          const std::int64_t tex_y_raw = static_cast<std::int64_t>(params.reference_y) +
                                         params.pc * dx + params.pd * dy;
          const std::optional<AffinePixel> affine_pixel =
              PpuBackgroundFetcher::fetch_affine_pixel(
                  memory, bg, static_cast<std::int32_t>(tex_x_raw >> 8),
                  static_cast<std::int32_t>(tex_y_raw >> 8));
          if (!affine_pixel.has_value() || affine_pixel->transparent) {
            continue;
          }
          const PixelCandidate candidate{
              normalize_color(affine_pixel->color), bg_index, bg.priority};
          const std::uint16_t prior_best_priority = best.priority;
          consider(candidate);
          if (candidate.priority <= prior_best_priority) {
            ++bg_pixels;
          }
        }
      }

      if (mode >= 3 &&
          (layer_mask & static_cast<std::uint16_t>(1U << 2)) != 0) {
        const std::optional<std::uint16_t> bitmap =
            fetch_bitmap_pixel(memory, control, mode, x, scanline);
        if (bitmap.has_value()) {
          ++bitmap_pixels;
          consider(PixelCandidate{normalize_color(bitmap.value()), 2, 0});
        }
      }

      if (obj_enabled &&
          (layer_mask & static_cast<std::uint16_t>(1U << 4)) != 0) {
        for (std::size_t index = active_sprite_count; index-- > 0;) {
          const SpriteAttributes& sprite = active_sprites.at(index);
          if (sprite.obj_window || sprite.priority > best.priority ||
              !column_intersects_sprite(sprite, x)) {
            continue;
          }
          std::uint8_t local_x = static_cast<std::uint8_t>(
              (static_cast<std::uint32_t>(x) -
               static_cast<std::uint32_t>(static_cast<std::uint16_t>(sprite.x))) &
              0x1FFU);
          std::uint8_t local_y = static_cast<std::uint8_t>(
              (static_cast<std::uint32_t>(scanline) -
               static_cast<std::uint32_t>(static_cast<std::uint16_t>(sprite.y))) &
              0xFFU);
          if (sprite.mosaic_enabled) {
            local_x = static_cast<std::uint8_t>(mosaic_block(local_x, obj_mosaic_w));
            local_y = static_cast<std::uint8_t>(mosaic_block(local_y, obj_mosaic_h));
          }
          const std::optional<SpritePixel> obj_pixel = PpuSpriteFetcher::fetch_sprite_pixel(
              memory, sprite, local_x, local_y, character_mapping_1d);
          if (!obj_pixel.has_value() || obj_pixel->transparent) {
            continue;
          }
          const PixelCandidate candidate{
              normalize_color(obj_pixel->color), 4, sprite.priority};
          const std::uint16_t prior_best_priority = best.priority;
          consider(candidate);
          if (candidate.priority <= prior_best_priority) {
            ++obj_pixels;
            winner_is_semi_transparent = sprite.semi_transparent;
          }
        }
      }
    }

    std::uint16_t color = normalize_color(best.color);
    bool blended = false;
    const std::uint16_t effect = static_cast<std::uint16_t>(control.bldcnt & 0x00C0U);
    const bool effects_allowed = (layer_mask & kWindowEffectsBit) != 0;
    const bool first_target =
        (control.bldcnt & static_cast<std::uint16_t>(1U << best.layer_bit)) != 0;
    const bool forced_obj_alpha = best.layer_bit == 4 && winner_is_semi_transparent;
    const bool do_alpha =
        effects_allowed && (forced_obj_alpha ||
                            (effect == kEffectAlpha && first_target));
    const bool do_brightness =
        effects_allowed && !do_alpha &&
        (effect == kEffectBrightnessIncrease ||
         effect == kEffectBrightnessDecrease) &&
        first_target;
    if (do_alpha) {
      const std::uint16_t eva = static_cast<std::uint16_t>(control.bldalpha & 0x1FU);
      const std::uint16_t evb =
          static_cast<std::uint16_t>((control.bldalpha >> 8) & 0x1FU);
      color = alpha_blend(best.color, second.color, eva, evb);
      blended = true;
    } else if (do_brightness) {
      const std::uint16_t amount = static_cast<std::uint16_t>(control.bldy & 0x1FU);
      if (amount != 0) {
        color = brightness_blend(color, amount, effect == kEffectBrightnessIncrease);
        blended = true;
      }
    }
    if (blended) {
      ++blend_pixels;
    }
    framebuffer_.at(static_cast<std::uint32_t>(scanline) * kScreenWidth + x) =
        normalize_color(color);
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
