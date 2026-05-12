#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace gba::core {

class MemoryBus;
class WaitStateControl;

enum class ArmCondition : std::uint8_t {
  eq = 0x0,
  ne = 0x1,
  cs = 0x2,
  cc = 0x3,
  mi = 0x4,
  pl = 0x5,
  vs = 0x6,
  vc = 0x7,
  hi = 0x8,
  ls = 0x9,
  ge = 0xA,
  lt = 0xB,
  gt = 0xC,
  le = 0xD,
  al = 0xE,
};

enum class ArmOpcode : std::uint8_t {
  and_ = 0x0,
  eor = 0x1,
  sub = 0x2,
  rsb = 0x3,
  add = 0x4,
  adc = 0x5,
  sbc = 0x6,
  rsc = 0x7,
  tst = 0x8,
  teq = 0x9,
  cmp = 0xA,
  cmn = 0xB,
  orr = 0xC,
  mov = 0xD,
  bic = 0xE,
  mvn = 0xF,
};

enum class ArmShiftType : std::uint8_t {
  lsl = 0x0,
  lsr = 0x1,
  asr = 0x2,
  ror = 0x3,
};

enum class ThumbOpcode : std::uint8_t {
  mov,
  cmp,
  add,
  sub,
};

enum class ThumbHighRegisterOpcode : std::uint8_t {
  add,
  cmp,
  mov,
  bx,
};

enum class ThumbAluOpcode : std::uint8_t {
  and_,
  eor,
  lsl,
  lsr,
  asr,
  adc,
  sbc,
  ror,
  tst,
  neg,
  cmp,
  cmn,
  orr,
  mul,
  bic,
  mvn,
};

enum class ThumbMemoryTransferKind : std::uint8_t {
  word,
  byte,
  halfword,
  signed_byte,
  signed_halfword,
};

enum class ArmProgramStatusRegister : std::uint8_t {
  cpsr,
  spsr,
};

enum class CpuMode : std::uint8_t {
  user = 0x10,
  fiq = 0x11,
  irq = 0x12,
  supervisor = 0x13,
  abort = 0x17,
  undefined = 0x1B,
  system = 0x1F,
};

enum class ExceptionKind : std::uint8_t {
  reset,
  undefined_instruction,
  software_interrupt,
  prefetch_abort,
  data_abort,
  irq,
  fiq,
};

enum class ExecuteStatus : std::uint8_t {
  executed,
  skipped_condition,
  unsupported,
};

struct ArmCycleEstimate {
  std::uint8_t sequential;
  std::uint8_t nonsequential;
  std::uint8_t internal;
  bool data_dependent;
};

struct ArmElapsedCycleEstimate {
  std::uint32_t cycles;
  bool data_dependent;
  bool memory_timing_applied;
};

struct ArmStepResult {
  ExecuteStatus status;
  std::uint32_t elapsed_cycles;
  std::uint64_t total_elapsed_cycles;
  bool data_dependent;
  bool memory_timing_applied;
};

struct ExceptionVector {
  ExceptionKind kind;
  std::uint32_t vector_address;
  CpuMode mode;
  std::uint32_t link_offset;
  bool save_cpsr;
  bool disable_irq;
  bool disable_fiq;
};

struct DecodedArmInstruction {
  ArmCondition condition;
  ArmOpcode opcode;
  bool set_flags;
  std::uint8_t rn;
  std::uint8_t rd;
  std::uint32_t operand2;
  bool shifter_carry_valid;
  bool shifter_carry;
};

struct DecodedBranchInstruction {
  ArmCondition condition;
  bool link;
  std::int32_t offset;
};

struct DecodedBranchExchangeInstruction {
  ArmCondition condition;
  std::uint8_t rm;
};

struct DecodedMultiplyInstruction {
  ArmCondition condition;
  bool accumulate;
  bool set_flags;
  std::uint8_t rd;
  std::uint8_t rn;
  std::uint8_t rs;
  std::uint8_t rm;
};

struct DecodedMultiplyLongInstruction {
  ArmCondition condition;
  bool signed_multiply;
  bool accumulate;
  bool set_flags;
  std::uint8_t rd_hi;
  std::uint8_t rd_lo;
  std::uint8_t rs;
  std::uint8_t rm;
};

struct DecodedSingleDataTransferInstruction {
  ArmCondition condition;
  bool load;
  bool byte;
  bool pre_index;
  bool up;
  bool write_back;
  std::uint8_t rn;
  std::uint8_t rd;
  std::uint32_t offset;
};

