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
};

struct SaveStateDecodeResult {
  SaveStateDecodeStatus status = SaveStateDecodeStatus::ok;
  std::uint32_t version = 0;
  std::uint64_t encoded_state_hash = 0;
};

class SaveStateCodec {
 public:
  static constexpr std::uint32_t kMagic = 0x53534247;  // GBSS, little-endian.
  static constexpr std::uint32_t kVersion = 1;

  [[nodiscard]] static std::vector<std::uint8_t> encode(const CoreSession& session);
  [[nodiscard]] static SaveStateDecodeResult decode_into(
      CoreSession& session, const std::vector<std::uint8_t>& bytes);
};

}  // namespace gba::core
