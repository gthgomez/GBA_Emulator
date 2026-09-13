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
constexpr std::uint32_t kSiodata32Low = 0x04000120U;
constexpr std::uint32_t kSiodata32High = 0x04000122U;
constexpr std::uint32_t kSiomulti2 = 0x04000124U;
constexpr std::uint32_t kSiomulti3 = 0x04000126U;
constexpr std::uint32_t kSiocnt = 0x04000128U;
constexpr std::uint32_t kSiodata8 = 0x0400012AU;
constexpr std::uint32_t kJoycnt = 0x04000140U;
constexpr std::uint32_t kJoyRecvLow = 0x04000150U;
constexpr std::uint32_t kJoyRecvHigh = 0x04000152U;
constexpr std::uint32_t kJoyTransLow = 0x04000154U;
constexpr std::uint32_t kJoyTransHigh = 0x04000156U;
constexpr std::uint32_t kJoystat = 0x0400015AU;
constexpr std::uint16_t kSiocntModeMask = 0x3000U;
constexpr std::uint16_t kRcModeMask = 0xC000U;
constexpr std::uint16_t kSioIdleControlBits = 0x4F8FU;
constexpr std::uint16_t kJoycntIdle = 0x0040U;
constexpr std::uint16_t kSioStartBit = 0x0080U;
constexpr std::uint16_t kSioInternalClockBit = 0x0001U;
constexpr std::uint16_t kSioFastClockBit = 0x0002U;
constexpr std::uint16_t kSioIrqEnableBit = 0x4000U;
constexpr std::uint32_t kSioSlowClockCyclesPerBit = 64U;
constexpr std::uint32_t kSioFastClockCyclesPerBit = 8U;
constexpr std::uint32_t kSioCompletionLatencyCycles = 40U;

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
    return true;
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

[[nodiscard]] constexpr std::uint16_t serial_mode(std::uint16_t siocnt) {
  return static_cast<std::uint16_t>(siocnt & kSiocntModeMask);
}

[[nodiscard]] constexpr std::uint16_t read_siocnt(std::uint16_t value) {
  const std::uint16_t mode = serial_mode(value);
  const std::uint16_t uart_external_clock =
      mode == 0x3000U ? static_cast<std::uint16_t>(value & 0x0020U) : 0;
  return static_cast<std::uint16_t>(kSioIdleControlBits | mode | uart_external_clock);
}

[[nodiscard]] constexpr std::uint16_t read_rcnt(std::uint16_t rcnt,
                                                std::uint16_t siocnt) {
  switch (rcnt & kRcModeMask) {
    case 0x8000U:
      return 0x81FFU;
    case 0xC000U:
      return 0xC1FCU;
    default:
      break;
  }
  const std::uint16_t mode = serial_mode(siocnt);
  return mode == 0x0000U || mode == 0x1000U ? 0x01F5U : 0x01FFU;
}

[[nodiscard]] constexpr std::uint16_t read_serial_data(std::uint32_t address,
                                                       std::uint16_t siocnt,
                                                       std::uint16_t stored) {
  const std::uint16_t mode = serial_mode(siocnt);
  switch (address) {
    case kSiodata32Low:
    case kSiodata32High:
      return mode == 0x1000U ? stored : 0;
    case kSiomulti2:
    case kSiomulti3:
      return 0;
    case kSiodata8:
      return mode == 0x3000U ? 0 : stored;
    default:
      return stored;
  }
}

[[nodiscard]] constexpr std::optional<std::uint32_t> serial_transfer_cycles(
    std::uint16_t siocnt) {
  if ((siocnt & kSioStartBit) == 0 || (siocnt & kSioInternalClockBit) == 0) {
    return std::nullopt;
  }

  const std::uint16_t mode = serial_mode(siocnt);
  std::uint32_t bits = 0;
  switch (mode) {
    case 0x0000U:
      bits = 8;
      break;
    case 0x1000U:
      bits = 32;
      break;
    default:
      return std::nullopt;
  }

  const std::uint32_t cycles_per_bit =
      (siocnt & kSioFastClockBit) != 0 ? kSioFastClockCyclesPerBit
                                       : kSioSlowClockCyclesPerBit;
  return bits * cycles_per_bit + kSioCompletionLatencyCycles;
}

[[nodiscard]] constexpr bool wave_ram_index(std::uint32_t address, std::size_t& index) {
  if (address < 0x04000090U || address > 0x0400009EU || !is_halfword_aligned(address)) {
    return false;
  }
  index = static_cast<std::size_t>((address - 0x04000090U) / 2U);
  return true;
}