struct DecodedPsrTransferInstruction {
  ArmCondition condition;
  bool write;
  ArmProgramStatusRegister psr;
  std::uint8_t rd_or_rm;
  std::uint8_t field_mask;
  std::uint32_t operand;
  bool immediate_operand;
};

struct DecodedSwapInstruction {
  ArmCondition condition;
  bool byte;
  std::uint8_t rn;
  std::uint8_t rd;
  std::uint8_t rm;
};

struct DecodedHalfwordDataTransferInstruction {
  ArmCondition condition;
  bool load;
  bool signed_transfer;
  bool halfword;
  bool pre_index;
  bool up;
  bool write_back;
  std::uint8_t rn;
  std::uint8_t rd;
  std::uint32_t offset;
};

struct DecodedBlockDataTransferInstruction {
  ArmCondition condition;
  bool load;
  bool pre_index;
  bool up;
  bool write_back;
  std::uint8_t rn;
  std::uint16_t register_list;
};

struct DecodedThumbInstruction {
  ThumbOpcode opcode;
  std::uint8_t rd;
  std::uint8_t rs;
  std::uint32_t operand;
  bool operand_is_register;
};

struct DecodedThumbShiftInstruction {
  ArmShiftType type;
  std::uint8_t rd;
  std::uint8_t rs;
  std::uint8_t amount;
};

struct DecodedThumbAluInstruction {
  ThumbAluOpcode opcode;
  std::uint8_t rd;
  std::uint8_t rs;
};

struct DecodedThumbBranchInstruction {
  bool conditional;
  ArmCondition condition;
  std::int32_t offset;
};

struct DecodedThumbLongBranchLinkInstruction {
  bool second_half;
  std::int32_t offset;
};

struct DecodedThumbHighRegisterInstruction {
  ThumbHighRegisterOpcode opcode;
  std::uint8_t rd;
  std::uint8_t rs;
};

struct DecodedThumbMemoryTransferInstruction {
  bool load;
  ThumbMemoryTransferKind kind;
  std::uint8_t rd;
  std::uint8_t rb;
  std::uint32_t offset;
  bool offset_is_register;
};

struct DecodedThumbBlockTransferInstruction {
  bool load;
  std::uint8_t rb;
  std::uint8_t register_list;
};

struct DecodedThumbStackInstruction {
  bool load;
  bool extra_register;
  std::uint8_t register_list;
};

struct DecodedThumbStackPointerInstruction {
  bool subtract;
  std::uint32_t offset;
};

struct DecodedThumbLoadAddressInstruction {
  bool base_is_sp;
  std::uint8_t rd;
  std::uint32_t offset;
};

class Arm7tdmi {
 public:
  static constexpr std::size_t kRegisterCount = 16;
  static constexpr std::uint8_t kLinkRegister = 14;
  static constexpr std::uint8_t kPc = 15;

  Arm7tdmi();

