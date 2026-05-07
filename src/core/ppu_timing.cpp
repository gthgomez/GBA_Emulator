#include "gba/core/ppu_timing.hpp"

#include "gba/core/state_hash.hpp"

#include <algorithm>

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

}  // namespace

PpuTiming::PpuTiming() {
  reset();
}

void PpuTiming::reset() {
  line_ = 0;
  line_cycle_ = 0;
  dispstat_control_ = 0;
}

void PpuTiming::write_dispstat(std::uint16_t value) {
  dispstat_control_ = static_cast<std::uint16_t>(value & kWritableDispstatMask);
}

void PpuTiming::tick(std::uint32_t cycles, InterruptController& interrupts) {
  while (cycles > 0) {
    const std::uint16_t next_event_cycle =
        line_cycle_ < kVisibleCycles ? kVisibleCycles : kCyclesPerLine;
    const std::uint32_t cycles_to_event = next_event_cycle - line_cycle_;
    const std::uint32_t step = std::min(cycles, cycles_to_event);
    line_cycle_ = static_cast<std::uint16_t>(line_cycle_ + step);
    cycles -= step;

    if (line_cycle_ == kVisibleCycles) {
      enter_hblank(interrupts);
    }
    if (line_cycle_ == kCyclesPerLine) {
      enter_next_line(interrupts);
    }
  }
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
  return line_cycle_ >= kVisibleCycles;
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
  if (hblank()) {
    return PpuPhase::hblank;
  }
  return PpuPhase::visible;
}

std::uint64_t PpuTiming::state_hash() const {
  StateHasher hasher;
  hasher.add_u16(line_);
  hasher.add_u16(line_cycle_);
  hasher.add_u16(dispstat_control_);
  return hasher.value();
}

void PpuTiming::enter_hblank(InterruptController& interrupts) {
  if (line_ < kVisibleLines && hblank_irq_enabled()) {
    interrupts.request(InterruptSource::hblank);
  }
}

void PpuTiming::enter_next_line(InterruptController& interrupts) {
  line_cycle_ = 0;
  ++line_;
  if (line_ == kTotalLines) {
    line_ = 0;
  }

  if (line_ == kVisibleLines && vblank_irq_enabled()) {
    interrupts.request(InterruptSource::vblank);
  }
  if (vcount_match() && vcount_irq_enabled()) {
    interrupts.request(InterruptSource::vcount);
  }
}

}  // namespace gba::core
