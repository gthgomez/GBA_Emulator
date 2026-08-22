#include "gba/core/interrupt_controller.hpp"
#include "gba/core/ppu_timing.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

#include "test_helpers.hpp"

int main() {
  using gba::core::InterruptController;
  using gba::core::InterruptSource;
  using gba::core::PpuPhase;
  using gba::core::PpuTiming;

  static_assert(PpuTiming::kVisibleWidth == 240, "GBA visible width is fixed");
  static_assert(PpuTiming::kVisibleLines == 160, "GBA visible lines are fixed");
  static_assert(PpuTiming::kTotalLines == 228, "GBA total lines are fixed");
  static_assert(PpuTiming::kVisibleCycles == 960, "GBA visible scanline cycles are fixed");
  static_assert(PpuTiming::kHblankFlagCycles == 1004,
                "DISPSTAT HBlank flag asserts after 1004 cycles per scanline");
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
  expect(ppu.write_lcd_control(0x04000042, 0x6011),
         "PPU accepts WIN1H writes for render control");
  expect(ppu.write_lcd_control(0x04000044, 0x5020),
         "PPU accepts WIN0V writes for render control");
  expect(ppu.write_lcd_control(0x04000046, 0x4012),
         "PPU accepts WIN1V writes for render control");
  expect(ppu.write_lcd_control(0x04000048, 0x1234),
         "PPU accepts WININ writes for render control");
  expect(ppu.write_lcd_control(0x0400004A, 0x003C),
         "PPU accepts WINOUT writes for render control");
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
  expect(render_control.win1h == 0x6011, "render control exposes WIN1H");
  expect(render_control.win0v == 0x5020, "render control exposes WIN0V");
  expect(render_control.win1v == 0x4012, "render control exposes WIN1V");
  expect(render_control.winin == 0x1234, "render control exposes WININ");
  expect(render_control.winout == 0x003C, "render control exposes WINOUT");
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
  expect(!interrupts.requested(InterruptSource::hblank),
         "HBlank IRQ waits for the calibrated event point");

  ppu.tick(PpuTiming::kHblankFlagCycles - PpuTiming::kVisibleCycles, interrupts);
  expect(ppu.hblank(), "DISPSTAT HBlank flag sets after the event delay");
  expect(interrupts.requested(InterruptSource::hblank), "visible HBlank requests IRQ");

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
  expect(!interrupts.requested(InterruptSource::hblank),
         "HBlank IRQ waits for the calibrated point on VBlank lines too");

  ppu.tick(PpuTiming::kHblankFlagCycles - PpuTiming::kVisibleCycles, interrupts);
  expect(interrupts.requested(InterruptSource::hblank),
         "HBlank IRQ IS requested during VBlank scanlines (IRQ fires on all lines)");
  interrupts.write_interrupt_flags(irq_bit(InterruptSource::hblank));

  ppu.tick(1, interrupts);
  expect(ppu.hblank(), "DISPSTAT HBlank flag sets during VBlank scanlines");

  ppu.tick(static_cast<std::uint32_t>(PpuTiming::kCyclesPerLine) *
                   (PpuTiming::kTotalLines - PpuTiming::kVisibleLines - 2) +
               (PpuTiming::kCyclesPerLine -
                (PpuTiming::kHblankFlagCycles - PpuTiming::kVisibleCycles)),
           interrupts);
  expect(ppu.vcount() == PpuTiming::kTotalLines - 1,
         "PPU reaches final hidden line 227");
  expect(!ppu.vblank(), "PPU clears VBlank flag on line 227");

  ppu.reset();
  ppu.tick(PpuTiming::kCyclesPerFrame, interrupts);
  expect(ppu.vcount() == 0, "PPU wraps VCOUNT after line 227");
  expect(ppu.frame_cycle() == 0, "PPU frame cycle wraps to zero");

  gba::core::PpuTickEvents frame_events = ppu.tick(PpuTiming::kCyclesPerFrame, interrupts);
  expect(frame_events.hblank_entries == PpuTiming::kTotalLines - 1,
         "HBlank events count on every line except line 227");
  expect(frame_events.vblank_entries == 1, "VBlank event counts once per frame");
  expect(frame_events.vcount_matches == 1,
         "default VCount setting matches once per frame");

  ppu.reset();
  [[maybe_unused]] const gba::core::PpuTickEvents pre_line161 =
      ppu.tick(static_cast<std::uint32_t>(PpuTiming::kCyclesPerLine) * 161U +
                   PpuTiming::kHblankFlagCycles - 1U,
               interrupts);
  const gba::core::PpuTickEvents line161_entry =
      ppu.tick(1, interrupts);
  expect(pre_line161.hblank_entries == 161,
         "HBlank entries accumulate through line 160 during VBlank");
  expect(line161_entry.hblank_entries == 1,
         "HDMA HBlank entry counted on line 161");

  ppu.reset();
  expect(!ppu.recheck_vcount_match(static_cast<std::uint16_t>(5U << 8)),
         "DISPSTAT write away from current line does not assert match");
  ppu.tick(static_cast<std::uint32_t>(PpuTiming::kCyclesPerLine) * 4U, interrupts);
  expect(!ppu.recheck_vcount_match(
             static_cast<std::uint16_t>((5U << 8) | 0x0020U)),
         "DISPSTAT write below matching line does not assert match");
  expect(ppu.vcount_setting() == 5, "recheck stores the new DISPSTAT value");
  expect(!ppu.vcount_match(), "line 4 does not match setting 5");
  ppu.tick(PpuTiming::kCyclesPerLine, interrupts);
  expect(ppu.vcount_match(), "PPU reaches the programmed compare line");
  expect(!ppu.recheck_vcount_match(static_cast<std::uint16_t>(7U << 8)),
         "DISPSTAT write clearing the match does not assert");
  expect(!ppu.vcount_match(), "match flag cleared by the DISPSTAT write");
  expect(ppu.recheck_vcount_match(static_cast<std::uint16_t>(5U << 8)),
         "DISPSTAT write onto matching line newly asserts match");
  expect((ppu.dispstat() & 0x0004U) != 0,
         "rechecked match exposes the VCOUNT flag in DISPSTAT");

  const gba::core::PpuTiming::State saved = ppu.save_state();
  ppu.write_dispstat(0);
  ppu.tick(PpuTiming::kCyclesPerLine, interrupts);
  expect(ppu.load_state(saved), "PPU state loads");
  const gba::core::PpuTiming::State restored = ppu.save_state();
  expect(restored.line == saved.line && restored.line_cycle == saved.line_cycle &&
             restored.dispstat_control == saved.dispstat_control &&
             restored.lcd_control == saved.lcd_control,
         "loaded PPU state restores timing registers");

  std::cout << "ppu_timing_test: PASS\n";
  return 0;
}
