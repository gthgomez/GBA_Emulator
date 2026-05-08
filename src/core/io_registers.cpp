#include "gba/core/io_registers.hpp"

#include "gba/core/state_hash.hpp"

#include <cstddef>

namespace gba::core {
namespace {

constexpr std::uint32_t kDmaChannelStride = 12;
constexpr std::uint32_t kDmaRegisterBytes = kDmaChannelStride * DmaController::kChannelCount;
constexpr std::uint32_t kTimerStride = 4;
constexpr std::uint32_t kTimerRegisterBytes = kTimerStride * Timers::kTimerCount;
constexpr std::array<std::uint32_t, 7> kSerialRegisterAddresses = {
    0x04000120U, 0x04000122U, 0x04000124U, 0x04000126U,
    0x04000128U, 0x0400012AU, IoRegisters::kRcnt,
};

[[nodiscard]] constexpr bool is_halfword_aligned(std::uint32_t address) {
  return (address & 0x1U) == 0;
}

[[nodiscard]] constexpr bool is_word_aligned(std::uint32_t address) {
  return (address & 0x3U) == 0;
}

[[nodiscard]] constexpr bool is_lcd_control_address(std::uint32_t address) {
  if (!is_halfword_aligned(address)) {
    return false;
  }
  if (address <= 0x0400001EU) {
    return address != IoRegisters::kDispstat && address != IoRegisters::kVcount;
  }
  if (address >= 0x04000020U && address <= 0x0400003EU) {
    return true;
  }
  if (address >= 0x04000040U && address <= 0x04000054U) {
    return address != 0x0400004EU;
  }
  return false;
}

[[nodiscard]] constexpr bool dma_index(std::uint32_t address, std::size_t& channel,
                                       std::uint32_t& offset) {
  if (address < IoRegisters::kDmaBase ||
      address >= IoRegisters::kDmaBase + kDmaRegisterBytes) {
    return false;
  }
  const std::uint32_t relative = address - IoRegisters::kDmaBase;
  channel = relative / kDmaChannelStride;
  offset = relative % kDmaChannelStride;
  return true;
}

[[nodiscard]] constexpr bool timer_index(std::uint32_t address, std::size_t& index,
                                         std::uint32_t& offset) {
  if (address < IoRegisters::kTimerBase ||
      address >= IoRegisters::kTimerBase + kTimerRegisterBytes) {
    return false;
  }
  const std::uint32_t relative = address - IoRegisters::kTimerBase;
  index = relative / kTimerStride;
  offset = relative % kTimerStride;
  return true;
}

[[nodiscard]] constexpr bool serial_index(std::uint32_t address, std::size_t& index) {
  for (std::size_t i = 0; i < kSerialRegisterAddresses.size(); ++i) {
    if (address == kSerialRegisterAddresses.at(i)) {
      index = i;
      return true;
    }
  }
  return false;
}

[[nodiscard]] constexpr std::uint16_t low16(std::uint32_t value) {
  return static_cast<std::uint16_t>(value & 0xFFFFU);
}

[[nodiscard]] constexpr std::uint16_t high16(std::uint32_t value) {
  return static_cast<std::uint16_t>((value >> 16U) & 0xFFFFU);
}

[[nodiscard]] constexpr std::uint32_t replace_low16(std::uint32_t current,
                                                    std::uint16_t value) {
  return (current & 0xFFFF0000U) | value;
}

[[nodiscard]] constexpr std::uint32_t replace_high16(std::uint32_t current,
                                                     std::uint16_t value) {
  return (current & 0x0000FFFFU) | (static_cast<std::uint32_t>(value) << 16U);
}

[[nodiscard]] constexpr std::uint32_t combine16(std::uint16_t low, std::uint16_t high) {
  return static_cast<std::uint32_t>(low) | (static_cast<std::uint32_t>(high) << 16U);
}

[[nodiscard]] constexpr bool can_write16_address(std::uint32_t address) {
  if (!is_halfword_aligned(address)) {
    return false;
  }

  switch (address) {
    case IoRegisters::kDispstat:
    case IoRegisters::kSoundcntL:
    case IoRegisters::kSoundcntH:
    case IoRegisters::kSoundcntX:
    case IoRegisters::kSoundbias:
    case IoRegisters::kKeycnt:
    case IoRegisters::kIe:
    case IoRegisters::kIf:
    case IoRegisters::kWaitcnt:
    case IoRegisters::kIme:
      return true;
    case IoRegisters::kVcount:
      return false;
    default:
      break;
  }

  if (is_lcd_control_address(address)) {
    return true;
  }

  std::size_t index = 0;
  std::uint32_t offset = 0;
  if (timer_index(address, index, offset)) {
    return offset == 0 || offset == 2;
  }
  if (serial_index(address, index)) {
    return true;
  }
  if (dma_index(address, index, offset)) {
    return offset == 0 || offset == 2 || offset == 4 || offset == 6 || offset == 8 ||
           offset == 10;
  }
  return false;
}

}  // namespace

IoRegisters::IoRegisters(InterruptController& interrupts, Timers& timers,
                         DmaController& dma, PpuTiming& ppu, Apu& apu,
                         WaitStateControl& waitcnt, Keypad& keypad)
    : interrupts_(interrupts),
      timers_(timers),
      dma_(dma),
      ppu_(ppu),
      apu_(apu),
      waitcnt_(waitcnt),
      keypad_(keypad),
      serial_{} {}

void IoRegisters::reset() {
  serial_.fill(0);
}

IoRegistersState IoRegisters::save_state() const {
  return {serial_};
}

void IoRegisters::load_state(const IoRegistersState& state) {
  serial_ = state.serial;
}

std::uint64_t IoRegisters::state_hash() const {
  StateHasher hasher;
  for (const std::uint16_t value : serial_) {
    hasher.add_u16(value);
  }
  return hasher.value();
}

std::optional<std::uint16_t> IoRegisters::read16(std::uint32_t address) const {
  if (!is_halfword_aligned(address)) {
    return std::nullopt;
  }

  switch (address) {
    case kDispstat:
      return ppu_.dispstat();
    case kVcount:
      return ppu_.vcount();
    case kSoundcntL:
      return apu_.soundcnt_l();
    case kSoundcntH:
      return apu_.soundcnt_h();
    case kSoundcntX:
      return apu_.soundcnt_x();
    case kSoundbias:
      return apu_.soundbias();
    case kKeyinput:
      return keypad_.keyinput();
    case kKeycnt:
      return keypad_.keycnt();
    case kIe:
      return interrupts_.interrupt_enable();
    case kIf:
      return interrupts_.interrupt_flags();
    case kWaitcnt:
      return waitcnt_.read_control();
    case kIme:
      return interrupts_.ime();
    default:
      break;
  }

  if (const std::optional<std::uint16_t> value = ppu_.read_lcd_control(address);
      value.has_value()) {
    return value;
  }

  std::size_t timer = 0;
  std::uint32_t timer_offset = 0;
  if (timer_index(address, timer, timer_offset)) {
    if (timer_offset == 0) {
      return timers_.counter(timer);
    }
    if (timer_offset == 2) {
      return timers_.control(timer);
    }
    return std::nullopt;
  }

  std::size_t serial = 0;
  if (serial_index(address, serial)) {
    return serial_.at(serial);
  }

  std::size_t channel = 0;
  std::uint32_t dma_offset = 0;
  if (dma_index(address, channel, dma_offset)) {
    switch (dma_offset) {
      case 0:
        return low16(dma_.source(channel));
      case 2:
        return high16(dma_.source(channel));
      case 4:
        return low16(dma_.destination(channel));
      case 6:
        return high16(dma_.destination(channel));
      case 8:
        return dma_.word_count(channel);
      case 10:
        return dma_.control(channel);
      default:
        return std::nullopt;
    }
  }

  return std::nullopt;
}

std::optional<std::uint32_t> IoRegisters::read32(std::uint32_t address) const {
  if (!is_word_aligned(address)) {
    return std::nullopt;
  }
  if (address == kIme) {
    return read16(address);
  }
  const std::optional<std::uint16_t> low = read16(address);
  const std::optional<std::uint16_t> high = read16(address + 2U);
  if (!low.has_value() || !high.has_value()) {
    return std::nullopt;
  }
  return combine16(low.value(), high.value());
}

bool IoRegisters::write16(std::uint32_t address, std::uint16_t value) {
  if (!is_halfword_aligned(address)) {
    return false;
  }

  switch (address) {
    case kDispstat:
      ppu_.write_dispstat(value);
      return true;
    case kVcount:
      return false;
    case kSoundcntL:
      apu_.write_soundcnt_l(value);
      return true;
    case kSoundcntH:
      apu_.write_soundcnt_h(value);
      return true;
    case kSoundcntX:
      apu_.write_soundcnt_x(value);
      return true;
    case kSoundbias:
      apu_.write_soundbias(value);
      return true;
    case kKeyinput:
      return false;
    case kKeycnt:
      keypad_.write_keycnt(value);
      keypad_.poll_interrupt(interrupts_);
      return true;
    case kIe:
      interrupts_.write_interrupt_enable(value);
      return true;
    case kIf:
      interrupts_.write_interrupt_flags(value);
      return true;
    case kWaitcnt:
      waitcnt_.write_control(value);
      return true;
    case kIme:
      interrupts_.write_ime(value);
      return true;
    default:
      break;
  }

  if (ppu_.write_lcd_control(address, value)) {
    return true;
  }

  std::size_t timer = 0;
  std::uint32_t timer_offset = 0;
  if (timer_index(address, timer, timer_offset)) {
    if (timer_offset == 0) {
      timers_.write_reload(timer, value);
      return true;
    }
    if (timer_offset == 2) {
      timers_.write_control(timer, value);
      return true;
    }
    return false;
  }

  std::size_t serial = 0;
  if (serial_index(address, serial)) {
    serial_.at(serial) = value;
    return true;
  }

  std::size_t channel = 0;
  std::uint32_t dma_offset = 0;
  if (dma_index(address, channel, dma_offset)) {
    switch (dma_offset) {
      case 0:
        dma_.write_source(channel, replace_low16(dma_.source(channel), value));
        return true;
      case 2:
        dma_.write_source(channel, replace_high16(dma_.source(channel), value));
        return true;
      case 4:
        dma_.write_destination(channel, replace_low16(dma_.destination(channel), value));
        return true;
      case 6:
        dma_.write_destination(channel, replace_high16(dma_.destination(channel), value));
        return true;
      case 8:
        dma_.write_word_count(channel, value);
        return true;
      case 10:
        dma_.write_control(channel, value);
        return true;
      default:
        return false;
    }
  }

  return false;
}

bool IoRegisters::write32(std::uint32_t address, std::uint32_t value) {
  if (!is_word_aligned(address)) {
    return false;
  }

  if (address == kIme) {
    return write16(address, low16(value));
  }
  if (address == kWaitcnt) {
    return write16(address, low16(value));
  }

  if (address == kFifoA) {
    apu_.write_fifo(DirectSoundChannel::a, value);
    return true;
  }
  if (address == kFifoB) {
    apu_.write_fifo(DirectSoundChannel::b, value);
    return true;
  }

  if (!can_write16_address(address) || !can_write16_address(address + 2U)) {
    return false;
  }

  return write16(address, low16(value)) && write16(address + 2U, high16(value));
}

}  // namespace gba::core
