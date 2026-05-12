#include "gba/core/dma_controller.hpp"

#include "gba/core/memory_bus.hpp"
#include "gba/core/state_hash.hpp"
#include "gba/core/wait_state_control.hpp"

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
constexpr std::uint32_t kFifoAAddress = 0x040000A0;
constexpr std::uint32_t kFifoBAddress = 0x040000A4;
constexpr std::uint32_t kSoundFifoWordsPerRequest = 4;

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

[[nodiscard]] constexpr DmaStartTiming trigger_timing(DmaTrigger trigger) {
  switch (trigger) {
    case DmaTrigger::immediate:
      return DmaStartTiming::immediate;
    case DmaTrigger::vblank:
      return DmaStartTiming::vblank;
    case DmaTrigger::hblank:
      return DmaStartTiming::hblank;
    case DmaTrigger::special:
    case DmaTrigger::fifo_a:
    case DmaTrigger::fifo_b:
      return DmaStartTiming::special;
  }
  return DmaStartTiming::immediate;
}

[[nodiscard]] constexpr DirectSoundChannel trigger_fifo(DmaTrigger trigger) {
  return trigger == DmaTrigger::fifo_b ? DirectSoundChannel::b : DirectSoundChannel::a;
}

[[nodiscard]] constexpr std::uint32_t trigger_fifo_address(DmaTrigger trigger) {
  return trigger == DmaTrigger::fifo_b ? kFifoBAddress : kFifoAAddress;
}

[[nodiscard]] bool dma_source_uses_latch(std::uint32_t address) {
  return MemoryBus::describe(address).region == Region::bios;
}

[[nodiscard]] bool dma_source_steps_as_game_pak_stream(std::size_t channel,
                                                       std::uint32_t address) {
  return channel != 0 && MemoryBus::describe(address & 0x0FFFFFFFU).region ==
                             Region::game_pak_rom;
}

[[nodiscard]] constexpr std::uint32_t duplicate_halfword(std::uint16_t value) {
  return static_cast<std::uint32_t>(value) |
         (static_cast<std::uint32_t>(value) << 16U);
}

[[nodiscard]] bool game_pak_region(Region region) {
  return region == Region::game_pak_rom || region == Region::game_pak_save;
}

[[nodiscard]] MemoryAccessTiming dma_timing(std::uint32_t address, AccessWidth width,
                                            const WaitStateControl* waitcnt) {
  return waitcnt != nullptr ? MemoryBus::timing(address, width, *waitcnt)
                            : MemoryBus::timing(address, width);
}

[[nodiscard]] std::uint32_t dma_access_phase_cycles(const MemoryAccessTiming& timing,
                                                    bool sequential,
                                                    bool external_waitcnt_access) {
  const std::uint32_t cycles =
      sequential ? static_cast<std::uint32_t>(timing.sequential)
                 : static_cast<std::uint32_t>(timing.nonsequential);
  return external_waitcnt_access ? cycles + 1U : cycles;
}

[[nodiscard]] std::uint32_t dma_access_cycles(std::uint32_t address, AccessWidth width,
                                              bool sequential,
                                              const WaitStateControl* waitcnt) {
  const MemoryAccessTiming timing = dma_timing(address, width, waitcnt);
  const bool external_waitcnt_access =
      waitcnt != nullptr && game_pak_region(MemoryBus::describe(address).region);
  if (width == AccessWidth::word && external_waitcnt_access) {
    return dma_access_phase_cycles(timing, sequential, true) +
           dma_access_phase_cycles(timing, true, true);
  }
  return dma_access_phase_cycles(timing, sequential, external_waitcnt_access);
}

[[nodiscard]] std::uint32_t dma_transfer_bus_cycles(std::uint32_t source_address,
                                                    std::uint32_t destination_address,
                                                    bool word_transfer,
                                                    bool source_sequential,
                                                    bool destination_sequential,
                                                    const WaitStateControl* waitcnt) {
  const AccessWidth width = word_transfer ? AccessWidth::word : AccessWidth::halfword;
  return dma_access_cycles(source_address, width, source_sequential, waitcnt) +
         dma_access_cycles(destination_address, width, destination_sequential, waitcnt);
}

[[nodiscard]] constexpr bool dma_stream_is_sequential(std::uint32_t unit,
                                                      DmaAddressControl control) {
  return unit != 0 && (control == DmaAddressControl::increment ||
                      control == DmaAddressControl::increment_reload);
}

