#include "gba/core/arm7tdmi.hpp"
#include "gba/core/interrupt_controller.hpp"
#include "gba/core/timers.hpp"

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
  expect(timers.overflow_count(0) == 0, "timer reset clears overflow count");
  timers.write_reload(1, 0);
  timers.write_control(1, 0x0081);
  timers.tick(63, interrupts);
  expect(timers.counter(1) == 0, "timer1 prescaler waits for divisor");
  timers.tick(1, interrupts);
  expect(timers.counter(1) == 1, "timer1 prescaler increments at 64 cycles");
  timers.write_reload(1, 0xFFF0);
  expect(timers.counter(1) == 1, "timer1 reload write does not change running counter");
  timers.write_control(1, 0);
  timers.write_control(1, 0x0081);
  expect(timers.counter(1) == 0xFFF0, "timer1 restart reloads counter");

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
