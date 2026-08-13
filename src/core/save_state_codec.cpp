#include "gba/core/save_state_codec.hpp"

#include "gba/core/arm7tdmi.hpp"
#include "gba/core/core_session.hpp"

#include <array>
#include <cstddef>
#include <optional>

namespace gba::core {
namespace {

void write_u8(std::vector<std::uint8_t>& out, std::uint8_t value) {
  out.push_back(value);
}

void write_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
  write_u8(out, static_cast<std::uint8_t>(value & 0xFFU));
  write_u8(out, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void write_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (std::uint8_t byte = 0; byte < 4; ++byte) {
    write_u8(out, static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xFFU));
  }
}

void write_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (std::uint8_t byte = 0; byte < 8; ++byte) {
    write_u8(out, static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xFFU));
  }
}

void write_bytes(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& bytes) {
  write_u32(out, static_cast<std::uint32_t>(bytes.size()));
  out.insert(out.end(), bytes.begin(), bytes.end());
}

void write_timer_state(std::vector<std::uint8_t>& out,
                       const Timers::State& timers) {
  write_u64(out, timers.cycle_counter);
  for (const Timers::TimerState& timer : timers.timers) {
    write_u16(out, timer.counter);
    write_u16(out, timer.reload);
    write_u16(out, timer.control);
    write_u64(out, timer.prescaler_remainder);
    write_u64(out, timer.overflow_count);
    write_u32(out, timer.enable_delay_cycles);
    write_u16(out, timer.last_enable_phase);
    write_u8(out, timer.just_enabled ? 1U : 0U);
  }
}

void write_interrupt_state(std::vector<std::uint8_t>& out,
                           const InterruptController& interrupts) {
  write_u16(out, interrupts.interrupt_enable());
  write_u16(out, interrupts.interrupt_flags());
  write_u8(out, interrupts.master_enabled() ? 1U : 0U);
}

class Reader {
 public:
  explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

  [[nodiscard]] bool read_u8(std::uint8_t& value) {
    if (offset_ >= bytes_.size()) {
      return false;
    }
    value = bytes_.at(offset_++);
    return true;
  }

  [[nodiscard]] bool read_u16(std::uint16_t& value) {
    std::uint8_t b0 = 0;
    std::uint8_t b1 = 0;
    if (!read_u8(b0) || !read_u8(b1)) {
      return false;
    }
    value = static_cast<std::uint16_t>(b0 | (static_cast<std::uint16_t>(b1) << 8U));
    return true;
  }

  [[nodiscard]] bool read_u32(std::uint32_t& value) {
    value = 0;
    for (std::uint8_t byte = 0; byte < 4; ++byte) {
      std::uint8_t next = 0;
      if (!read_u8(next)) {
        return false;
      }
      value |= static_cast<std::uint32_t>(next) << (byte * 8U);
    }
    return true;
  }

  [[nodiscard]] bool read_u64(std::uint64_t& value) {
    value = 0;
    for (std::uint8_t byte = 0; byte < 8; ++byte) {
      std::uint8_t next = 0;
      if (!read_u8(next)) {
        return false;
      }
      value |= static_cast<std::uint64_t>(next) << (byte * 8U);
    }
    return true;
  }

  [[nodiscard]] bool read_bytes(std::vector<std::uint8_t>& out) {
    std::uint32_t size = 0;
    if (!read_u32(size) || bytes_.size() - offset_ < size) {
      return false;
    }
    out.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
               bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + size));
    offset_ += size;
    return true;
  }

  [[nodiscard]] bool read_bool(bool& value) {
    std::uint8_t raw = 0;
    if (!read_u8(raw) || raw > 1U) {
      return false;
    }
    value = raw != 0;
    return true;
  }

  [[nodiscard]] bool read_timer_state(Timers::State& out) {
    if (!read_u64(out.cycle_counter)) {
      return false;
    }
    for (Timers::TimerState& timer : out.timers) {
      if (!read_u16(timer.counter) || !read_u16(timer.reload) ||
          !read_u16(timer.control) || !read_u64(timer.prescaler_remainder) ||
          !read_u64(timer.overflow_count) ||
          !read_u32(timer.enable_delay_cycles) ||
          !read_u16(timer.last_enable_phase) || !read_bool(timer.just_enabled)) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool read_interrupt_state(InterruptController& interrupts) {
    std::uint16_t interrupt_enable = 0;
    std::uint16_t interrupt_flags = 0;
    bool master_enabled = false;
    if (!read_u16(interrupt_enable) || !read_u16(interrupt_flags) ||
        !read_bool(master_enabled) ||
        (interrupt_enable & ~InterruptController::kSupportedMask) != 0 ||
        (interrupt_flags & ~InterruptController::kSupportedMask) != 0) {
      return false;
    }

    interrupts.write_interrupt_enable(interrupt_enable);
    for (std::uint8_t bit = 0; bit < 14U; ++bit) {
      const std::uint16_t mask = static_cast<std::uint16_t>(1U << bit);
      if ((interrupt_flags & mask) != 0) {
        interrupts.request(static_cast<InterruptSource>(bit));
      }
    }
    interrupts.set_master_enabled(master_enabled);
    return true;
  }

  [[nodiscard]] bool at_end() const {
    return offset_ == bytes_.size();
  }

 private:
  const std::vector<std::uint8_t>& bytes_;
  std::size_t offset_ = 0;
};

}  // namespace

