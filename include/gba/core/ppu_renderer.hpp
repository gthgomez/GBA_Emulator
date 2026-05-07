#pragma once

#include "gba/core/memory_bus.hpp"
#include "gba/core/ppu_background.hpp"
#include "gba/core/ppu_sprites.hpp"

#include <array>
#include <cstdint>

namespace gba::core {

struct PpuRenderControl {
  std::uint16_t dispcnt;
  std::array<std::uint16_t, 4> bg_control;
  std::array<std::uint16_t, 4> bg_scroll_x;
  std::array<std::uint16_t, 4> bg_scroll_y;
};

struct PpuRenderStats {
  std::uint16_t scanline;
  std::uint16_t bg_pixels;
  std::uint16_t obj_pixels;
  bool supported_mode;
};

class PpuRenderer {
 public:
  static constexpr std::uint16_t kScreenWidth = 240;
  static constexpr std::uint16_t kScreenHeight = 160;
  static constexpr std::uint32_t kFramebufferPixels =
      static_cast<std::uint32_t>(kScreenWidth) * kScreenHeight;
  using Framebuffer = std::array<std::uint16_t, kFramebufferPixels>;

  PpuRenderer();

  void clear(std::uint16_t color = 0);
  [[nodiscard]] PpuRenderStats render_scanline(const MemoryBus& memory,
                                               const PpuRenderControl& control,
                                               std::uint16_t scanline);
  [[nodiscard]] std::uint16_t pixel(std::uint16_t x, std::uint16_t y) const;
  [[nodiscard]] const Framebuffer& framebuffer() const;

 private:
  Framebuffer framebuffer_;
};

}  // namespace gba::core
