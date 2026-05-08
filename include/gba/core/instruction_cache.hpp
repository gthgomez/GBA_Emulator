#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace gba::core {

enum class PredecodedInstructionSet : std::uint8_t {
  arm,
  thumb,
};

enum class PredecodedOperation : std::uint8_t {
  unsupported,
  arm_data_processing_immediate,
  arm_branch,
  arm_single_data_transfer,
  arm_swi,
  thumb_alu,
  thumb_branch,
  thumb_memory,
  thumb_stack,
  thumb_swi,
};

struct PredecodedInstruction {
  PredecodedInstructionSet instruction_set = PredecodedInstructionSet::arm;
  PredecodedOperation operation = PredecodedOperation::unsupported;
  std::uint32_t address = 0;
  std::uint32_t raw = 0;
  std::uint8_t width_bytes = 4;
  bool supported = false;
};

class InstructionCache {
 public:
  explicit InstructionCache(std::size_t capacity = 256);

  void clear();
  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] std::size_t capacity() const;
  [[nodiscard]] std::uint64_t hits() const;
  [[nodiscard]] std::uint64_t misses() const;
  [[nodiscard]] PredecodedInstruction lookup_or_decode_arm(std::uint32_t address,
                                                           std::uint32_t raw);
  [[nodiscard]] PredecodedInstruction lookup_or_decode_thumb(std::uint32_t address,
                                                             std::uint16_t raw);

 private:
  struct Entry {
    bool valid = false;
    PredecodedInstruction decoded;
  };

  std::vector<Entry> entries_;
  std::uint64_t hits_ = 0;
  std::uint64_t misses_ = 0;

  [[nodiscard]] std::size_t slot(std::uint32_t address, std::uint8_t width_bytes) const;
};

[[nodiscard]] PredecodedInstruction predecode_arm(std::uint32_t address,
                                                  std::uint32_t raw);
[[nodiscard]] PredecodedInstruction predecode_thumb(std::uint32_t address,
                                                    std::uint16_t raw);

}  // namespace gba::core