void apply_dma_bus_adjustment(std::uint32_t& bus_cycles, std::int32_t adjustment) {
  if (adjustment >= 0) {
    bus_cycles += static_cast<std::uint32_t>(adjustment);
    return;
  }
  const std::uint32_t magnitude = static_cast<std::uint32_t>(-adjustment);
  bus_cycles = bus_cycles > magnitude ? bus_cycles - magnitude : 0;
}

[[nodiscard]] std::int32_t dma_game_pak_arbitration_adjustment(
    std::uint32_t source_address, std::uint32_t destination_address,
    const WaitStateControl* waitcnt) {
  if (waitcnt == nullptr) {
    return 0;
  }

  const bool source_game_pak =
      game_pak_region(MemoryBus::describe(source_address).region);
  const bool destination_game_pak =
      game_pak_region(MemoryBus::describe(destination_address).region);
  if (!source_game_pak && !destination_game_pak) {
    return 0;
  }

  if (source_game_pak && destination_game_pak) {
    const MemoryAccessTiming timing =
        MemoryBus::timing(source_address, AccessWidth::halfword, *waitcnt);
    std::int32_t adjustment =
        -static_cast<std::int32_t>(timing.nonsequential > 2U
                                       ? timing.nonsequential - 2U
                                       : 0U);
    if (!waitcnt->prefetch_enabled() && timing.sequential <= 1U) {
      --adjustment;
    }
    return adjustment;
  }

  if (source_game_pak) {
    const MemoryAccessTiming timing =
        MemoryBus::timing(source_address, AccessWidth::halfword, *waitcnt);
    return waitcnt->prefetch_enabled() && timing.sequential <= 1U ? 1 : 0;
  }

  const MemoryAccessTiming timing =
      MemoryBus::timing(destination_address, AccessWidth::halfword, *waitcnt);
  return waitcnt->prefetch_enabled() && timing.sequential > 1U ? 1 : 0;
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
    channel.data_latch = 0;
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
    hasher.add_u32(channel.data_latch);
  }
  return hasher.value();
}

DmaRunResult DmaController::run_immediate(MemoryBus& memory,
                                          InterruptController& interrupts,
                                          const WaitStateControl* waitcnt) {
  return run_trigger(DmaTrigger::immediate, memory, interrupts, waitcnt);
}

DmaRunResult DmaController::run_trigger(DmaTrigger trigger, MemoryBus& memory,
                                        InterruptController& interrupts,
                                        const WaitStateControl* waitcnt) {
  DmaRunResult result{0, 0, false, 0};
  const DmaStartTiming timing = trigger_timing(trigger);
  if (trigger == DmaTrigger::fifo_a || trigger == DmaTrigger::fifo_b) {
    result.unsupported_request = true;
    return result;
  }
  for (std::size_t channel = 0; channel < kChannelCount; ++channel) {
    if (!enabled(channel) || start_timing(channel) != timing) {
      continue;
    }
    if (!execute_channel(channel, memory, interrupts, waitcnt, result.units_transferred,
                         result.bus_cycles)) {
      result.unsupported_request = true;
      return result;
    }
    ++result.channels_executed;
  }
  return result;
}

