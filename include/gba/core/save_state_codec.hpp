#pragma once

#include "gba/core/memory_bus.hpp"

#include <cstdint>
#include <vector>

namespace gba::core {

class CoreSession;

enum class SaveStateDecodeStatus : std::uint8_t {
  ok,
  too_small,
  bad_magic,
  unsupported_version,
  corrupt_payload,
  restore_rejected,
  // Decoded payload is structurally valid and passed every component loader
  // policy, but the restored machine hashes to a different value than the
  // hash stored in the blob header. The target session is NOT mutated.
  state_hash_mismatch,
};

struct SaveStateDecodeResult {
  SaveStateDecodeStatus status = SaveStateDecodeStatus::ok;
  std::uint32_t version = 0;
  std::uint64_t encoded_state_hash = 0;
};

// Wire format (little-endian, fixed-width, no padding):
//   u32 magic "GBSS", u32 version, u64 CoreSession state_hash, then the full
//   machine snapshot in a fixed field order: CPU registers/flags/banks,
//   MemoryBus RAM regions + cartridge blobs + flash FSM + open-bus latch +
//   debug surfaces, InterruptController IE/IF/IME, Timers, DMA channels,
//   PPU timing registers, APU channels/FIFOs/buffers, keypad, WAITCNT, BIOS
//   mode, serial/SIO IO state, and all 28 CoreSchedulerState fields.
// Version history:
//   1-2: partial snapshots (visible CPU regs + CPSR, WAITCNT, keypad, IE/IF/
//        IME, scheduler_cycles + halted, Timers incl. prescaler_remainder,
//        ROM/save/type only). No longer decodable.
//   3  : complete-machine capture; every component serialized through its
//        own State struct. TimerState::prescaler_remainder dropped.
class SaveStateCodec {
 public:
  static constexpr std::uint32_t kMagic = 0x53534247;  // GBSS, little-endian.
  static constexpr std::uint32_t kVersion = 3;

  [[nodiscard]] static std::vector<std::uint8_t> encode(const CoreSession& session);
  [[nodiscard]] static SaveStateDecodeResult decode_into(
      CoreSession& session, const std::vector<std::uint8_t>& bytes);
};

}  // namespace gba::core