[[nodiscard]] constexpr std::optional<std::uint16_t> zero_read_hole(
    std::uint32_t address) {
  switch (address) {
    case 0x04000066U:
    case 0x0400006AU:
    case 0x0400006EU:
    case 0x04000076U:
    case 0x0400007AU:
    case 0x0400007EU:
    case 0x04000086U:
    case 0x0400008AU:
    case 0x04000136U:
    case 0x04000142U:
    case kJoystat:
    case 0x04000206U:
    case 0x0400020AU:
    case 0x04000302U:
      return 0;
    default:
      return std::nullopt;
  }
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
      serial_{},
      sio_transfer_active_(false),
      sio_transfer_cycles_remaining_(0) {}

void IoRegisters::reset() {
  serial_.fill(0);
  sio_transfer_active_ = false;
  sio_transfer_cycles_remaining_ = 0;
}

IoRegistersState IoRegisters::save_state() const {
  return {serial_, sio_transfer_active_, sio_transfer_cycles_remaining_};
}

void IoRegisters::load_state(const IoRegistersState& state) {
  serial_ = state.serial;
  sio_transfer_active_ = state.sio_transfer_active;
  sio_transfer_cycles_remaining_ = state.sio_transfer_cycles_remaining;
}

std::uint64_t IoRegisters::state_hash() const {
  StateHasher hasher;
  for (const std::uint16_t value : serial_) {
    hasher.add_u16(value);
  }
  hasher.add_bool(sio_transfer_active_);
  hasher.add_u32(sio_transfer_cycles_remaining_);
  return hasher.value();
}

void IoRegisters::tick(std::uint32_t cycles) {
  if (!sio_transfer_active_ || cycles == 0) {
    return;
  }
  if (cycles < sio_transfer_cycles_remaining_) {
    sio_transfer_cycles_remaining_ -= cycles;
    return;
  }

  sio_transfer_active_ = false;
  sio_transfer_cycles_remaining_ = 0;
  if ((serial_.at(4) & kSioIrqEnableBit) != 0) {
    interrupts_.request(InterruptSource::serial);
  }
}

std::optional<std::uint16_t> IoRegisters::read16(std::uint32_t address) const {
  if (!is_halfword_aligned(address)) {
    return std::nullopt;
  }

  switch (address) {
    case 0x04000060U:
      return 0x007FU;
    case 0x04000062U:
      return 0xFFC0U;
    case 0x04000064U:
      return 0x4000U;
    case 0x04000068U:
      return 0xFFC0U;
    case 0x0400006CU:
      return 0x4000U;
    case 0x04000070U:
      return 0x00E0U;
    case 0x04000072U:
      return 0xE000U;
    case 0x04000074U:
      return 0x4000U;
    case 0x04000078U:
      return 0xFF00U;
    case 0x0400007CU:
      return 0x40FFU;
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

  if (const std::optional<std::uint16_t> zero = zero_read_hole(address);
      zero.has_value()) {
    return zero;
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
    if (address == kSiocnt) {
      return read_siocnt(serial_.at(serial));
    }
    if (address == kRcnt) {
      return read_rcnt(serial_.at(serial), serial_.at(4));
    }
    return read_serial_data(address, serial_.at(4), serial_.at(serial));
  }

  switch (address) {
    case kJoycnt:
      return kJoycntIdle;
    case kJoyRecvLow:
    case kJoyRecvHigh:
    case kJoyTransLow:
    case kJoyTransHigh:
      return 0;
    default:
      break;
  }

  std::size_t wave_ram = 0;
  if (wave_ram_index(address, wave_ram)) {
    return apu_.wave_ram(wave_ram);
  }

  std::size_t channel = 0;
  std::uint32_t dma_offset = 0;
  if (dma_index(address, channel, dma_offset)) {
    switch (dma_offset) {
      case 0:
      case 2:
      case 4:
      case 6:
        return std::nullopt;
      case 8:
        return 0;
      case 10:
        // DMA3CNT_H bit 11 (Game Pak DRQ) is storable on channel 3, so the
        // stored control value round-trips as-is; no forced bits.
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
      if (ppu_.recheck_vcount_match(value) && ppu_.vcount_irq_enabled()) {
        interrupts_.request(InterruptSource::vcount);
      }
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
    if (address == kSiocnt) {
      const std::optional<std::uint32_t> transfer_cycles =
          serial_transfer_cycles(value);
      sio_transfer_active_ = transfer_cycles.has_value();
      sio_transfer_cycles_remaining_ = transfer_cycles.value_or(0);
    }
    return true;
  }

  switch (address) {
    case kJoycnt:
    case kJoyRecvLow:
    case kJoyRecvHigh:
    case kJoyTransLow:
    case kJoyTransHigh:
      return true;
    default:
      break;
  }

  std::size_t wave_ram = 0;
  if (wave_ram_index(address, wave_ram)) {
    apu_.write_wave_ram(wave_ram, value);
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

  // Half-open 32-bit IO writes apply each halfword independently: a
  // read-only (or unmodeled) half is dropped, but writable halves still
  // commit. E.g. STR to 0x04000004 writes DISPSTAT and silently ignores the
  // VCOUNT half instead of discarding the whole word.
  bool handled_any_half = false;
  if (can_write16_address(address)) {
    handled_any_half = write16(address, low16(value));
  }
  if (can_write16_address(address + 2U)) {
    handled_any_half =
        write16(address + 2U, high16(value)) || handled_any_half;
  }
  return handled_any_half;
}

}  // namespace gba::core
