#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gba::core {

class StateHasher {
 public:
  static constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
  static constexpr std::uint64_t kPrime = 1099511628211ULL;

  void add_u8(std::uint8_t value) {
    hash_ ^= value;
    hash_ *= kPrime;
  }

  void add_u16(std::uint16_t value) {
    add_u8(static_cast<std::uint8_t>(value & 0xFFU));
    add_u8(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  }

  void add_u32(std::uint32_t value) {
    for (std::uint8_t byte = 0; byte < 4; ++byte) {
      add_u8(static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xFFU));
    }
  }

  void add_u64(std::uint64_t value) {
    for (std::uint8_t byte = 0; byte < 8; ++byte) {
      add_u8(static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xFFU));
    }
  }

  void add_bool(bool value) {
    add_u8(value ? 1 : 0);
  }

  template <typename Container>
  void add_bytes(const Container& bytes) {
    add_u64(static_cast<std::uint64_t>(bytes.size()));
    for (const auto byte : bytes) {
      add_u8(static_cast<std::uint8_t>(byte));
    }
  }

  [[nodiscard]] std::uint64_t value() const {
    return hash_;
  }

 private:
  std::uint64_t hash_ = kOffsetBasis;
};

[[nodiscard]] inline std::uint64_t combine_state_hash(std::uint64_t left,
                                                      std::uint64_t right) {
  StateHasher hasher;
  hasher.add_u64(left);
  hasher.add_u64(right);
  return hasher.value();
}

}  // namespace gba::core
