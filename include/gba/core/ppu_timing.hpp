#pragma once

#include "gba/core/interrupt_controller.hpp"

#include <cstdint>

namespace gba::core {

enum class PpuPhase : std::uint8_t {
  visible,
  hblank,
  vblank,
};

class PpuTiming {
 public:
  static constexpr std::uint16_t kVisibleWidth = 240;
  static constexpr std::uint16_t kVisibleLines = 160;
  static constexpr std::uint16_t kVblankLines = 68;
  static constexpr std::uint16_t kTotalLines = kVisibleLines + kVblankLines;
  static constexpr std::uint16_t kVisibleCycles = 960;
  static constexpr std::uint16_t kHblankCycles = 272;
  static constexpr std::uint16_t kCyclesPerLine = kVisibleCycles + kHblankCycles;
  static constexpr std::uint32_t kCyclesPerFrame =
      static_cast<std::uint32_t>(kCyclesPerLine) * kTotalLines;

  PpuTiming();

  void reset();
  void write_dispstat(std::uint16_t value);
  void tick(std::uint32_t cycles, InterruptController& interrupts);

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

  void enter_hblank(InterruptController& interrupts);
  void enter_next_line(InterruptController& interrupts);
};

}  // namespace gba::core
