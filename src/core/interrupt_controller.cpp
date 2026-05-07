#include "gba/core/interrupt_controller.hpp"

#include "gba/core/arm7tdmi.hpp"
#include "gba/core/state_hash.hpp"

namespace gba::core {
namespace {

[[nodiscard]] constexpr std::uint16_t interrupt_bit(InterruptSource source) {
  return static_cast<std::uint16_t>(1U << static_cast<std::uint8_t>(source));
}

}  // namespace

InterruptController::InterruptController() {
  reset();
}

void InterruptController::reset() {
  interrupt_enable_ = 0;
  interrupt_flags_ = 0;
  master_enabled_ = false;
}

std::uint16_t InterruptController::interrupt_enable() const {
  return interrupt_enable_;
}

std::uint16_t InterruptController::interrupt_flags() const {
  return interrupt_flags_;
}

bool InterruptController::master_enabled() const {
  return master_enabled_;
}

std::uint16_t InterruptController::ime() const {
  return master_enabled_ ? 1 : 0;
}

void InterruptController::write_interrupt_enable(std::uint16_t value) {
  interrupt_enable_ = static_cast<std::uint16_t>(value & kSupportedMask);
}

void InterruptController::write_interrupt_flags(std::uint16_t acknowledge_mask) {
  interrupt_flags_ = static_cast<std::uint16_t>(
      interrupt_flags_ & ~(acknowledge_mask & kSupportedMask));
}

void InterruptController::write_ime(std::uint16_t value) {
  master_enabled_ = (value & 0x1U) != 0;
}

void InterruptController::set_master_enabled(bool enabled) {
  master_enabled_ = enabled;
}

void InterruptController::request(InterruptSource source) {
  interrupt_flags_ = static_cast<std::uint16_t>(
      interrupt_flags_ | (interrupt_bit(source) & kSupportedMask));
}

bool InterruptController::requested(InterruptSource source) const {
  return (interrupt_flags_ & interrupt_bit(source)) != 0;
}

bool InterruptController::enabled(InterruptSource source) const {
  return (interrupt_enable_ & interrupt_bit(source)) != 0;
}

std::uint16_t InterruptController::pending_mask() const {
  return static_cast<std::uint16_t>(interrupt_enable_ & interrupt_flags_ & kSupportedMask);
}

bool InterruptController::irq_line() const {
  return master_enabled_ && pending_mask() != 0;
}

bool InterruptController::service_pending_irq(Arm7tdmi& cpu) const {
  if (!irq_line() || cpu.irq_disabled()) {
    return false;
  }
  return cpu.enter_exception(ExceptionKind::irq) == ExecuteStatus::executed;
}

std::uint64_t InterruptController::state_hash() const {
  StateHasher hasher;
  hasher.add_u16(interrupt_enable_);
  hasher.add_u16(interrupt_flags_);
  hasher.add_bool(master_enabled_);
  return hasher.value();
}

}  // namespace gba::core