  [[nodiscard]] static bool can_decode_data_processing_immediate(std::uint32_t instruction);
  [[nodiscard]] static DecodedArmInstruction decode_data_processing_immediate(
      std::uint32_t instruction);
  [[nodiscard]] static bool can_decode_data_processing_register_shift(
      std::uint32_t instruction);
  [[nodiscard]] static DecodedArmInstruction decode_data_processing_register_shift(
      std::uint32_t instruction, std::uint32_t rm_value);
  [[nodiscard]] static DecodedArmInstruction decode_data_processing_register_shift(
      std::uint32_t instruction, std::uint32_t rm_value, bool carry_in);
  [[nodiscard]] static DecodedArmInstruction decode_data_processing_register_shift(
      std::uint32_t instruction, std::uint32_t rm_value, std::uint32_t rs_value);
  [[nodiscard]] static bool can_decode_multiply(std::uint32_t instruction);
  [[nodiscard]] static DecodedMultiplyInstruction decode_multiply(
      std::uint32_t instruction);
  [[nodiscard]] static bool can_decode_multiply_long(std::uint32_t instruction);
  [[nodiscard]] static DecodedMultiplyLongInstruction decode_multiply_long(
      std::uint32_t instruction);
  [[nodiscard]] static bool can_decode_branch(std::uint32_t instruction);
  [[nodiscard]] static DecodedBranchInstruction decode_branch(std::uint32_t instruction);
  [[nodiscard]] static bool can_decode_branch_exchange(std::uint32_t instruction);
  [[nodiscard]] static DecodedBranchExchangeInstruction decode_branch_exchange(
      std::uint32_t instruction);
  [[nodiscard]] static bool can_decode_single_data_transfer_immediate(
      std::uint32_t instruction);
  [[nodiscard]] static DecodedSingleDataTransferInstruction decode_single_data_transfer_immediate(
      std::uint32_t instruction);
  [[nodiscard]] static bool can_decode_single_data_transfer_register(
      std::uint32_t instruction);
  [[nodiscard]] static DecodedSingleDataTransferInstruction decode_single_data_transfer_register(
      std::uint32_t instruction, std::uint32_t rm_value);
  [[nodiscard]] static DecodedSingleDataTransferInstruction decode_single_data_transfer_register(
      std::uint32_t instruction, std::uint32_t rm_value, bool carry_in);
  [[nodiscard]] static bool can_decode_psr_transfer(std::uint32_t instruction);
  [[nodiscard]] static DecodedPsrTransferInstruction decode_psr_transfer(
      std::uint32_t instruction);
  [[nodiscard]] static bool can_decode_software_interrupt(std::uint32_t instruction);
  [[nodiscard]] static bool can_decode_swap(std::uint32_t instruction);
  [[nodiscard]] static DecodedSwapInstruction decode_swap(std::uint32_t instruction);
  [[nodiscard]] static bool can_decode_halfword_data_transfer_immediate(
      std::uint32_t instruction);
  [[nodiscard]] static DecodedHalfwordDataTransferInstruction
  decode_halfword_data_transfer_immediate(std::uint32_t instruction);
  [[nodiscard]] static bool can_decode_halfword_data_transfer_register(
      std::uint32_t instruction);
  [[nodiscard]] static DecodedHalfwordDataTransferInstruction
  decode_halfword_data_transfer_register(std::uint32_t instruction, std::uint32_t rm_value);
  [[nodiscard]] static bool can_decode_block_data_transfer(std::uint32_t instruction);
  [[nodiscard]] static DecodedBlockDataTransferInstruction decode_block_data_transfer(
      std::uint32_t instruction);
  [[nodiscard]] static bool can_decode_thumb_shift_immediate(std::uint16_t instruction);
  [[nodiscard]] static DecodedThumbShiftInstruction decode_thumb_shift_immediate(
      std::uint16_t instruction);
  [[nodiscard]] static bool can_decode_thumb_alu(std::uint16_t instruction);
  [[nodiscard]] static DecodedThumbAluInstruction decode_thumb_alu(
      std::uint16_t instruction);
  [[nodiscard]] static bool can_decode_thumb_add_subtract(std::uint16_t instruction);
  [[nodiscard]] static DecodedThumbInstruction decode_thumb_add_subtract(
      std::uint16_t instruction);
  [[nodiscard]] static bool can_decode_thumb_immediate(std::uint16_t instruction);
  [[nodiscard]] static DecodedThumbInstruction decode_thumb_immediate(
      std::uint16_t instruction);
  [[nodiscard]] static bool can_decode_thumb_unconditional_branch(
      std::uint16_t instruction);
  [[nodiscard]] static DecodedThumbBranchInstruction decode_thumb_unconditional_branch(
      std::uint16_t instruction);
  [[nodiscard]] static bool can_decode_thumb_conditional_branch(
      std::uint16_t instruction);
  [[nodiscard]] static DecodedThumbBranchInstruction decode_thumb_conditional_branch(
      std::uint16_t instruction);
  [[nodiscard]] static bool can_decode_thumb_long_branch_link(std::uint16_t instruction);
  [[nodiscard]] static DecodedThumbLongBranchLinkInstruction decode_thumb_long_branch_link(
      std::uint16_t instruction);
  [[nodiscard]] static bool can_decode_thumb_high_register(std::uint16_t instruction);
  [[nodiscard]] static DecodedThumbHighRegisterInstruction decode_thumb_high_register(
      std::uint16_t instruction);
  [[nodiscard]] static bool can_decode_thumb_memory_transfer(std::uint16_t instruction);
  [[nodiscard]] static DecodedThumbMemoryTransferInstruction decode_thumb_memory_transfer(
      std::uint16_t instruction);
  [[nodiscard]] static bool can_decode_thumb_block_transfer(std::uint16_t instruction);
  [[nodiscard]] static DecodedThumbBlockTransferInstruction decode_thumb_block_transfer(
      std::uint16_t instruction);
  [[nodiscard]] static bool can_decode_thumb_stack_transfer(std::uint16_t instruction);
  [[nodiscard]] static DecodedThumbStackInstruction decode_thumb_stack_transfer(
      std::uint16_t instruction);
  [[nodiscard]] static bool can_decode_thumb_stack_pointer_adjust(
      std::uint16_t instruction);
  [[nodiscard]] static DecodedThumbStackPointerInstruction
  decode_thumb_stack_pointer_adjust(std::uint16_t instruction);
  [[nodiscard]] static bool can_decode_thumb_load_address(std::uint16_t instruction);
  [[nodiscard]] static DecodedThumbLoadAddressInstruction decode_thumb_load_address(
      std::uint16_t instruction);
  [[nodiscard]] static bool can_decode_thumb_software_interrupt(std::uint16_t instruction);
  [[nodiscard]] static ExceptionVector exception_vector(ExceptionKind kind);
  [[nodiscard]] static std::optional<ArmCycleEstimate> estimate_arm_cycles(
      std::uint32_t instruction);
  [[nodiscard]] static std::optional<ArmElapsedCycleEstimate> estimate_arm_elapsed_cycles(
      std::uint32_t instruction, std::uint32_t data_address);
  [[nodiscard]] static std::optional<ArmElapsedCycleEstimate> estimate_arm_elapsed_cycles(
      std::uint32_t instruction, std::uint32_t data_address,
      const WaitStateControl& waitcnt);

