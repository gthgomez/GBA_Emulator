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
  const CoreSchedulerState scheduler = session.scheduler().save_state();
  write_u64(out, scheduler.scheduler_cycles);
  write_u8(out, scheduler.halted ? 1U : 0U);
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
  std::uint64_t scheduler_cycles = 0;
  std::uint8_t halted = 0;
  std::vector<std::uint8_t> rom;
  std::uint8_t save_type_raw = 0;
  std::vector<std::uint8_t> save;
  if (!reader.read_u32(cpsr) || !reader.read_u16(waitcnt) || !reader.read_u16(pressed) ||
      !reader.read_u16(keycnt) || !reader.read_u64(scheduler_cycles) ||
      !reader.read_u8(halted) || !reader.read_bytes(rom) ||
      !reader.read_u8(save_type_raw) || !reader.read_bytes(save) || !reader.at_end()) {
    return {SaveStateDecodeStatus::corrupt_payload, version, state_hash};
  }

  session.reset();
  for (std::uint8_t index = 0; index < Arm7tdmi::kRegisterCount; ++index) {
    session.cpu().set_register(index, registers.at(index));
  }
  if (!session.cpu().set_cpsr(cpsr) || !session.keypad().set_pressed_mask(pressed)) {
    return {SaveStateDecodeStatus::restore_rejected, version, state_hash};
  }
  session.waitcnt().write_control(waitcnt);
  session.keypad().write_keycnt(keycnt);
  if (!rom.empty() && !session.memory().load_game_pak_rom(rom)) {
    return {SaveStateDecodeStatus::restore_rejected, version, state_hash};
  }

  const GamePakSaveType save_type = static_cast<GamePakSaveType>(save_type_raw);
  if (save_type != GamePakSaveType::none) {
    if (!session.memory().import_game_pak_save(save_type, save)) {
      return {SaveStateDecodeStatus::restore_rejected, version, state_hash};
    }
  }
  session.scheduler().load_state({scheduler_cycles, halted != 0, std::nullopt,
                                  std::nullopt, std::nullopt, 0, std::nullopt,
                                  std::nullopt});
  return {SaveStateDecodeStatus::ok, version, state_hash};
}

}  // namespace gba::core
