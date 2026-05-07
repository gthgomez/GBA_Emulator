#include "gba/core/dma_controller.hpp"

#include "gba/core/memory_bus.hpp"
#include "gba/core/state_hash.hpp"

#include <optional>
#include <stdexcept>

namespace gba::core {
namespace {

constexpr std::uint16_t kDestinationControlMask = 0x0060;
constexpr std::uint16_t kSourceControlMask = 0x0180;
constexpr std::uint16_t kRepeatFlag = 0x0200;
constexpr std::uint16_t kTransfer32Flag = 0x0400;
constexpr std::uint16_t kStartTimingMask = 0x3000;
constexpr std::uint16_t kIrqFlag = 0x4000;
constexpr std::uint16_t kEnableFlag = 0x8000;
constexpr std::uint16_t kControlMask = kDestinationControlMask | kSourceControlMask |
                                       kRepeatFlag | kTransfer32Flag |
                                       kStartTimingMask | kIrqFlag | kEnableFlag;

[[nodiscard]] constexpr InterruptSource dma_interrupt_source(std::size_t channel) {
  return static_cast<InterruptSource>(
      static_cast<std::uint8_t>(InterruptSource::dma0) + channel);
}

[[nodiscard]] constexpr std::uint32_t step_address(std::uint32_t address,
                                                   DmaAddressControl control,
                                                   std::uint32_t unit_size) {
  switch (control) {
    case DmaAddressControl::increment:
    case DmaAddressControl::increment_reload:
      return address + unit_size;
    case DmaAddressControl::decrement:
      return address - unit_size;
    case DmaAddressControl::fixed:
      return address;
  }
  return address;
}

}  // namespace

DmaController::DmaController() {
  reset();
}

void DmaController::reset() {
  for (Channel& channel : channels_) {
    channel.source = 0;
    channel.destination = 0;
    channel.word_count = 0;
    channel.control = 0;
    channel.current_source = 0;
    channel.current_destination = 0;
    channel.current_count = 0;
  }
}

void DmaController::write_source(std::size_t channel, std::uint32_t value) {
  checked_channel(channel).source = value;
}

void DmaController::write_destination(std::size_t channel, std::uint32_t value) {
  checked_channel(channel).destination = value;
}

void DmaController::write_word_count(std::size_t channel, std::uint16_t value) {
  checked_channel(channel).word_count = value;
}

void DmaController::write_control(std::size_t channel, std::uint16_t value) {
  Channel& state = checked_channel(channel);
  const bool was_enabled = (state.control & kEnableFlag) != 0;
  state.control = static_cast<std::uint16_t>(value & kControlMask);
  const bool is_enabled = (state.control & kEnableFlag) != 0;
  if (!was_enabled && is_enabled) {
    state.current_source = state.source;
    state.current_destination = state.destination;
    state.current_count = normalized_word_count(channel);
  }
  if (!is_enabled) {
    state.current_count = 0;
  }
}

std::uint32_t DmaController::source(std::size_t channel) const {
  return checked_channel(channel).source;
}

std::uint32_t DmaController::destination(std::size_t channel) const {
  return checked_channel(channel).destination;
}

std::uint16_t DmaController::word_count(std::size_t channel) const {
  return checked_channel(channel).word_count;
}

std::uint16_t DmaController::control(std::size_t channel) const {
  return checked_channel(channel).control;
}

std::uint32_t DmaController::active_count(std::size_t channel) const {
  return checked_channel(channel).current_count;
}

bool DmaController::enabled(std::size_t channel) const {
  return (checked_channel(channel).control & kEnableFlag) != 0;
}

bool DmaController::repeat(std::size_t channel) const {
  return (checked_channel(channel).control & kRepeatFlag) != 0;
}

bool DmaController::transfer_32bit(std::size_t channel) const {
  return (checked_channel(channel).control & kTransfer32Flag) != 0;
}

bool DmaController::irq_on_completion(std::size_t channel) const {
  return (checked_channel(channel).control & kIrqFlag) != 0;
}

DmaAddressControl DmaController::destination_control(std::size_t channel) const {
  return static_cast<DmaAddressControl>((checked_channel(channel).control >> 5) & 0x3U);
}

DmaAddressControl DmaController::source_control(std::size_t channel) const {
  return static_cast<DmaAddressControl>((checked_channel(channel).control >> 7) & 0x3U);
}

DmaStartTiming DmaController::start_timing(std::size_t channel) const {
  return static_cast<DmaStartTiming>((checked_channel(channel).control >> 12) & 0x3U);
}

std::uint64_t DmaController::state_hash() const {
  StateHasher hasher;
  for (const Channel& channel : channels_) {
    hasher.add_u32(channel.source);
    hasher.add_u32(channel.destination);
    hasher.add_u16(channel.word_count);
    hasher.add_u16(channel.control);
    hasher.add_u32(channel.current_source);
    hasher.add_u32(channel.current_destination);
    hasher.add_u32(channel.current_count);
  }
  return hasher.value();
}

DmaRunResult DmaController::run_immediate(MemoryBus& memory,
                                          InterruptController& interrupts) {
  DmaRunResult result{0, 0, false};
  for (std::size_t channel = 0; channel < kChannelCount; ++channel) {
    if (!enabled(channel) || start_timing(channel) != DmaStartTiming::immediate) {
      continue;
    }
    if (!execute_channel(channel, memory, interrupts, result.units_transferred)) {
      result.unsupported_request = true;
      return result;
    }
    ++result.channels_executed;
  }
  return result;
}

DmaController::Channel& DmaController::checked_channel(std::size_t channel) {
  if (channel >= kChannelCount) {
    throw std::out_of_range("DMA channel index out of range");
  }
  return channels_.at(channel);
}

const DmaController::Channel& DmaController::checked_channel(std::size_t channel) const {
  if (channel >= kChannelCount) {
    throw std::out_of_range("DMA channel index out of range");
  }
  return channels_.at(channel);
}

std::uint32_t DmaController::normalized_word_count(std::size_t channel) const {
  const Channel& state = checked_channel(channel);
  const std::uint32_t mask = channel == 3 ? 0xFFFFU : 0x3FFFU;
  const std::uint32_t count = state.word_count & mask;
  return count == 0 ? mask + 1U : count;
}

bool DmaController::execute_channel(std::size_t channel, MemoryBus& memory,
                                    InterruptController& interrupts,
                                    std::uint32_t& units_transferred) {
  if (source_control(channel) == DmaAddressControl::increment_reload) {
    return false;
  }

  Channel& state = checked_channel(channel);
  const bool word_transfer = transfer_32bit(channel);
  const std::uint32_t unit_size = word_transfer ? 4U : 2U;
  std::uint32_t source_address = state.current_source;
  std::uint32_t destination_address = state.current_destination;

  for (std::uint32_t unit = 0; unit < state.current_count; ++unit) {
    if (word_transfer) {
      const std::optional<std::uint32_t> value = memory.read32(source_address);
      if (!value.has_value() || !memory.write32(destination_address, value.value())) {
        return false;
      }
    } else {
      const std::optional<std::uint16_t> value = memory.read16(source_address);
      if (!value.has_value() || !memory.write16(destination_address, value.value())) {
        return false;
      }
    }
    source_address = step_address(source_address, source_control(channel), unit_size);
    destination_address =
        step_address(destination_address, destination_control(channel), unit_size);
  }

  units_transferred += state.current_count;
  state.current_source = source_address;
  state.current_destination = destination_address;
  state.current_count = 0;

  if (irq_on_completion(channel)) {
    interrupts.request(dma_interrupt_source(channel));
  }

  if (repeat(channel)) {
    state.current_count = normalized_word_count(channel);
    if (destination_control(channel) == DmaAddressControl::increment_reload) {
      state.current_destination = state.destination;
    }
    return true;
  }

  state.control = static_cast<std::uint16_t>(state.control & ~kEnableFlag);
  return true;
}

}  // namespace gba::core
