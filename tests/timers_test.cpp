#include "gba/core/arm7tdmi.hpp"
#include "gba/core/interrupt_controller.hpp"
#include "gba/core/timers.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

#include "test_helpers.hpp"

int main() {
  using gba::core::Arm7tdmi;
  using gba::core::CpuMode;
  using gba::core::InterruptController;
  using gba::core::InterruptSource;
  using gba::core::Timers;

  constexpr std::uint16_t timer0_irq = irq_bit(InterruptSource::timer0);
  constexpr std::uint16_t timer1_irq = irq_bit(InterruptSource::timer1);

  InterruptController interrupts;
  expect(interrupts.interrupt_enable() == 0, "IE resets clear");
  expect(interrupts.interrupt_flags() == 0, "IF resets clear");
  expect(!interrupts.master_enabled(), "IME resets disabled");
  expect(!interrupts.irq_line(), "IRQ line starts low");
  interrupts.request(InterruptSource::timer0);
  expect(interrupts.requested(InterruptSource::timer0), "timer0 request sets IF bit");
  expect(!interrupts.irq_line(), "IF alone does not raise IRQ line");
  interrupts.write_interrupt_enable(timer0_irq);
  expect(interrupts.enabled(InterruptSource::timer0), "IE enables timer0");
  expect(!interrupts.irq_line(), "IE and IF need IME");
  interrupts.write_ime(1);
  expect(interrupts.master_enabled(), "IME enables master interrupt gate");
  expect(interrupts.pending_mask() == timer0_irq, "pending mask combines IE and IF");
  expect(interrupts.irq_line(), "IE plus IF plus IME raises IRQ line");
  interrupts.write_interrupt_flags(timer1_irq);
  expect(interrupts.requested(InterruptSource::timer0), "acknowledging unrelated IF bit is ignored");
  interrupts.write_interrupt_flags(timer0_irq);
  expect(!interrupts.requested(InterruptSource::timer0), "writing one acknowledges IF bit");
  expect(!interrupts.irq_line(), "acknowledged IF lowers IRQ line");

  Timers timers;
  expect(timers.counter(0) == 0, "timer0 counter resets");
  expect(timers.reload(0) == 0, "timer0 reload resets");
  expect(timers.control(0) == 0, "timer0 control resets");
  timers.write_reload(0, 0xFFFE);
  timers.write_control(0, 0x0040);
  expect(!timers.enabled(0), "timer0 control without enable stays stopped");
  timers.write_control(0, 0x00C7);
  expect(timers.control(0) == 0x00C3, "timer0 count-up bit is ignored");
  expect(timers.counter(0) == 0xFFFE, "starting timer0 loads reload value");
  expect(timers.prescaler_divisor(0) == 1024, "timer0 prescaler 3 maps to 1024");
  timers.write_control(0, 0);

  timers.write_reload(0, 0xFFFE);
  timers.write_control(0, 0x00C0);
  interrupts.reset();
  timers.tick(1, interrupts);
  expect(timers.counter(0) == 0xFFFF, "timer0 increments after one CPU cycle");
  expect(!interrupts.requested(InterruptSource::timer0), "timer0 does not request IRQ before overflow");
  timers.tick(1, interrupts);
  expect(timers.counter(0) == 0xFFFE, "timer0 overflow reloads counter");
  expect(interrupts.requested(InterruptSource::timer0), "timer0 overflow requests IRQ");
  expect(timers.overflow_count(0) == 1,
         "timer0 overflow count records scheduler-visible event");

  timers.reset();
  interrupts.reset();
  timers.write_reload(0, 0);
  timers.write_control(0, 0x0080);
  timers.defer_newly_enabled_ticks();
  timers.tick(0, interrupts);
  timers.tick(1, interrupts);
  expect(timers.counter(0) == 0,
         "zero-cycle tick preserves deferred timer start state");
  timers.tick(1, interrupts);
  expect(timers.counter(0) == 1,
         "deferred timer start counts the next cycle after the delay");

  timers.reset();
  interrupts.reset();
  timers.write_reload(0, 0);
  timers.write_control(0, 0x0080);
  timers.defer_newly_enabled_ticks();
  timers.tick(3, interrupts);
  expect(timers.counter(0) == 2,
         "deferred timer start counts remaining cycles in the same tick batch");

  timers.reset();
  interrupts.reset();
  timers.tick(179, interrupts);
  timers.write_reload(0, 0xFFEE);
  timers.write_control(0, 0x00C3);
  timers.defer_newly_enabled_ticks();
  const Timers::State deferred_timer_state = timers.save_state();
  timers.tick(1024, interrupts);
  expect(timers.load_state(deferred_timer_state),
         "timer state restore accepts deferred active timer state");
  timers.tick(1, interrupts);
  expect(timers.counter(0) == 0xFFEE,
         "timer state restore preserves deferred enable delay");
  timers.tick(843, interrupts);
  expect(timers.counter(0) == 0xFFEE,
         "timer state restore preserves 1024-prescaler phase before edge");
  timers.tick(1, interrupts);
  expect(timers.counter(0) == 0xFFEF,
         "timer state restore resumes on the original prescaler edge");

  timers.reset();
  interrupts.reset();
  expect(timers.overflow_count(0) == 0, "timer reset clears overflow count");
  timers.write_reload(1, 0);
  timers.write_control(1, 0x0081);
  expect(timers.cycles_until_next_prescaler_tick(1) == 64,
         "timer1 prescaler phase starts one full divisor from the first tick");
  timers.tick(63, interrupts);
  expect(timers.counter(1) == 0, "timer1 prescaler waits for divisor");
  expect(timers.cycles_until_next_prescaler_tick(1) == 1,
         "timer1 prescaler phase reports the next edge before it lands");
  timers.tick(1, interrupts);
  expect(timers.counter(1) == 1, "timer1 prescaler increments at 64 cycles");
  expect(timers.cycles_until_next_prescaler_tick(1) == 64,
         "timer1 prescaler phase wraps after the edge lands");
  timers.write_reload(1, 0xFFF0);
  expect(timers.counter(1) == 1, "timer1 reload write does not change running counter");
  timers.write_control(1, 0);
  timers.write_control(1, 0x0081);
  expect(timers.counter(1) == 0xFFF0, "timer1 restart reloads counter");

  timers.reset();
  interrupts.reset();
  timers.tick(179, interrupts);
  timers.write_reload(0, 0xFFEE);
  timers.write_control(0, 0x00C3);
  expect(timers.last_enable_phase(0) == 179,
         "timer0 records 1024-prescaler enable phase");
  expect(timers.cycles_until_next_prescaler_tick(0) == 845,
         "timer0 reports cycles until the next 1024-prescaler edge");
  timers.tick(844, interrupts);
  expect(timers.counter(0) == 0xFFEE,
         "timer0 1024-prescaler waits until the recorded edge");
  expect(timers.cycles_until_next_prescaler_tick(0) == 1,
         "timer0 reports the final cycle before the 1024-prescaler edge");
  timers.tick(1, interrupts);
  expect(timers.counter(0) == 0xFFEF,
         "timer0 increments on the recorded 1024-prescaler edge");
  timers.tick(16U * 1024U, interrupts);
  expect(timers.counter(0) == 0xFFFF,
         "timer0 reaches the pre-overflow state on 1024-prescaler edges");
  timers.tick(1019, interrupts);
  expect(timers.counter(0) == 0xFFFF,
         "timer0 holds the pre-overflow value before the final edge");
  expect(timers.cycles_until_next_prescaler_tick(0) == 5,
         "timer0 reports five cycles until the overflowing 1024-prescaler edge");
  timers.tick(5, interrupts);
  expect(timers.counter(0) == 0xFFEE,
         "timer0 overflows back to reload on the traced 1024-prescaler edge");
  expect(interrupts.requested(InterruptSource::timer0),
         "timer0 requests IRQ on the traced 1024-prescaler overflow edge");

  timers.reset();
  interrupts.reset();
  timers.write_reload(0, 0xFFFF);
  timers.write_control(0, 0x00C0);
  const Timers::TickResult repeated_overflow = timers.tick(3, interrupts);
  expect(timers.counter(0) == 0xFFFF,
         "batched one-cycle timer overflows keep the reload value");
  expect(timers.overflow_count(0) == 3,
         "batched one-cycle timer records every overflow");
  expect(repeated_overflow.first_irq_cycle.has_value() &&
             repeated_overflow.first_irq_cycle.value() == 1,
         "batched one-cycle timer reports the first IRQ cycle");

  timers.reset();
  interrupts.reset();
  timers.write_reload(0, 0xFFFF);
  timers.write_reload(1, 0xFFFE);
  timers.write_control(0, 0x0080);
  timers.write_control(1, 0x00C4);
  const Timers::TickResult cascaded_overflow = timers.tick(3, interrupts);
  expect(timers.counter(1) == 0xFFFF,
         "batched count-up timer observes every source overflow");
  expect(timers.overflow_count(1) == 1,
         "batched count-up timer records cascaded overflow");
  expect(cascaded_overflow.first_irq_cycle.has_value() &&
             cascaded_overflow.first_irq_cycle.value() == 2,
         "batched count-up timer reports the cascaded IRQ cycle");

  timers.reset();
  interrupts.reset();
  timers.write_reload(0, 0xFFFF);
  timers.write_control(0, 0x0080);
  timers.tick(1, interrupts);
  interrupts.reset();
  timers.write_reload(1, 0xFFFE);
  timers.write_control(1, 0x00C4);
  timers.defer_newly_enabled_ticks();
  const Timers::TickResult deferred_cascade = timers.tick(3, interrupts);
  expect(timers.counter(1) == 0xFFFE,
         "deferred count-up timer ignores the first source overflow");
  expect(timers.overflow_count(1) == 1,
         "deferred count-up timer still records later cascaded overflow");
  expect(deferred_cascade.first_irq_cycle.has_value() &&
             deferred_cascade.first_irq_cycle.value() == 3,
         "deferred count-up timer shifts the cascaded IRQ cycle");

  timers.reset();
  interrupts.reset();
  timers.write_reload(0, 0xFFFF);
  timers.write_reload(1, 0xFFFE);
  timers.write_control(0, 0x0080);
  timers.write_control(1, 0x00C4);
  expect(timers.count_up(1), "timer1 count-up mode is enabled");
  timers.tick(1, interrupts);
  expect(timers.counter(1) == 0xFFFF, "timer0 overflow cascades into timer1");
  expect(timers.overflow_count(0) == 1, "timer0 cascade source overflow is counted");
  expect(!interrupts.requested(InterruptSource::timer1), "timer1 cascade has not overflowed yet");
  timers.tick(1, interrupts);
  expect(timers.counter(1) == 0xFFFE, "timer1 cascade overflow reloads");
  expect(interrupts.requested(InterruptSource::timer1), "timer1 cascade overflow requests IRQ");
  expect(timers.overflow_count(1) == 1, "timer1 cascaded overflow is counted");

  // M10/M11: the cascade enable-delay window must be anchored to absolute
  // cycles so suppression stays exact once cycle_counter_ passes 2^32.
  timers.reset();
  interrupts.reset();
  for (int batch = 0; batch < 5; ++batch) {
    timers.tick(0x40000000U, interrupts);
  }
  timers.write_reload(0, 0xFFFF);
  timers.write_control(0, 0x0080);
  timers.tick(1, interrupts);
  interrupts.reset();
  timers.write_reload(1, 0xFFFE);
  timers.write_control(1, 0x00C4);
  timers.defer_newly_enabled_ticks();
  const Timers::TickResult late_deferred_cascade = timers.tick(3, interrupts);
  expect(timers.counter(1) == 0xFFFE,
         "64-bit deferred count-up timer ignores the first source overflow");
  expect(timers.overflow_count(1) == 1,
         "64-bit deferred count-up timer records the later cascaded overflow");
  expect(late_deferred_cascade.first_irq_cycle.has_value() &&
             late_deferred_cascade.first_irq_cycle.value() == 3,
         "64-bit deferred cascade reports the relative IRQ cycle past 2^32");

  const Timers::State late_state = timers.save_state();
  const std::uint64_t late_hash = timers.state_hash();
  timers.tick(97, interrupts);
  expect(timers.load_state(late_state),
         "timer state restore accepts post-2^32 snapshot");
  expect(timers.state_hash() == late_hash,
         "timer state hash is symmetric across save/load");

  Arm7tdmi cpu;
  cpu.set_register(Arm7tdmi::kPc, 0x08000000);
  expect(cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::user)),
         "IRQ service starts from user mode");
  interrupts.write_interrupt_enable(timer1_irq);
  interrupts.write_ime(1);
  expect(interrupts.irq_line(), "timer1 pending request raises IRQ line");
  expect(interrupts.service_pending_irq(cpu), "interrupt controller services pending IRQ");
  expect(cpu.current_mode() == CpuMode::irq, "pending IRQ enters IRQ mode");
  expect(cpu.register_value(Arm7tdmi::kPc) == 0x00000018, "pending IRQ vectors PC");
  expect(cpu.register_value(Arm7tdmi::kLinkRegister) == 0x08000004,
         "pending IRQ writes LR");
  expect(cpu.irq_disabled(), "pending IRQ sets CPSR I bit");
  expect(cpu.spsr().value() == static_cast<std::uint32_t>(CpuMode::user),
         "pending IRQ saves previous CPSR");
  expect(!interrupts.service_pending_irq(cpu), "CPSR I bit blocks immediate nested IRQ");
  interrupts.write_interrupt_flags(timer1_irq);
  expect(!interrupts.irq_line(), "acknowledging timer1 IF lowers IRQ line");

  std::cout << "timers_test: PASS\n";
  return 0;
}