  [[nodiscard]] std::uint32_t register_value(std::uint8_t index) const;
  void set_register(std::uint8_t index, std::uint32_t value);
  void reset();
  void reset_elapsed_cycles();

  [[nodiscard]] bool negative() const;
  [[nodiscard]] bool zero() const;
  [[nodiscard]] bool carry() const;
  [[nodiscard]] bool overflow() const;
  [[nodiscard]] bool irq_disabled() const;
  [[nodiscard]] bool fiq_disabled() const;
  [[nodiscard]] CpuMode current_mode() const;
  [[nodiscard]] bool thumb_state() const;
  [[nodiscard]] std::uint32_t cpsr() const;
  [[nodiscard]] bool set_cpsr(std::uint32_t value);
  [[nodiscard]] bool has_spsr() const;
  [[nodiscard]] std::optional<std::uint32_t> spsr() const;
  [[nodiscard]] bool set_spsr(std::uint32_t value);
  [[nodiscard]] std::uint64_t elapsed_cycles() const;
  [[nodiscard]] std::uint64_t state_hash() const;

  [[nodiscard]] ExecuteStatus execute_arm(std::uint32_t instruction);
  [[nodiscard]] ExecuteStatus execute_arm(std::uint32_t instruction, MemoryBus& memory);
  [[nodiscard]] ExecuteStatus execute_thumb(std::uint16_t instruction);
  [[nodiscard]] ExecuteStatus execute_thumb(std::uint16_t instruction, MemoryBus& memory);
  [[nodiscard]] ExecuteStatus enter_exception(ExceptionKind kind);
  [[nodiscard]] ExecuteStatus return_from_exception(std::uint32_t link_adjustment);
  [[nodiscard]] ArmStepResult step_arm(std::uint32_t instruction);
  [[nodiscard]] ArmStepResult step_arm(std::uint32_t instruction, MemoryBus& memory);
  [[nodiscard]] ArmStepResult step_arm(std::uint32_t instruction, MemoryBus& memory,
                                       const WaitStateControl& waitcnt,
                                       std::optional<ArmElapsedCycleEstimate>
                                           elapsed_override = std::nullopt);
  [[nodiscard]] ArmStepResult step_thumb(std::uint16_t instruction);
  [[nodiscard]] ArmStepResult step_thumb(std::uint16_t instruction, MemoryBus& memory);
  [[nodiscard]] ArmStepResult step_thumb(std::uint16_t instruction, MemoryBus& memory,
                                         const WaitStateControl& waitcnt);
  [[nodiscard]] ArmStepResult step_thumb(std::uint16_t instruction, MemoryBus& memory,
                                         const WaitStateControl& waitcnt,
                                         bool prefetch_internal_load_overlap,
                                         std::optional<ArmElapsedCycleEstimate>
                                             elapsed_override = std::nullopt);

 private:
  std::array<std::uint32_t, kRegisterCount> registers_;
  std::uint64_t elapsed_cycles_;
  bool negative_;
  bool zero_;
  bool carry_;
  bool overflow_;
  bool irq_disabled_;
  bool fiq_disabled_;
  bool thumb_state_;
  CpuMode mode_;
  std::array<std::uint32_t, 5> shared_r8_r12_;
  std::array<std::uint32_t, 5> fiq_r8_r12_;
  std::uint32_t user_sp_;
  std::uint32_t user_lr_;
  std::uint32_t fiq_sp_;
  std::uint32_t fiq_lr_;
  std::uint32_t irq_sp_;
  std::uint32_t irq_lr_;
  std::uint32_t supervisor_sp_;
  std::uint32_t supervisor_lr_;
  std::uint32_t abort_sp_;
  std::uint32_t abort_lr_;
  std::uint32_t undefined_sp_;
  std::uint32_t undefined_lr_;
  std::uint32_t fiq_spsr_;
  std::uint32_t supervisor_spsr_;
  std::uint32_t abort_spsr_;
  std::uint32_t irq_spsr_;
  std::uint32_t undefined_spsr_;

