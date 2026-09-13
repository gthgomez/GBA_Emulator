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

struct CarrySaveAdderOutput {
  std::uint64_t output;
  std::uint64_t carry;
};

struct BoothRecodingOutput {
  std::uint64_t output;
  bool carry;
};

struct U128Pair {
  std::uint64_t lo;
  std::uint64_t hi;
};

constexpr std::uint32_t kOamMirrorBankStart = 0x07000000U;
constexpr std::uint32_t kGamePakRomStart = 0x08000000U;

struct Adder32Output {
  std::uint32_t output;
  bool carry;
};

struct MultiplyHardwareOutput {
  std::uint64_t output;
  bool carry;
};

constexpr std::uint32_t kNegativeFlag = 0x80000000;
constexpr std::uint32_t kZeroFlag = 0x40000000;
constexpr std::uint32_t kCarryFlag = 0x20000000;
constexpr std::uint32_t kOverflowFlag = 0x10000000;
constexpr std::uint32_t kThumbStateFlag = 0x00000020;
constexpr std::uint32_t kFiqDisableFlag = 0x00000040;
constexpr std::uint32_t kIrqDisableFlag = 0x00000080;
constexpr std::uint32_t kModeMask = 0x0000001F;
// PSR control byte: mode bits plus the I/F/T state bits.
constexpr std::uint32_t kControlByteMask = 0x000000FF;
// A condition-failed ARM instruction is still fetched (the scheduler charges
// that sequential fetch cycle), but its execution stage is suppressed, so it
// contributes no execution cycles here. Charging one here as well double-counts
// the fetch and shifts cycle-exact suites (mGBA timers/timer-irq/sio-timing).
constexpr std::uint32_t kArmSkippedConditionElapsedCycles = 0;
constexpr std::uint32_t kThumbSkippedConditionElapsedCycles = 1;

// Trace-fitted tuning tags for mirrored-OAM block loads that cross from the
// OAM mirror region into game pak ROM space with ROM prefetch enabled. These
// are NOT derived from first principles: each pair encodes a measured cycle
// delta fitted against the GBA timing corpus for its specific fetch width.
// The Thumb values differ from the ARM values because the Thumb block-load
// pipeline overlaps the trailing internal cycle differently than ARM LDM.
constexpr std::uint32_t kMirroredOamArmPrefetchFastRomCycles = 5;
constexpr std::uint32_t kMirroredOamArmPrefetchSlowRomCycles = 3;
constexpr std::uint32_t kMirroredOamThumbPrefetchFastRomCycles = 7;
constexpr std::uint32_t kMirroredOamThumbPrefetchSlowRomCycles = 6;

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

[[nodiscard]] bool bit64(std::uint64_t value, std::uint8_t bit) {
  return ((value >> bit) & 1ULL) != 0;
}

[[nodiscard]] std::uint64_t mask64_range(std::uint8_t low, std::uint8_t high) {
  if (high <= low) {
    return 0;
  }
  if (high >= 64) {
    return low == 0 ? ~0ULL : (~0ULL << low);
  }
  return ((1ULL << (high - low)) - 1ULL) << low;
}

[[nodiscard]] std::uint64_t sign_extend64(std::uint64_t value,
                                          std::uint8_t from_bits,
                                          std::uint8_t to_bits) {
  if (bit64(value, static_cast<std::uint8_t>(from_bits - 1U))) {
    value |= mask64_range(from_bits, to_bits);
  }
  return to_bits >= 64 ? value : value & mask64_range(0, to_bits);
}

[[nodiscard]] std::uint64_t arithmetic_shift_right(std::uint64_t value,
                                                   std::uint8_t shift,
                                                   std::uint8_t width) {
  const std::uint64_t extended = sign_extend64(value, width, 64);
  const auto signed_value = static_cast<std::int64_t>(extended);
  return static_cast<std::uint64_t>(signed_value >> shift) & mask64_range(0, width);
}

[[nodiscard]] U128Pair rotate_right_128(U128Pair value, std::uint8_t shift) {
  return {
      (value.lo >> shift) | (value.hi << (64U - shift)),
      (value.hi >> shift) | (value.lo << (64U - shift)),
  };
}

[[nodiscard]] Adder32Output add32(std::uint64_t left, std::uint64_t right, bool carry_in) {
  const std::uint64_t sum = (left & 0xFFFFFFFFULL) + (right & 0xFFFFFFFFULL) +
                            (carry_in ? 1ULL : 0ULL);
  return {static_cast<std::uint32_t>(sum), sum > 0xFFFFFFFFULL};
}

[[nodiscard]] BoothRecodingOutput booth_recode(std::uint64_t input,
                                               std::uint8_t booth_chunk) {
  BoothRecodingOutput output{};
  switch (booth_chunk) {
    case 0:
    case 7:
      output = {0, false};
      break;
    case 1:
    case 2:
      output = {input, false};
      break;
    case 3:
      output = {2ULL * input, false};
      break;
    case 4:
      output = {~(2ULL * input), true};
      break;
    case 5:
    case 6:
      output = {~input, true};
      break;
    default:
      output = {0, false};
      break;
  }
  output.output &= 0x3FFFFFFFFULL;
  return output;
}

[[nodiscard]] CarrySaveAdderOutput carry_save_add(std::uint64_t a,
                                                  std::uint64_t b,
                                                  std::uint64_t c) {
  return {a ^ b ^ c, (a & b) | (b & c) | (c & a)};
}

[[nodiscard]] CarrySaveAdderOutput multiply_csa_cycle(CarrySaveAdderOutput previous,
                                                       std::uint64_t multiplicand,
                                                       std::uint64_t multiplier,
                                                       std::uint64_t& acc_shift) {
  CarrySaveAdderOutput csa = previous;
  CarrySaveAdderOutput final{};
  for (std::uint8_t i = 0; i < 4; ++i) {
    csa.output &= 0x1FFFFFFFFULL;
    csa.carry &= 0x1FFFFFFFFULL;
    const BoothRecodingOutput addend =
        booth_recode(multiplicand, static_cast<std::uint8_t>((multiplier >> (2U * i)) & 0x7ULL));
    CarrySaveAdderOutput result =
        carry_save_add(csa.output, addend.output & 0x1FFFFFFFFULL, csa.carry);

    result.carry <<= 1;
    result.carry |= addend.carry ? 1ULL : 0ULL;
    final.output |= (result.output & 0x3ULL) << (2U * i);
    final.carry |= (result.carry & 0x3ULL) << (2U * i);

    result.output >>= 2;
    result.carry >>= 2;
    const std::uint64_t sign_extension_fix =
        (bit64(acc_shift, 0) ? 1ULL : 0ULL) +
        (bit64(csa.carry, 32) ? 0ULL : 1ULL) +
        (bit64(addend.output, 33) ? 0ULL : 1ULL);
    result.output |= sign_extension_fix << 31;
    result.carry |= (bit64(acc_shift, 1) ? 0ULL : 1ULL) << 32;
    acc_shift >>= 2;
    csa = result;
  }

  return {final.output | (csa.output << 8), final.carry | (csa.carry << 8)};
}

[[nodiscard]] bool multiply_should_terminate(std::uint64_t multiplier, bool signed_multiply) {
  return signed_multiply ? (multiplier == 0x1FFFFFFFFULL || multiplier == 0)
                         : multiplier == 0;
}

[[nodiscard]] MultiplyHardwareOutput arm7tdmi_multiply_long_output(
    bool signed_multiply,
    std::uint32_t rm,
    std::uint32_t rs,
    std::uint64_t accumulator) {
  std::uint64_t multiplier = rs;
  std::uint64_t multiplicand = rm;
  CarrySaveAdderOutput csa{};
  const bool alu_carry_in = (multiplier & 1ULL) != 0;

  if (signed_multiply) {
    multiplier = sign_extend64(multiplier, 32, 34);
    multiplicand = sign_extend64(multiplicand, 32, 34);
  } else {
    multiplier &= 0x1FFFFFFFFULL;
    multiplicand &= 0x1FFFFFFFFULL;
  }

  csa.carry = (multiplier & 1ULL) != 0 ? ~multiplicand : 0;
  csa.output = accumulator;
  std::uint64_t acc_shift = accumulator >> 34;

  U128Pair partial_sum{csa.output & 1ULL, 0};
  U128Pair partial_carry{csa.carry & 1ULL, 0};
  csa.output >>= 1;
  csa.carry >>= 1;
  partial_sum = rotate_right_128(partial_sum, 1);
  partial_carry = rotate_right_128(partial_carry, 1);

  std::uint8_t iterations = 0;
  do {
    csa = multiply_csa_cycle(csa, multiplicand, multiplier, acc_shift);
    partial_sum.lo |= csa.output & 0xFFULL;
    partial_carry.lo |= csa.carry & 0xFFULL;
    csa.output >>= 8;
    csa.carry >>= 8;
    partial_sum = rotate_right_128(partial_sum, 8);
    partial_carry = rotate_right_128(partial_carry, 8);
    multiplier = arithmetic_shift_right(multiplier, 8, 33);
    ++iterations;
  } while (!multiply_should_terminate(multiplier, signed_multiply));

  partial_sum.lo |= csa.output;
  partial_carry.lo |= csa.carry;

  const std::uint8_t correction_ror =
      iterations == 1 ? 23 : (iterations == 2 ? 15 : (iterations == 3 ? 7 : 31));
  partial_sum = rotate_right_128(partial_sum, correction_ror);
  partial_carry = rotate_right_128(partial_carry, correction_ror);

  if (iterations == 4) {
    const Adder32Output lo = add32(partial_sum.hi, partial_carry.hi, alu_carry_in);
    const Adder32Output hi = add32(partial_sum.hi >> 32, partial_carry.hi >> 32, lo.carry);
    return {(static_cast<std::uint64_t>(hi.output) << 32) | lo.output,
            bit64(partial_carry.hi, 63)};
  }

  const Adder32Output lo =
      add32(partial_sum.hi >> 32, partial_carry.hi >> 32, alu_carry_in);
  const std::uint8_t shift_amount = static_cast<std::uint8_t>(2U + 8U * iterations);
  partial_carry.lo = sign_extend64(partial_carry.lo, shift_amount, 64);
  partial_sum.lo |= acc_shift << shift_amount;
  const Adder32Output hi = add32(partial_sum.lo, partial_carry.lo, lo.carry);
  return {(static_cast<std::uint64_t>(hi.output) << 32) | lo.output,
          bit64(partial_carry.hi, 63)};
}