DmaRunResult DmaController::run_sound_fifo(DmaTrigger trigger, MemoryBus& memory,
                                           Apu& apu,
                                           InterruptController& interrupts,
                                           const WaitStateControl* waitcnt) {
  DmaRunResult result{0, 0, false, 0};
  if (trigger != DmaTrigger::fifo_a && trigger != DmaTrigger::fifo_b) {
    result.unsupported_request = true;
    return result;
  }

  for (std::size_t channel = 1; channel <= 2; ++channel) {
    if (!enabled(channel) || start_timing(channel) != DmaStartTiming::special ||
        destination(channel) != trigger_fifo_address(trigger)) {
      continue;
    }
    if (!execute_sound_fifo_channel(channel, trigger_fifo(trigger), memory, apu,
                                    interrupts, waitcnt, result.units_transferred,
                                    result.bus_cycles)) {
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

std::uint32_t DmaController::effective_source_address(std::size_t channel,
                                                      std::uint32_t address) {
  const std::uint32_t mask = channel == 0 ? 0x07FFFFFFU : 0x0FFFFFFFU;
  return address & mask;
}

std::uint32_t DmaController::effective_destination_address(std::size_t channel,
                                                           std::uint32_t address) {
  const std::uint32_t mask = channel == 3 ? 0x0FFFFFFFU : 0x07FFFFFFU;
  return address & mask;
}

bool DmaController::execute_channel(std::size_t channel, MemoryBus& memory,
                                    InterruptController& interrupts,
                                    const WaitStateControl* waitcnt,
                                    std::uint32_t& units_transferred,
                                    std::uint32_t& bus_cycles) {
  if (source_control(channel) == DmaAddressControl::increment_reload) {
    return false;
  }

  Channel& state = checked_channel(channel);
  const bool word_transfer = transfer_32bit(channel);
  const std::uint32_t unit_size = word_transfer ? 4U : 2U;
  std::uint32_t source_address = state.current_source;
  std::uint32_t destination_address = state.current_destination;
  const std::uint32_t initial_aligned_source =
      effective_source_address(channel, source_address) & ~(unit_size - 1U);
  const std::uint32_t initial_aligned_destination =
      effective_destination_address(channel, destination_address) & ~(unit_size - 1U);
  bus_cycles += 2U;

  for (std::uint32_t unit = 0; unit < state.current_count; ++unit) {
    const std::uint32_t aligned_source =
        effective_source_address(channel, source_address) & ~(unit_size - 1U);
    const std::uint32_t aligned_destination =
        effective_destination_address(channel, destination_address) & ~(unit_size - 1U);
    const DmaAddressControl source_step_control =
        dma_source_steps_as_game_pak_stream(channel, source_address)
            ? DmaAddressControl::increment
            : source_control(channel);
    bus_cycles += dma_transfer_bus_cycles(
        aligned_source, aligned_destination, word_transfer,
        dma_stream_is_sequential(unit, source_step_control),
        dma_stream_is_sequential(unit, destination_control(channel)), waitcnt);
    if (word_transfer) {
      const std::optional<std::uint32_t> read_value =
          dma_source_uses_latch(aligned_source) ? std::nullopt : memory.read32(aligned_source);
      const std::uint32_t value = read_value.value_or(state.data_latch);
      state.data_latch = value;
      [[maybe_unused]] const bool written = memory.write32(aligned_destination, value);
    } else {
      const std::optional<std::uint16_t> read_value =
          dma_source_uses_latch(aligned_source) ? std::nullopt : memory.read16(aligned_source);
      const std::uint16_t value = read_value.value_or(
          static_cast<std::uint16_t>(state.data_latch & 0xFFFFU));
      if (read_value.has_value()) {
        state.data_latch = duplicate_halfword(value);
      }
      [[maybe_unused]] const bool written = memory.write16(aligned_destination, value);
    }
    source_address = step_address(source_address, source_step_control, unit_size);
    destination_address =
        step_address(destination_address, destination_control(channel), unit_size);
  }

  units_transferred += state.current_count;
  apply_dma_bus_adjustment(
      bus_cycles, dma_game_pak_arbitration_adjustment(initial_aligned_source,
                                                      initial_aligned_destination,
                                                      waitcnt));
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

bool DmaController::execute_sound_fifo_channel(std::size_t channel,
                                               DirectSoundChannel fifo,
                                               MemoryBus& memory, Apu& apu,
                                               InterruptController& interrupts,
                                               const WaitStateControl* waitcnt,
                                               std::uint32_t& units_transferred,
                                               std::uint32_t& bus_cycles) {
  if (!transfer_32bit(channel) || source_control(channel) == DmaAddressControl::increment_reload ||
      destination_control(channel) != DmaAddressControl::fixed) {
    return false;
  }

  Channel& state = checked_channel(channel);
  std::uint32_t source_address = state.current_source;
  for (std::uint32_t unit = 0; unit < kSoundFifoWordsPerRequest; ++unit) {
    const std::uint32_t aligned_source =
        effective_source_address(channel, source_address) & ~0x3U;
    bus_cycles += dma_transfer_bus_cycles(
        aligned_source, destination(channel), true,
        dma_stream_is_sequential(unit, source_control(channel)), false, waitcnt);
    const std::optional<std::uint32_t> value =
        memory.read32(aligned_source);
    if (!value.has_value()) {
      return false;
    }
    state.data_latch = value.value();
    apu.write_fifo(fifo, value.value());
    source_address = step_address(source_address, source_control(channel), 4U);
  }

  units_transferred += kSoundFifoWordsPerRequest;
  state.current_source = source_address;
  state.current_destination = destination(channel);
  state.current_count = repeat(channel) ? normalized_word_count(channel) : 0;

  if (irq_on_completion(channel)) {
    interrupts.request(dma_interrupt_source(channel));
  }
  if (!repeat(channel)) {
    state.control = static_cast<std::uint16_t>(state.control & ~kEnableFlag);
  }
  return true;
}

}  // namespace gba::core
