#include "gba/core/core_session.hpp"
#include "gba/core/save_state_codec.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
void expect(bool ok, const char* message) {
  if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

void expect_corruption_rejected(const std::vector<std::uint8_t>& encoded,
                                std::size_t offset) {
  using namespace gba::core;
  auto tampered = encoded;
  tampered.at(offset) ^= 1U;
  auto target = std::make_unique<CoreSession>();
  const auto original_hash = target->state_hash();
  expect(SaveStateCodec::decode_into(*target, tampered).status ==
             SaveStateDecodeStatus::state_hash_mismatch,
         "every saved register byte participates in snapshot integrity");
  expect(target->state_hash() == original_hash,
         "rejected register corruption leaves target unchanged");
}

void set_legacy_header(std::vector<std::uint8_t>& bytes, std::uint8_t version,
                       std::uint64_t hash) {
  bytes[4] = version;
  for (unsigned i = 0; i < 8; ++i) bytes[8 + i] = (hash >> (8 * i)) & 0xffU;
}

void automatic_irq_test() {
  using namespace gba::core;
  auto session = std::make_unique<CoreSession>();
  session->bios().set_mode(BiosExecutionMode::hle);
  expect(session->memory().write32(BiosHleConstants::kUserIrqHandlerPointer,
                                    0x03000100), "automatic handler pointer");
  expect(session->memory().write32(0x03000100, 0xe12fff1e), "ARM BX LR handler");
  auto& cpu = session->cpu();
  auto& interrupts = session->interrupts();
  interrupts.write_interrupt_enable(1);
  interrupts.write_ime(1);
  for (unsigned iteration = 0; iteration < 3; ++iteration) {
    const auto program_pc = 0x03000200U + iteration * 32;
    expect(cpu.set_cpsr(0x1f), "program system mode");
    cpu.set_register(0, 0x12345678);
    cpu.set_register(15, program_pc);
    interrupts.request(InterruptSource::vblank);
    expect(session->scheduler().service_pending_irq(), "ordinary IRQ admission");
    expect(session->step().step.has_value(), "ordinary vector dispatch");
    expect(session->scheduler().save_state().hle_irq_outer_frames.empty(),
           "ordinary IRQ does not create an outer frame");
    interrupts.write_interrupt_flags(1);
    if (iteration == 2) {
      expect(cpu.set_cpsr(0x1f), "handler enables nested IRQ in system mode");
      cpu.set_register(0, 0x87654321);
      cpu.set_register(15, 0x03000140);
      interrupts.request(InterruptSource::vblank);
      expect(session->scheduler().service_pending_irq(), "nested IRQ admission");
      expect(session->step().step.has_value(), "nested vector dispatch");
      expect(session->scheduler().save_state().hle_irq_outer_frames.size() == 1,
             "real nested IRQ saves exactly one outer frame");
      interrupts.write_interrupt_flags(1);
      cpu.set_register(0, 0);
      expect(session->step().step.has_value(), "nested handler BX LR");
      expect(session->step().step.has_value(), "nested IRQ return");
      expect(cpu.register_value(15) == 0x03000140 &&
                 cpu.register_value(0) == 0x87654321,
             "nested admission restores interrupted handler");
      expect(cpu.set_cpsr(0x92) && cpu.set_spsr(0x1f),
             "outer handler restores banked exception state");
      cpu.set_register(15, BiosHleConstants::kIrqReturnSentinelPc);
    } else {
      expect(session->step().step.has_value(), "ordinary handler BX LR");
    }
    expect(session->step().step.has_value(), "ordinary IRQ return");
    const auto returned = session->scheduler().save_state();
    expect(!returned.hle_irq_return_lr && !returned.hle_irq_saved_registers &&
               returned.hle_irq_outer_frames.empty(),
           "completed outer IRQ clears all active context");
    expect(cpu.register_value(15) == program_pc &&
               cpu.register_value(0) == 0x12345678,
           "successive IRQs restore their own interrupted program");
  }
}
}

int main() {
  using namespace gba::core;
  automatic_irq_test();
  auto session = std::make_unique<CoreSession>();
  session->bios().set_mode(BiosExecutionMode::hle);
  expect(session->memory().write32(BiosHleConstants::kUserIrqHandlerPointer,
                                    0x03000100), "IRQ handler pointer");

  // v3 predates the empty outer-IRQ and pending-work counts.
  auto legacy = SaveStateCodec::encode(*session);
  expect(legacy.size() >= 20, "snapshot has frame count");
  set_legacy_header(legacy, 3, 1869358511553600127ULL);
  legacy.resize(legacy.size() - 8);
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
  const auto active = SaveStateCodec::encode(*session);
  // Registers precede 21 scalar bytes and two empty vector counts.
  for (std::size_t byte = 0; byte < 52; ++byte) {
    expect_corruption_rejected(active, active.size() - 81 + byte);
  }
  // Golden hashes captured from the v4 encoder before the integrity repair.
  // v3/v4 share the active-register layout and historical hash calculation.
  for (const auto version : {3U, 4U}) {
    auto old_active = active;
    old_active.resize(old_active.size() - 4); // v6 work count
    set_legacy_header(old_active, version, 9517732750589066455ULL);
    if (version == 3) old_active.resize(old_active.size() - 4);
    auto restored_active = std::make_unique<CoreSession>();
    expect(SaveStateCodec::decode_into(*restored_active, old_active).status ==
               SaveStateDecodeStatus::ok,
           "legacy active IRQ snapshot remains readable");
    expect(restored_active->state_hash() == session->state_hash(),
           "legacy active registers migrate to the full machine hash");
  }
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
  expect(encoded[4] == 6, "new snapshots include pending-work version");
  // One outer frame ends in LR(4), presence(1), registers(52), flags(2).
  for (std::size_t byte = 0; byte < 52; ++byte) {
    expect_corruption_rejected(encoded, encoded.size() - 58 + byte);
  }
  auto old_nested = encoded;
  old_nested.resize(old_nested.size() - 4);
  set_legacy_header(old_nested, 4, 2430423098251959970ULL);
  auto migrated = std::make_unique<CoreSession>();
  expect(SaveStateCodec::decode_into(*migrated, old_nested).status ==
             SaveStateDecodeStatus::ok,
         "legacy nested IRQ snapshot remains readable");
  expect(migrated->state_hash() == session->state_hash(),
         "legacy outer registers migrate to the full machine hash");
  auto tampered = encoded;
  tampered.at(tampered.size() - 5) ^= 1U;
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
