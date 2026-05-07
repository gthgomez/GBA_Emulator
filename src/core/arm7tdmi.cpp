#include "gba/core/arm7tdmi.hpp"

#include "gba/core/memory_bus.hpp"
#include "gba/core/state_hash.hpp"
#include "gba/core/wait_state_control.hpp"

#include <stdexcept>

namespace gba::core {
namespace {

struct ShifterResult {
  std::uint32_t value;
  bool carry_valid;
  bool carry;
};

struct BlockTransferAddress {
  std::uint32_t first;
  std::uint32_t write_back;
};

constexpr std::uint32_t kNegativeFlag = 0x80000000;
constexpr std::uint32_t kZeroFlag = 0x40000000;
constexpr std::uint32_t kCarryFlag = 0x20000000;
constexpr std::uint32_t kOverflowFlag = 0x10000000;
constexpr std::uint32_t kThumbStateFlag = 0x00000020;
constexpr std::uint32_t kFiqDisableFlag = 0x00000040;
constexpr std::uint32_t kIrqDisableFlag = 0x00000080;
constexpr std::uint32_t kModeMask = 0x0000001F;

[[nodiscard]] std::uint8_t bits(std::uint32_t value, std::uint8_t shift, std::uint32_t mask) {
  return static_cast<std::uint8_t>((value >> shift) & mask);
}

[[nodiscard]] std::uint32_t rotate_right(std::uint32_t value, std::uint8_t amount) {
  const std::uint8_t shift = static_cast<std::uint8_t>(amount % 32);
  if (shift == 0) {
    return value;
  }
  return (value >> shift) | (value << (32 - shift));
}

[[nodiscard]] bool supported_condition(std::uint8_t condition) {
  return condition <= static_cast<std::uint8_t>(ArmCondition::al);
}

[[nodiscard]] std::optional<CpuMode> decode_cpu_mode(std::uint32_t value) {
  const auto mode = static_cast<CpuMode>(value & kModeMask);
  switch (mode) {
    case CpuMode::user:
    case CpuMode::fiq:
    case CpuMode::irq:
    case CpuMode::supervisor:
    case CpuMode::abort:
    case CpuMode::undefined:
    case CpuMode::system:
      return mode;
  }
  return std::nullopt;
}

[[nodiscard]] bool mode_has_spsr(CpuMode mode) {
  return mode == CpuMode::fiq || mode == CpuMode::irq ||
         mode == CpuMode::supervisor || mode == CpuMode::abort ||
         mode == CpuMode::undefined;
}

[[nodiscard]] std::int32_t sign_extend_branch_offset(std::uint32_t instruction) {
  std::uint32_t raw = instruction & 0x00FFFFFFU;
  if ((raw & 0x00800000U) != 0) {
    raw |= 0xFF000000U;
  }
  return static_cast<std::int32_t>(raw) * 4;
}

[[nodiscard]] std::int32_t sign_extend_thumb_offset(std::uint16_t raw, std::uint8_t bits) {
  const std::uint16_t sign_bit = static_cast<std::uint16_t>(1U << (bits - 1));
  const std::uint16_t mask = static_cast<std::uint16_t>((1U << bits) - 1U);
  std::uint32_t extended = raw & mask;
  if ((extended & sign_bit) != 0) {
    extended |= ~static_cast<std::uint32_t>(mask);
  }
  return static_cast<std::int32_t>(extended) * 2;
}

[[nodiscard]] bool supported_data_processing_opcode(std::uint8_t opcode) {
  return opcode == static_cast<std::uint8_t>(ArmOpcode::and_) ||
         opcode == static_cast<std::uint8_t>(ArmOpcode::eor) ||
         opcode == static_cast<std::uint8_t>(ArmOpcode::sub) ||
         opcode == static_cast<std::uint8_t>(ArmOpcode::rsb) ||
         opcode == static_cast<std::uint8_t>(ArmOpcode::add) ||
         opcode == static_cast<std::uint8_t>(ArmOpcode::adc) ||
         opcode == static_cast<std::uint8_t>(ArmOpcode::sbc) ||
         opcode == static_cast<std::uint8_t>(ArmOpcode::rsc) ||
         opcode == static_cast<std::uint8_t>(ArmOpcode::tst) ||
         opcode == static_cast<std::uint8_t>(ArmOpcode::teq) ||
         opcode == static_cast<std::uint8_t>(ArmOpcode::cmp) ||
         opcode == static_cast<std::uint8_t>(ArmOpcode::cmn) ||
         opcode == static_cast<std::uint8_t>(ArmOpcode::orr) ||
         opcode == static_cast<std::uint8_t>(ArmOpcode::mov) ||
         opcode == static_cast<std::uint8_t>(ArmOpcode::bic) ||
         opcode == static_cast<std::uint8_t>(ArmOpcode::mvn);
}

[[nodiscard]] bool supported_thumb_branch_condition(std::uint8_t condition) {
  return condition == static_cast<std::uint8_t>(ArmCondition::eq) ||
         condition == static_cast<std::uint8_t>(ArmCondition::ne);
}

[[nodiscard]] ShifterResult apply_immediate_shift(std::uint32_t value, ArmShiftType type,
                                                  std::uint8_t amount,
                                                  bool carry_in = false) {
  switch (type) {
    case ArmShiftType::lsl:
      if (amount == 0) {
        return {value, false, false};
      }
      return {value << amount, true, ((value >> (32 - amount)) & 0x1U) == 1};
    case ArmShiftType::lsr:
      if (amount == 0) {
        return {0, true, (value & 0x80000000U) != 0};
      }
      return {value >> amount, true, ((value >> (amount - 1)) & 0x1U) == 1};
    case ArmShiftType::asr:
      if (amount == 0) {
        return {(value & 0x80000000U) != 0 ? 0xFFFFFFFFU : 0, true,
                (value & 0x80000000U) != 0};
      }
      if ((value & 0x80000000U) == 0) {
        return {value >> amount, true, ((value >> (amount - 1)) & 0x1U) == 1};
      }
      return {(value >> amount) | (0xFFFFFFFFU << (32 - amount)), true,
              ((value >> (amount - 1)) & 0x1U) == 1};
    case ArmShiftType::ror:
      if (amount == 0) {
        return {(carry_in ? 0x80000000U : 0U) | (value >> 1), true,
                (value & 0x1U) != 0};
      }
      return {rotate_right(value, amount), true, (rotate_right(value, amount) & 0x80000000U) != 0};
  }
  return {value, false, false};
}

[[nodiscard]] ShifterResult apply_register_shift(std::uint32_t value, ArmShiftType type,
                                                 std::uint8_t amount) {
  if (amount == 0) {
    return {value, false, false};
  }

  switch (type) {
    case ArmShiftType::lsl:
      if (amount < 32) {
        return {value << amount, true, ((value >> (32 - amount)) & 0x1U) == 1};
      }
      if (amount == 32) {
        return {0, true, (value & 0x1U) != 0};
      }
      return {0, true, false};
    case ArmShiftType::lsr:
      if (amount < 32) {
        return {value >> amount, true, ((value >> (amount - 1)) & 0x1U) == 1};
      }
      if (amount == 32) {
        return {0, true, (value & 0x80000000U) != 0};
      }
      return {0, true, false};
    case ArmShiftType::asr:
      if (amount < 32) {
        if ((value & 0x80000000U) == 0) {
          return {value >> amount, true, ((value >> (amount - 1)) & 0x1U) == 1};
        }
        return {(value >> amount) | (0xFFFFFFFFU << (32 - amount)), true,
                ((value >> (amount - 1)) & 0x1U) == 1};
      }
      return {(value & 0x80000000U) != 0 ? 0xFFFFFFFFU : 0, true,
              (value & 0x80000000U) != 0};
    case ArmShiftType::ror: {
      const std::uint8_t rotate = static_cast<std::uint8_t>(amount % 32);
      const std::uint32_t result = rotate == 0 ? value : rotate_right(value, rotate);
      return {result, true, (result & 0x80000000U) != 0};
    }
  }
  return {value, false, false};
}

[[nodiscard]] bool supported_transfer_addressing(bool pre_index, bool write_back) {
  return pre_index || !write_back;
}

[[nodiscard]] std::uint32_t psr_field_mask(std::uint8_t field_mask) {
  std::uint32_t mask = 0;
  if ((field_mask & 0x1U) != 0) {
    mask |= 0x000000FFU;
  }
  if ((field_mask & 0x2U) != 0) {
    mask |= 0x0000FF00U;
  }
  if ((field_mask & 0x4U) != 0) {
    mask |= 0x00FF0000U;
  }
  if ((field_mask & 0x8U) != 0) {
    mask |= 0xFF000000U;
  }
  return mask;
}

[[nodiscard]] bool transfer_needs_write_back(bool pre_index, bool write_back) {
  return !pre_index || write_back;
}

[[nodiscard]] std::uint32_t offset_transfer_address(std::uint32_t base, std::uint32_t offset,
                                                    bool up) {
  return up ? base + offset : base - offset;
}

[[nodiscard]] std::uint8_t count_registers(std::uint16_t register_list) {
  std::uint8_t count = 0;
  while (register_list != 0) {
    count = static_cast<std::uint8_t>(count + (register_list & 0x1U));
    register_list = static_cast<std::uint16_t>(register_list >> 1);
  }
  return count;
}

[[nodiscard]] bool register_list_contains(std::uint16_t register_list, std::uint8_t index) {
  return (register_list & (static_cast<std::uint16_t>(1U) << index)) != 0;
}

[[nodiscard]] bool block_transfer_has_pc(std::uint16_t register_list) {
  return register_list_contains(register_list, Arm7tdmi::kPc);
}

[[nodiscard]] BlockTransferAddress block_transfer_address(std::uint32_t base,
                                                          std::uint8_t register_count,
                                                          bool pre_index, bool up) {
  const std::uint32_t byte_count = static_cast<std::uint32_t>(register_count) * 4U;
  if (up) {
    return {pre_index ? base + 4U : base, base + byte_count};
  }
  return {pre_index ? base - byte_count : base - byte_count + 4U, base - byte_count};
}

[[nodiscard]] ArmCycleEstimate data_processing_cycle_estimate(std::uint32_t instruction) {
  const bool register_shift = ((instruction >> 4) & 0x1U) == 1 &&
                              ((instruction >> 7) & 0x1U) == 0;
  const bool writes_pc = bits(instruction, 12, 0xFU) == Arm7tdmi::kPc;
  return {
      static_cast<std::uint8_t>(writes_pc ? 2U : 1U),
      static_cast<std::uint8_t>(writes_pc ? 1U : 0U),
      static_cast<std::uint8_t>(register_shift ? 1U : 0U),
      false,
  };
}

[[nodiscard]] ArmCycleEstimate branch_cycle_estimate() {
  return {2, 1, 0, false};
}

[[nodiscard]] ArmCycleEstimate single_transfer_cycle_estimate(std::uint32_t instruction) {
  const bool load = ((instruction >> 20) & 0x1U) == 1;
  const bool loads_pc = load && bits(instruction, 12, 0xFU) == Arm7tdmi::kPc;
  if (load) {
    return {
        static_cast<std::uint8_t>(loads_pc ? 2U : 1U),
        static_cast<std::uint8_t>(loads_pc ? 2U : 1U),
        1,
        false,
    };
  }
  return {0, 2, 0, false};
}

[[nodiscard]] ArmCycleEstimate block_transfer_cycle_estimate(std::uint32_t instruction) {
  const std::uint16_t register_list = static_cast<std::uint16_t>(instruction & 0xFFFFU);
  const std::uint8_t register_count = count_registers(register_list);
  const bool load = ((instruction >> 20) & 0x1U) == 1;
  const bool loads_pc = load && block_transfer_has_pc(register_list);
  if (load) {
    return {
        static_cast<std::uint8_t>(register_count + (loads_pc ? 1U : 0U)),
        static_cast<std::uint8_t>(1U + (loads_pc ? 1U : 0U)),
        1,
        false,
    };
  }
  return {static_cast<std::uint8_t>(register_count - 1U), 2, 0, false};
}

[[nodiscard]] ArmCycleEstimate multiply_cycle_estimate(bool accumulate_or_long,
                                                       bool long_accumulate) {
  return {
      1,
      0,
      static_cast<std::uint8_t>(accumulate_or_long ? (long_accumulate ? 2U : 1U) : 0U),
      true,
  };
}

[[nodiscard]] bool can_estimate_memory_elapsed_cycles(std::uint32_t instruction) {
  return Arm7tdmi::can_decode_single_data_transfer_immediate(instruction) ||
         Arm7tdmi::can_decode_single_data_transfer_register(instruction) ||
         Arm7tdmi::can_decode_halfword_data_transfer_immediate(instruction) ||
         Arm7tdmi::can_decode_halfword_data_transfer_register(instruction) ||
         Arm7tdmi::can_decode_block_data_transfer(instruction) ||
         Arm7tdmi::can_decode_swap(instruction);
}

[[nodiscard]] AccessWidth transfer_access_width(std::uint32_t instruction) {
  if (Arm7tdmi::can_decode_swap(instruction)) {
    return ((instruction >> 22) & 0x1U) == 1 ? AccessWidth::byte : AccessWidth::word;
  }

  if (Arm7tdmi::can_decode_block_data_transfer(instruction)) {
    return AccessWidth::word;
  }

  if (Arm7tdmi::can_decode_single_data_transfer_immediate(instruction) ||
      Arm7tdmi::can_decode_single_data_transfer_register(instruction)) {
    return ((instruction >> 22) & 0x1U) == 1 ? AccessWidth::byte : AccessWidth::word;
  }

  if (Arm7tdmi::can_decode_halfword_data_transfer_immediate(instruction) ||
      Arm7tdmi::can_decode_halfword_data_transfer_register(instruction)) {
    const std::uint8_t transfer_kind = bits(instruction, 5, 0x3U);
    return transfer_kind == 0x2U ? AccessWidth::byte : AccessWidth::halfword;
  }

  return AccessWidth::word;
}

[[nodiscard]] ArmElapsedCycleEstimate compose_elapsed_cycles(
    const ArmCycleEstimate& cycles, const MemoryAccessTiming& timing) {
  return {
      static_cast<std::uint32_t>(cycles.sequential) * timing.sequential +
          static_cast<std::uint32_t>(cycles.nonsequential) * timing.nonsequential +
          cycles.internal,
      cycles.data_dependent,
      true,
  };
}

[[nodiscard]] bool has_elapsed_timing_for_estimate(std::uint32_t address,
                                                   const MemoryAccessTiming& timing,
                                                   bool waitcnt_aware) {
  if (timing.readable || timing.writable) {
    return true;
  }
  if (!waitcnt_aware || timing.nonsequential == 0 || timing.sequential == 0) {
    return false;
  }

  const Region region = MemoryBus::describe(address).region;
  return region == Region::game_pak_rom || region == Region::game_pak_save;
}

[[nodiscard]] std::optional<ArmElapsedCycleEstimate> estimate_arm_elapsed_cycles_with_timing(
    std::uint32_t instruction, std::uint32_t data_address,
    const MemoryAccessTiming& timing, bool waitcnt_aware) {
  const std::optional<ArmCycleEstimate> cycles = Arm7tdmi::estimate_arm_cycles(instruction);
  if (!cycles.has_value()) {
    return std::nullopt;
  }

  if (!can_estimate_memory_elapsed_cycles(instruction)) {
    return ArmElapsedCycleEstimate{
        static_cast<std::uint32_t>(cycles.value().sequential) +
            static_cast<std::uint32_t>(cycles.value().nonsequential) +
            cycles.value().internal,
        cycles.value().data_dependent,
        false,
    };
  }

  if (!has_elapsed_timing_for_estimate(data_address, timing, waitcnt_aware)) {
    return std::nullopt;
  }

  return compose_elapsed_cycles(cycles.value(), timing);
}

}  // namespace

Arm7tdmi::Arm7tdmi() {
  reset();
}

bool Arm7tdmi::can_decode_data_processing_immediate(std::uint32_t instruction) {
  if (can_decode_psr_transfer(instruction)) {
    return false;
  }

  const bool data_processing_group = ((instruction >> 26) & 0x3U) == 0;
  const bool immediate_operand = ((instruction >> 25) & 0x1U) == 1;
  if (!data_processing_group || !immediate_operand) {
    return false;
  }

  const std::uint8_t opcode = bits(instruction, 21, 0xFU);
  return supported_condition(bits(instruction, 28, 0xFU)) && supported_data_processing_opcode(opcode);
}

DecodedArmInstruction Arm7tdmi::decode_data_processing_immediate(std::uint32_t instruction) {
  if (!can_decode_data_processing_immediate(instruction)) {
    throw std::invalid_argument("unsupported ARM data-processing immediate instruction");
  }

  const std::uint8_t rotate = bits(instruction, 8, 0xFU);
  const std::uint32_t immediate = instruction & 0xFFU;
  const std::uint32_t operand2 = rotate_right(immediate, static_cast<std::uint8_t>(rotate * 2));
  return {
      static_cast<ArmCondition>(bits(instruction, 28, 0xFU)),
      static_cast<ArmOpcode>(bits(instruction, 21, 0xFU)),
      ((instruction >> 20) & 0x1U) == 1,
      bits(instruction, 16, 0xFU),
      bits(instruction, 12, 0xFU),
      operand2,
      rotate != 0,
      (operand2 & 0x80000000U) != 0,
  };
}

bool Arm7tdmi::can_decode_data_processing_register_shift(std::uint32_t instruction) {
  if (can_decode_psr_transfer(instruction) || can_decode_swap(instruction)) {
    return false;
  }

  const bool data_processing_group = ((instruction >> 26) & 0x3U) == 0;
  const bool register_operand = ((instruction >> 25) & 0x1U) == 0;
  const bool immediate_shift = ((instruction >> 4) & 0x1U) == 0;
  const bool register_shift = ((instruction >> 4) & 0x1U) == 1 &&
                              ((instruction >> 7) & 0x1U) == 0;
  if (!data_processing_group || !register_operand || (!immediate_shift && !register_shift)) {
    return false;
  }

  const ArmShiftType shift_type = static_cast<ArmShiftType>(bits(instruction, 5, 0x3U));
  const std::uint8_t opcode = bits(instruction, 21, 0xFU);
  (void)shift_type;
  return supported_condition(bits(instruction, 28, 0xFU)) &&
         supported_data_processing_opcode(opcode);
}

DecodedArmInstruction Arm7tdmi::decode_data_processing_register_shift(
    std::uint32_t instruction, std::uint32_t rm_value) {
  return decode_data_processing_register_shift(instruction, rm_value, false);
}

DecodedArmInstruction Arm7tdmi::decode_data_processing_register_shift(
    std::uint32_t instruction, std::uint32_t rm_value, bool carry_in) {
  const bool immediate_shift = ((instruction >> 4) & 0x1U) == 0;
  if (!can_decode_data_processing_register_shift(instruction) || !immediate_shift) {
    throw std::invalid_argument("unsupported ARM data-processing register-shift instruction");
  }

  const ArmShiftType shift_type = static_cast<ArmShiftType>(bits(instruction, 5, 0x3U));
  const std::uint8_t shift_amount = bits(instruction, 7, 0x1FU);
  const ShifterResult shifter =
      apply_immediate_shift(rm_value, shift_type, shift_amount, carry_in);
  return {
      static_cast<ArmCondition>(bits(instruction, 28, 0xFU)),
      static_cast<ArmOpcode>(bits(instruction, 21, 0xFU)),
      ((instruction >> 20) & 0x1U) == 1,
      bits(instruction, 16, 0xFU),
      bits(instruction, 12, 0xFU),
      shifter.value,
      shifter.carry_valid,
      shifter.carry,
  };
}

DecodedArmInstruction Arm7tdmi::decode_data_processing_register_shift(
    std::uint32_t instruction, std::uint32_t rm_value, std::uint32_t rs_value) {
  const bool register_shift = ((instruction >> 4) & 0x1U) == 1 &&
                              ((instruction >> 7) & 0x1U) == 0;
  if (!can_decode_data_processing_register_shift(instruction) || !register_shift) {
    throw std::invalid_argument("unsupported ARM data-processing register-controlled shift instruction");
  }

  const ArmShiftType shift_type = static_cast<ArmShiftType>(bits(instruction, 5, 0x3U));
  const ShifterResult shifter =
      apply_register_shift(rm_value, shift_type, static_cast<std::uint8_t>(rs_value & 0xFFU));
  return {
      static_cast<ArmCondition>(bits(instruction, 28, 0xFU)),
      static_cast<ArmOpcode>(bits(instruction, 21, 0xFU)),
      ((instruction >> 20) & 0x1U) == 1,
      bits(instruction, 16, 0xFU),
      bits(instruction, 12, 0xFU),
      shifter.value,
      shifter.carry_valid,
      shifter.carry,
  };
}

bool Arm7tdmi::can_decode_multiply(std::uint32_t instruction) {
  const bool multiply_group = ((instruction >> 22) & 0x3FU) == 0;
  const bool multiply_shape = ((instruction >> 4) & 0xFU) == 0x9U;
  return multiply_group && multiply_shape && supported_condition(bits(instruction, 28, 0xFU));
}

DecodedMultiplyInstruction Arm7tdmi::decode_multiply(std::uint32_t instruction) {
  if (!can_decode_multiply(instruction)) {
    throw std::invalid_argument("unsupported ARM multiply instruction");
  }

  return {
      static_cast<ArmCondition>(bits(instruction, 28, 0xFU)),
      ((instruction >> 21) & 0x1U) == 1,
      ((instruction >> 20) & 0x1U) == 1,
      bits(instruction, 16, 0xFU),
      bits(instruction, 12, 0xFU),
      bits(instruction, 8, 0xFU),
      bits(instruction, 0, 0xFU),
  };
}

bool Arm7tdmi::can_decode_multiply_long(std::uint32_t instruction) {
  const bool multiply_long_group = ((instruction >> 23) & 0x1FU) == 0x1U;
  const bool multiply_shape = ((instruction >> 4) & 0xFU) == 0x9U;
  return multiply_long_group && multiply_shape &&
         supported_condition(bits(instruction, 28, 0xFU));
}

DecodedMultiplyLongInstruction Arm7tdmi::decode_multiply_long(std::uint32_t instruction) {
  if (!can_decode_multiply_long(instruction)) {
    throw std::invalid_argument("unsupported ARM multiply-long instruction");
  }

  return {
      static_cast<ArmCondition>(bits(instruction, 28, 0xFU)),
      ((instruction >> 22) & 0x1U) == 1,
      ((instruction >> 21) & 0x1U) == 1,
      ((instruction >> 20) & 0x1U) == 1,
      bits(instruction, 16, 0xFU),
      bits(instruction, 12, 0xFU),
      bits(instruction, 8, 0xFU),
      bits(instruction, 0, 0xFU),
  };
}

bool Arm7tdmi::can_decode_branch(std::uint32_t instruction) {
  const bool branch_group = ((instruction >> 25) & 0x7U) == 0x5U;
  return branch_group && supported_condition(bits(instruction, 28, 0xFU));
}

DecodedBranchInstruction Arm7tdmi::decode_branch(std::uint32_t instruction) {
  if (!can_decode_branch(instruction)) {
    throw std::invalid_argument("unsupported ARM branch instruction");
  }

  return {
      static_cast<ArmCondition>(bits(instruction, 28, 0xFU)),
      ((instruction >> 24) & 0x1U) == 1,
      sign_extend_branch_offset(instruction),
  };
}

[[nodiscard]] std::uint32_t sign_extend8(std::uint8_t value) {
  if ((value & 0x80U) != 0) {
    return 0xFFFFFF00U | value;
  }
  return value;
}

[[nodiscard]] std::uint32_t sign_extend16(std::uint16_t value) {
  if ((value & 0x8000U) != 0) {
    return 0xFFFF0000U | value;
  }
  return value;
}

bool Arm7tdmi::can_decode_single_data_transfer_immediate(std::uint32_t instruction) {
  const bool single_data_transfer_group = ((instruction >> 26) & 0x3U) == 0x1U;
  const bool immediate_offset = ((instruction >> 25) & 0x1U) == 0;
  return single_data_transfer_group && immediate_offset &&
         supported_condition(bits(instruction, 28, 0xFU));
}

DecodedSingleDataTransferInstruction Arm7tdmi::decode_single_data_transfer_immediate(
    std::uint32_t instruction) {
  if (!can_decode_single_data_transfer_immediate(instruction)) {
    throw std::invalid_argument("unsupported ARM single data transfer immediate instruction");
  }

  return {
      static_cast<ArmCondition>(bits(instruction, 28, 0xFU)),
      ((instruction >> 20) & 0x1U) == 1,
      ((instruction >> 22) & 0x1U) == 1,
      ((instruction >> 24) & 0x1U) == 1,
      ((instruction >> 23) & 0x1U) == 1,
      ((instruction >> 21) & 0x1U) == 1,
      bits(instruction, 16, 0xFU),
      bits(instruction, 12, 0xFU),
      instruction & 0xFFFU,
  };
}

bool Arm7tdmi::can_decode_single_data_transfer_register(std::uint32_t instruction) {
  const bool single_data_transfer_group = ((instruction >> 26) & 0x3U) == 0x1U;
  const bool register_offset = ((instruction >> 25) & 0x1U) == 1;
  const bool shift_operand_shape = ((instruction >> 4) & 0x1U) == 0;
  return single_data_transfer_group && register_offset && shift_operand_shape &&
         supported_condition(bits(instruction, 28, 0xFU));
}

DecodedSingleDataTransferInstruction Arm7tdmi::decode_single_data_transfer_register(
    std::uint32_t instruction, std::uint32_t rm_value) {
  return decode_single_data_transfer_register(instruction, rm_value, false);
}

DecodedSingleDataTransferInstruction Arm7tdmi::decode_single_data_transfer_register(
    std::uint32_t instruction, std::uint32_t rm_value, bool carry_in) {
  if (!can_decode_single_data_transfer_register(instruction)) {
    throw std::invalid_argument("unsupported ARM single data transfer register instruction");
  }

  const ArmShiftType shift_type = static_cast<ArmShiftType>(bits(instruction, 5, 0x3U));
  const std::uint8_t shift_amount = bits(instruction, 7, 0x1FU);
  const ShifterResult shifter =
      apply_immediate_shift(rm_value, shift_type, shift_amount, carry_in);
  return {
      static_cast<ArmCondition>(bits(instruction, 28, 0xFU)),
      ((instruction >> 20) & 0x1U) == 1,
      ((instruction >> 22) & 0x1U) == 1,
      ((instruction >> 24) & 0x1U) == 1,
      ((instruction >> 23) & 0x1U) == 1,
      ((instruction >> 21) & 0x1U) == 1,
      bits(instruction, 16, 0xFU),
      bits(instruction, 12, 0xFU),
      shifter.value,
  };
}

bool Arm7tdmi::can_decode_psr_transfer(std::uint32_t instruction) {
  if (!supported_condition(bits(instruction, 28, 0xFU))) {
    return false;
  }

  const std::uint8_t op_group = bits(instruction, 23, 0x1FU);
  const bool mrs = op_group == 0x2U && bits(instruction, 16, 0x3FU) == 0x0FU &&
                   (instruction & 0xFFFU) == 0;
  if (mrs) {
    return true;
  }

  const bool register_msr = op_group == 0x2U && ((instruction >> 21) & 0x1U) == 1 &&
                            ((instruction >> 20) & 0x1U) == 0 &&
                            bits(instruction, 12, 0xFU) == 0xFU &&
                            bits(instruction, 4, 0xFFU) == 0 &&
                            bits(instruction, 16, 0xFU) != 0;
  const bool immediate_msr = op_group == 0x6U && ((instruction >> 21) & 0x1U) == 1 &&
                             ((instruction >> 20) & 0x1U) == 0 &&
                             bits(instruction, 12, 0xFU) == 0xFU &&
                             bits(instruction, 16, 0xFU) != 0;
  return register_msr || immediate_msr;
}

DecodedPsrTransferInstruction Arm7tdmi::decode_psr_transfer(std::uint32_t instruction) {
  if (!can_decode_psr_transfer(instruction)) {
    throw std::invalid_argument("unsupported ARM PSR transfer instruction");
  }

  const bool mrs = bits(instruction, 23, 0x1FU) == 0x2U &&
                   bits(instruction, 16, 0x3FU) == 0x0FU &&
                   (instruction & 0xFFFU) == 0;
  if (mrs) {
    return {
        static_cast<ArmCondition>(bits(instruction, 28, 0xFU)),
        false,
        ((instruction >> 22) & 0x1U) == 1 ? ArmProgramStatusRegister::spsr
                                          : ArmProgramStatusRegister::cpsr,
        bits(instruction, 12, 0xFU),
        0,
        0,
        false,
    };
  }

  const bool immediate_operand = ((instruction >> 25) & 0x1U) == 1;
  std::uint32_t operand = 0;
  std::uint8_t rd_or_rm = bits(instruction, 0, 0xFU);
  if (immediate_operand) {
    const std::uint8_t rotate = bits(instruction, 8, 0xFU);
    operand = rotate_right(instruction & 0xFFU, static_cast<std::uint8_t>(rotate * 2));
    rd_or_rm = 0;
  }

  return {
      static_cast<ArmCondition>(bits(instruction, 28, 0xFU)),
      true,
      ((instruction >> 22) & 0x1U) == 1 ? ArmProgramStatusRegister::spsr
                                        : ArmProgramStatusRegister::cpsr,
      rd_or_rm,
      bits(instruction, 16, 0xFU),
      operand,
      immediate_operand,
  };
}

bool Arm7tdmi::can_decode_software_interrupt(std::uint32_t instruction) {
  return supported_condition(bits(instruction, 28, 0xFU)) &&
         ((instruction >> 24) & 0xFU) == 0xFU;
}

bool Arm7tdmi::can_decode_swap(std::uint32_t instruction) {
  const bool swap_group = bits(instruction, 23, 0x1FU) == 0x2U;
  const bool swap_shape = ((instruction >> 20) & 0x3U) == 0 &&
                          bits(instruction, 4, 0xFFU) == 0x9U;
  const std::uint8_t rn_value = bits(instruction, 16, 0xFU);
  const std::uint8_t rd_value = bits(instruction, 12, 0xFU);
  const std::uint8_t rm_value = bits(instruction, 0, 0xFU);
  return swap_group && swap_shape && supported_condition(bits(instruction, 28, 0xFU)) &&
         rn_value != kPc && rd_value != kPc && rm_value != kPc;
}

DecodedSwapInstruction Arm7tdmi::decode_swap(std::uint32_t instruction) {
  if (!can_decode_swap(instruction)) {
    throw std::invalid_argument("unsupported ARM swap instruction");
  }

  return {
      static_cast<ArmCondition>(bits(instruction, 28, 0xFU)),
      ((instruction >> 22) & 0x1U) == 1,
      bits(instruction, 16, 0xFU),
      bits(instruction, 12, 0xFU),
      bits(instruction, 0, 0xFU),
  };
}

bool Arm7tdmi::can_decode_halfword_data_transfer_immediate(std::uint32_t instruction) {
  const bool halfword_transfer_group = ((instruction >> 25) & 0x7U) == 0;
  const bool immediate_offset = ((instruction >> 22) & 0x1U) == 1;
  const bool halfword_shape = ((instruction >> 7) & 0x1U) == 1 &&
                              ((instruction >> 4) & 0x1U) == 1;
  const std::uint8_t transfer_kind = bits(instruction, 5, 0x3U);
  const bool load = ((instruction >> 20) & 0x1U) == 1;
  const bool unsigned_halfword = transfer_kind == 0x1U;
  const bool signed_byte_or_halfword =
      load && (transfer_kind == 0x2U || transfer_kind == 0x3U);
  return halfword_transfer_group && immediate_offset && halfword_shape &&
         (unsigned_halfword || signed_byte_or_halfword) &&
         supported_condition(bits(instruction, 28, 0xFU));
}

DecodedHalfwordDataTransferInstruction Arm7tdmi::decode_halfword_data_transfer_immediate(
    std::uint32_t instruction) {
  if (!can_decode_halfword_data_transfer_immediate(instruction)) {
    throw std::invalid_argument("unsupported ARM halfword data transfer immediate instruction");
  }

  return {
      static_cast<ArmCondition>(bits(instruction, 28, 0xFU)),
      ((instruction >> 20) & 0x1U) == 1,
      ((instruction >> 6) & 0x1U) == 1,
      ((instruction >> 5) & 0x1U) == 1,
      ((instruction >> 24) & 0x1U) == 1,
      ((instruction >> 23) & 0x1U) == 1,
      ((instruction >> 21) & 0x1U) == 1,
      bits(instruction, 16, 0xFU),
      bits(instruction, 12, 0xFU),
      (static_cast<std::uint32_t>(bits(instruction, 8, 0xFU)) << 4) |
          static_cast<std::uint32_t>(bits(instruction, 0, 0xFU)),
  };
}

bool Arm7tdmi::can_decode_halfword_data_transfer_register(std::uint32_t instruction) {
  const bool halfword_transfer_group = ((instruction >> 25) & 0x7U) == 0;
  const bool register_offset = ((instruction >> 22) & 0x1U) == 0;
  const bool halfword_shape = ((instruction >> 7) & 0x1U) == 1 &&
                              ((instruction >> 4) & 0x1U) == 1;
  const std::uint8_t transfer_kind = bits(instruction, 5, 0x3U);
  const bool load = ((instruction >> 20) & 0x1U) == 1;
  const bool unsigned_halfword = transfer_kind == 0x1U;
  const bool signed_byte_or_halfword =
      load && (transfer_kind == 0x2U || transfer_kind == 0x3U);
  const bool high_offset_clear = bits(instruction, 8, 0xFU) == 0;
  return halfword_transfer_group && register_offset && halfword_shape && high_offset_clear &&
         (unsigned_halfword || signed_byte_or_halfword) &&
         supported_condition(bits(instruction, 28, 0xFU));
}

DecodedHalfwordDataTransferInstruction Arm7tdmi::decode_halfword_data_transfer_register(
    std::uint32_t instruction, std::uint32_t rm_value) {
  if (!can_decode_halfword_data_transfer_register(instruction)) {
    throw std::invalid_argument("unsupported ARM halfword data transfer register instruction");
  }

  return {
      static_cast<ArmCondition>(bits(instruction, 28, 0xFU)),
      ((instruction >> 20) & 0x1U) == 1,
      ((instruction >> 6) & 0x1U) == 1,
      ((instruction >> 5) & 0x1U) == 1,
      ((instruction >> 24) & 0x1U) == 1,
      ((instruction >> 23) & 0x1U) == 1,
      ((instruction >> 21) & 0x1U) == 1,
      bits(instruction, 16, 0xFU),
      bits(instruction, 12, 0xFU),
      rm_value,
  };
}

bool Arm7tdmi::can_decode_block_data_transfer(std::uint32_t instruction) {
  const bool block_transfer_group = ((instruction >> 25) & 0x7U) == 0x4U;
  const bool force_user_or_psr = ((instruction >> 22) & 0x1U) == 1;
  const std::uint16_t register_list = static_cast<std::uint16_t>(instruction & 0xFFFFU);
  return block_transfer_group && !force_user_or_psr && register_list != 0 &&
         bits(instruction, 16, 0xFU) != kPc &&
         !register_list_contains(register_list, kPc) &&
         supported_condition(bits(instruction, 28, 0xFU));
}

DecodedBlockDataTransferInstruction Arm7tdmi::decode_block_data_transfer(
    std::uint32_t instruction) {
  if (!can_decode_block_data_transfer(instruction)) {
    throw std::invalid_argument("unsupported ARM block data transfer instruction");
  }

  return {
      static_cast<ArmCondition>(bits(instruction, 28, 0xFU)),
      ((instruction >> 20) & 0x1U) == 1,
      ((instruction >> 24) & 0x1U) == 1,
      ((instruction >> 23) & 0x1U) == 1,
      ((instruction >> 21) & 0x1U) == 1,
      bits(instruction, 16, 0xFU),
      static_cast<std::uint16_t>(instruction & 0xFFFFU),
  };
}

bool Arm7tdmi::can_decode_thumb_add_subtract(std::uint16_t instruction) {
  return ((instruction >> 11) & 0x1FU) == 0x3U;
}

DecodedThumbInstruction Arm7tdmi::decode_thumb_add_subtract(std::uint16_t instruction) {
  if (!can_decode_thumb_add_subtract(instruction)) {
    throw std::invalid_argument("unsupported Thumb add/subtract instruction");
  }

  const bool immediate_operand = ((instruction >> 10) & 0x1U) == 1;
  const bool subtract = ((instruction >> 9) & 0x1U) == 1;
  const std::uint8_t operand = bits(instruction, 6, 0x7U);
  return {
      subtract ? ThumbOpcode::sub : ThumbOpcode::add,
      bits(instruction, 0, 0x7U),
      bits(instruction, 3, 0x7U),
      immediate_operand ? operand : static_cast<std::uint32_t>(operand),
      !immediate_operand,
  };
}

bool Arm7tdmi::can_decode_thumb_immediate(std::uint16_t instruction) {
  return ((instruction >> 13) & 0x7U) == 0x1U;
}

DecodedThumbInstruction Arm7tdmi::decode_thumb_immediate(std::uint16_t instruction) {
  if (!can_decode_thumb_immediate(instruction)) {
    throw std::invalid_argument("unsupported Thumb immediate instruction");
  }

  const std::uint8_t opcode = bits(instruction, 11, 0x3U);
  ThumbOpcode decoded_opcode = ThumbOpcode::mov;
  switch (opcode) {
    case 0x0:
      decoded_opcode = ThumbOpcode::mov;
      break;
    case 0x1:
      decoded_opcode = ThumbOpcode::cmp;
      break;
    case 0x2:
      decoded_opcode = ThumbOpcode::add;
      break;
    case 0x3:
      decoded_opcode = ThumbOpcode::sub;
      break;
  }

  const std::uint8_t rd = bits(instruction, 8, 0x7U);
  return {decoded_opcode, rd, rd, static_cast<std::uint32_t>(instruction & 0xFFU), false};
}

bool Arm7tdmi::can_decode_thumb_unconditional_branch(std::uint16_t instruction) {
  return ((instruction >> 11) & 0x1FU) == 0x1CU;
}

DecodedThumbBranchInstruction Arm7tdmi::decode_thumb_unconditional_branch(
    std::uint16_t instruction) {
  if (!can_decode_thumb_unconditional_branch(instruction)) {
    throw std::invalid_argument("unsupported Thumb unconditional branch instruction");
  }

  return {false, ArmCondition::al,
          sign_extend_thumb_offset(static_cast<std::uint16_t>(instruction & 0x7FFU), 11)};
}

bool Arm7tdmi::can_decode_thumb_conditional_branch(std::uint16_t instruction) {
  if (((instruction >> 12) & 0xFU) != 0xDU) {
    return false;
  }

  return supported_thumb_branch_condition(bits(instruction, 8, 0xFU));
}

DecodedThumbBranchInstruction Arm7tdmi::decode_thumb_conditional_branch(
    std::uint16_t instruction) {
  if (!can_decode_thumb_conditional_branch(instruction)) {
    throw std::invalid_argument("unsupported Thumb conditional branch instruction");
  }

  return {true, static_cast<ArmCondition>(bits(instruction, 8, 0xFU)),
          sign_extend_thumb_offset(static_cast<std::uint16_t>(instruction & 0xFFU), 8)};
}

bool Arm7tdmi::can_decode_thumb_high_register(std::uint16_t instruction) {
  if (((instruction >> 10) & 0x3FU) != 0x11U) {
    return false;
  }

  const std::uint8_t opcode = bits(instruction, 8, 0x3U);
  if (opcode == 0x3U) {
    return true;
  }
  return ((instruction >> 6) & 0x3U) != 0;
}

DecodedThumbHighRegisterInstruction Arm7tdmi::decode_thumb_high_register(
    std::uint16_t instruction) {
  if (!can_decode_thumb_high_register(instruction)) {
    throw std::invalid_argument("unsupported Thumb high-register instruction");
  }

  const std::uint8_t opcode = bits(instruction, 8, 0x3U);
  ThumbHighRegisterOpcode decoded_opcode = ThumbHighRegisterOpcode::add;
  switch (opcode) {
    case 0x0:
      decoded_opcode = ThumbHighRegisterOpcode::add;
      break;
    case 0x1:
      decoded_opcode = ThumbHighRegisterOpcode::cmp;
      break;
    case 0x2:
      decoded_opcode = ThumbHighRegisterOpcode::mov;
      break;
    case 0x3:
      decoded_opcode = ThumbHighRegisterOpcode::bx;
      break;
  }

  const std::uint8_t rd = static_cast<std::uint8_t>(bits(instruction, 0, 0x7U) |
                                                    (bits(instruction, 7, 0x1U) << 3));
  const std::uint8_t rs = static_cast<std::uint8_t>(bits(instruction, 3, 0x7U) |
                                                    (bits(instruction, 6, 0x1U) << 3));
  return {decoded_opcode, rd, rs};
}

bool Arm7tdmi::can_decode_thumb_memory_transfer(std::uint16_t instruction) {
  const std::uint8_t top_nibble = bits(instruction, 12, 0xFU);
  if (top_nibble == 0x5U) {
    return true;
  }
  if (((instruction >> 13) & 0x7U) == 0x3U) {
    return true;
  }
  if (top_nibble == 0x8U) {
    return true;
  }
  return top_nibble == 0x9U;
}

DecodedThumbMemoryTransferInstruction Arm7tdmi::decode_thumb_memory_transfer(
    std::uint16_t instruction) {
  if (!can_decode_thumb_memory_transfer(instruction)) {
    throw std::invalid_argument("unsupported Thumb memory-transfer instruction");
  }

  const std::uint8_t top_nibble = bits(instruction, 12, 0xFU);
  if (top_nibble == 0x5U) {
    const std::uint8_t op = bits(instruction, 9, 0x7U);
    ThumbMemoryTransferKind kind = ThumbMemoryTransferKind::word;
    switch (op) {
      case 0x0:
        kind = ThumbMemoryTransferKind::word;
        break;
      case 0x1:
        kind = ThumbMemoryTransferKind::halfword;
        break;
      case 0x2:
        kind = ThumbMemoryTransferKind::byte;
        break;
      case 0x3:
        kind = ThumbMemoryTransferKind::signed_byte;
        break;
      case 0x4:
        kind = ThumbMemoryTransferKind::word;
        break;
      case 0x5:
        kind = ThumbMemoryTransferKind::halfword;
        break;
      case 0x6:
        kind = ThumbMemoryTransferKind::byte;
        break;
      case 0x7:
        kind = ThumbMemoryTransferKind::signed_halfword;
        break;
    }
    return {
        op >= 0x4U || op == 0x3U,
        kind,
        bits(instruction, 0, 0x7U),
        bits(instruction, 3, 0x7U),
        bits(instruction, 6, 0x7U),
        true,
    };
  }

  if (((instruction >> 13) & 0x7U) == 0x3U) {
    const bool byte = ((instruction >> 12) & 0x1U) == 1;
    const bool load = ((instruction >> 11) & 0x1U) == 1;
    const std::uint32_t raw_offset = bits(instruction, 6, 0x1FU);
    return {
        load,
        byte ? ThumbMemoryTransferKind::byte : ThumbMemoryTransferKind::word,
        bits(instruction, 0, 0x7U),
        bits(instruction, 3, 0x7U),
        byte ? raw_offset : raw_offset * 4U,
        false,
    };
  }

  if (top_nibble == 0x8U) {
    return {
        ((instruction >> 11) & 0x1U) == 1,
        ThumbMemoryTransferKind::halfword,
        bits(instruction, 0, 0x7U),
        bits(instruction, 3, 0x7U),
        static_cast<std::uint32_t>(bits(instruction, 6, 0x1FU)) * 2U,
        false,
    };
  }

  return {
      ((instruction >> 11) & 0x1U) == 1,
      ThumbMemoryTransferKind::word,
      bits(instruction, 8, 0x7U),
      13,
      static_cast<std::uint32_t>(instruction & 0xFFU) * 4U,
      false,
  };
}

bool Arm7tdmi::can_decode_thumb_stack_transfer(std::uint16_t instruction) {
  return (instruction & 0xFE00U) == 0xB400U || (instruction & 0xFE00U) == 0xBC00U;
}

DecodedThumbStackInstruction Arm7tdmi::decode_thumb_stack_transfer(
    std::uint16_t instruction) {
  if (!can_decode_thumb_stack_transfer(instruction)) {
    throw std::invalid_argument("unsupported Thumb stack-transfer instruction");
  }

  return {
      (instruction & 0x0800U) != 0,
      (instruction & 0x0100U) != 0,
      static_cast<std::uint8_t>(instruction & 0x00FFU),
  };
}

bool Arm7tdmi::can_decode_thumb_software_interrupt(std::uint16_t instruction) {
  return (instruction & 0xFF00U) == 0xDF00U;
}

ExceptionVector Arm7tdmi::exception_vector(ExceptionKind kind) {
  switch (kind) {
    case ExceptionKind::reset:
      return {kind, 0x00000000, CpuMode::supervisor, 0, false, true, true};
    case ExceptionKind::undefined_instruction:
      return {kind, 0x00000004, CpuMode::undefined, 4, true, true, false};
    case ExceptionKind::software_interrupt:
      return {kind, 0x00000008, CpuMode::supervisor, 4, true, true, false};
    case ExceptionKind::prefetch_abort:
      return {kind, 0x0000000C, CpuMode::abort, 4, true, true, false};
    case ExceptionKind::data_abort:
      return {kind, 0x00000010, CpuMode::abort, 8, true, true, false};
    case ExceptionKind::irq:
      return {kind, 0x00000018, CpuMode::irq, 4, true, true, false};
    case ExceptionKind::fiq:
      return {kind, 0x0000001C, CpuMode::fiq, 4, true, true, true};
  }
  return {kind, 0x00000000, CpuMode::supervisor, 0, false, true, true};
}

std::optional<ArmCycleEstimate> Arm7tdmi::estimate_arm_cycles(std::uint32_t instruction) {
  if (can_decode_software_interrupt(instruction)) {
    return branch_cycle_estimate();
  }

  if (can_decode_swap(instruction)) {
    return ArmCycleEstimate{0, 2, 1, false};
  }

  if (can_decode_psr_transfer(instruction)) {
    return ArmCycleEstimate{1, 0, 0, false};
  }

  if (can_decode_branch(instruction)) {
    return branch_cycle_estimate();
  }

  if (can_decode_multiply_long(instruction)) {
    const bool accumulate = ((instruction >> 21) & 0x1U) == 1;
    return multiply_cycle_estimate(true, accumulate);
  }

  if (can_decode_multiply(instruction)) {
    const bool accumulate = ((instruction >> 21) & 0x1U) == 1;
    return multiply_cycle_estimate(accumulate, false);
  }

  if (can_decode_block_data_transfer(instruction)) {
    return block_transfer_cycle_estimate(instruction);
  }

  if (can_decode_single_data_transfer_immediate(instruction) ||
      can_decode_single_data_transfer_register(instruction) ||
      can_decode_halfword_data_transfer_immediate(instruction) ||
      can_decode_halfword_data_transfer_register(instruction)) {
    return single_transfer_cycle_estimate(instruction);
  }

  if (can_decode_data_processing_immediate(instruction) ||
      can_decode_data_processing_register_shift(instruction)) {
    return data_processing_cycle_estimate(instruction);
  }

  return std::nullopt;
}

std::optional<ArmElapsedCycleEstimate> Arm7tdmi::estimate_arm_elapsed_cycles(
    std::uint32_t instruction, std::uint32_t data_address) {
  const MemoryAccessTiming timing =
      MemoryBus::timing(data_address, transfer_access_width(instruction));
  return estimate_arm_elapsed_cycles_with_timing(instruction, data_address, timing, false);
}

std::optional<ArmElapsedCycleEstimate> Arm7tdmi::estimate_arm_elapsed_cycles(
    std::uint32_t instruction, std::uint32_t data_address,
    const WaitStateControl& waitcnt) {
  const MemoryAccessTiming timing =
      MemoryBus::timing(data_address, transfer_access_width(instruction), waitcnt);
  return estimate_arm_elapsed_cycles_with_timing(instruction, data_address, timing, true);
}

std::uint32_t Arm7tdmi::register_value(std::uint8_t index) const {
  if (index >= registers_.size()) {
    throw std::out_of_range("register index");
  }
  return registers_.at(index);
}

std::uint32_t Arm7tdmi::arm_visible_register_value(std::uint8_t index) const {
  const std::uint32_t value = registers_.at(index);
  return index == kPc ? value + 8U : value;
}

std::uint32_t Arm7tdmi::thumb_visible_register_value(std::uint8_t index) const {
  const std::uint32_t value = registers_.at(index);
  return index == kPc ? ((value + 4U) & ~1U) : value;
}

void Arm7tdmi::set_register(std::uint8_t index, std::uint32_t value) {
  if (index >= registers_.size()) {
    throw std::out_of_range("register index");
  }
  registers_.at(index) = value;
}

void Arm7tdmi::reset() {
  registers_.fill(0);
  elapsed_cycles_ = 0;
  negative_ = false;
  zero_ = false;
  carry_ = false;
  overflow_ = false;
  irq_disabled_ = false;
  fiq_disabled_ = false;
  thumb_state_ = false;
  mode_ = CpuMode::supervisor;
  shared_r8_r12_.fill(0);
  fiq_r8_r12_.fill(0);
  user_sp_ = 0;
  user_lr_ = 0;
  fiq_sp_ = 0;
  fiq_lr_ = 0;
  irq_sp_ = 0;
  irq_lr_ = 0;
  supervisor_sp_ = 0;
  supervisor_lr_ = 0;
  abort_sp_ = 0;
  abort_lr_ = 0;
  undefined_sp_ = 0;
  undefined_lr_ = 0;
  fiq_spsr_ = 0;
  supervisor_spsr_ = 0;
  abort_spsr_ = 0;
  irq_spsr_ = 0;
  undefined_spsr_ = 0;
}

void Arm7tdmi::reset_elapsed_cycles() {
  elapsed_cycles_ = 0;
}

bool Arm7tdmi::negative() const {
  return negative_;
}

bool Arm7tdmi::zero() const {
  return zero_;
}

bool Arm7tdmi::carry() const {
  return carry_;
}

bool Arm7tdmi::overflow() const {
  return overflow_;
}

bool Arm7tdmi::irq_disabled() const {
  return irq_disabled_;
}

bool Arm7tdmi::fiq_disabled() const {
  return fiq_disabled_;
}

CpuMode Arm7tdmi::current_mode() const {
  return mode_;
}

bool Arm7tdmi::thumb_state() const {
  return thumb_state_;
}

std::uint32_t Arm7tdmi::cpsr() const {
  std::uint32_t value = static_cast<std::uint32_t>(mode_);
  if (negative_) {
    value |= kNegativeFlag;
  }
  if (zero_) {
    value |= kZeroFlag;
  }
  if (carry_) {
    value |= kCarryFlag;
  }
  if (overflow_) {
    value |= kOverflowFlag;
  }
  if (thumb_state_) {
    value |= kThumbStateFlag;
  }
  if (fiq_disabled_) {
    value |= kFiqDisableFlag;
  }
  if (irq_disabled_) {
    value |= kIrqDisableFlag;
  }
  return value;
}

bool Arm7tdmi::set_cpsr(std::uint32_t value) {
  const std::optional<CpuMode> mode = decode_cpu_mode(value);
  if (!mode.has_value()) {
    return false;
  }

  negative_ = (value & kNegativeFlag) != 0;
  zero_ = (value & kZeroFlag) != 0;
  carry_ = (value & kCarryFlag) != 0;
  overflow_ = (value & kOverflowFlag) != 0;
  thumb_state_ = (value & kThumbStateFlag) != 0;
  fiq_disabled_ = (value & kFiqDisableFlag) != 0;
  irq_disabled_ = (value & kIrqDisableFlag) != 0;
  switch_mode(mode.value());
  return true;
}

bool Arm7tdmi::has_spsr() const {
  return mode_has_spsr(mode_);
}

std::optional<std::uint32_t> Arm7tdmi::spsr() const {
  switch (mode_) {
    case CpuMode::fiq:
      return fiq_spsr_;
    case CpuMode::supervisor:
      return supervisor_spsr_;
    case CpuMode::abort:
      return abort_spsr_;
    case CpuMode::irq:
      return irq_spsr_;
    case CpuMode::undefined:
      return undefined_spsr_;
    case CpuMode::user:
    case CpuMode::system:
      return std::nullopt;
  }
  return std::nullopt;
}

bool Arm7tdmi::set_spsr(std::uint32_t value) {
  switch (mode_) {
    case CpuMode::fiq:
      fiq_spsr_ = value;
      return true;
    case CpuMode::supervisor:
      supervisor_spsr_ = value;
      return true;
    case CpuMode::abort:
      abort_spsr_ = value;
      return true;
    case CpuMode::irq:
      irq_spsr_ = value;
      return true;
    case CpuMode::undefined:
      undefined_spsr_ = value;
      return true;
    case CpuMode::user:
    case CpuMode::system:
      return false;
  }
  return false;
}

void Arm7tdmi::set_spsr_for_mode(CpuMode mode, std::uint32_t value) {
  switch (mode) {
    case CpuMode::fiq:
      fiq_spsr_ = value;
      return;
    case CpuMode::supervisor:
      supervisor_spsr_ = value;
      return;
    case CpuMode::abort:
      abort_spsr_ = value;
      return;
    case CpuMode::irq:
      irq_spsr_ = value;
      return;
    case CpuMode::undefined:
      undefined_spsr_ = value;
      return;
    case CpuMode::user:
    case CpuMode::system:
      return;
  }
}

void Arm7tdmi::save_banked_registers(CpuMode mode) {
  if (mode == CpuMode::fiq) {
    for (std::uint8_t index = 0; index < 5; ++index) {
      fiq_r8_r12_.at(index) = registers_.at(8U + index);
    }
  } else {
    for (std::uint8_t index = 0; index < 5; ++index) {
      shared_r8_r12_.at(index) = registers_.at(8U + index);
    }
  }

  switch (mode) {
    case CpuMode::fiq:
      fiq_sp_ = registers_.at(13);
      fiq_lr_ = registers_.at(14);
      return;
    case CpuMode::irq:
      irq_sp_ = registers_.at(13);
      irq_lr_ = registers_.at(14);
      return;
    case CpuMode::supervisor:
      supervisor_sp_ = registers_.at(13);
      supervisor_lr_ = registers_.at(14);
      return;
    case CpuMode::abort:
      abort_sp_ = registers_.at(13);
      abort_lr_ = registers_.at(14);
      return;
    case CpuMode::undefined:
      undefined_sp_ = registers_.at(13);
      undefined_lr_ = registers_.at(14);
      return;
    case CpuMode::user:
    case CpuMode::system:
      user_sp_ = registers_.at(13);
      user_lr_ = registers_.at(14);
      return;
  }
}

void Arm7tdmi::load_banked_registers(CpuMode mode) {
  const std::array<std::uint32_t, 5>& r8_r12 =
      mode == CpuMode::fiq ? fiq_r8_r12_ : shared_r8_r12_;
  for (std::uint8_t index = 0; index < 5; ++index) {
    registers_.at(8U + index) = r8_r12.at(index);
  }

  switch (mode) {
    case CpuMode::fiq:
      registers_.at(13) = fiq_sp_;
      registers_.at(14) = fiq_lr_;
      return;
    case CpuMode::irq:
      registers_.at(13) = irq_sp_;
      registers_.at(14) = irq_lr_;
      return;
    case CpuMode::supervisor:
      registers_.at(13) = supervisor_sp_;
      registers_.at(14) = supervisor_lr_;
      return;
    case CpuMode::abort:
      registers_.at(13) = abort_sp_;
      registers_.at(14) = abort_lr_;
      return;
    case CpuMode::undefined:
      registers_.at(13) = undefined_sp_;
      registers_.at(14) = undefined_lr_;
      return;
    case CpuMode::user:
    case CpuMode::system:
      registers_.at(13) = user_sp_;
      registers_.at(14) = user_lr_;
      return;
  }
}

void Arm7tdmi::switch_mode(CpuMode mode) {
  if (mode_ == mode) {
    return;
  }
  save_banked_registers(mode_);
  mode_ = mode;
  load_banked_registers(mode_);
}

std::uint64_t Arm7tdmi::elapsed_cycles() const {
  return elapsed_cycles_;
}

std::uint64_t Arm7tdmi::state_hash() const {
  StateHasher hasher;
  for (const std::uint32_t reg : registers_) {
    hasher.add_u32(reg);
  }
  hasher.add_u64(elapsed_cycles_);
  hasher.add_bool(negative_);
  hasher.add_bool(zero_);
  hasher.add_bool(carry_);
  hasher.add_bool(overflow_);
  hasher.add_bool(irq_disabled_);
  hasher.add_bool(fiq_disabled_);
  hasher.add_bool(thumb_state_);
  hasher.add_u8(static_cast<std::uint8_t>(mode_));
  for (const std::uint32_t reg : shared_r8_r12_) {
    hasher.add_u32(reg);
  }
  for (const std::uint32_t reg : fiq_r8_r12_) {
    hasher.add_u32(reg);
  }
  hasher.add_u32(user_sp_);
  hasher.add_u32(user_lr_);
  hasher.add_u32(fiq_sp_);
  hasher.add_u32(fiq_lr_);
  hasher.add_u32(irq_sp_);
  hasher.add_u32(irq_lr_);
  hasher.add_u32(supervisor_sp_);
  hasher.add_u32(supervisor_lr_);
  hasher.add_u32(abort_sp_);
  hasher.add_u32(abort_lr_);
  hasher.add_u32(undefined_sp_);
  hasher.add_u32(undefined_lr_);
  hasher.add_u32(fiq_spsr_);
  hasher.add_u32(supervisor_spsr_);
  hasher.add_u32(abort_spsr_);
  hasher.add_u32(irq_spsr_);
  hasher.add_u32(undefined_spsr_);
  return hasher.value();
}

ExecuteStatus Arm7tdmi::execute_arm(std::uint32_t instruction) {
  if (can_decode_software_interrupt(instruction)) {
    const ArmCondition condition = static_cast<ArmCondition>(bits(instruction, 28, 0xFU));
    if (!condition_passed(condition)) {
      return ExecuteStatus::skipped_condition;
    }
    return enter_exception(ExceptionKind::software_interrupt);
  }

  if (can_decode_psr_transfer(instruction)) {
    const DecodedPsrTransferInstruction decoded = decode_psr_transfer(instruction);
    if (!condition_passed(decoded.condition)) {
      return ExecuteStatus::skipped_condition;
    }
    return execute_psr_transfer(decoded);
  }

  if (can_decode_branch(instruction)) {
    const DecodedBranchInstruction decoded = decode_branch(instruction);
    if (!condition_passed(decoded.condition)) {
      return ExecuteStatus::skipped_condition;
    }

    const std::uint32_t pc = registers_.at(kPc);
    if (decoded.link) {
      registers_.at(kLinkRegister) = pc + 4;
    }
    registers_.at(kPc) = static_cast<std::uint32_t>(
        static_cast<std::int64_t>(pc) + 8 + static_cast<std::int64_t>(decoded.offset));
    return ExecuteStatus::executed;
  }

  if (can_decode_multiply_long(instruction)) {
    const DecodedMultiplyLongInstruction decoded = decode_multiply_long(instruction);
    if (!condition_passed(decoded.condition)) {
      return ExecuteStatus::skipped_condition;
    }
    return execute_multiply_long(decoded);
  }

  if (can_decode_multiply(instruction)) {
    const DecodedMultiplyInstruction decoded = decode_multiply(instruction);
    if (!condition_passed(decoded.condition)) {
      return ExecuteStatus::skipped_condition;
    }
    return execute_multiply(decoded);
  }

  if (!can_decode_data_processing_immediate(instruction)) {
    if (!can_decode_data_processing_register_shift(instruction)) {
      return ExecuteStatus::unsupported;
    }

    const std::uint8_t rm = bits(instruction, 0, 0xFU);
    const bool register_shift = ((instruction >> 4) & 0x1U) == 1 &&
                                ((instruction >> 7) & 0x1U) == 0;
    const DecodedArmInstruction decoded = register_shift
                                             ? decode_data_processing_register_shift(
                                                   instruction, arm_visible_register_value(rm),
                                                   arm_visible_register_value(
                                                       bits(instruction, 8, 0xFU)))
                                             : decode_data_processing_register_shift(
                                                   instruction, arm_visible_register_value(rm),
                                                   carry_);
    if (!condition_passed(decoded.condition)) {
      return ExecuteStatus::skipped_condition;
    }
    return execute_data_processing(decoded);
  }

  const DecodedArmInstruction decoded = decode_data_processing_immediate(instruction);
  if (!condition_passed(decoded.condition)) {
    return ExecuteStatus::skipped_condition;
  }

  return execute_data_processing(decoded);
}

ExecuteStatus Arm7tdmi::execute_data_processing(const DecodedArmInstruction& decoded) {
  const std::uint32_t left = arm_visible_register_value(decoded.rn);
  switch (decoded.opcode) {
    case ArmOpcode::and_: {
      const std::uint32_t result = left & decoded.operand2;
      registers_.at(decoded.rd) = result;
      if (decoded.set_flags) {
        set_logical_flags(result, decoded);
      }
      return ExecuteStatus::executed;
    }
    case ArmOpcode::eor: {
      const std::uint32_t result = left ^ decoded.operand2;
      registers_.at(decoded.rd) = result;
      if (decoded.set_flags) {
        set_logical_flags(result, decoded);
      }
      return ExecuteStatus::executed;
    }
    case ArmOpcode::mov:
      registers_.at(decoded.rd) = decoded.operand2;
      if (decoded.set_flags) {
        set_logical_flags(decoded.operand2, decoded);
      }
      return ExecuteStatus::executed;
    case ArmOpcode::add: {
      const std::uint32_t result = left + decoded.operand2;
      registers_.at(decoded.rd) = result;
      if (decoded.set_flags) {
        set_add_flags(left, decoded.operand2, result);
      }
      return ExecuteStatus::executed;
    }
    case ArmOpcode::sub: {
      const std::uint32_t result = left - decoded.operand2;
      registers_.at(decoded.rd) = result;
      if (decoded.set_flags) {
        set_sub_flags(left, decoded.operand2, result);
      }
      return ExecuteStatus::executed;
    }
    case ArmOpcode::rsb: {
      const std::uint32_t result = decoded.operand2 - left;
      registers_.at(decoded.rd) = result;
      if (decoded.set_flags) {
        set_sub_flags(decoded.operand2, left, result);
      }
      return ExecuteStatus::executed;
    }
    case ArmOpcode::adc: {
      const bool carry_in = carry_;
      const std::uint32_t result = left + decoded.operand2 + (carry_in ? 1U : 0U);
      registers_.at(decoded.rd) = result;
      if (decoded.set_flags) {
        set_adc_flags(left, decoded.operand2, carry_in, result);
      }
      return ExecuteStatus::executed;
    }
    case ArmOpcode::sbc: {
      const bool carry_in = carry_;
      const std::uint32_t borrow = carry_in ? 0U : 1U;
      const std::uint32_t result = left - decoded.operand2 - borrow;
      registers_.at(decoded.rd) = result;
      if (decoded.set_flags) {
        set_sbc_flags(left, decoded.operand2, carry_in, result);
      }
      return ExecuteStatus::executed;
    }
    case ArmOpcode::rsc: {
      const bool carry_in = carry_;
      const std::uint32_t borrow = carry_in ? 0U : 1U;
      const std::uint32_t result = decoded.operand2 - left - borrow;
      registers_.at(decoded.rd) = result;
      if (decoded.set_flags) {
        set_sbc_flags(decoded.operand2, left, carry_in, result);
      }
      return ExecuteStatus::executed;
    }
    case ArmOpcode::cmp: {
      const std::uint32_t result = left - decoded.operand2;
      set_sub_flags(left, decoded.operand2, result);
      return ExecuteStatus::executed;
    }
    case ArmOpcode::cmn: {
      const std::uint32_t result = left + decoded.operand2;
      set_add_flags(left, decoded.operand2, result);
      return ExecuteStatus::executed;
    }
    case ArmOpcode::tst: {
      const std::uint32_t result = left & decoded.operand2;
      set_logical_flags(result, decoded);
      return ExecuteStatus::executed;
    }
    case ArmOpcode::teq: {
      const std::uint32_t result = left ^ decoded.operand2;
      set_logical_flags(result, decoded);
      return ExecuteStatus::executed;
    }
    case ArmOpcode::orr: {
      const std::uint32_t result = left | decoded.operand2;
      registers_.at(decoded.rd) = result;
      if (decoded.set_flags) {
        set_logical_flags(result, decoded);
      }
      return ExecuteStatus::executed;
    }
    case ArmOpcode::bic: {
      const std::uint32_t result = left & ~decoded.operand2;
      registers_.at(decoded.rd) = result;
      if (decoded.set_flags) {
        set_logical_flags(result, decoded);
      }
      return ExecuteStatus::executed;
    }
    case ArmOpcode::mvn: {
      const std::uint32_t result = ~decoded.operand2;
      registers_.at(decoded.rd) = result;
      if (decoded.set_flags) {
        set_logical_flags(result, decoded);
      }
      return ExecuteStatus::executed;
    }
  }

  return ExecuteStatus::unsupported;
}

ExecuteStatus Arm7tdmi::execute_psr_transfer(const DecodedPsrTransferInstruction& decoded) {
  if (!decoded.write) {
    if (decoded.psr == ArmProgramStatusRegister::spsr) {
      const std::optional<std::uint32_t> value = spsr();
      if (!value.has_value()) {
        return ExecuteStatus::unsupported;
      }
      registers_.at(decoded.rd_or_rm) = value.value();
      return ExecuteStatus::executed;
    }

    registers_.at(decoded.rd_or_rm) = cpsr();
    return ExecuteStatus::executed;
  }

  const std::uint32_t field_mask = psr_field_mask(decoded.field_mask);
  if (field_mask == 0) {
    return ExecuteStatus::unsupported;
  }

  const std::uint32_t operand =
      decoded.immediate_operand ? decoded.operand : arm_visible_register_value(decoded.rd_or_rm);
  if (decoded.psr == ArmProgramStatusRegister::spsr) {
    const std::optional<std::uint32_t> old_value = spsr();
    if (!old_value.has_value()) {
      return ExecuteStatus::unsupported;
    }
    return set_spsr((old_value.value() & ~field_mask) | (operand & field_mask))
               ? ExecuteStatus::executed
               : ExecuteStatus::unsupported;
  }

  const std::uint32_t merged = (cpsr() & ~field_mask) | (operand & field_mask);
  return set_cpsr(merged) ? ExecuteStatus::executed : ExecuteStatus::unsupported;
}

ExecuteStatus Arm7tdmi::execute_multiply(const DecodedMultiplyInstruction& decoded) {
  const std::uint64_t product = static_cast<std::uint64_t>(registers_.at(decoded.rm)) *
                                static_cast<std::uint64_t>(registers_.at(decoded.rs));
  std::uint32_t result = static_cast<std::uint32_t>(product);
  if (decoded.accumulate) {
    result += registers_.at(decoded.rn);
  }

  registers_.at(decoded.rd) = result;
  if (decoded.set_flags) {
    set_nz(result);
  }
  return ExecuteStatus::executed;
}

ExecuteStatus Arm7tdmi::execute_multiply_long(
    const DecodedMultiplyLongInstruction& decoded) {
  std::uint64_t result = 0;
  if (decoded.signed_multiply) {
    const auto left = static_cast<std::int64_t>(
        static_cast<std::int32_t>(registers_.at(decoded.rm)));
    const auto right = static_cast<std::int64_t>(
        static_cast<std::int32_t>(registers_.at(decoded.rs)));
    result = static_cast<std::uint64_t>(left * right);
  } else {
    result = static_cast<std::uint64_t>(registers_.at(decoded.rm)) *
             static_cast<std::uint64_t>(registers_.at(decoded.rs));
  }

  if (decoded.accumulate) {
    const std::uint64_t accumulator =
        (static_cast<std::uint64_t>(registers_.at(decoded.rd_hi)) << 32) |
        static_cast<std::uint64_t>(registers_.at(decoded.rd_lo));
    result += accumulator;
  }

  registers_.at(decoded.rd_lo) = static_cast<std::uint32_t>(result & 0xFFFFFFFFULL);
  registers_.at(decoded.rd_hi) = static_cast<std::uint32_t>(result >> 32);
  if (decoded.set_flags) {
    set_nz64(result);
  }
  return ExecuteStatus::executed;
}

ExecuteStatus Arm7tdmi::execute_thumb(std::uint16_t instruction) {
  if (can_decode_thumb_software_interrupt(instruction)) {
    return enter_exception(ExceptionKind::software_interrupt);
  }

  if (can_decode_thumb_high_register(instruction)) {
    return execute_thumb_high_register(decode_thumb_high_register(instruction));
  }

  if (can_decode_thumb_conditional_branch(instruction)) {
    return execute_thumb_branch(decode_thumb_conditional_branch(instruction));
  }

  if (can_decode_thumb_unconditional_branch(instruction)) {
    return execute_thumb_branch(decode_thumb_unconditional_branch(instruction));
  }

  if (can_decode_thumb_add_subtract(instruction)) {
    return execute_thumb_data_processing(decode_thumb_add_subtract(instruction));
  }

  if (can_decode_thumb_immediate(instruction)) {
    return execute_thumb_data_processing(decode_thumb_immediate(instruction));
  }

  return ExecuteStatus::unsupported;
}

ExecuteStatus Arm7tdmi::execute_thumb(std::uint16_t instruction, MemoryBus& memory) {
  if (can_decode_thumb_stack_transfer(instruction)) {
    return execute_thumb_stack_transfer(decode_thumb_stack_transfer(instruction), memory);
  }

  if (can_decode_thumb_memory_transfer(instruction)) {
    return execute_thumb_memory_transfer(decode_thumb_memory_transfer(instruction), memory);
  }

  return execute_thumb(instruction);
}

ExecuteStatus Arm7tdmi::execute_thumb_branch(
    const DecodedThumbBranchInstruction& decoded) {
  if (decoded.conditional && !condition_passed(decoded.condition)) {
    return ExecuteStatus::skipped_condition;
  }

  registers_.at(kPc) = registers_.at(kPc) + static_cast<std::uint32_t>(decoded.offset);
  return ExecuteStatus::executed;
}

ExecuteStatus Arm7tdmi::execute_thumb_high_register(
    const DecodedThumbHighRegisterInstruction& decoded) {
  const std::uint32_t source = thumb_visible_register_value(decoded.rs);
  switch (decoded.opcode) {
    case ThumbHighRegisterOpcode::add: {
      const std::uint32_t result = thumb_visible_register_value(decoded.rd) + source;
      registers_.at(decoded.rd) = decoded.rd == kPc ? (result & ~1U) : result;
      return ExecuteStatus::executed;
    }
    case ThumbHighRegisterOpcode::cmp: {
      const std::uint32_t left = thumb_visible_register_value(decoded.rd);
      const std::uint32_t result = left - source;
      set_sub_flags(left, source, result);
      return ExecuteStatus::executed;
    }
    case ThumbHighRegisterOpcode::mov:
      registers_.at(decoded.rd) = decoded.rd == kPc ? (source & ~1U) : source;
      return ExecuteStatus::executed;
    case ThumbHighRegisterOpcode::bx:
      thumb_state_ = (source & 0x1U) != 0;
      registers_.at(kPc) = source & ~1U;
      return ExecuteStatus::executed;
  }

  return ExecuteStatus::unsupported;
}

ExecuteStatus Arm7tdmi::execute_thumb_memory_transfer(
    const DecodedThumbMemoryTransferInstruction& decoded, MemoryBus& memory) {
  const std::uint32_t base = registers_.at(decoded.rb);
  const std::uint32_t offset =
      decoded.offset_is_register ? registers_.at(decoded.offset) : decoded.offset;
  const std::uint32_t address = base + offset;

  if (decoded.load) {
    switch (decoded.kind) {
      case ThumbMemoryTransferKind::word: {
        const std::optional<std::uint32_t> value = memory.read32(address);
        if (!value.has_value()) {
          return ExecuteStatus::unsupported;
        }
        registers_.at(decoded.rd) = value.value();
        return ExecuteStatus::executed;
      }
      case ThumbMemoryTransferKind::byte: {
        const std::optional<std::uint8_t> value = memory.read8(address);
        if (!value.has_value()) {
          return ExecuteStatus::unsupported;
        }
        registers_.at(decoded.rd) = value.value();
        return ExecuteStatus::executed;
      }
      case ThumbMemoryTransferKind::halfword: {
        const std::optional<std::uint16_t> value = memory.read16(address);
        if (!value.has_value()) {
          return ExecuteStatus::unsupported;
        }
        registers_.at(decoded.rd) = value.value();
        return ExecuteStatus::executed;
      }
      case ThumbMemoryTransferKind::signed_byte: {
        const std::optional<std::uint8_t> value = memory.read8(address);
        if (!value.has_value()) {
          return ExecuteStatus::unsupported;
        }
        registers_.at(decoded.rd) = sign_extend8(value.value());
        return ExecuteStatus::executed;
      }
      case ThumbMemoryTransferKind::signed_halfword: {
        const std::optional<std::uint16_t> value = memory.read16(address);
        if (!value.has_value()) {
          return ExecuteStatus::unsupported;
        }
        registers_.at(decoded.rd) = sign_extend16(value.value());
        return ExecuteStatus::executed;
      }
    }
  }

  switch (decoded.kind) {
    case ThumbMemoryTransferKind::word:
      return memory.write32(address, registers_.at(decoded.rd)) ? ExecuteStatus::executed
                                                                : ExecuteStatus::unsupported;
    case ThumbMemoryTransferKind::byte:
      return memory.write8(address, static_cast<std::uint8_t>(registers_.at(decoded.rd) & 0xFFU))
                 ? ExecuteStatus::executed
                 : ExecuteStatus::unsupported;
    case ThumbMemoryTransferKind::halfword:
      return memory.write16(address,
                            static_cast<std::uint16_t>(registers_.at(decoded.rd) & 0xFFFFU))
                 ? ExecuteStatus::executed
                 : ExecuteStatus::unsupported;
    case ThumbMemoryTransferKind::signed_byte:
    case ThumbMemoryTransferKind::signed_halfword:
      return ExecuteStatus::unsupported;
  }

  return ExecuteStatus::unsupported;
}

ExecuteStatus Arm7tdmi::execute_thumb_stack_transfer(
    const DecodedThumbStackInstruction& decoded, MemoryBus& memory) {
  const std::uint8_t low_register_count = count_registers(decoded.register_list);
  const std::uint8_t register_count =
      static_cast<std::uint8_t>(low_register_count + (decoded.extra_register ? 1U : 0U));
  if (register_count == 0) {
    return ExecuteStatus::unsupported;
  }

  const std::uint32_t old_sp = registers_.at(13);
  if (!decoded.load) {
    std::uint32_t address = old_sp - static_cast<std::uint32_t>(register_count) * 4U;
    const std::uint32_t new_sp = address;
    for (std::uint8_t index = 0; index < 8; ++index) {
      if (!register_list_contains(decoded.register_list, index)) {
        continue;
      }
      if (!memory.write32(address, registers_.at(index))) {
        return ExecuteStatus::unsupported;
      }
      address += 4U;
    }
    if (decoded.extra_register) {
      if (!memory.write32(address, registers_.at(kLinkRegister))) {
        return ExecuteStatus::unsupported;
      }
    }
    registers_.at(13) = new_sp;
    return ExecuteStatus::executed;
  }

  std::array<std::uint32_t, 9> loaded{};
  std::uint8_t loaded_count = 0;
  std::uint32_t address = old_sp;
  for (std::uint8_t index = 0; index < 8; ++index) {
    if (!register_list_contains(decoded.register_list, index)) {
      continue;
    }
    const std::optional<std::uint32_t> value = memory.read32(address);
    if (!value.has_value()) {
      return ExecuteStatus::unsupported;
    }
    loaded.at(loaded_count++) = value.value();
    address += 4U;
  }
  if (decoded.extra_register) {
    const std::optional<std::uint32_t> value = memory.read32(address);
    if (!value.has_value()) {
      return ExecuteStatus::unsupported;
    }
    loaded.at(loaded_count++) = value.value();
    address += 4U;
  }

  std::uint8_t read_index = 0;
  for (std::uint8_t index = 0; index < 8; ++index) {
    if (register_list_contains(decoded.register_list, index)) {
      registers_.at(index) = loaded.at(read_index++);
    }
  }
  if (decoded.extra_register) {
    registers_.at(kPc) = loaded.at(read_index) & ~1U;
  }
  registers_.at(13) = old_sp + static_cast<std::uint32_t>(register_count) * 4U;
  return ExecuteStatus::executed;
}

ExecuteStatus Arm7tdmi::execute_swap(const DecodedSwapInstruction& decoded,
                                     MemoryBus& memory) {
  if (!condition_passed(decoded.condition)) {
    return ExecuteStatus::skipped_condition;
  }

  const std::uint32_t address = registers_.at(decoded.rn);
  if (decoded.byte) {
    const std::optional<std::uint8_t> old_value = memory.read8(address);
    if (!old_value.has_value()) {
      return ExecuteStatus::unsupported;
    }
    if (!memory.write8(address,
                       static_cast<std::uint8_t>(registers_.at(decoded.rm) & 0xFFU))) {
      return ExecuteStatus::unsupported;
    }
    registers_.at(decoded.rd) = old_value.value();
    return ExecuteStatus::executed;
  }

  const std::optional<std::uint32_t> old_value = memory.read32(address);
  if (!old_value.has_value()) {
    return ExecuteStatus::unsupported;
  }
  if (!memory.write32(address, registers_.at(decoded.rm))) {
    return ExecuteStatus::unsupported;
  }
  registers_.at(decoded.rd) = old_value.value();
  return ExecuteStatus::executed;
}

ExecuteStatus Arm7tdmi::enter_exception(ExceptionKind kind) {
  const ExceptionVector vector = exception_vector(kind);
  const std::uint32_t saved_cpsr = cpsr();
  const std::uint32_t saved_pc = registers_.at(kPc);

  switch_mode(vector.mode);
  if (vector.save_cpsr) {
    set_spsr_for_mode(vector.mode, saved_cpsr);
    registers_.at(kLinkRegister) = saved_pc + vector.link_offset;
  }

  thumb_state_ = false;
  if (vector.disable_irq) {
    irq_disabled_ = true;
  }
  if (vector.disable_fiq) {
    fiq_disabled_ = true;
  }
  registers_.at(kPc) = vector.vector_address;
  return ExecuteStatus::executed;
}

ExecuteStatus Arm7tdmi::return_from_exception(std::uint32_t link_adjustment) {
  const std::optional<std::uint32_t> saved_psr = spsr();
  if (!saved_psr.has_value()) {
    return ExecuteStatus::unsupported;
  }

  const std::uint32_t return_pc = registers_.at(kLinkRegister) - link_adjustment;
  if (!set_cpsr(saved_psr.value())) {
    return ExecuteStatus::unsupported;
  }
  registers_.at(kPc) = return_pc;
  return ExecuteStatus::executed;
}

ExecuteStatus Arm7tdmi::execute_thumb_data_processing(
    const DecodedThumbInstruction& decoded) {
  const std::uint32_t operand =
      decoded.operand_is_register ? registers_.at(decoded.operand) : decoded.operand;

  switch (decoded.opcode) {
    case ThumbOpcode::mov:
      registers_.at(decoded.rd) = operand;
      set_nz(operand);
      return ExecuteStatus::executed;
    case ThumbOpcode::cmp: {
      const std::uint32_t left = registers_.at(decoded.rd);
      const std::uint32_t result = left - operand;
      set_sub_flags(left, operand, result);
      return ExecuteStatus::executed;
    }
    case ThumbOpcode::add: {
      const std::uint32_t left = registers_.at(decoded.rs);
      const std::uint32_t result = left + operand;
      registers_.at(decoded.rd) = result;
      set_add_flags(left, operand, result);
      return ExecuteStatus::executed;
    }
    case ThumbOpcode::sub: {
      const std::uint32_t left = registers_.at(decoded.rs);
      const std::uint32_t result = left - operand;
      registers_.at(decoded.rd) = result;
      set_sub_flags(left, operand, result);
      return ExecuteStatus::executed;
    }
  }

  return ExecuteStatus::unsupported;
}

ExecuteStatus Arm7tdmi::execute_arm(std::uint32_t instruction, MemoryBus& memory) {
  if (can_decode_swap(instruction)) {
    return execute_swap(decode_swap(instruction), memory);
  }

  if (can_decode_block_data_transfer(instruction)) {
    const DecodedBlockDataTransferInstruction decoded = decode_block_data_transfer(instruction);
    if (!condition_passed(decoded.condition)) {
      return ExecuteStatus::skipped_condition;
    }

    if (decoded.write_back && register_list_contains(decoded.register_list, decoded.rn)) {
      return ExecuteStatus::unsupported;
    }

    const std::uint8_t register_count = count_registers(decoded.register_list);
    const BlockTransferAddress transfer =
        block_transfer_address(registers_.at(decoded.rn), register_count, decoded.pre_index,
                               decoded.up);
    std::uint32_t address = transfer.first;

    if (decoded.load) {
      std::array<std::uint32_t, kRegisterCount> loaded{};
      for (std::uint8_t index = 0; index < kPc; ++index) {
        if (!register_list_contains(decoded.register_list, index)) {
          continue;
        }
        const std::optional<std::uint32_t> value = memory.read32(address);
        if (!value.has_value()) {
          return ExecuteStatus::unsupported;
        }
        loaded.at(index) = value.value();
        address += 4U;
      }

      for (std::uint8_t index = 0; index < kPc; ++index) {
        if (register_list_contains(decoded.register_list, index)) {
          registers_.at(index) = loaded.at(index);
        }
      }
    } else {
      for (std::uint8_t index = 0; index < kPc; ++index) {
        if (!register_list_contains(decoded.register_list, index)) {
          continue;
        }
        if (!memory.write32(address, registers_.at(index))) {
          return ExecuteStatus::unsupported;
        }
        address += 4U;
      }
    }

    if (decoded.write_back) {
      registers_.at(decoded.rn) = transfer.write_back;
    }
    return ExecuteStatus::executed;
  }

  if (can_decode_halfword_data_transfer_immediate(instruction) ||
      can_decode_halfword_data_transfer_register(instruction)) {
    const DecodedHalfwordDataTransferInstruction decoded =
        can_decode_halfword_data_transfer_register(instruction)
            ? decode_halfword_data_transfer_register(instruction,
                                                     arm_visible_register_value(
                                                         bits(instruction, 0, 0xFU)))
            : decode_halfword_data_transfer_immediate(instruction);
    if (!condition_passed(decoded.condition)) {
      return ExecuteStatus::skipped_condition;
    }

    if (!supported_transfer_addressing(decoded.pre_index, decoded.write_back)) {
      return ExecuteStatus::unsupported;
    }

    const std::uint32_t base = arm_visible_register_value(decoded.rn);
    const std::uint32_t write_back_address =
        offset_transfer_address(base, decoded.offset, decoded.up);
    const std::uint32_t address = decoded.pre_index ? write_back_address : base;
    const bool needs_write_back =
        transfer_needs_write_back(decoded.pre_index, decoded.write_back);
    if (needs_write_back && decoded.load && decoded.rn == decoded.rd) {
      return ExecuteStatus::unsupported;
    }

    if (decoded.load) {
      if (decoded.signed_transfer && !decoded.halfword) {
        const std::optional<std::uint8_t> value = memory.read8(address);
        if (!value.has_value()) {
          return ExecuteStatus::unsupported;
        }
        registers_.at(decoded.rd) = sign_extend8(value.value());
        if (needs_write_back) {
          registers_.at(decoded.rn) = write_back_address;
        }
        return ExecuteStatus::executed;
      }

      const std::optional<std::uint16_t> value = memory.read16(address);
      if (!value.has_value()) {
        return ExecuteStatus::unsupported;
      }
      registers_.at(decoded.rd) =
          decoded.signed_transfer ? sign_extend16(value.value()) : value.value();
      if (needs_write_back) {
        registers_.at(decoded.rn) = write_back_address;
      }
      return ExecuteStatus::executed;
    }

    if (decoded.signed_transfer || !decoded.halfword) {
      return ExecuteStatus::unsupported;
    }

    if (!memory.write16(address,
                        static_cast<std::uint16_t>(registers_.at(decoded.rd) & 0xFFFFU))) {
      return ExecuteStatus::unsupported;
    }
    if (needs_write_back) {
      registers_.at(decoded.rn) = write_back_address;
    }
    return ExecuteStatus::executed;
  }

  if (!can_decode_single_data_transfer_immediate(instruction) &&
      !can_decode_single_data_transfer_register(instruction)) {
    return execute_arm(instruction);
  }

  const DecodedSingleDataTransferInstruction decoded =
      can_decode_single_data_transfer_register(instruction)
          ? decode_single_data_transfer_register(instruction,
                                                 arm_visible_register_value(
                                                     bits(instruction, 0, 0xFU)),
                                                 carry_)
          : decode_single_data_transfer_immediate(instruction);
  if (!condition_passed(decoded.condition)) {
    return ExecuteStatus::skipped_condition;
  }

  if (!supported_transfer_addressing(decoded.pre_index, decoded.write_back)) {
    return ExecuteStatus::unsupported;
  }

  const std::uint32_t base = arm_visible_register_value(decoded.rn);
  const std::uint32_t write_back_address =
      offset_transfer_address(base, decoded.offset, decoded.up);
  const std::uint32_t address = decoded.pre_index ? write_back_address : base;
  const bool needs_write_back =
      transfer_needs_write_back(decoded.pre_index, decoded.write_back);
  if (needs_write_back && decoded.load && decoded.rn == decoded.rd) {
    return ExecuteStatus::unsupported;
  }

  if (decoded.load) {
    if (decoded.byte) {
      const std::optional<std::uint8_t> value = memory.read8(address);
      if (!value.has_value()) {
        return ExecuteStatus::unsupported;
      }
      registers_.at(decoded.rd) = value.value();
      if (needs_write_back) {
        registers_.at(decoded.rn) = write_back_address;
      }
      return ExecuteStatus::executed;
    }

    const std::optional<std::uint32_t> value = memory.read32(address);
    if (!value.has_value()) {
      return ExecuteStatus::unsupported;
    }
    registers_.at(decoded.rd) = value.value();
    if (needs_write_back) {
      registers_.at(decoded.rn) = write_back_address;
    }
    return ExecuteStatus::executed;
  }

  if (decoded.byte) {
    if (!memory.write8(address,
                       static_cast<std::uint8_t>(registers_.at(decoded.rd) & 0xFFU))) {
      return ExecuteStatus::unsupported;
    }
    if (needs_write_back) {
      registers_.at(decoded.rn) = write_back_address;
    }
    return ExecuteStatus::executed;
  }

  if (!memory.write32(address, registers_.at(decoded.rd))) {
    return ExecuteStatus::unsupported;
  }
  if (needs_write_back) {
    registers_.at(decoded.rn) = write_back_address;
  }
  return ExecuteStatus::executed;
}

std::optional<std::uint32_t> Arm7tdmi::first_data_address(
    std::uint32_t instruction) const {
  if (can_decode_swap(instruction)) {
    return registers_.at(bits(instruction, 16, 0xFU));
  }

  if (can_decode_block_data_transfer(instruction)) {
    const DecodedBlockDataTransferInstruction decoded = decode_block_data_transfer(instruction);
    const std::uint8_t register_count = count_registers(decoded.register_list);
    return block_transfer_address(registers_.at(decoded.rn), register_count, decoded.pre_index,
                                  decoded.up)
        .first;
  }

  if (can_decode_halfword_data_transfer_immediate(instruction) ||
      can_decode_halfword_data_transfer_register(instruction)) {
    const DecodedHalfwordDataTransferInstruction decoded =
        can_decode_halfword_data_transfer_register(instruction)
            ? decode_halfword_data_transfer_register(instruction,
                                                     arm_visible_register_value(
                                                         bits(instruction, 0, 0xFU)))
            : decode_halfword_data_transfer_immediate(instruction);
    const std::uint32_t base = arm_visible_register_value(decoded.rn);
    const std::uint32_t offset_address =
        offset_transfer_address(base, decoded.offset, decoded.up);
    return decoded.pre_index ? offset_address : base;
  }

  if (can_decode_single_data_transfer_immediate(instruction) ||
      can_decode_single_data_transfer_register(instruction)) {
    const DecodedSingleDataTransferInstruction decoded =
        can_decode_single_data_transfer_register(instruction)
            ? decode_single_data_transfer_register(instruction,
                                                   arm_visible_register_value(
                                                       bits(instruction, 0, 0xFU)),
                                                   carry_)
            : decode_single_data_transfer_immediate(instruction);
    const std::uint32_t base = arm_visible_register_value(decoded.rn);
    const std::uint32_t offset_address =
        offset_transfer_address(base, decoded.offset, decoded.up);
    return decoded.pre_index ? offset_address : base;
  }

  return std::nullopt;
}

bool Arm7tdmi::memory_condition_passed(std::uint32_t instruction) const {
  if (can_decode_swap(instruction)) {
    return condition_passed(decode_swap(instruction).condition);
  }

  if (can_decode_block_data_transfer(instruction)) {
    return condition_passed(decode_block_data_transfer(instruction).condition);
  }

  if (can_decode_halfword_data_transfer_immediate(instruction) ||
      can_decode_halfword_data_transfer_register(instruction)) {
    const DecodedHalfwordDataTransferInstruction decoded =
        can_decode_halfword_data_transfer_register(instruction)
            ? decode_halfword_data_transfer_register(instruction,
                                                     registers_.at(bits(instruction, 0, 0xFU)))
            : decode_halfword_data_transfer_immediate(instruction);
    return condition_passed(decoded.condition);
  }

  if (can_decode_single_data_transfer_immediate(instruction) ||
      can_decode_single_data_transfer_register(instruction)) {
    const DecodedSingleDataTransferInstruction decoded =
        can_decode_single_data_transfer_register(instruction)
            ? decode_single_data_transfer_register(instruction,
                                                   registers_.at(bits(instruction, 0, 0xFU)),
                                                   carry_)
            : decode_single_data_transfer_immediate(instruction);
    return condition_passed(decoded.condition);
  }

  return true;
}

ArmStepResult Arm7tdmi::finish_step(std::uint32_t instruction,
                                    std::optional<std::uint32_t> data_address,
                                    ExecuteStatus status,
                                    const WaitStateControl* waitcnt) {
  if (status != ExecuteStatus::executed) {
    return {status, 0, elapsed_cycles_, false, false};
  }

  const std::optional<ArmElapsedCycleEstimate> estimate =
      waitcnt == nullptr
          ? estimate_arm_elapsed_cycles(instruction, data_address.value_or(0))
          : estimate_arm_elapsed_cycles(instruction, data_address.value_or(0), *waitcnt);
  if (!estimate.has_value()) {
    return {status, 0, elapsed_cycles_, false, false};
  }

  elapsed_cycles_ += estimate.value().cycles;
  return {
      status,
      estimate.value().cycles,
      elapsed_cycles_,
      estimate.value().data_dependent,
      estimate.value().memory_timing_applied,
  };
}

ArmStepResult Arm7tdmi::step_arm(std::uint32_t instruction) {
  const ExecuteStatus status = execute_arm(instruction);
  return finish_step(instruction, std::nullopt, status);
}

ArmStepResult Arm7tdmi::step_arm(std::uint32_t instruction, MemoryBus& memory) {
  const std::optional<std::uint32_t> data_address = first_data_address(instruction);
  if (data_address.has_value() && memory_condition_passed(instruction) &&
      !estimate_arm_elapsed_cycles(instruction, data_address.value()).has_value()) {
    return {ExecuteStatus::unsupported, 0, elapsed_cycles_, false, false};
  }
  const ExecuteStatus status = execute_arm(instruction, memory);
  return finish_step(instruction, data_address, status);
}

ArmStepResult Arm7tdmi::step_arm(std::uint32_t instruction, MemoryBus& memory,
                                 const WaitStateControl& waitcnt) {
  const std::optional<std::uint32_t> data_address = first_data_address(instruction);
  if (data_address.has_value() && memory_condition_passed(instruction) &&
      !estimate_arm_elapsed_cycles(instruction, data_address.value(), waitcnt).has_value()) {
    return {ExecuteStatus::unsupported, 0, elapsed_cycles_, false, false};
  }
  const ExecuteStatus status = execute_arm(instruction, memory);
  return finish_step(instruction, data_address, status, &waitcnt);
}

ArmStepResult Arm7tdmi::step_thumb(std::uint16_t instruction) {
  const ExecuteStatus status = execute_thumb(instruction);
  if (status != ExecuteStatus::executed) {
    return {status, 0, elapsed_cycles_, false, false};
  }

  constexpr std::uint32_t kSeedThumbElapsedCycles = 1;
  elapsed_cycles_ += kSeedThumbElapsedCycles;
  return {status, kSeedThumbElapsedCycles, elapsed_cycles_, false, false};
}

ArmStepResult Arm7tdmi::step_thumb(std::uint16_t instruction, MemoryBus& memory) {
  const ExecuteStatus status = execute_thumb(instruction, memory);
  if (status != ExecuteStatus::executed) {
    return {status, 0, elapsed_cycles_, false, false};
  }

  constexpr std::uint32_t kSeedThumbElapsedCycles = 1;
  elapsed_cycles_ += kSeedThumbElapsedCycles;
  return {status, kSeedThumbElapsedCycles, elapsed_cycles_, false, false};
}

bool Arm7tdmi::condition_passed(ArmCondition condition) const {
  switch (condition) {
    case ArmCondition::eq:
      return zero_;
    case ArmCondition::ne:
      return !zero_;
    case ArmCondition::cs:
      return carry_;
    case ArmCondition::cc:
      return !carry_;
    case ArmCondition::mi:
      return negative_;
    case ArmCondition::pl:
      return !negative_;
    case ArmCondition::vs:
      return overflow_;
    case ArmCondition::vc:
      return !overflow_;
    case ArmCondition::hi:
      return carry_ && !zero_;
    case ArmCondition::ls:
      return !carry_ || zero_;
    case ArmCondition::ge:
      return negative_ == overflow_;
    case ArmCondition::lt:
      return negative_ != overflow_;
    case ArmCondition::gt:
      return !zero_ && negative_ == overflow_;
    case ArmCondition::le:
      return zero_ || negative_ != overflow_;
    case ArmCondition::al:
      return true;
  }
  return false;
}

void Arm7tdmi::set_nz(std::uint32_t result) {
  negative_ = (result & 0x80000000U) != 0;
  zero_ = result == 0;
}

void Arm7tdmi::set_nz64(std::uint64_t result) {
  negative_ = (result & 0x8000000000000000ULL) != 0;
  zero_ = result == 0;
}

void Arm7tdmi::set_add_flags(std::uint32_t left, std::uint32_t right, std::uint32_t result) {
  set_nz(result);
  carry_ = result < left;
  const bool left_negative = (left & 0x80000000U) != 0;
  const bool right_negative = (right & 0x80000000U) != 0;
  const bool result_negative = (result & 0x80000000U) != 0;
  overflow_ = left_negative == right_negative && left_negative != result_negative;
}

void Arm7tdmi::set_adc_flags(std::uint32_t left, std::uint32_t right, bool carry_in,
                             std::uint32_t result) {
  set_nz(result);
  const std::uint64_t sum = static_cast<std::uint64_t>(left) +
                            static_cast<std::uint64_t>(right) + (carry_in ? 1ULL : 0ULL);
  carry_ = sum > 0xFFFFFFFFULL;
  const bool left_negative = (left & 0x80000000U) != 0;
  const bool right_negative = (right & 0x80000000U) != 0;
  const bool result_negative = (result & 0x80000000U) != 0;
  overflow_ = left_negative == right_negative && left_negative != result_negative;
}

void Arm7tdmi::set_sub_flags(std::uint32_t left, std::uint32_t right, std::uint32_t result) {
  set_nz(result);
  carry_ = left >= right;
  const bool left_negative = (left & 0x80000000U) != 0;
  const bool right_negative = (right & 0x80000000U) != 0;
  const bool result_negative = (result & 0x80000000U) != 0;
  overflow_ = left_negative != right_negative && left_negative != result_negative;
}

void Arm7tdmi::set_sbc_flags(std::uint32_t left, std::uint32_t right, bool carry_in,
                             std::uint32_t result) {
  set_nz(result);
  const std::uint64_t subtrahend =
      static_cast<std::uint64_t>(right) + (carry_in ? 0ULL : 1ULL);
  carry_ = static_cast<std::uint64_t>(left) >= subtrahend;
  const bool left_negative = (left & 0x80000000U) != 0;
  const bool right_negative = (right & 0x80000000U) != 0;
  const bool result_negative = (result & 0x80000000U) != 0;
  overflow_ = left_negative != right_negative && left_negative != result_negative;
}

void Arm7tdmi::set_logical_flags(std::uint32_t result,
                                 const DecodedArmInstruction& decoded) {
  set_nz(result);
  if (decoded.shifter_carry_valid) {
    carry_ = decoded.shifter_carry;
  }
}

}  // namespace gba::core