  [[nodiscard]] bool condition_passed(ArmCondition condition) const;
  [[nodiscard]] ExecuteStatus execute_data_processing(const DecodedArmInstruction& decoded,
                                                      std::uint32_t pc_offset = 8U);
  [[nodiscard]] ExecuteStatus execute_psr_transfer(const DecodedPsrTransferInstruction& decoded);
  [[nodiscard]] ExecuteStatus execute_swap(const DecodedSwapInstruction& decoded,
                                           MemoryBus& memory);
  [[nodiscard]] ExecuteStatus execute_multiply(const DecodedMultiplyInstruction& decoded);
  [[nodiscard]] ExecuteStatus execute_multiply_long(
      const DecodedMultiplyLongInstruction& decoded);
  [[nodiscard]] ExecuteStatus execute_thumb_shift(
      const DecodedThumbShiftInstruction& decoded);
  [[nodiscard]] ExecuteStatus execute_thumb_alu(const DecodedThumbAluInstruction& decoded);
  [[nodiscard]] ExecuteStatus execute_thumb_data_processing(
      const DecodedThumbInstruction& decoded);
  [[nodiscard]] ExecuteStatus execute_thumb_branch(
      const DecodedThumbBranchInstruction& decoded);
  [[nodiscard]] ExecuteStatus execute_thumb_long_branch_link(
      const DecodedThumbLongBranchLinkInstruction& decoded);
  [[nodiscard]] ExecuteStatus execute_thumb_high_register(
      const DecodedThumbHighRegisterInstruction& decoded);
  [[nodiscard]] ExecuteStatus execute_thumb_memory_transfer(
      const DecodedThumbMemoryTransferInstruction& decoded, MemoryBus& memory);
  [[nodiscard]] ExecuteStatus execute_thumb_block_transfer(
      const DecodedThumbBlockTransferInstruction& decoded, MemoryBus& memory);
  [[nodiscard]] ExecuteStatus execute_thumb_stack_transfer(
      const DecodedThumbStackInstruction& decoded, MemoryBus& memory);
  [[nodiscard]] ExecuteStatus execute_thumb_stack_pointer_adjust(
      const DecodedThumbStackPointerInstruction& decoded);
  [[nodiscard]] ExecuteStatus execute_thumb_load_address(
      const DecodedThumbLoadAddressInstruction& decoded);
  void set_spsr_for_mode(CpuMode mode, std::uint32_t value);
  void save_banked_registers(CpuMode mode);
  void load_banked_registers(CpuMode mode);
  void switch_mode(CpuMode mode);
  [[nodiscard]] std::optional<std::uint32_t> first_data_address(
      std::uint32_t instruction) const;
  [[nodiscard]] bool memory_condition_passed(std::uint32_t instruction) const;
  [[nodiscard]] ArmStepResult finish_step(std::uint32_t instruction,
                                          std::optional<std::uint32_t> data_address,
                                          ExecuteStatus status,
                                          const WaitStateControl* waitcnt = nullptr,
                                          std::optional<ArmElapsedCycleEstimate>
                                              elapsed_override = std::nullopt);
  [[nodiscard]] std::optional<ArmElapsedCycleEstimate> runtime_multiply_elapsed_cycles(
      std::uint32_t instruction) const;
  [[nodiscard]] std::optional<ArmElapsedCycleEstimate> runtime_thumb_elapsed_cycles(
      std::uint16_t instruction) const;
  [[nodiscard]] std::uint32_t arm_visible_register_value(std::uint8_t index) const;
  [[nodiscard]] std::uint32_t thumb_visible_register_value(std::uint8_t index) const;
  void set_nz(std::uint32_t result);
  void set_nz64(std::uint64_t result);
  void set_add_flags(std::uint32_t left, std::uint32_t right, std::uint32_t result);
  void set_adc_flags(std::uint32_t left, std::uint32_t right, bool carry_in,
                     std::uint32_t result);
  void set_sub_flags(std::uint32_t left, std::uint32_t right, std::uint32_t result);
  void set_sbc_flags(std::uint32_t left, std::uint32_t right, bool carry_in,
                     std::uint32_t result);
  void set_logical_flags(std::uint32_t result, const DecodedArmInstruction& decoded);
};

}  // namespace gba::core