std::vector<std::uint8_t> SaveStateCodec::encode(const CoreSession& session) {
  std::vector<std::uint8_t> out;
  write_u32(out, kMagic);
  write_u32(out, kVersion);
  write_u64(out, session.state_hash());
  for (std::uint8_t index = 0; index < Arm7tdmi::kRegisterCount; ++index) {
    write_u32(out, session.cpu().register_value(index));
  }
  write_u32(out, session.cpu().cpsr());
  write_u16(out, session.waitcnt().read_control());
  write_u16(out, session.keypad().pressed_mask());
  write_u16(out, session.keypad().keycnt());
  write_u8(out, static_cast<std::uint8_t>(session.bios().mode()));
  write_interrupt_state(out, session.interrupts());
  const CoreSchedulerState scheduler = session.scheduler().save_state();
  write_u64(out, scheduler.scheduler_cycles);
  write_u8(out, scheduler.halted ? 1U : 0U);
  write_timer_state(out, session.timers().save_state());
  write_bytes(out, session.memory().export_game_pak_rom());
  write_u8(out, static_cast<std::uint8_t>(session.memory().game_pak_save_type()));
  write_bytes(out, session.memory().export_game_pak_save());
  return out;
}

SaveStateDecodeResult SaveStateCodec::decode_into(CoreSession& session,
                                                  const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < 20) {
    return {SaveStateDecodeStatus::too_small, 0, 0};
  }

  Reader reader(bytes);
  std::uint32_t magic = 0;
  std::uint32_t version = 0;
  std::uint64_t state_hash = 0;
  if (!reader.read_u32(magic) || magic != kMagic) {
    return {SaveStateDecodeStatus::bad_magic, version, state_hash};
  }
  if (!reader.read_u32(version)) {
    return {SaveStateDecodeStatus::corrupt_payload, version, state_hash};
  }
  if (version != kVersion) {
    return {SaveStateDecodeStatus::unsupported_version, version, state_hash};
  }
  if (!reader.read_u64(state_hash)) {
    return {SaveStateDecodeStatus::corrupt_payload, version, state_hash};
  }

  std::array<std::uint32_t, Arm7tdmi::kRegisterCount> registers{};
  for (std::uint8_t index = 0; index < Arm7tdmi::kRegisterCount; ++index) {
    if (!reader.read_u32(registers.at(index))) {
      return {SaveStateDecodeStatus::corrupt_payload, version, state_hash};
    }
  }
  std::uint32_t cpsr = 0;
  std::uint16_t waitcnt = 0;
  std::uint16_t pressed = 0;
  std::uint16_t keycnt = 0;
  std::uint8_t bios_mode = 0;
  InterruptController interrupts_state;
  std::uint64_t scheduler_cycles = 0;
  std::uint8_t halted = 0;
  Timers::State timers_state{};
  std::vector<std::uint8_t> rom;
  std::uint8_t save_type_raw = 0;
  std::vector<std::uint8_t> save;
  if (!reader.read_u32(cpsr) || !reader.read_u16(waitcnt) || !reader.read_u16(pressed) ||
      !reader.read_u16(keycnt) || !reader.read_u8(bios_mode) ||
      !reader.read_interrupt_state(interrupts_state) ||
      !reader.read_u64(scheduler_cycles) ||
      !reader.read_u8(halted) || !reader.read_timer_state(timers_state) ||
      !reader.read_bytes(rom) ||
      !reader.read_u8(save_type_raw) || !reader.read_bytes(save) || !reader.at_end()) {
    return {SaveStateDecodeStatus::corrupt_payload, version, state_hash};
  }

  session.reset();
  if (!session.cpu().set_cpsr(cpsr) || !session.keypad().set_pressed_mask(pressed)) {
    return {SaveStateDecodeStatus::restore_rejected, version, state_hash};
  }
  for (std::uint8_t index = 0; index < Arm7tdmi::kRegisterCount; ++index) {
    session.cpu().set_register(index, registers.at(index));
  }
  session.waitcnt().write_control(waitcnt);
  session.keypad().write_keycnt(keycnt);
  if (bios_mode > static_cast<std::uint8_t>(BiosExecutionMode::hle)) {
    return {SaveStateDecodeStatus::restore_rejected, version, state_hash};
  }
  session.bios().set_mode(static_cast<BiosExecutionMode>(bios_mode));
  session.interrupts() = interrupts_state;
  if (!rom.empty() && !session.memory().load_game_pak_rom(rom)) {
    return {SaveStateDecodeStatus::restore_rejected, version, state_hash};
  }

  const GamePakSaveType save_type = static_cast<GamePakSaveType>(save_type_raw);
  if (save_type != GamePakSaveType::none) {
    if (!session.memory().import_game_pak_save(save_type, save)) {
      return {SaveStateDecodeStatus::restore_rejected, version, state_hash};
    }
  }
  if (!session.timers().load_state(timers_state)) {
    return {SaveStateDecodeStatus::restore_rejected, version, state_hash};
  }
  CoreSchedulerState scheduler_state{};
  scheduler_state.scheduler_cycles = scheduler_cycles;
  scheduler_state.halted = halted != 0;
  session.scheduler().load_state(scheduler_state);
  return {SaveStateDecodeStatus::ok, version, state_hash};
}

}  // namespace gba::core