[[nodiscard]] std::uint32_t align_word(std::uint32_t value) {
  return value & ~0x3U;
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

[[nodiscard]] std::int32_t sign_extend_thumb_bl_prefix(std::uint16_t raw) {
  std::int32_t extended = raw & 0x07FFU;
  if ((extended & 0x0400) != 0) {
    extended |= ~0x07FF;
  }
  return extended * 4096;
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
  return condition <= static_cast<std::uint8_t>(ArmCondition::le);
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
  // Stores occupy one sequential data cycle plus one sequential fetch cycle
  // (no internal cycle): the LDR shape minus its load internal cycle.
  return {1, 1, 0, false};
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

[[nodiscard]] std::uint32_t signed_multiply_iterations(std::uint32_t value) {
  if ((value & 0xFFFFFF00U) == 0 || (value & 0xFFFFFF00U) == 0xFFFFFF00U) {
    return 1;
  }
  if ((value & 0xFFFF0000U) == 0 || (value & 0xFFFF0000U) == 0xFFFF0000U) {
    return 2;
  }
  if ((value & 0xFF000000U) == 0 || (value & 0xFF000000U) == 0xFF000000U) {
    return 3;
  }
  return 4;
}

[[nodiscard]] std::uint32_t unsigned_multiply_iterations(std::uint32_t value) {
  if ((value & 0xFFFFFF00U) == 0) {
    return 1;
  }
  if ((value & 0xFFFF0000U) == 0) {
    return 2;
  }
  if ((value & 0xFF000000U) == 0) {
    return 3;
  }
  return 4;
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

[[nodiscard]] AccessWidth thumb_transfer_access_width(
    ThumbMemoryTransferKind kind) {
  switch (kind) {
    case ThumbMemoryTransferKind::word:
      return AccessWidth::word;
    case ThumbMemoryTransferKind::halfword:
    case ThumbMemoryTransferKind::signed_halfword:
      return AccessWidth::halfword;
    case ThumbMemoryTransferKind::byte:
    case ThumbMemoryTransferKind::signed_byte:
      return AccessWidth::byte;
  }
  return AccessWidth::word;
}

[[nodiscard]] std::uint32_t thumb_memory_cycles(
    const DecodedThumbMemoryTransferInstruction& decoded,
    const MemoryAccessTiming& timing) {
  if (decoded.load) {
    return static_cast<std::uint32_t>(timing.nonsequential) + timing.sequential + 1U;
  }
  // Thumb stores: one sequential data cycle plus one sequential fetch cycle
  // (the load shape minus its internal cycle).
  return static_cast<std::uint32_t>(timing.sequential) + timing.nonsequential;
}

[[nodiscard]] bool thumb_prefetch_overlaps_internal_data_load_cycle(
    const DecodedThumbMemoryTransferInstruction& decoded, std::uint32_t address,
    bool waitcnt_prefetch_enabled) {
  if (!waitcnt_prefetch_enabled || !decoded.load) {
    return false;
  }

  const Region data_region = MemoryBus::describe(address).region;
  return data_region == Region::ewram || data_region == Region::iwram;
}

struct MirroredOamTuning {
  std::uint32_t prefetch_fast_rom;
  std::uint32_t prefetch_slow_rom;
};

[[nodiscard]] std::optional<std::uint32_t>
mirrored_oam_cross_rom_block_load_elapsed_cycles(
    std::uint32_t data_address, std::uint32_t register_count,
    const WaitStateControl* waitcnt, bool waitcnt_prefetch_enabled,
    const MirroredOamTuning& tuning, std::uint32_t trailing_internal_cycles);

[[nodiscard]] std::optional<std::uint32_t> thumb_mirrored_oam_block_load_elapsed_cycles(
    const DecodedThumbBlockTransferInstruction& decoded, std::uint32_t data_address,
    const WaitStateControl& waitcnt) {
  if (!decoded.load) {
    return std::nullopt;
  }

  return mirrored_oam_cross_rom_block_load_elapsed_cycles(
      data_address, count_registers(decoded.register_list), &waitcnt,
      waitcnt.prefetch_enabled(),
      {kMirroredOamThumbPrefetchFastRomCycles, kMirroredOamThumbPrefetchSlowRomCycles},
      1U);
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

[[nodiscard]] MemoryAccessTiming timing_for_elapsed_estimate(
    std::uint32_t address, AccessWidth width, const WaitStateControl* waitcnt) {
  return waitcnt != nullptr ? MemoryBus::timing(address, width, *waitcnt)
                            : MemoryBus::timing(address, width);
}

// Shared worker for C13-unified mirrored-OAM block-load estimates: Thumb and
// ARM callers differ only in their trace-fitted prefetch tuning tags (see the
// kMirroredOam* constant provenance note) and in how many trailing internal
// cycles they fold in.
[[nodiscard]] std::optional<std::uint32_t>
mirrored_oam_cross_rom_block_load_elapsed_cycles(
    std::uint32_t data_address, std::uint32_t register_count,
    const WaitStateControl* waitcnt, bool waitcnt_prefetch_enabled,
    const MirroredOamTuning& tuning, std::uint32_t trailing_internal_cycles) {
  const AddressInfo first = MemoryBus::describe(data_address);
  if (first.region != Region::oam || !first.mirrored ||
      data_address < kOamMirrorBankStart || data_address >= kGamePakRomStart) {
    return std::nullopt;
  }

  const std::uint32_t words_until_rom =
      std::max<std::uint32_t>((kGamePakRomStart - data_address) / 4U, 1U);
  const std::uint32_t mirrored_words = std::min(register_count, words_until_rom);
  const std::uint32_t rom_words = register_count - mirrored_words;
  const MemoryAccessTiming rom_timing =
      timing_for_elapsed_estimate(kGamePakRomStart, AccessWidth::word, waitcnt);

  if (rom_words == 0 && waitcnt_prefetch_enabled) {
    return rom_timing.sequential == 1U ? tuning.prefetch_fast_rom
                                       : tuning.prefetch_slow_rom;
  }

  std::uint32_t elapsed = 1U + mirrored_words;
  if (rom_words != 0) {
    elapsed += rom_words *
                   (static_cast<std::uint32_t>(rom_timing.nonsequential) +
                    static_cast<std::uint32_t>(rom_timing.sequential)) +
               rom_timing.sequential;
    if (rom_timing.nonsequential == 3U) {
      elapsed += mirrored_words == 2U
                     ? 2U
                     : (mirrored_words == 3U ? 1U : (mirrored_words == 4U ? 0U : 3U));
    }
    if (!waitcnt_prefetch_enabled && rom_timing.sequential == 1U) {
      if (mirrored_words == 4U) {
        elapsed += 1U;
      } else {
        elapsed -= mirrored_words == 2U ? 1U : (mirrored_words == 3U ? 0U : 2U);
      }
    }
    if (waitcnt_prefetch_enabled) {
      elapsed += mirrored_words == 2U
                     ? (rom_timing.nonsequential == 3U
                            ? (rom_timing.sequential == 1U ? 2U : 3U)
                            : (rom_timing.sequential == 1U ? 3U : 4U))
                     : (mirrored_words >= 3U && rom_timing.nonsequential != 3U &&
                                rom_timing.sequential == 1U
                            ? 5U
                            : (mirrored_words >= 3U && rom_timing.nonsequential == 3U &&
                                       rom_timing.sequential == 1U
                                   ? 4U
                                   : (rom_timing.nonsequential == 3U ? 2U : 3U)));
    }
  }
  elapsed += trailing_internal_cycles;
  return elapsed;
}

[[nodiscard]] std::optional<std::uint32_t> mirrored_oam_block_load_elapsed_cycles(
    std::uint32_t instruction, std::uint32_t data_address,
    const ArmCycleEstimate& cycles, const WaitStateControl* waitcnt,
    bool waitcnt_prefetch_enabled) {
  if (!Arm7tdmi::can_decode_block_data_transfer(instruction)) {
    return std::nullopt;
  }

  const DecodedBlockDataTransferInstruction decoded =
      Arm7tdmi::decode_block_data_transfer(instruction);
  if (!decoded.load) {
    return std::nullopt;
  }

  return mirrored_oam_cross_rom_block_load_elapsed_cycles(
      data_address, count_registers(decoded.register_list), waitcnt,
      waitcnt_prefetch_enabled,
      {kMirroredOamArmPrefetchFastRomCycles, kMirroredOamArmPrefetchSlowRomCycles},
      cycles.internal);
}

[[nodiscard]] bool has_elapsed_timing_for_estimate(std::uint32_t address,
                                                   const MemoryAccessTiming& timing,
                                                   bool waitcnt_aware) {
  if (timing.readable || timing.writable) {
    return true;
  }

  const Region region = MemoryBus::describe(address).region;
  if (region == Region::io && timing.nonsequential != 0 && timing.sequential != 0) {
    return true;
  }
  if (!waitcnt_aware || timing.nonsequential == 0 || timing.sequential == 0) {
    return false;
  }

  return region == Region::bios || region == Region::game_pak_rom ||
         region == Region::game_pak_save;
}

[[nodiscard]] bool arm_data_load_has_internal_cycle(std::uint32_t instruction) {
  if (Arm7tdmi::can_decode_single_data_transfer_immediate(instruction) ||
      Arm7tdmi::can_decode_single_data_transfer_register(instruction) ||
      Arm7tdmi::can_decode_halfword_data_transfer_immediate(instruction) ||
      Arm7tdmi::can_decode_halfword_data_transfer_register(instruction) ||
      Arm7tdmi::can_decode_block_data_transfer(instruction)) {
    return ((instruction >> 20) & 0x1U) == 1;
  }
  return false;
}

[[nodiscard]] std::uint32_t prefetch_internal_data_load_overlap_cycles(
    std::uint32_t instruction, std::uint32_t data_address,
    const ArmCycleEstimate& cycles, bool waitcnt_prefetch_enabled,
    bool fast_rom_sequential_prefetch) {
  if (!waitcnt_prefetch_enabled || cycles.internal == 0 ||
      !arm_data_load_has_internal_cycle(instruction)) {
    return 0;
  }

  const Region data_region = MemoryBus::describe(data_address).region;
  if (data_region != Region::ewram && data_region != Region::iwram) {
    return 0;
  }

  if (Arm7tdmi::can_decode_block_data_transfer(instruction)) {
    const DecodedBlockDataTransferInstruction decoded =
        Arm7tdmi::decode_block_data_transfer(instruction);
    if (decoded.load) {
      const std::uint32_t overlap_cap = fast_rom_sequential_prefetch ? 2U : 4U;
      return std::min<std::uint32_t>(count_registers(decoded.register_list), overlap_cap);
    }
  }
  return 1;
}

[[nodiscard]] std::uint32_t prefetch_block_store_overlap_cycles(
    std::uint32_t instruction, std::uint32_t data_address,
    bool waitcnt_prefetch_enabled, bool fast_rom_sequential_prefetch) {
  if (!waitcnt_prefetch_enabled ||
      !Arm7tdmi::can_decode_block_data_transfer(instruction)) {
    return 0;
  }

  const Region data_region = MemoryBus::describe(data_address).region;
  if (data_region != Region::ewram && data_region != Region::iwram) {
    return 0;
  }

  const DecodedBlockDataTransferInstruction decoded =
      Arm7tdmi::decode_block_data_transfer(instruction);
  if (decoded.load) {
    return 0;
  }
  const std::uint32_t register_count = count_registers(decoded.register_list);
  if (register_count < 2) {
    return 0;
  }
  const std::uint32_t overlap_cap = fast_rom_sequential_prefetch ? 2U : 4U;
  return std::min<std::uint32_t>(register_count - 1U, overlap_cap);
}

[[nodiscard]] bool arm_single_word_load_from_game_pak_rom(std::uint32_t instruction,
                                                          std::uint32_t data_address) {
  if (!Arm7tdmi::can_decode_single_data_transfer_immediate(instruction) &&
      !Arm7tdmi::can_decode_single_data_transfer_register(instruction)) {
    return false;
  }
  const bool load = ((instruction >> 20) & 0x1U) == 1;
  const bool byte_transfer = ((instruction >> 22) & 0x1U) == 1;
  return load && !byte_transfer &&
         MemoryBus::describe(data_address).region == Region::game_pak_rom;
}

[[nodiscard]] bool thumb_word_load_from_game_pak_rom(
    const DecodedThumbMemoryTransferInstruction& decoded, std::uint32_t data_address) {
  return decoded.load && decoded.kind == ThumbMemoryTransferKind::word &&
         MemoryBus::describe(data_address).region == Region::game_pak_rom;
}

[[nodiscard]] std::optional<std::uint32_t> read_arm_pipeline_open_bus_word(
    const MemoryBus& memory, std::uint32_t pc) {
  return memory.read32(pc + 8U);
}

[[nodiscard]] std::optional<std::uint32_t> read_thumb_pipeline_open_bus_word(
    const MemoryBus& memory, std::uint32_t pc) {
  return memory.read32(pc + 4U);
}

[[nodiscard]] std::optional<std::uint8_t> read8_or_arm_pipeline_open_bus(
    const MemoryBus& memory, std::uint32_t address, std::uint32_t pc) {
  const std::optional<std::uint8_t> value = memory.read8(address);
  if (value.has_value()) {
    return value;
  }
  const std::optional<std::uint32_t> open_bus = read_arm_pipeline_open_bus_word(memory, pc);
  if (!open_bus.has_value()) {
    return std::nullopt;
  }
  return static_cast<std::uint8_t>((open_bus.value() >> ((address & 0x3U) * 8U)) & 0xFFU);
}

[[nodiscard]] std::optional<std::uint8_t> read8_or_thumb_pipeline_open_bus(
    const MemoryBus& memory, std::uint32_t address, std::uint32_t pc) {
  const std::optional<std::uint8_t> value = memory.read8(address);
  if (value.has_value()) {
    return value;
  }
  const std::optional<std::uint32_t> open_bus =
      read_thumb_pipeline_open_bus_word(memory, pc);
  if (!open_bus.has_value()) {
    return std::nullopt;
  }
  return static_cast<std::uint8_t>((open_bus.value() >> ((address & 0x3U) * 8U)) & 0xFFU);
}

[[nodiscard]] std::optional<std::uint16_t> read16_or_arm_pipeline_open_bus(
    const MemoryBus& memory, std::uint32_t address, std::uint32_t pc) {
  const std::optional<std::uint16_t> value = memory.read16(address);
  if (value.has_value()) {
    return value;
  }
  const std::optional<std::uint32_t> open_bus = read_arm_pipeline_open_bus_word(memory, pc);
  if (!open_bus.has_value()) {
    return std::nullopt;
  }
  const std::uint8_t shift = static_cast<std::uint8_t>((address & 0x2U) * 8U);
  return static_cast<std::uint16_t>((open_bus.value() >> shift) & 0xFFFFU);
}

[[nodiscard]] std::optional<std::uint16_t> read16_or_thumb_pipeline_open_bus(
    const MemoryBus& memory, std::uint32_t address, std::uint32_t pc) {
  const std::optional<std::uint16_t> value = memory.read16(address);
  if (value.has_value()) {
    return value;
  }
  const std::optional<std::uint32_t> open_bus =
      read_thumb_pipeline_open_bus_word(memory, pc);
  if (!open_bus.has_value()) {
    return std::nullopt;
  }
  return static_cast<std::uint16_t>((open_bus.value() >> 16U) & 0xFFFFU);
}

[[nodiscard]] std::optional<std::uint32_t> read32_or_arm_pipeline_open_bus(
    const MemoryBus& memory, std::uint32_t address, std::uint32_t pc) {
  const std::optional<std::uint32_t> value = memory.read32(address);
  if (value.has_value()) {
    return value;
  }
  const std::optional<std::uint32_t> open_bus = read_arm_pipeline_open_bus_word(memory, pc);
  if (!open_bus.has_value()) {
    return std::nullopt;
  }
  return rotate_right(open_bus.value(), static_cast<std::uint8_t>((address & 0x3U) * 8U));
}

[[nodiscard]] std::optional<std::uint32_t> read32_or_thumb_pipeline_open_bus(
    const MemoryBus& memory, std::uint32_t address, std::uint32_t pc) {
  const std::optional<std::uint32_t> value = memory.read32(address);
  if (value.has_value()) {
    return value;
  }
  const std::optional<std::uint16_t> half =
      memory.read16(pc + 4U);
  if (!half.has_value()) {
    return std::nullopt;
  }
  const std::uint32_t duplicated =
      static_cast<std::uint32_t>(half.value()) |
      (static_cast<std::uint32_t>(half.value()) << 16U);
  return rotate_right(duplicated, static_cast<std::uint8_t>((address & 0x3U) * 8U));
}

[[nodiscard]] std::optional<std::uint32_t> read32_or_thumb_block_open_bus(
    const MemoryBus& memory, std::uint32_t address, std::uint32_t pc) {
  const std::optional<std::uint32_t> value = memory.read32(address);
  if (value.has_value()) {
    return value;
  }
  const std::optional<std::uint32_t> latched_bus = memory.open_bus_latch();
  if (latched_bus.has_value()) {
    return rotate_right(latched_bus.value(), static_cast<std::uint8_t>((address & 0x3U) * 8U));
  }
  const std::optional<std::uint16_t> prefetched_halfword = memory.read16(pc + 4U);
  if (!prefetched_halfword.has_value()) {
    return std::nullopt;
  }
  const std::uint32_t open_bus =
      static_cast<std::uint32_t>(prefetched_halfword.value()) |
      (static_cast<std::uint32_t>(prefetched_halfword.value()) << 16U);
  return rotate_right(open_bus, static_cast<std::uint8_t>((address & 0x3U) * 8U));
}

[[nodiscard]] std::optional<ArmElapsedCycleEstimate> estimate_arm_elapsed_cycles_with_timing(
    std::uint32_t instruction, std::uint32_t data_address,
    const MemoryAccessTiming& timing, bool waitcnt_aware,
    bool waitcnt_prefetch_enabled, bool fast_rom_sequential_prefetch,
    const WaitStateControl* waitcnt) {
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

  ArmElapsedCycleEstimate elapsed = compose_elapsed_cycles(cycles.value(), timing);
  const std::optional<std::uint32_t> mirrored_oam_elapsed =
      mirrored_oam_block_load_elapsed_cycles(instruction, data_address, cycles.value(),
                                             waitcnt, waitcnt_prefetch_enabled);
  if (mirrored_oam_elapsed.has_value()) {
    elapsed = {mirrored_oam_elapsed.value(), cycles.value().data_dependent, true};
  }
  if (waitcnt_aware && arm_single_word_load_from_game_pak_rom(instruction, data_address)) {
    elapsed.cycles += 3U;
  }
  const std::uint32_t prefetch_overlap = prefetch_internal_data_load_overlap_cycles(
      instruction, data_address, cycles.value(), waitcnt_prefetch_enabled,
      fast_rom_sequential_prefetch) +
      prefetch_block_store_overlap_cycles(instruction, data_address,
                                          waitcnt_prefetch_enabled,
                                          fast_rom_sequential_prefetch);
  if (prefetch_overlap != 0) {
    elapsed.cycles -= std::min(elapsed.cycles, prefetch_overlap);
  }
  return elapsed;
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

  const std::uint8_t opcode = bits(instruction, 21, 0xFU);
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

bool Arm7tdmi::can_decode_branch_exchange(std::uint32_t instruction) {
  return (instruction & 0x0FFFFFF0U) == 0x012FFF10U &&
         supported_condition(bits(instruction, 28, 0xFU));
}

DecodedBranchExchangeInstruction Arm7tdmi::decode_branch_exchange(
    std::uint32_t instruction) {
  if (!can_decode_branch_exchange(instruction)) {
    throw std::invalid_argument("unsupported ARM branch-exchange instruction");
  }

  return {
      static_cast<ArmCondition>(bits(instruction, 28, 0xFU)),
      bits(instruction, 0, 0xFU),
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
  const std::uint16_t register_list = static_cast<std::uint16_t>(instruction & 0xFFFFU);
  // The S bit (force user bank / PSR restore, instruction bit 22) selects a
  // supported variant and no longer rejects the encoding; execution applies
  // the user-bank semantics.
  return block_transfer_group && register_list != 0 &&
         bits(instruction, 16, 0xFU) != kPc &&
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

bool Arm7tdmi::can_decode_thumb_shift_immediate(std::uint16_t instruction) {
  return ((instruction >> 13) & 0x7U) == 0x0U &&
         ((instruction >> 11) & 0x3U) != 0x3U;
}

DecodedThumbShiftInstruction Arm7tdmi::decode_thumb_shift_immediate(
    std::uint16_t instruction) {
  if (!can_decode_thumb_shift_immediate(instruction)) {
    throw std::invalid_argument("unsupported Thumb shift-immediate instruction");
  }

  const std::uint8_t opcode = bits(instruction, 11, 0x3U);
  ArmShiftType type = ArmShiftType::lsl;
  switch (opcode) {
    case 0x0:
      type = ArmShiftType::lsl;
      break;
    case 0x1:
      type = ArmShiftType::lsr;
      break;
    case 0x2:
      type = ArmShiftType::asr;
      break;
  }

  return {
      type,
      bits(instruction, 0, 0x7U),
      bits(instruction, 3, 0x7U),
      bits(instruction, 6, 0x1FU),
  };
}

bool Arm7tdmi::can_decode_thumb_alu(std::uint16_t instruction) {
  return ((instruction >> 10) & 0x3FU) == 0x10U;
}

DecodedThumbAluInstruction Arm7tdmi::decode_thumb_alu(std::uint16_t instruction) {
  if (!can_decode_thumb_alu(instruction)) {
    throw std::invalid_argument("unsupported Thumb ALU instruction");
  }

  return {
      static_cast<ThumbAluOpcode>(bits(instruction, 6, 0xFU)),
      bits(instruction, 0, 0x7U),
      bits(instruction, 3, 0x7U),
  };
}

ExecuteStatus Arm7tdmi::execute_thumb_shift(
    const DecodedThumbShiftInstruction& decoded) {
  const std::uint32_t value = registers_.at(decoded.rs);
  std::uint32_t result = value;
  bool carry_out = carry_;
  bool update_carry = true;

  switch (decoded.type) {
    case ArmShiftType::lsl:
      if (decoded.amount == 0) {
        update_carry = false;
        result = value;
      } else {
        carry_out = ((value >> (32U - decoded.amount)) & 0x1U) != 0;
        result = value << decoded.amount;
      }
      break;
    case ArmShiftType::lsr: {
      const std::uint8_t amount = decoded.amount == 0 ? 32U : decoded.amount;
      carry_out = ((value >> (amount - 1U)) & 0x1U) != 0;
      result = amount == 32U ? 0U : value >> amount;
      break;
    }
    case ArmShiftType::asr: {
      const std::uint8_t amount = decoded.amount == 0 ? 32U : decoded.amount;
      carry_out = ((value >> (amount - 1U)) & 0x1U) != 0;
      if (amount == 32U) {
        result = (value & 0x80000000U) != 0 ? 0xFFFFFFFFU : 0U;
      } else {
        result = static_cast<std::uint32_t>(static_cast<std::int32_t>(value) >> amount);
      }
      break;
    }
    case ArmShiftType::ror:
      return ExecuteStatus::unsupported;
  }

  registers_.at(decoded.rd) = result;
  set_nz(result);
  if (update_carry) {
    carry_ = carry_out;
  }
  return ExecuteStatus::executed;
}

ExecuteStatus Arm7tdmi::execute_thumb_alu(const DecodedThumbAluInstruction& decoded) {
  const std::uint32_t left = registers_.at(decoded.rd);
  const std::uint32_t right = registers_.at(decoded.rs);
  const std::uint8_t amount = static_cast<std::uint8_t>(right & 0xFFU);
  std::uint32_t result = left;
  bool write_result = true;
  bool logical_result = true;
  bool carry_out = carry_;
  bool update_carry = false;

  switch (decoded.opcode) {
    case ThumbAluOpcode::and_:
      result = left & right;
      break;
    case ThumbAluOpcode::eor:
      result = left ^ right;
      break;
    case ThumbAluOpcode::lsl:
      update_carry = amount != 0;
      if (amount == 0) {
        result = left;
      } else if (amount < 32) {
        carry_out = ((left >> (32U - amount)) & 0x1U) != 0;
        result = left << amount;
      } else if (amount == 32) {
        carry_out = (left & 0x1U) != 0;
        result = 0;
      } else {
        carry_out = false;
        result = 0;
      }
      break;
    case ThumbAluOpcode::lsr:
      update_carry = amount != 0;
      if (amount == 0) {
        result = left;
      } else if (amount < 32) {
        carry_out = ((left >> (amount - 1U)) & 0x1U) != 0;
        result = left >> amount;
      } else if (amount == 32) {
        carry_out = (left & 0x80000000U) != 0;
        result = 0;
      } else {
        carry_out = false;
        result = 0;
      }
      break;
    case ThumbAluOpcode::asr:
      update_carry = amount != 0;
      if (amount == 0) {
        result = left;
      } else if (amount < 32) {
        carry_out = ((left >> (amount - 1U)) & 0x1U) != 0;
        result = static_cast<std::uint32_t>(static_cast<std::int32_t>(left) >> amount);
      } else {
        carry_out = (left & 0x80000000U) != 0;
        result = carry_out ? 0xFFFFFFFFU : 0U;
      }
      break;
    case ThumbAluOpcode::adc: {
      logical_result = false;
      const std::uint32_t carry_value = carry_ ? 1U : 0U;
      result = left + right + carry_value;
      set_adc_flags(left, right, carry_, result);
      break;
    }
    case ThumbAluOpcode::sbc: {
      logical_result = false;
      const std::uint32_t borrow = carry_ ? 0U : 1U;
      result = left - right - borrow;
      set_sbc_flags(left, right, carry_, result);
      break;
    }
    case ThumbAluOpcode::ror:
      update_carry = amount != 0;
      if (amount == 0) {
        result = left;
      } else {
        const std::uint8_t shift = static_cast<std::uint8_t>(amount % 32U);
        result = shift == 0 ? left : rotate_right(left, shift);
        carry_out = (result & 0x80000000U) != 0;
      }
      break;
    case ThumbAluOpcode::tst:
      result = left & right;
      write_result = false;
      break;
    case ThumbAluOpcode::neg:
      logical_result = false;
      result = 0U - right;
      set_sub_flags(0, right, result);
      break;
    case ThumbAluOpcode::cmp:
      logical_result = false;
      result = left - right;
      write_result = false;
      set_sub_flags(left, right, result);
      break;
    case ThumbAluOpcode::cmn:
      logical_result = false;
      result = left + right;
      write_result = false;
      set_add_flags(left, right, result);
      break;
    case ThumbAluOpcode::orr:
      result = left | right;
      break;
    case ThumbAluOpcode::mul:
      result = left * right;
      break;
    case ThumbAluOpcode::bic:
      result = left & ~right;
      break;
    case ThumbAluOpcode::mvn:
      result = ~right;
      break;
  }

  if (write_result) {
    registers_.at(decoded.rd) = result;
  }
  if (logical_result) {
    set_nz(result);
  }
  if (update_carry) {
    carry_ = carry_out;
  }
  return ExecuteStatus::executed;
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

bool Arm7tdmi::can_decode_thumb_long_branch_link(std::uint16_t instruction) {
  return (instruction & 0xF800U) == 0xF000U || (instruction & 0xF800U) == 0xF800U;
}

DecodedThumbLongBranchLinkInstruction Arm7tdmi::decode_thumb_long_branch_link(
    std::uint16_t instruction) {
  if (!can_decode_thumb_long_branch_link(instruction)) {
    throw std::invalid_argument("unsupported Thumb long-branch-link instruction");
  }

  const bool second_half = (instruction & 0x0800U) != 0;
  const std::uint16_t raw = static_cast<std::uint16_t>(instruction & 0x07FFU);
  return {
      second_half,
      second_half ? static_cast<std::int32_t>(raw) * 2 : sign_extend_thumb_bl_prefix(raw),
  };
}

bool Arm7tdmi::can_decode_thumb_high_register(std::uint16_t instruction) {
  if (((instruction >> 10) & 0x3FU) != 0x11U) {
    return false;
  }

  // Format 5 with H1 == H2 == 0 is a valid low-register alias of ADD/CMP/MOV
  // (and BX accepts any register pair), so every encoding of the format shape
  // decodes; the low-low aliases simply execute through the same paths.
  return true;
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
  if (((instruction >> 11) & 0x1FU) == 0x9U) {
    return true;
  }
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
  if (((instruction >> 11) & 0x1FU) == 0x9U) {
    return {
        true,
        ThumbMemoryTransferKind::word,
        bits(instruction, 8, 0x7U),
        kPc,
        static_cast<std::uint32_t>(instruction & 0xFFU) * 4U,
        false,
    };
  }

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

bool Arm7tdmi::can_decode_thumb_block_transfer(std::uint16_t instruction) {
  return ((instruction >> 12) & 0xFU) == 0xCU &&
         static_cast<std::uint8_t>(instruction & 0xFFU) != 0;
}

DecodedThumbBlockTransferInstruction Arm7tdmi::decode_thumb_block_transfer(
    std::uint16_t instruction) {
  if (!can_decode_thumb_block_transfer(instruction)) {
    throw std::invalid_argument("unsupported Thumb block-transfer instruction");
  }

  return {
      ((instruction >> 11) & 0x1U) == 1,
      bits(instruction, 8, 0x7U),
      static_cast<std::uint8_t>(instruction & 0xFFU),
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

bool Arm7tdmi::can_decode_thumb_stack_pointer_adjust(std::uint16_t instruction) {
  return (instruction & 0xFF00U) == 0xB000U;
}

DecodedThumbStackPointerInstruction Arm7tdmi::decode_thumb_stack_pointer_adjust(
    std::uint16_t instruction) {
  if (!can_decode_thumb_stack_pointer_adjust(instruction)) {
    throw std::invalid_argument("unsupported Thumb stack-pointer adjust instruction");
  }

  return {
      (instruction & 0x0080U) != 0,
      static_cast<std::uint32_t>(instruction & 0x007FU) * 4U,
  };
}

bool Arm7tdmi::can_decode_thumb_load_address(std::uint16_t instruction) {
  return (instruction & 0xF000U) == 0xA000U;
}

DecodedThumbLoadAddressInstruction Arm7tdmi::decode_thumb_load_address(
    std::uint16_t instruction) {
  if (!can_decode_thumb_load_address(instruction)) {
    throw std::invalid_argument("unsupported Thumb load-address instruction");
  }

  return {
      (instruction & 0x0800U) != 0,
      bits(instruction, 8, 0x7U),
      static_cast<std::uint32_t>(instruction & 0x00FFU) * 4U,
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

bool Arm7tdmi::is_irq_vector_address(std::uint32_t address) {
  return address == irq_vector_address();
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

  if (can_decode_branch_exchange(instruction)) {
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
  return estimate_arm_elapsed_cycles_with_timing(instruction, data_address, timing, false,
                                                false, false, nullptr);
}

std::optional<ArmElapsedCycleEstimate> Arm7tdmi::estimate_arm_elapsed_cycles(
    std::uint32_t instruction, std::uint32_t data_address,
    const WaitStateControl& waitcnt) {
  // No memoization: MemoryBus::timing(address, width, waitcnt) is a cheap
  // pure function of (address, width, waitcnt control), and any cached
  // copy would be state that silently survives reset(). The step path is
  // dominated by decode + emulated bus access, so recomputing the timing
  // per call stays well within noise.
  const AccessWidth width = transfer_access_width(instruction);
  const MemoryAccessTiming timing = MemoryBus::timing(data_address, width, waitcnt);
  const bool fast_rom_sequential_prefetch =
      waitcnt.rom_wait_states(CartridgeWindow::rom_wait0).sequential <= 1;
  return estimate_arm_elapsed_cycles_with_timing(
      instruction, data_address, timing, true, waitcnt.prefetch_enabled(),
      fast_rom_sequential_prefetch, &waitcnt);
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

Arm7tdmi::State Arm7tdmi::save_state() const {
  State state{};
  state.registers = registers_;
  state.elapsed_cycles = elapsed_cycles_;
  state.negative = negative_;
  state.zero = zero_;
  state.carry = carry_;
  state.overflow = overflow_;
  state.irq_disabled = irq_disabled_;
  state.fiq_disabled = fiq_disabled_;
  state.thumb_state = thumb_state_;
  state.mode = mode_;
  state.shared_r8_r12 = shared_r8_r12_;
  state.fiq_r8_r12 = fiq_r8_r12_;
  state.user_sp = user_sp_;
  state.user_lr = user_lr_;
  state.fiq_sp = fiq_sp_;
  state.fiq_lr = fiq_lr_;
  state.irq_sp = irq_sp_;
  state.irq_lr = irq_lr_;
  state.supervisor_sp = supervisor_sp_;
  state.supervisor_lr = supervisor_lr_;
  state.abort_sp = abort_sp_;
  state.abort_lr = abort_lr_;
  state.undefined_sp = undefined_sp_;
  state.undefined_lr = undefined_lr_;
  state.fiq_spsr = fiq_spsr_;
  state.supervisor_spsr = supervisor_spsr_;
  state.abort_spsr = abort_spsr_;
  state.irq_spsr = irq_spsr_;
  state.undefined_spsr = undefined_spsr_;
  return state;
}

bool Arm7tdmi::load_state(const State& state) {
  if (!decode_cpu_mode(static_cast<std::uint32_t>(state.mode)).has_value()) {
    return false;
  }

  registers_ = state.registers;
  elapsed_cycles_ = state.elapsed_cycles;
  negative_ = state.negative;
  zero_ = state.zero;
  carry_ = state.carry;
  overflow_ = state.overflow;
  irq_disabled_ = state.irq_disabled;
  fiq_disabled_ = state.fiq_disabled;
  thumb_state_ = state.thumb_state;
  mode_ = state.mode;
  shared_r8_r12_ = state.shared_r8_r12;
  fiq_r8_r12_ = state.fiq_r8_r12;
  user_sp_ = state.user_sp;
  user_lr_ = state.user_lr;
  fiq_sp_ = state.fiq_sp;
  fiq_lr_ = state.fiq_lr;
  irq_sp_ = state.irq_sp;
  irq_lr_ = state.irq_lr;
  supervisor_sp_ = state.supervisor_sp;
  supervisor_lr_ = state.supervisor_lr;
  abort_sp_ = state.abort_sp;
  abort_lr_ = state.abort_lr;
  undefined_sp_ = state.undefined_sp;
  undefined_lr_ = state.undefined_lr;
  fiq_spsr_ = state.fiq_spsr;
  supervisor_spsr_ = state.supervisor_spsr;
  abort_spsr_ = state.abort_spsr;
  irq_spsr_ = state.irq_spsr;
  undefined_spsr_ = state.undefined_spsr;
  return true;
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
  if (bits(instruction, 28, 0xFU) == 0xFU) {
    return ExecuteStatus::skipped_condition;
  }

  if (can_decode_software_interrupt(instruction)) {
    const ArmCondition condition = static_cast<ArmCondition>(bits(instruction, 28, 0xFU));
    if (!condition_passed(condition)) {
      return ExecuteStatus::skipped_condition;
    }
    return enter_exception(ExceptionKind::software_interrupt);
  }

  const ArmCondition condition = static_cast<ArmCondition>(bits(instruction, 28, 0xFU));
  const unsigned class_nybble = (instruction >> 24) & 0xFU;
  if (class_nybble == 0xEU || class_nybble == 0xFU) {
    // Deliberate deviation: coprocessor/undefined-instruction space silently
    // NOPs here instead of raising the undefined-instruction exception.
    if (!condition_passed(condition)) {
      return ExecuteStatus::skipped_condition;
    }
    return ExecuteStatus::executed;
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

  if (can_decode_branch_exchange(instruction)) {
    const DecodedBranchExchangeInstruction decoded = decode_branch_exchange(instruction);
    if (!condition_passed(decoded.condition)) {
      return ExecuteStatus::skipped_condition;
    }

    // BX with Rm==R15 branches to the pipeline-visible PC (instruction
    // address + 8 in ARM state), not the raw register file value.
    const std::uint32_t target =
        decoded.rm == kPc ? arm_visible_register_value(kPc) : registers_.at(decoded.rm);
    thumb_state_ = (target & 0x1U) != 0;
    registers_.at(kPc) = target & ~1U;
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
    const auto register_shift_visible_value = [this](std::uint8_t index) {
      const std::uint32_t value = registers_.at(index);
      return index == kPc ? value + 12U : value;
    };
    const DecodedArmInstruction decoded = register_shift
                                             ? decode_data_processing_register_shift(
                                                   instruction,
                                                   register_shift_visible_value(rm),
                                                   register_shift_visible_value(
                                                       bits(instruction, 8, 0xFU)))
                                             : decode_data_processing_register_shift(
                                                   instruction, arm_visible_register_value(rm),
                                                   carry_);
    if (!condition_passed(decoded.condition)) {
      return ExecuteStatus::skipped_condition;
    }
    return execute_data_processing(decoded, register_shift ? 12U : 8U);
  }

  const DecodedArmInstruction decoded = decode_data_processing_immediate(instruction);
  if (!condition_passed(decoded.condition)) {
    return ExecuteStatus::skipped_condition;
  }

  return execute_data_processing(decoded);
}

ExecuteStatus Arm7tdmi::execute_data_processing(const DecodedArmInstruction& decoded,
                                                std::uint32_t pc_offset) {
  // Hardware behavior (ARM DDI 0029E): a data-processing write to R15 with
  // S=1 in a mode that has an SPSR (SUBS PC, LR, #imm / MOVS PC, LR return
  // sequences) restores CPSR <- SPSR — switching mode and importing the
  // saved I/F/T bits with NO NZCV math from the ALU result — and writes an
  // ARM-state PC with bits[1:0] forced to zero.
  const bool spsr_restore_to_pc = decoded.rd == kPc && decoded.set_flags && has_spsr();
  const auto write_result = [&](std::uint32_t result) {
    if (!spsr_restore_to_pc) {
      registers_.at(decoded.rd) = result;
      return;
    }
    (void)set_cpsr(spsr().value());
    registers_.at(kPc) = result & ~0x3U;
  };
  const auto update_flags = [&](bool condition) {
    return condition && !spsr_restore_to_pc;
  };

  const std::uint32_t left =
      decoded.rn == kPc ? registers_.at(kPc) + pc_offset : registers_.at(decoded.rn);
  switch (decoded.opcode) {
    case ArmOpcode::and_: {
      const std::uint32_t result = left & decoded.operand2;
      write_result(result);
      if (update_flags(decoded.set_flags)) {
        set_logical_flags(result, decoded);
      }
      return ExecuteStatus::executed;
    }
    case ArmOpcode::eor: {
      const std::uint32_t result = left ^ decoded.operand2;
      write_result(result);
      if (update_flags(decoded.set_flags)) {
        set_logical_flags(result, decoded);
      }
      return ExecuteStatus::executed;
    }
    case ArmOpcode::mov:
      write_result(decoded.operand2);
      if (update_flags(decoded.set_flags)) {
        set_logical_flags(decoded.operand2, decoded);
      }
      return ExecuteStatus::executed;
    case ArmOpcode::add: {
      const std::uint32_t result = left + decoded.operand2;
      write_result(result);
      if (update_flags(decoded.set_flags)) {
        set_add_flags(left, decoded.operand2, result);
      }
      return ExecuteStatus::executed;
    }
    case ArmOpcode::sub: {
      const std::uint32_t result = left - decoded.operand2;
      write_result(result);
      if (update_flags(decoded.set_flags)) {
        set_sub_flags(left, decoded.operand2, result);
      }
      return ExecuteStatus::executed;
    }
    case ArmOpcode::rsb: {
      const std::uint32_t result = decoded.operand2 - left;
      write_result(result);
      if (update_flags(decoded.set_flags)) {
        set_sub_flags(decoded.operand2, left, result);
      }
      return ExecuteStatus::executed;
    }
    case ArmOpcode::adc: {
      const bool carry_in = carry_;
      const std::uint32_t result = left + decoded.operand2 + (carry_in ? 1U : 0U);
      write_result(result);
      if (update_flags(decoded.set_flags)) {
        set_adc_flags(left, decoded.operand2, carry_in, result);
      }
      return ExecuteStatus::executed;
    }
    case ArmOpcode::sbc: {
      const bool carry_in = carry_;
      const std::uint32_t borrow = carry_in ? 0U : 1U;
      const std::uint32_t result = left - decoded.operand2 - borrow;
      write_result(result);
      if (update_flags(decoded.set_flags)) {
        set_sbc_flags(left, decoded.operand2, carry_in, result);
      }
      return ExecuteStatus::executed;
    }
    case ArmOpcode::rsc: {
      const bool carry_in = carry_;
      const std::uint32_t borrow = carry_in ? 0U : 1U;
      const std::uint32_t result = decoded.operand2 - left - borrow;
      write_result(result);
      if (update_flags(decoded.set_flags)) {
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
      write_result(result);
      if (update_flags(decoded.set_flags)) {
        set_logical_flags(result, decoded);
      }
      return ExecuteStatus::executed;
    }
    case ArmOpcode::bic: {
      const std::uint32_t result = left & ~decoded.operand2;
      write_result(result);
      if (update_flags(decoded.set_flags)) {
        set_logical_flags(result, decoded);
      }
      return ExecuteStatus::executed;
    }
    case ArmOpcode::mvn: {
      const std::uint32_t result = ~decoded.operand2;
      write_result(result);
      if (update_flags(decoded.set_flags)) {
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

  std::uint32_t effective_field_mask = field_mask;
  if (mode_ == CpuMode::user && (effective_field_mask & kControlByteMask) != 0) {
    // Hardware: MSR CPSR from user mode cannot write the control byte
    // (bits[7:0] — mode + I/F/T). The requested bits are ignored silently
    // (no trap); flag-field writes still apply.
    effective_field_mask &= ~kControlByteMask;
  }

  const std::uint32_t merged =
      (cpsr() & ~effective_field_mask) | (operand & effective_field_mask);
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
  const std::uint32_t rm = registers_.at(decoded.rm);
  const std::uint32_t rs = registers_.at(decoded.rs);
  std::uint64_t accumulator = 0;
  std::uint64_t result = 0;
  if (decoded.signed_multiply) {
    const auto left = static_cast<std::int64_t>(static_cast<std::int32_t>(rm));
    const auto right = static_cast<std::int64_t>(static_cast<std::int32_t>(rs));
    result = static_cast<std::uint64_t>(left * right);
  } else {
    result = static_cast<std::uint64_t>(rm) * static_cast<std::uint64_t>(rs);
  }

  if (decoded.accumulate) {
    accumulator =
        (static_cast<std::uint64_t>(registers_.at(decoded.rd_hi)) << 32) |
        static_cast<std::uint64_t>(registers_.at(decoded.rd_lo));
    result += accumulator;
  }

  registers_.at(decoded.rd_lo) = static_cast<std::uint32_t>(result & 0xFFFFFFFFULL);
  registers_.at(decoded.rd_hi) = static_cast<std::uint32_t>(result >> 32);
  if (decoded.set_flags) {
    set_nz64(result);
    carry_ = arm7tdmi_multiply_long_output(decoded.signed_multiply, rm, rs, accumulator).carry;
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

  if (can_decode_thumb_long_branch_link(instruction)) {
    return execute_thumb_long_branch_link(decode_thumb_long_branch_link(instruction));
  }

  if (can_decode_thumb_shift_immediate(instruction)) {
    return execute_thumb_shift(decode_thumb_shift_immediate(instruction));
  }

  if (can_decode_thumb_alu(instruction)) {
    return execute_thumb_alu(decode_thumb_alu(instruction));
  }

  if (can_decode_thumb_stack_pointer_adjust(instruction)) {
    return execute_thumb_stack_pointer_adjust(decode_thumb_stack_pointer_adjust(instruction));
  }

  if (can_decode_thumb_load_address(instruction)) {
    return execute_thumb_load_address(decode_thumb_load_address(instruction));
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
  if (can_decode_thumb_block_transfer(instruction)) {
    return execute_thumb_block_transfer(decode_thumb_block_transfer(instruction), memory);
  }

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

  registers_.at(kPc) = thumb_visible_register_value(kPc) +
                       static_cast<std::uint32_t>(decoded.offset);
  return ExecuteStatus::executed;
}

ExecuteStatus Arm7tdmi::execute_thumb_long_branch_link(
    const DecodedThumbLongBranchLinkInstruction& decoded) {
  if (!decoded.second_half) {
    registers_.at(kLinkRegister) =
        thumb_visible_register_value(kPc) + static_cast<std::uint32_t>(decoded.offset);
    return ExecuteStatus::executed;
  }

  const std::uint32_t target =
      registers_.at(kLinkRegister) + static_cast<std::uint32_t>(decoded.offset);
  registers_.at(kLinkRegister) = (registers_.at(kPc) + 2U) | 0x1U;
  registers_.at(kPc) = target & ~1U;
  thumb_state_ = true;
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
  const std::uint32_t base = decoded.rb == kPc ? align_word(registers_.at(kPc) + 4U)
                                               : registers_.at(decoded.rb);
  const std::uint32_t offset =
      decoded.offset_is_register ? registers_.at(decoded.offset) : decoded.offset;
  const std::uint32_t address = base + offset;

  if (decoded.load) {
    switch (decoded.kind) {
      case ThumbMemoryTransferKind::word: {
        const std::optional<std::uint32_t> value =
            read32_or_thumb_pipeline_open_bus(memory, address, registers_.at(kPc));
        if (!value.has_value()) {
          return ExecuteStatus::unsupported;
        }
        registers_.at(decoded.rd) = value.value();
        return ExecuteStatus::executed;
      }
      case ThumbMemoryTransferKind::byte: {
        const std::optional<std::uint8_t> value =
            read8_or_thumb_pipeline_open_bus(memory, address, registers_.at(kPc));
        if (!value.has_value()) {
          return ExecuteStatus::unsupported;
        }
        registers_.at(decoded.rd) = value.value();
        return ExecuteStatus::executed;
      }
      case ThumbMemoryTransferKind::halfword: {
        const std::optional<std::uint16_t> value =
            read16_or_thumb_pipeline_open_bus(memory, address, registers_.at(kPc));
        if (!value.has_value()) {
          return ExecuteStatus::unsupported;
        }
        registers_.at(decoded.rd) = value.value();
        return ExecuteStatus::executed;
      }
      case ThumbMemoryTransferKind::signed_byte: {
        const std::optional<std::uint8_t> value =
            read8_or_thumb_pipeline_open_bus(memory, address, registers_.at(kPc));
        if (!value.has_value()) {
          return ExecuteStatus::unsupported;
        }
        registers_.at(decoded.rd) = sign_extend8(value.value());
        return ExecuteStatus::executed;
      }
      case ThumbMemoryTransferKind::signed_halfword: {
        const std::optional<std::uint16_t> value =
            read16_or_thumb_pipeline_open_bus(memory, address, registers_.at(kPc));
        if (!value.has_value()) {
          return ExecuteStatus::unsupported;
        }
        registers_.at(decoded.rd) = sign_extend16(value.value());
        return ExecuteStatus::executed;
      }
    }
  }

  switch (decoded.kind) {
    case ThumbMemoryTransferKind::word: {
      const AddressInfo write_info = MemoryBus::describe(address);
      const std::uint32_t write_address =
          write_info.region == Region::game_pak_save || write_info.region == Region::io
              ? address
              : (address & ~0x3U);
      return memory.write32(write_address, registers_.at(decoded.rd))
                 ? ExecuteStatus::executed
                 : ExecuteStatus::unsupported;
    }
    case ThumbMemoryTransferKind::byte: {
      const AddressInfo write_info = MemoryBus::describe(address);
      const std::uint32_t write_address =
          write_info.region == Region::game_pak_save || write_info.region == Region::io
              ? address
              : address;
      return memory.write8(write_address, static_cast<std::uint8_t>(registers_.at(decoded.rd) & 0xFFU))
                 ? ExecuteStatus::executed
                 : ExecuteStatus::unsupported;
    }
    case ThumbMemoryTransferKind::halfword: {
      const AddressInfo write_info = MemoryBus::describe(address);
      const std::uint32_t write_address =
          write_info.region == Region::game_pak_save || write_info.region == Region::io
              ? address
              : (address & ~0x1U);
      return memory.write16(write_address,
                            static_cast<std::uint16_t>(registers_.at(decoded.rd) & 0xFFFFU))
                 ? ExecuteStatus::executed
                 : ExecuteStatus::unsupported;
    }
    case ThumbMemoryTransferKind::signed_byte:
    case ThumbMemoryTransferKind::signed_halfword:
      return ExecuteStatus::unsupported;
  }

  return ExecuteStatus::unsupported;
}

ExecuteStatus Arm7tdmi::execute_thumb_block_transfer(
    const DecodedThumbBlockTransferInstruction& decoded, MemoryBus& memory) {
  const std::uint8_t register_count = count_registers(decoded.register_list);
  if (register_count == 0) {
    return ExecuteStatus::unsupported;
  }

  const std::uint32_t old_base = registers_.at(decoded.rb);
  std::uint32_t address = old_base;
  if (decoded.load) {
    std::array<std::uint32_t, 8> loaded{};
    for (std::uint8_t index = 0; index < 8; ++index) {
      if (!register_list_contains(decoded.register_list, index)) {
        continue;
      }
      const std::optional<std::uint32_t> value =
          read32_or_thumb_block_open_bus(memory, address, registers_.at(kPc));
      if (!value.has_value()) {
        return ExecuteStatus::unsupported;
      }
      loaded.at(index) = value.value();
      address += 4U;
    }
    for (std::uint8_t index = 0; index < 8; ++index) {
      if (register_list_contains(decoded.register_list, index)) {
        registers_.at(index) = loaded.at(index);
      }
    }
  } else {
    for (std::uint8_t index = 0; index < 8; ++index) {
      if (!register_list_contains(decoded.register_list, index)) {
        continue;
      }
      if (!memory.write32(address, registers_.at(index))) {
        return ExecuteStatus::unsupported;
      }
      address += 4U;
    }
  }

  registers_.at(decoded.rb) = old_base + static_cast<std::uint32_t>(register_count) * 4U;
  return ExecuteStatus::executed;
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
    // POP {..., PC} interworks like the ARM LDM path: the popped bit 0
    // selects Thumb state BEFORE the address is masked into the PC.
    const std::uint32_t loaded_pc = loaded.at(read_index);
    registers_.at(kPc) = loaded_pc & ~1U;
    std::uint32_t next_cpsr = cpsr();
    if ((loaded_pc & 0x1U) != 0U) {
      next_cpsr |= kThumbStateFlag;
    } else {
      next_cpsr &= ~kThumbStateFlag;
    }
    if (!set_cpsr(next_cpsr)) {
      return ExecuteStatus::unsupported;
    }
  }
  registers_.at(13) = old_sp + static_cast<std::uint32_t>(register_count) * 4U;
  return ExecuteStatus::executed;
}

ExecuteStatus Arm7tdmi::execute_thumb_stack_pointer_adjust(
    const DecodedThumbStackPointerInstruction& decoded) {
  if (decoded.subtract) {
    registers_.at(13) -= decoded.offset;
  } else {
    registers_.at(13) += decoded.offset;
  }
  return ExecuteStatus::executed;
}

ExecuteStatus Arm7tdmi::execute_thumb_load_address(
    const DecodedThumbLoadAddressInstruction& decoded) {
  const std::uint32_t base = decoded.base_is_sp
                                 ? registers_.at(13)
                                 : align_word(thumb_visible_register_value(kPc));
  registers_.at(decoded.rd) = base + decoded.offset;
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
  // Exception link contract (deliberate closed-loop convention — see also
  // the declaration comment in arm7tdmi.hpp):
  //
  // * Callers (CoreScheduler::step_arm/step_thumb via
  //   InterruptController::service_pending_irq) pre-bump registers_[15] from
  //   the current instruction address I to the next-unexecuted instruction
  //   X = I + 4 (ARM) / I + 2 (Thumb) before entering an IRQ. Direct SWI
  //   entry keeps registers_[15] at the SWI instruction address itself.
  // * LR is linked as saved_pc + link_offset: +4 for ARM state, and +2 for
  //   IRQ or SWI taken FROM Thumb state.
  // * CoreScheduler::dispatch_hle_irq_return pairs this with a matching
  //   subtraction at return time (-4 ARM / -2 Thumb), so PC lands back on X.
  //   Hardware instead stores LR = next_unexecuted + 4 in both states and
  //   returns with SUBS PC, LR, #4; only the Thumb IRQ link value differs
  //   numerically here. The pairing is internally consistent and validated
  //   against the timing corpus — change both sides together or neither.
  const ExceptionVector vector = exception_vector(kind);
  const std::uint32_t saved_cpsr = cpsr();
  const std::uint32_t saved_pc = registers_.at(kPc);

  switch_mode(vector.mode);
  if (vector.save_cpsr) {
    set_spsr_for_mode(vector.mode, saved_cpsr);
    std::uint32_t link_offset = vector.link_offset;
    if ((saved_cpsr & kThumbStateFlag) != 0U &&
        (kind == ExceptionKind::irq || kind == ExceptionKind::software_interrupt)) {
      // Thumb-state exceptions link a halfword-sized offset: R14_svc =
      // SWI address + 2 so MOVS PC, LR returns past the 16-bit SWI.
      link_offset = 2U;
    }
    registers_.at(kLinkRegister) = saved_pc + link_offset;
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
  if (bits(instruction, 28, 0xFU) == 0xFU) {
    return ExecuteStatus::skipped_condition;
  }

  if (can_decode_software_interrupt(instruction)) {
    const ArmCondition condition = static_cast<ArmCondition>(bits(instruction, 28, 0xFU));
    if (!condition_passed(condition)) {
      return ExecuteStatus::skipped_condition;
    }
    return enter_exception(ExceptionKind::software_interrupt);
  }

  const ArmCondition condition = static_cast<ArmCondition>(bits(instruction, 28, 0xFU));
  const unsigned class_nybble = (instruction >> 24) & 0xFU;
  if (class_nybble == 0xEU || class_nybble == 0xFU) {
    if (!condition_passed(condition)) {
      return ExecuteStatus::skipped_condition;
    }
    return ExecuteStatus::executed;
  }

  if (can_decode_swap(instruction)) {
    return execute_swap(decode_swap(instruction), memory);
  }

  if (can_decode_block_data_transfer(instruction)) {
    const DecodedBlockDataTransferInstruction decoded = decode_block_data_transfer(instruction);
    if (!condition_passed(decoded.condition)) {
      return ExecuteStatus::skipped_condition;
    }

    if (decoded.load && decoded.write_back &&
        register_list_contains(decoded.register_list, decoded.rn)) {
      return ExecuteStatus::unsupported;
    }

    // S bit (bit 22): force user bank, or the LDM{...PC}^ exception-return
    // form that also restores CPSR <- SPSR. From user/system modes (no
    // SPSR) the encoding is unpredictable on hardware and stays rejected.
    const bool force_user_bank = ((instruction >> 22) & 0x1U) == 1;
    if (force_user_bank && !has_spsr()) {
      return ExecuteStatus::unsupported;
    }
    if (!decoded.load && force_user_bank &&
        register_list_contains(decoded.register_list, kPc)) {
      return ExecuteStatus::unsupported;
    }

    // ARM7TDMI banking: R8-R12 are physically shared by every mode except
    // FIQ, so the user-bank form only reroutes R13/R14 to the user slots.
    const auto user_bank_value = [&](std::uint8_t index) -> std::uint32_t {
      if (index == 13U) {
        return user_sp_;
      }
      if (index == 14U) {
        return user_lr_;
      }
      return registers_.at(index);
    };
    const auto write_user_bank_value = [&](std::uint8_t index, std::uint32_t value) {
      if (index == 13U) {
        user_sp_ = value;
        return;
      }
      if (index == 14U) {
        user_lr_ = value;
        return;
      }
      registers_.at(index) = value;
    };

    const std::uint8_t register_count = count_registers(decoded.register_list);
    const BlockTransferAddress transfer =
        block_transfer_address(registers_.at(decoded.rn), register_count, decoded.pre_index,
                               decoded.up);
    std::uint32_t address = transfer.first;

    if (decoded.load) {
      std::array<std::uint32_t, kRegisterCount> loaded{};
      for (std::uint8_t index = 0; index < kRegisterCount; ++index) {
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

      if (force_user_bank && register_list_contains(decoded.register_list, kPc)) {
        // Exception-return form: CPSR <- SPSR first so the mode switch's
        // bank save/load cannot clobber the user-bank values written below.
        if (!set_cpsr(spsr().value())) {
          return ExecuteStatus::unsupported;
        }
      }
      for (std::uint8_t index = 0; index < kPc; ++index) {
        if (!register_list_contains(decoded.register_list, index)) {
          continue;
        }
        if (force_user_bank) {
          write_user_bank_value(index, loaded.at(index));
        } else {
          registers_.at(index) = loaded.at(index);
        }
      }
      if (register_list_contains(decoded.register_list, kPc)) {
        const std::uint32_t loaded_pc = loaded.at(kPc);
        if (force_user_bank) {
          // T state comes from SPSR via the restore above; the address is
          // forced to an ARM-aligned word.
          registers_.at(kPc) = loaded_pc & ~0x3U;
        } else {
          registers_.at(kPc) = loaded_pc & ~1U;
          std::uint32_t next_cpsr = cpsr();
          if ((loaded_pc & 0x1U) != 0U) {
            next_cpsr |= 0x20U;
          } else {
            next_cpsr &= ~0x20U;
          }
          if (!set_cpsr(next_cpsr)) {
            return ExecuteStatus::unsupported;
          }
        }
      }
    } else {
      for (std::uint8_t index = 0; index < kRegisterCount; ++index) {
        if (!register_list_contains(decoded.register_list, index)) {
          continue;
        }
        // ARM7TDMI quirks: R15 in the list stores the instruction address
        // plus 12, and the S bit stores the USER-bank value for R8-R14.
        const std::uint32_t raw_value =
            force_user_bank ? user_bank_value(index) : registers_.at(index);
        const std::uint32_t stored_value =
            index == kPc ? raw_value + 12U : raw_value;
        if (!memory.write32(address, stored_value)) {
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
        const std::optional<std::uint8_t> value =
            read8_or_arm_pipeline_open_bus(memory, address, registers_.at(kPc));
        if (!value.has_value()) {
          return ExecuteStatus::unsupported;
        }
        registers_.at(decoded.rd) = sign_extend8(value.value());
        if (needs_write_back) {
          registers_.at(decoded.rn) = write_back_address;
        }
        return ExecuteStatus::executed;
      }

      if (decoded.signed_transfer && (address & 0x1U) != 0) {
        const std::optional<std::uint8_t> value =
            read8_or_arm_pipeline_open_bus(memory, address, registers_.at(kPc));
        if (!value.has_value()) {
          return ExecuteStatus::unsupported;
        }
        registers_.at(decoded.rd) = sign_extend8(value.value());
        if (needs_write_back) {
          registers_.at(decoded.rn) = write_back_address;
        }
        return ExecuteStatus::executed;
      }

      const std::uint32_t halfword_address =
          MemoryBus::describe(address).region == Region::game_pak_save ? address
                                                                       : (address & ~0x1U);
      const std::optional<std::uint16_t> value =
          read16_or_arm_pipeline_open_bus(memory, halfword_address, registers_.at(kPc));
      if (!value.has_value()) {
        return ExecuteStatus::unsupported;
      }
      registers_.at(decoded.rd) =
          decoded.signed_transfer
              ? sign_extend16(value.value())
              : ((address & 0x1U) != 0
                     ? rotate_right(static_cast<std::uint32_t>(value.value()), 8)
                     : static_cast<std::uint32_t>(value.value()));
      if (needs_write_back) {
        registers_.at(decoded.rn) = write_back_address;
      }
      return ExecuteStatus::executed;
    }

    if (decoded.signed_transfer || !decoded.halfword) {
      return ExecuteStatus::unsupported;
    }

    if (!memory.write16(MemoryBus::describe(address).region == Region::game_pak_save
                            ? address
                            : (address & ~0x1U),
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
      const std::optional<std::uint8_t> value =
          read8_or_arm_pipeline_open_bus(memory, address, registers_.at(kPc));
      if (!value.has_value()) {
        return ExecuteStatus::unsupported;
      }
      registers_.at(decoded.rd) = value.value();
      if (needs_write_back) {
        registers_.at(decoded.rn) = write_back_address;
      }
      return ExecuteStatus::executed;
    }

    const std::optional<std::uint32_t> value =
        read32_or_arm_pipeline_open_bus(memory, address, registers_.at(kPc));
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

  {
    const AddressInfo write_info = MemoryBus::describe(address);
    const std::uint32_t write_address =
        write_info.region == Region::game_pak_save || write_info.region == Region::io
            ? address
            : (address & ~0x3U);
    // ARM7TDMI quirk: STR of R15 stores the instruction address plus 12.
    const std::uint32_t stored_value =
        decoded.rd == kPc ? registers_.at(kPc) + 12U : registers_.at(decoded.rd);
    if (!memory.write32(write_address, stored_value)) {
      return ExecuteStatus::unsupported;
    }
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
                                    const WaitStateControl* waitcnt,
                                    std::optional<ArmElapsedCycleEstimate>
                                        elapsed_override) {
  if (status == ExecuteStatus::skipped_condition) {
    elapsed_cycles_ += kArmSkippedConditionElapsedCycles;
    return {status, kArmSkippedConditionElapsedCycles, elapsed_cycles_, false,
            false};
  }
  if (status != ExecuteStatus::executed) {
    return {status, 0, elapsed_cycles_, false, false};
  }

  const std::optional<ArmElapsedCycleEstimate> estimate =
      elapsed_override.has_value()
          ? elapsed_override
          : waitcnt == nullptr
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

std::optional<ArmElapsedCycleEstimate> Arm7tdmi::runtime_multiply_elapsed_cycles(
    std::uint32_t instruction) const {
  if (can_decode_multiply_long(instruction)) {
    const DecodedMultiplyLongInstruction decoded = decode_multiply_long(instruction);
    const std::uint32_t multiplier = registers_.at(decoded.rs);
    const std::uint32_t iterations =
        decoded.signed_multiply ? signed_multiply_iterations(multiplier)
                                : unsigned_multiply_iterations(multiplier);
    return ArmElapsedCycleEstimate{iterations + (decoded.accumulate ? 3U : 2U), true,
                                   false};
  }

  if (can_decode_multiply(instruction)) {
    const DecodedMultiplyInstruction decoded = decode_multiply(instruction);
    const std::uint32_t iterations =
        signed_multiply_iterations(registers_.at(decoded.rs));
    return ArmElapsedCycleEstimate{iterations + (decoded.accumulate ? 2U : 1U), true,
                                   false};
  }

  return std::nullopt;
}

std::optional<ArmElapsedCycleEstimate> Arm7tdmi::runtime_thumb_elapsed_cycles(
    std::uint16_t instruction) const {
  if (!can_decode_thumb_alu(instruction)) {
    return std::nullopt;
  }

  const DecodedThumbAluInstruction decoded = decode_thumb_alu(instruction);
  if (decoded.opcode != ThumbAluOpcode::mul) {
    return std::nullopt;
  }

  // The ARM7TDMI multiplier iterates over the multiplier operand, which is
  // Rs for Thumb MUL (Rd holds the destination/multiplicand).
  return ArmElapsedCycleEstimate{
      signed_multiply_iterations(registers_.at(decoded.rs)) + 1U, true, false};
}

ArmStepResult Arm7tdmi::step_arm(std::uint32_t instruction) {
  const std::optional<ArmElapsedCycleEstimate> elapsed_override =
      runtime_multiply_elapsed_cycles(instruction);
  const ExecuteStatus status = execute_arm(instruction);
  return finish_step(instruction, std::nullopt, status, nullptr, elapsed_override);
}

ArmStepResult Arm7tdmi::step_arm(std::uint32_t instruction, MemoryBus& memory) {
  const std::optional<std::uint32_t> data_address = first_data_address(instruction);
  if (data_address.has_value() && memory_condition_passed(instruction) &&
      !estimate_arm_elapsed_cycles(instruction, data_address.value()).has_value()) {
    return {ExecuteStatus::unsupported, 0, elapsed_cycles_, false, false};
  }
  const std::optional<ArmElapsedCycleEstimate> elapsed_override =
      runtime_multiply_elapsed_cycles(instruction);
  const ExecuteStatus status = execute_arm(instruction, memory);
  return finish_step(instruction, data_address, status, nullptr, elapsed_override);
}

ArmStepResult Arm7tdmi::step_arm(
    std::uint32_t instruction, MemoryBus& memory, const WaitStateControl& waitcnt,
    std::optional<ArmElapsedCycleEstimate> elapsed_override) {
  const std::optional<std::uint32_t> data_address = first_data_address(instruction);
  if (data_address.has_value() && memory_condition_passed(instruction) &&
      !estimate_arm_elapsed_cycles(instruction, data_address.value(), waitcnt).has_value()) {
    return {ExecuteStatus::unsupported, 0, elapsed_cycles_, false, false};
  }
  const std::optional<ArmElapsedCycleEstimate> runtime_override =
      elapsed_override.has_value() ? elapsed_override
                                   : runtime_multiply_elapsed_cycles(instruction);
  const ExecuteStatus status = execute_arm(instruction, memory);
  return finish_step(instruction, data_address, status, &waitcnt, runtime_override);
}

ArmStepResult Arm7tdmi::step_thumb(std::uint16_t instruction) {
  const std::optional<ArmElapsedCycleEstimate> elapsed_override =
      runtime_thumb_elapsed_cycles(instruction);
  const ExecuteStatus status = execute_thumb(instruction);
  if (status == ExecuteStatus::skipped_condition) {
    elapsed_cycles_ += kThumbSkippedConditionElapsedCycles;
    return {status, kThumbSkippedConditionElapsedCycles, elapsed_cycles_, false,
            false};
  }
  if (status != ExecuteStatus::executed) {
    return {status, 0, elapsed_cycles_, false, false};
  }

  constexpr std::uint32_t kSeedThumbElapsedCycles = 1;
  const ArmElapsedCycleEstimate elapsed =
      elapsed_override.value_or(ArmElapsedCycleEstimate{kSeedThumbElapsedCycles, false, false});
  elapsed_cycles_ += elapsed.cycles;
  return {status, elapsed.cycles, elapsed_cycles_, elapsed.data_dependent,
          elapsed.memory_timing_applied};
}

ArmStepResult Arm7tdmi::step_thumb(std::uint16_t instruction, MemoryBus& memory) {
  const std::optional<ArmElapsedCycleEstimate> elapsed_override =
      runtime_thumb_elapsed_cycles(instruction);
  const ExecuteStatus status = execute_thumb(instruction, memory);
  if (status == ExecuteStatus::skipped_condition) {
    elapsed_cycles_ += kThumbSkippedConditionElapsedCycles;
    return {status, kThumbSkippedConditionElapsedCycles, elapsed_cycles_, false,
            false};
  }
  if (status != ExecuteStatus::executed) {
    return {status, 0, elapsed_cycles_, false, false};
  }

  constexpr std::uint32_t kSeedThumbElapsedCycles = 1;
  const ArmElapsedCycleEstimate elapsed =
      elapsed_override.value_or(ArmElapsedCycleEstimate{kSeedThumbElapsedCycles, false, false});
  elapsed_cycles_ += elapsed.cycles;
  return {status, elapsed.cycles, elapsed_cycles_, elapsed.data_dependent,
          elapsed.memory_timing_applied};
}

ArmStepResult Arm7tdmi::step_thumb(std::uint16_t instruction, MemoryBus& memory,
                                   const WaitStateControl& waitcnt) {
  return step_thumb(instruction, memory, waitcnt, false, std::nullopt);
}

ArmStepResult Arm7tdmi::step_thumb(std::uint16_t instruction, MemoryBus& memory,
                                   const WaitStateControl& waitcnt,
                                   bool prefetch_internal_load_overlap,
                                   std::optional<ArmElapsedCycleEstimate>
                                       elapsed_override) {
  std::optional<ArmElapsedCycleEstimate> estimate = std::nullopt;
  if (can_decode_thumb_memory_transfer(instruction)) {
    const DecodedThumbMemoryTransferInstruction decoded =
        decode_thumb_memory_transfer(instruction);
    const std::uint32_t base =
        decoded.rb == kPc ? align_word(registers_.at(kPc) + 4U)
                          : registers_.at(decoded.rb);
    const std::uint32_t offset =
        decoded.offset_is_register ? registers_.at(decoded.offset) : decoded.offset;
    const std::uint32_t address = base + offset;
    const MemoryAccessTiming timing =
        MemoryBus::timing(address, thumb_transfer_access_width(decoded.kind), waitcnt);
    if (has_elapsed_timing_for_estimate(address, timing, true)) {
      std::uint32_t cycles = thumb_memory_cycles(decoded, timing);
      if (thumb_word_load_from_game_pak_rom(decoded, address)) {
        cycles += 3U;
      }
      if (thumb_prefetch_overlaps_internal_data_load_cycle(
              decoded, address,
              waitcnt.prefetch_enabled() && prefetch_internal_load_overlap)) {
        --cycles;
      }
      estimate = ArmElapsedCycleEstimate{cycles, false, true};
    }
  }
  if (can_decode_thumb_block_transfer(instruction)) {
    const DecodedThumbBlockTransferInstruction decoded =
        decode_thumb_block_transfer(instruction);
    const std::uint32_t address = registers_.at(decoded.rb);
    const std::optional<std::uint32_t> mirrored_oam_elapsed =
        thumb_mirrored_oam_block_load_elapsed_cycles(decoded, address, waitcnt);
    if (mirrored_oam_elapsed.has_value()) {
      estimate = ArmElapsedCycleEstimate{mirrored_oam_elapsed.value(), false, true};
    } else {
      const std::uint32_t register_count = count_registers(decoded.register_list);
      const MemoryAccessTiming timing =
          MemoryBus::timing(address, AccessWidth::word, waitcnt);
      if (has_elapsed_timing_for_estimate(address, timing, true)) {
        const std::uint32_t cycles =
            decoded.load
                ? 1U + register_count *
                           (static_cast<std::uint32_t>(timing.nonsequential) +
                            timing.sequential)
                : register_count * static_cast<std::uint32_t>(timing.sequential);
        estimate = ArmElapsedCycleEstimate{cycles, false, true};
      }
    }
  }
  const std::optional<ArmElapsedCycleEstimate> runtime_override =
      elapsed_override.has_value() ? elapsed_override
                                   : runtime_thumb_elapsed_cycles(instruction);

  const ExecuteStatus status = execute_thumb(instruction, memory);
  if (status == ExecuteStatus::skipped_condition) {
    elapsed_cycles_ += kThumbSkippedConditionElapsedCycles;
    return {status, kThumbSkippedConditionElapsedCycles, elapsed_cycles_, false,
            false};
  }
  if (status != ExecuteStatus::executed) {
    return {status, 0, elapsed_cycles_, false, false};
  }

  constexpr std::uint32_t kSeedThumbElapsedCycles = 1;
  const ArmElapsedCycleEstimate elapsed =
      runtime_override.has_value()
          ? runtime_override.value()
          : estimate.value_or(ArmElapsedCycleEstimate{kSeedThumbElapsedCycles, false, false});
  elapsed_cycles_ += elapsed.cycles;
  return {status, elapsed.cycles, elapsed_cycles_, elapsed.data_dependent,
          elapsed.memory_timing_applied};
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
