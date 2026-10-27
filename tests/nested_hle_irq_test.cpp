#include "gba/core/core_session.hpp"
#include "gba/core/save_state_codec.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
void expect(bool ok, const char* message) {
  if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main() {
  using namespace gba::core;
  auto session = std::make_unique<CoreSession>();
  session->bios().set_mode(BiosExecutionMode::hle);
  expect(session->memory().write32(BiosHleConstants::kUserIrqHandlerPointer,
                                    0x03000100), "IRQ handler pointer");

  // A v3 payload is the v4 encoding without the trailing empty frame count.
  auto legacy = SaveStateCodec::encode(*session);
  expect(legacy.size() >= 20, "snapshot has frame count");
  legacy[4] = 3;
  legacy.resize(legacy.size() - 4);
  auto legacy_restored = std::make_unique<CoreSession>();
  expect(SaveStateCodec::decode_into(*legacy_restored, legacy).status ==
             SaveStateDecodeStatus::ok,
         "version 3 snapshot remains readable");
  expect(legacy_restored->state_hash() == session->state_hash(),
         "version 3 snapshot preserves machine hash");

  auto& cpu = session->cpu();
  expect(cpu.set_cpsr(0x92), "outer IRQ mode");
  expect(cpu.set_spsr(0x1f), "outer SPSR");
  cpu.set_register(0, 0x11111111);
  cpu.set_register(14, 0x08000104);
  cpu.set_register(15, BiosHleConstants::kIrqVectorAddress);
  expect(session->step().step.has_value(), "outer dispatch");
  auto outer_scheduler = session->scheduler().save_state();
  outer_scheduler.hle_irq_long_timer_chained_return_pending = true;
  outer_scheduler.hle_irq_slow_timer0_return_pending = true;
  session->scheduler().load_state(outer_scheduler);

  expect(cpu.set_cpsr(0x1f), "outer handler system mode");
  cpu.set_register(0, 0x22222222);
  expect(cpu.set_cpsr(0x92), "inner IRQ mode");
  expect(cpu.set_spsr(0x1f), "inner SPSR");
  cpu.set_register(14, 0x08000204);
  cpu.set_register(15, BiosHleConstants::kIrqVectorAddress);
  expect(session->step().step.has_value(), "inner dispatch");

  const auto encoded = SaveStateCodec::encode(*session);
  auto tampered = encoded;
  tampered.back() ^= 1U;
  auto rejected = std::make_unique<CoreSession>();
  const auto rejected_hash = rejected->state_hash();
  expect(SaveStateCodec::decode_into(*rejected, tampered).status ==
             SaveStateDecodeStatus::state_hash_mismatch,
         "nested context participates in snapshot integrity");
  expect(rejected->state_hash() == rejected_hash,
         "rejected nested snapshot leaves target unchanged");
  auto restored = std::make_unique<CoreSession>();
  expect(SaveStateCodec::decode_into(*restored, encoded).status ==
             SaveStateDecodeStatus::ok,
         "restore nested IRQ snapshot");
  auto& nested = restored->cpu();
  nested.set_register(0, 0x33333333);
  nested.set_register(15, BiosHleConstants::kIrqReturnSentinelPc);
  expect(restored->step().step.has_value(), "inner return");
  expect(nested.register_value(15) == 0x08000200,
         "inner returns to interrupted outer handler");
  expect(nested.register_value(0) == 0x22222222, "inner restores registers");
  const auto resumed_outer = restored->scheduler().save_state();
  expect(resumed_outer.hle_irq_long_timer_chained_return_pending &&
             resumed_outer.hle_irq_slow_timer0_return_pending,
         "inner preserves outer IRQ return timing state");

  expect(nested.set_cpsr(0x92), "outer IRQ mode again");
  expect(nested.set_spsr(0x1f), "outer SPSR restored by handler");
  nested.set_register(15, BiosHleConstants::kIrqReturnSentinelPc);
  expect(restored->step().step.has_value(), "outer return");
  expect(nested.register_value(15) == 0x08000100,
         "outer returns to interrupted program");
  expect(nested.register_value(0) == 0x11111111, "outer restores registers");
  std::cout << "nested_hle_irq_test: PASS\n";
}
