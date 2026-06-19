#include "gba/core/ppu_timing.hpp"

#include "gba/core/state_hash.hpp"

#include <algorithm>
#include <cstddef>

namespace gba::core {
namespace {

constexpr std::uint16_t kVblankIrqEnable = 0x0008;
constexpr std::uint16_t kHblankIrqEnable = 0x0010;
constexpr std::uint16_t kVcountIrqEnable = 0x0020;
constexpr std::uint16_t kWritableDispstatMask =
    kVblankIrqEnable | kHblankIrqEnable | kVcountIrqEnable | 0xFF00;
constexpr std::uint16_t kVblankFlag = 0x0001;
constexpr std::uint16_t kHblankFlag = 0x0002;
constexpr std::uint16_t kVcountFlag = 0x0004;
constexpr std::uint32_t kLcdControlStart = 0x04000000;
constexpr std::uint32_t kLcdControlEnd = 0x04000054;
constexpr std::uint32_t kDispstatAddress = 0x04000004;
constexpr std::uint32_t kVcountAddress = 0x04000006;

[[nodiscard]] constexpr bool is_lcd_control_address(std::uint32_t address) {
  if ((address & 0x1U) != 0 || address < kLcdControlStart || address > kLcdControlEnd) {
    return false;
  }
  if (address == kDispstatAddress || address == kVcountAddress) {
    return false;
  }
  if (address <= 0x0400001EU) {
    return true;
  }
  if (address >= 0x04000020U && address <= 0x0400003EU) {
    return true;
  }
  if (address >= 0x04000040U && address <= kLcdControlEnd) {
    return true;
  }
  return false;
}

[[nodiscard]] constexpr std::size_t lcd_control_index(std::uint32_t address) {
  return static_cast<std::size_t>((address - kLcdControlStart) / 2U);
}

[[nodiscard]] constexpr std::uint16_t lcd_control_read_mask(std::uint32_t address) {
  switch (address) {
    case 0x04000000U:
      return 0xFFFFU;
    case 0x04000002U:
      return 0x0001U;
    case 0x04000008U:
    case 0x0400000AU:
      return 0xDFFFU;
    case 0x04000048U:
    case 0x0400004AU:
      return 0x3F3FU;
    case 0x04000050U:
      return 0x3FFFU;
    case 0x04000052U:
      return 0x1F1FU;
    default:
      return 0xFFFFU;
  }
}

[[nodiscard]] constexpr bool is_lcd_control_readable(std::uint32_t address) {
  switch (address) {
    case 0x04000000U:
    case 0x04000002U:
    case 0x04000008U:
    case 0x0400000AU:
    case 0x0400000CU:
    case 0x0400000EU:
    case 0x04000048U:
    case 0x0400004AU:
    case 0x04000050U:
    case 0x04000052U:
      return true;
    default:
      return false;
  }
}

}  // namespace

PpuTiming::PpuTiming() {
  reset();
}

void PpuTiming::reset() {
  line_ = 0;
  line_cycle_ = 0;
  dispstat_control_ = 0;
  lcd_control_.fill(0);
}

void PpuTiming::write_dispstat(std::uint16_t value) {
  dispstat_control_ = static_cast<std::uint16_t>(value & kWritableDispstatMask);
}

std::optional<std::uint16_t> PpuTiming::read_lcd_control(std::uint32_t address) const {
  if (!is_lcd_control_address(address) || !is_lcd_control_readable(address)) {
    return std::nullopt;
  }
  return static_cast<std::uint16_t>(lcd_control_.at(lcd_control_index(address)) &
                                    lcd_control_read_mask(address));
}

bool PpuTiming::write_lcd_control(std::uint32_t address, std::uint16_t value) {
  if (!is_lcd_control_address(address)) {
    return false;
  }
  lcd_control_.at(lcd_control_index(address)) =
      static_cast<std::uint16_t>(value & lcd_control_read_mask(address));
  return true;
}

PpuRenderControl PpuTiming::render_control() const {
  PpuRenderControl control{};
  control.dispcnt = lcd_control_.at(lcd_control_index(0x04000000U));
  for (std::size_t index = 0; index < control.bg_control.size(); ++index) {
    const std::uint32_t bg_base = 0x04000008U + static_cast<std::uint32_t>(index * 2U);
    const std::uint32_t scroll_base =
        0x04000010U + static_cast<std::uint32_t>(index * 4U);
    control.bg_control.at(index) = lcd_control_.at(lcd_control_index(bg_base));
    control.bg_scroll_x.at(index) = lcd_control_.at(lcd_control_index(scroll_base));
    control.bg_scroll_y.at(index) =
        lcd_control_.at(lcd_control_index(scroll_base + 2U));
  }
  control.win0h = lcd_control_.at(lcd_control_index(0x04000040U));
  control.win0v = lcd_control_.at(lcd_control_index(0x04000044U));
  control.win1h = lcd_control_.at(lcd_control_index(0x04000042U));
  control.win1v = lcd_control_.at(lcd_control_index(0x04000046U));
  control.winin = lcd_control_.at(lcd_control_index(0x04000048U));
  control.winout = lcd_control_.at(lcd_control_index(0x0400004AU));
  control.bldcnt = lcd_control_.at(lcd_control_index(0x04000050U));
  control.bldalpha = lcd_control_.at(lcd_control_index(0x04000052U));
  control.bldy = lcd_control_.at(lcd_control_index(0x04000054U));
  return control;
}

PpuTickEvents PpuTiming::tick(std::uint32_t cycles, InterruptController& interrupts) {
  PpuTickEvents events{};
  while (cycles > 0) {
    const std::uint16_t next_event_cycle =
        line_cycle_ < kVisibleCycles ? kVisibleCycles : kCyclesPerLine;
    const std::uint32_t cycles_to_event = next_event_cycle - line_cycle_;
    const std::uint32_t step = std::min(cycles, cycles_to_event);
    line_cycle_ = static_cast<std::uint16_t>(line_cycle_ + step);
    cycles -= step;

    if (line_cycle_ == kVisibleCycles) {
      enter_hblank(interrupts, events);
    }
    if (line_cycle_ == kCyclesPerLine) {
      enter_next_line(interrupts, events);
    }
  }
  return events;
}

std::uint16_t PpuTiming::dispstat() const {
  std::uint16_t value = dispstat_control_;
  if (vblank()) {
    value = static_cast<std::uint16_t>(value | kVblankFlag);
  }
  if (hblank()) {
    value = static_cast<std::uint16_t>(value | kHblankFlag);
  }
  if (vcount_match()) {
    value = static_cast<std::uint16_t>(value | kVcountFlag);
  }
  return value;
}

std::uint16_t PpuTiming::vcount() const {
  return line_;
}

std::uint16_t PpuTiming::line_cycle() const {
  return line_cycle_;
}

std::uint32_t PpuTiming::frame_cycle() const {
  return static_cast<std::uint32_t>(line_) * kCyclesPerLine + line_cycle_;
}

std::uint8_t PpuTiming::vcount_setting() const {
  return static_cast<std::uint8_t>((dispstat_control_ >> 8) & 0xFFU);
}

bool PpuTiming::vblank() const {
  return line_ >= kVisibleLines && line_ < kTotalLines - 1;
}

bool PpuTiming::hblank() const {
  return line_cycle_ >= kHblankFlagCycles;
}

bool PpuTiming::vcount_match() const {
  return line_ == vcount_setting();
}

bool PpuTiming::vblank_irq_enabled() const {
  return (dispstat_control_ & kVblankIrqEnable) != 0;
}

bool PpuTiming::hblank_irq_enabled() const {
  return (dispstat_control_ & kHblankIrqEnable) != 0;
}

bool PpuTiming::vcount_irq_enabled() const {
  return (dispstat_control_ & kVcountIrqEnable) != 0;
}

PpuPhase PpuTiming::phase() const {
  if (vblank()) {
    return PpuPhase::vblank;
  }
  if (line_cycle_ >= kVisibleCycles) {
    return PpuPhase::hblank;
  }
  return PpuPhase::visible;
}

std::uint64_t PpuTiming::state_hash() const {
  StateHasher hasher;
  hasher.add_u16(line_);
  hasher.add_u16(line_cycle_);
  hasher.add_u16(dispstat_control_);
  for (const std::uint16_t value : lcd_control_) {
    hasher.add_u16(value);
  }
  return hasher.value();
}

void PpuTiming::enter_hblank(InterruptController& interrupts, PpuTickEvents& events) {
  if (line_ < kVisibleLines) {
    ++events.hblank_entries;
  }
  if (hblank_irq_enabled()) {
    interrupts.request(InterruptSource::hblank);
  }
}

void PpuTiming::enter_next_line(InterruptController& interrupts, PpuTickEvents& events) {
  line_cycle_ = 0;
  ++line_;
  if (line_ == kTotalLines) {
    line_ = 0;
  }

  if (line_ == kVisibleLines && vblank_irq_enabled()) {
    interrupts.request(InterruptSource::vblank);
  }
  if (line_ == kVisibleLines) {
    ++events.vblank_entries;
  }
  if (vcount_match() && vcount_irq_enabled()) {
    interrupts.request(InterruptSource::vcount);
  }
  if (vcount_match()) {
    ++events.vcount_matches;
  }
}

}  // namespace gba::core
