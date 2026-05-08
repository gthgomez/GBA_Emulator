#include "gba/core/instruction_cache.hpp"

#include "gba/core/arm7tdmi.hpp"

#include <algorithm>

namespace gba::core {

PredecodedInstruction predecode_arm(std::uint32_t address, std::uint32_t raw) {
  PredecodedOperation operation = PredecodedOperation::unsupported;
  if (Arm7tdmi::can_decode_data_processing_immediate(raw)) {
    operation = PredecodedOperation::arm_data_processing_immediate;
  } else if (Arm7tdmi::can_decode_branch(raw)) {
    operation = PredecodedOperation::arm_branch;
  } else if (Arm7tdmi::can_decode_single_data_transfer_immediate(raw) ||
             Arm7tdmi::can_decode_single_data_transfer_register(raw)) {
    operation = PredecodedOperation::arm_single_data_transfer;
  } else if (Arm7tdmi::can_decode_software_interrupt(raw)) {
    operation = PredecodedOperation::arm_swi;
  }
  return {PredecodedInstructionSet::arm, operation, address, raw, 4,
          operation != PredecodedOperation::unsupported};
}

PredecodedInstruction predecode_thumb(std::uint32_t address, std::uint16_t raw) {
  PredecodedOperation operation = PredecodedOperation::unsupported;
  if (Arm7tdmi::can_decode_thumb_add_subtract(raw) ||
      Arm7tdmi::can_decode_thumb_immediate(raw) ||
      Arm7tdmi::can_decode_thumb_high_register(raw) ||
      Arm7tdmi::can_decode_thumb_load_address(raw)) {
    operation = PredecodedOperation::thumb_alu;
  } else if (Arm7tdmi::can_decode_thumb_unconditional_branch(raw) ||
             Arm7tdmi::can_decode_thumb_conditional_branch(raw)) {
    operation = PredecodedOperation::thumb_branch;
  } else if (Arm7tdmi::can_decode_thumb_memory_transfer(raw)) {
    operation = PredecodedOperation::thumb_memory;
  } else if (Arm7tdmi::can_decode_thumb_stack_transfer(raw)) {
    operation = PredecodedOperation::thumb_stack;
  } else if (Arm7tdmi::can_decode_thumb_software_interrupt(raw)) {
    operation = PredecodedOperation::thumb_swi;
  }
  return {PredecodedInstructionSet::thumb, operation, address, raw, 2,
          operation != PredecodedOperation::unsupported};
}

InstructionCache::InstructionCache(std::size_t capacity)
    : entries_(std::max<std::size_t>(capacity, 1U)) {}

void InstructionCache::clear() {
  for (Entry& entry : entries_) {
    entry.valid = false;
  }
  hits_ = 0;
  misses_ = 0;
}

std::size_t InstructionCache::size() const {
  std::size_t count = 0;
  for (const Entry& entry : entries_) {
    if (entry.valid) {
      ++count;
    }
  }
  return count;
}

std::size_t InstructionCache::capacity() const {
  return entries_.size();
}

std::uint64_t InstructionCache::hits() const {
  return hits_;
}

std::uint64_t InstructionCache::misses() const {
  return misses_;
}

PredecodedInstruction InstructionCache::lookup_or_decode_arm(std::uint32_t address,
                                                             std::uint32_t raw) {
  const std::size_t index = slot(address, 4);
  Entry& entry = entries_.at(index);
  if (entry.valid && entry.decoded.address == address && entry.decoded.raw == raw &&
      entry.decoded.instruction_set == PredecodedInstructionSet::arm) {
    ++hits_;
    return entry.decoded;
  }
  ++misses_;
  entry = {true, predecode_arm(address, raw)};
  return entry.decoded;
}

PredecodedInstruction InstructionCache::lookup_or_decode_thumb(std::uint32_t address,
                                                               std::uint16_t raw) {
  const std::size_t index = slot(address, 2);
  Entry& entry = entries_.at(index);
  if (entry.valid && entry.decoded.address == address && entry.decoded.raw == raw &&
      entry.decoded.instruction_set == PredecodedInstructionSet::thumb) {
    ++hits_;
    return entry.decoded;
  }
  ++misses_;
  entry = {true, predecode_thumb(address, raw)};
  return entry.decoded;
}

std::size_t InstructionCache::slot(std::uint32_t address, std::uint8_t width_bytes) const {
  return (address / width_bytes) % entries_.size();
}

}  // namespace gba::core
