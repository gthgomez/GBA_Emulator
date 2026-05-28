#pragma once

#include "gba/core/interrupt_controller.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace gba::core {

enum class PpuPhase : std::uint8_t {
  visible,
  hblank,
  vblank,
};

struct PpuTickEvents {
  std::uint16_t hblank_entries = 0;
  std::uint16_t vblank_entries = 0;
  std::uint16_t vcount_matches = 0;
};

class PpuTiming {
 public:
  static constexpr std::uint16_t kVisibleWidth = 240;
  static constexpr std::uint16_t kVisibleLines = 160;
  static constexpr std::uint16_t kVblankLines = 68;
  static constexpr std::uint16_t kTotalLines = kVisibleLines + kVblankLines;
  static constexpr std::uint16_t kVisibleCycles = 960;
  static constexpr std::uint16_t kHblankFlagCycles = 1006;
  static constexpr std::uint16_t kHblankCycles = 272;
  static constexpr std::uint16_t kCyclesPerLine = kVisibleCycles + kHblankCycles;
  static constexpr std::uint32_t kCyclesPerFrame =
      static_cast<std::uint32_t>(kCyclesPerLine) * kTotalLines;

  PpuTiming();

  void reset();
  void write_dispstat(std::uint16_t value);
  [[nodiscard]] std::optional<std::uint16_t> read_lcd_control(
      std::uint32_t address) const;
  [[nodiscard]] bool write_lcd_control(std::uint32_t address, std::uint16_t value);
  PpuTickEvents tick(std::uint32_t cycles, InterruptController& interrupts);

  [[nodiscard]] std::uint16_t dispstat() const;
  [[nodiscard]] std::uint16_t vcount() const;
  [[nodiscard]] std::uint16_t line_cycle() const;
  [[nodiscard]] std::uint32_t frame_cycle() const;
  [[nodiscard]] std::uint8_t vcount_setting() const;
  [[nodiscard]] bool vblank() const;
  [[nodiscard]] bool hblank() const;
  [[nodiscard]] bool vcount_match() const;
  [[nodiscard]] bool vblank_irq_enabled() const;
  [[nodiscard]] bool hblank_irq_enabled() const;
  [[nodiscard]] bool vcount_irq_enabled() const;
  [[nodiscard]] PpuPhase phase() const;
  [[nodiscard]] std::uint64_t state_hash() const;

 private:
  std::uint16_t line_;
  std::uint16_t line_cycle_;
  std::uint16_t dispstat_control_;
  std::array<std::uint16_t, 0x2BU> lcd_control_;

  void enter_hblank(InterruptController& interrupts, PpuTickEvents& events);
  void enter_next_line(InterruptController& interrupts, PpuTickEvents& events);
};

}  // namespace gba::core
