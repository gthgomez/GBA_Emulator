#include "gba/core/interrupt_controller.hpp"
#include "gba/core/ppu_timing.hpp"

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

constexpr std::uint16_t irq_bit(gba::core::InterruptSource source) {
  return static_cast<std::uint16_t>(1U << static_cast<std::uint8_t>(source));
}

}  // namespace

int main() {
  using gba::core::InterruptController;
  using gba::core::InterruptSource;
  using gba::core::PpuPhase;
  using gba::core::PpuTiming;

  static_assert(PpuTiming::kVisibleWidth == 240, "GBA visible width is fixed");
  static_assert(PpuTiming::kVisibleLines == 160, "GBA visible lines are fixed");
  static_assert(PpuTiming::kTotalLines == 228, "GBA total lines are fixed");
  static_assert(PpuTiming::kVisibleCycles == 960, "GBA visible scanline cycles are fixed");
  static_assert(PpuTiming::kHblankFlagCycles == 1006,
                "DISPSTAT HBlank flag is low for 1006 cycles per scanline");
  static_assert(PpuTiming::kHblankCycles == 272, "GBA HBlank cycles are fixed");
  static_assert(PpuTiming::kCyclesPerLine == 1232, "GBA scanline cycles are fixed");
  static_assert(PpuTiming::kCyclesPerFrame == 280896, "GBA frame cycles are fixed");

  InterruptController interrupts;
  PpuTiming ppu;

  expect(ppu.vcount() == 0, "PPU VCOUNT resets to line 0");
  expect(ppu.line_cycle() == 0, "PPU line cycle resets to 0");
  expect(ppu.frame_cycle() == 0, "PPU frame cycle resets to 0");
  expect(ppu.phase() == PpuPhase::visible, "PPU starts in visible phase");
  expect(!ppu.vblank(), "PPU reset is not VBlank");
  expect(!ppu.hblank(), "PPU reset is not HBlank");
  expect(ppu.vcount_match(), "PPU reset matches default VCount setting");
  expect(ppu.dispstat() == 0x0004, "DISPSTAT reset exposes only VCount match flag");

  expect(ppu.write_lcd_control(0x04000000, 0x1003),
         "PPU accepts DISPCNT writes for render control");
  expect(ppu.write_lcd_control(0x04000008, 0x1C02),
         "PPU accepts BG0 control writes for render control");
  expect(ppu.write_lcd_control(0x04000010, 7),
         "PPU accepts BG0 horizontal scroll writes for render control");
  expect(ppu.write_lcd_control(0x04000012, 9),
         "PPU accepts BG0 vertical scroll writes for render control");
  expect(ppu.write_lcd_control(0x04000040, 0x7010),
         "PPU accepts WIN0H writes for render control");
  expect(ppu.write_lcd_control(0x04000044, 0x5020),
         "PPU accepts WIN0V writes for render control");
  expect(ppu.write_lcd_control(0x04000048, 0x1234),
         "PPU accepts WININ writes for render control");
  expect(ppu.write_lcd_control(0x04000050, 0x00BF),
         "PPU accepts BLDCNT writes for render control");
  expect(ppu.write_lcd_control(0x04000052, 0x1008),
         "PPU accepts BLDALPHA writes for render control");
  expect(ppu.write_lcd_control(0x04000054, 0x000F),
         "PPU accepts BLDY writes for render control");
  const gba::core::PpuRenderControl render_control = ppu.render_control();
  expect(render_control.dispcnt == 0x1003, "render control exposes DISPCNT");
  expect(render_control.bg_control[0] == 0x1C02,
         "render control exposes BG0 control");
  expect(render_control.bg_scroll_x[0] == 7, "render control exposes BG0 X scroll");
  expect(render_control.bg_scroll_y[0] == 9, "render control exposes BG0 Y scroll");
  expect(render_control.win0h == 0x7010, "render control exposes WIN0H");
  expect(render_control.win0v == 0x5020, "render control exposes WIN0V");
  expect(render_control.winin == 0x1234, "render control exposes WININ");
  expect(render_control.bldcnt == 0x00BF, "render control exposes BLDCNT");
  expect(render_control.bldalpha == 0x1008, "render control exposes BLDALPHA");
  expect(render_control.bldy == 0x000F, "render control exposes BLDY");
  ppu.reset();

  ppu.write_dispstat(0xFFFF);
  expect(ppu.vblank_irq_enabled(), "DISPSTAT enables VBlank IRQ bit");
  expect(ppu.hblank_irq_enabled(), "DISPSTAT enables HBlank IRQ bit");
  expect(ppu.vcount_irq_enabled(), "DISPSTAT enables VCount IRQ bit");
  expect(ppu.vcount_setting() == 0xFF, "DISPSTAT stores VCount setting bits");
  expect((ppu.dispstat() & 0x00C0) == 0, "DISPSTAT masks unused low control bits");
  ppu.write_dispstat(0);

  ppu.tick(PpuTiming::kVisibleCycles - 1, interrupts);
  expect(ppu.vcount() == 0, "PPU stays on line 0 before HBlank");
  expect(ppu.line_cycle() == PpuTiming::kVisibleCycles - 1,
         "PPU accumulates visible cycles");
  expect(!ppu.hblank(), "PPU is not HBlank before visible cycles finish");
  expect(ppu.phase() == PpuPhase::visible, "PPU remains visible before HBlank");

  ppu.write_dispstat(0x0010);
  ppu.tick(1, interrupts);
  expect(!ppu.hblank(), "DISPSTAT HBlank flag is not yet set at cycle 960");
  expect(ppu.phase() == PpuPhase::hblank, "PPU phase reports HBlank");
  expect(interrupts.requested(InterruptSource::hblank), "visible HBlank requests IRQ");
  interrupts.write_interrupt_flags(irq_bit(InterruptSource::hblank));

  ppu.tick(PpuTiming::kHblankFlagCycles - PpuTiming::kVisibleCycles, interrupts);
  expect(ppu.hblank(), "DISPSTAT HBlank flag sets after the event delay");

  ppu.tick(PpuTiming::kCyclesPerLine - PpuTiming::kHblankFlagCycles, interrupts);
  expect(ppu.vcount() == 1, "PPU advances to line 1 after full scanline");
  expect(ppu.line_cycle() == 0, "PPU starts next line at cycle 0");
  expect(!ppu.hblank(), "PPU clears HBlank on next line");

  ppu.write_dispstat(static_cast<std::uint16_t>(0x0020 | (2U << 8)));
  ppu.tick(PpuTiming::kCyclesPerLine, interrupts);
  expect(ppu.vcount() == 2, "PPU reaches VCount compare line");
  expect(ppu.vcount_match(), "PPU sets VCount match flag on compare line");
  expect(interrupts.requested(InterruptSource::vcount), "VCount match requests IRQ");
  interrupts.write_interrupt_flags(irq_bit(InterruptSource::vcount));

  ppu.reset();
  ppu.write_dispstat(0x0018);
  ppu.tick(static_cast<std::uint32_t>(PpuTiming::kCyclesPerLine) *
               PpuTiming::kVisibleLines,
           interrupts);
  expect(ppu.vcount() == PpuTiming::kVisibleLines, "PPU reaches first VBlank line");
  expect(ppu.line_cycle() == 0, "PPU enters VBlank at start of line 160");
  expect(ppu.vblank(), "PPU sets VBlank flag on line 160");
  expect(ppu.phase() == PpuPhase::vblank, "PPU phase reports VBlank");
  expect(interrupts.requested(InterruptSource::vblank), "VBlank start requests IRQ");
  interrupts.write_interrupt_flags(irq_bit(InterruptSource::vblank));
  interrupts.reset();

  ppu.tick(PpuTiming::kVisibleCycles, interrupts);
  expect(!ppu.hblank(), "DISPSTAT HBlank flag remains delayed during VBlank lines");
  expect(interrupts.requested(InterruptSource::hblank),
         "HBlank IRQ is requested during VBlank scanlines");
  interrupts.write_interrupt_flags(irq_bit(InterruptSource::hblank));

  ppu.tick(PpuTiming::kHblankFlagCycles - PpuTiming::kVisibleCycles, interrupts);
  expect(ppu.hblank(), "DISPSTAT HBlank flag sets during VBlank scanlines");

  ppu.tick(static_cast<std::uint32_t>(PpuTiming::kCyclesPerLine) *
                   (PpuTiming::kTotalLines - PpuTiming::kVisibleLines - 2) +
               (PpuTiming::kHblankCycles -
                (PpuTiming::kHblankFlagCycles - PpuTiming::kVisibleCycles)),
           interrupts);
  expect(ppu.vcount() == PpuTiming::kTotalLines - 1,
         "PPU reaches final hidden line 227");
  expect(!ppu.vblank(), "PPU clears VBlank flag on line 227");

  ppu.tick(PpuTiming::kCyclesPerLine, interrupts);
  expect(ppu.vcount() == 0, "PPU wraps VCOUNT after line 227");
  expect(ppu.frame_cycle() == 0, "PPU frame cycle wraps to zero");

  std::cout << "ppu_timing_test: PASS\n";
  return 0;
}
