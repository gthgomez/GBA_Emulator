#include "gba/core/arm7tdmi.hpp"
#include "gba/core/memory_bus.hpp"
#include "gba/core/wait_state_control.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint32_t kCondAl = 0xE0000000;
constexpr std::uint32_t kCondEq = 0x00000000;
constexpr std::uint32_t kCondNe = 0x10000000;
constexpr std::uint32_t kCondMi = 0x40000000;
constexpr std::uint32_t kCondGe = 0xA0000000;
constexpr std::uint32_t kCondLt = 0xB0000000;
constexpr std::uint32_t kCondNv = 0xF0000000;
constexpr std::uint32_t kBranch = 0x0A000000;
constexpr std::uint32_t kBranchLink = 0x0B000000;
constexpr std::uint32_t kDataProcessingImmediate = 0x02000000;
constexpr std::uint32_t kSoftwareInterrupt = 0x0F000000;
constexpr std::uint32_t kSingleDataTransferImmediate = 0x04000000;
constexpr std::uint32_t kSingleDataTransferRegister = 0x06000000;
constexpr std::uint32_t kPreIndexed = 0x01000000;
constexpr std::uint32_t kUp = 0x00800000;
constexpr std::uint32_t kLoad = 0x00100000;
constexpr std::uint32_t kWriteBack = 0x00200000;
constexpr std::uint32_t kByteTransfer = 0x00400000;
constexpr std::uint32_t kSetFlags = 0x00100000;
constexpr std::uint32_t kHalfwordDataTransferImmediate = 0x004000B0;
constexpr std::uint32_t kSignedByteDataTransferImmediate = 0x004000D0;
constexpr std::uint32_t kSignedHalfwordDataTransferImmediate = 0x004000F0;
constexpr std::uint32_t kHalfwordDataTransferRegister = 0x000000B0;
constexpr std::uint32_t kSignedByteDataTransferRegister = 0x000000D0;
constexpr std::uint32_t kSignedHalfwordDataTransferRegister = 0x000000F0;
constexpr std::uint32_t kBlockDataTransfer = 0x08000000;
constexpr std::uint32_t kMultiply = 0x00000090;
constexpr std::uint32_t kMultiplyAccumulate = 0x00200090;
constexpr std::uint32_t kMultiplyLong = 0x00800090;
constexpr std::uint32_t kSignedMultiplyLong = 0x00C00090;
constexpr std::uint32_t kMultiplyLongAccumulate = 0x00A00090;
constexpr std::uint32_t kSignedMultiplyLongAccumulate = 0x00E00090;

constexpr std::uint32_t opcode(std::uint8_t value) {
  return static_cast<std::uint32_t>(value) << 21;
}

constexpr std::uint32_t rn(std::uint8_t value) {
  return static_cast<std::uint32_t>(value) << 16;
}

constexpr std::uint32_t rd(std::uint8_t value) {
  return static_cast<std::uint32_t>(value) << 12;
}

constexpr std::uint32_t multiply_rd(std::uint8_t value) {
  return static_cast<std::uint32_t>(value) << 16;
}

constexpr std::uint32_t multiply_rn(std::uint8_t value) {
  return static_cast<std::uint32_t>(value) << 12;
}

constexpr std::uint32_t multiply_rs(std::uint8_t value) {
  return static_cast<std::uint32_t>(value) << 8;
}

constexpr std::uint32_t multiply_long_rd_hi(std::uint8_t value) {
  return static_cast<std::uint32_t>(value) << 16;
}

constexpr std::uint32_t multiply_long_rd_lo(std::uint8_t value) {
  return static_cast<std::uint32_t>(value) << 12;
}

constexpr std::uint32_t rm(std::uint8_t value) {
  return value;
}

constexpr std::uint32_t arm_mrs(bool spsr, std::uint8_t destination) {
  return kCondAl | 0x010F0000U | (spsr ? 0x00400000U : 0U) | rd(destination);
}

constexpr std::uint32_t arm_msr_register(bool spsr, std::uint8_t field_mask,
                                         std::uint8_t source) {
  return kCondAl | 0x0120F000U | (spsr ? 0x00400000U : 0U) |
         (static_cast<std::uint32_t>(field_mask & 0xFU) << 16) | rm(source);
}

constexpr std::uint32_t arm_msr_immediate(bool spsr, std::uint8_t field_mask,
                                          std::uint8_t immediate,
                                          std::uint8_t rotate = 0) {
  return kCondAl | 0x0320F000U | (spsr ? 0x00400000U : 0U) |
         (static_cast<std::uint32_t>(field_mask & 0xFU) << 16) |
         (static_cast<std::uint32_t>(rotate & 0xFU) << 8) | immediate;
}

constexpr std::uint32_t arm_swi(std::uint32_t comment) {
  return kCondAl | kSoftwareInterrupt | (comment & 0x00FFFFFFU);
}

constexpr std::uint32_t arm_swp(bool byte, std::uint8_t base, std::uint8_t destination,
                                std::uint8_t source) {
  return kCondAl | 0x01000090U | (byte ? 0x00400000U : 0U) | rn(base) |
         rd(destination) | rm(source);
}

constexpr std::uint32_t arm_bx(std::uint32_t condition, std::uint8_t source) {
  return condition | 0x012FFF10U | rm(source);
}

constexpr std::uint32_t imm(std::uint8_t value) {
  return value;
}

constexpr std::uint32_t shift_imm(std::uint8_t value) {
  return static_cast<std::uint32_t>(value) << 7;
}

constexpr std::uint32_t shift_type(std::uint8_t value) {
  return static_cast<std::uint32_t>(value) << 5;
}

constexpr std::uint32_t rs(std::uint8_t value) {
  return static_cast<std::uint32_t>(value) << 8;
}

constexpr std::uint32_t branch_offset(std::int32_t words) {
  return static_cast<std::uint32_t>(words) & 0x00FFFFFFU;
}

constexpr std::uint32_t offset12(std::uint16_t value) {
  return static_cast<std::uint32_t>(value) & 0xFFFU;
}

constexpr std::uint32_t halfword_offset(std::uint8_t value) {
  return (static_cast<std::uint32_t>(value & 0xF0U) << 4) |
         static_cast<std::uint32_t>(value & 0x0FU);
}

constexpr std::uint32_t reg_list(std::uint16_t value) {
  return value;
}

constexpr std::uint16_t thumb_add_sub(bool immediate_operand, bool subtract,
                                      std::uint8_t operand, std::uint8_t rs,
                                      std::uint8_t rd) {
  return static_cast<std::uint16_t>(
      0x1800U | (static_cast<std::uint16_t>(immediate_operand ? 1U : 0U) << 10) |
      (static_cast<std::uint16_t>(subtract ? 1U : 0U) << 9) |
      (static_cast<std::uint16_t>(operand & 0x7U) << 6) |
      (static_cast<std::uint16_t>(rs & 0x7U) << 3) |
      static_cast<std::uint16_t>(rd & 0x7U));
}

constexpr std::uint16_t thumb_immediate(std::uint8_t opcode_value, std::uint8_t rd,
                                        std::uint8_t immediate) {
  return static_cast<std::uint16_t>(0x2000U |
                                    (static_cast<std::uint16_t>(opcode_value & 0x3U) << 11) |
                                    (static_cast<std::uint16_t>(rd & 0x7U) << 8) |
                                    immediate);
}

constexpr std::uint16_t thumb_shift_immediate(std::uint8_t opcode_value,
                                              std::uint8_t amount, std::uint8_t rs,
                                              std::uint8_t rd) {
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(opcode_value & 0x3U) << 11) |
      (static_cast<std::uint16_t>(amount & 0x1FU) << 6) |
      (static_cast<std::uint16_t>(rs & 0x7U) << 3) |
      static_cast<std::uint16_t>(rd & 0x7U));
}

constexpr std::uint16_t thumb_alu(std::uint8_t opcode_value, std::uint8_t rs,
                                  std::uint8_t rd) {
  return static_cast<std::uint16_t>(
      0x4000U | (static_cast<std::uint16_t>(opcode_value & 0xFU) << 6) |
      (static_cast<std::uint16_t>(rs & 0x7U) << 3) |
      static_cast<std::uint16_t>(rd & 0x7U));
}

constexpr std::uint16_t thumb_conditional_branch(std::uint8_t condition,
                                                 std::int8_t halfword_offset) {
  return static_cast<std::uint16_t>(
      0xD000U | (static_cast<std::uint16_t>(condition & 0xFU) << 8) |
      static_cast<std::uint8_t>(halfword_offset));
}

constexpr std::uint16_t thumb_unconditional_branch(std::int16_t halfword_offset) {
  return static_cast<std::uint16_t>(
      0xE000U | (static_cast<std::uint16_t>(halfword_offset) & 0x7FFU));
}

constexpr std::uint16_t thumb_bl_prefix(std::int16_t page_offset) {
  return static_cast<std::uint16_t>(0xF000U |
                                    (static_cast<std::uint16_t>(page_offset) & 0x7FFU));
}

constexpr std::uint16_t thumb_bl_suffix(std::uint16_t halfword_offset) {
  return static_cast<std::uint16_t>(0xF800U | (halfword_offset & 0x7FFU));
}

constexpr std::uint16_t thumb_ldr_literal(std::uint8_t rd, std::uint8_t word_offset) {
  return static_cast<std::uint16_t>(
      0x4800U | (static_cast<std::uint16_t>(rd & 0x7U) << 8) | word_offset);
}

constexpr std::uint16_t thumb_high_register(std::uint8_t opcode_value, std::uint8_t rd,
                                            std::uint8_t rs) {
  return static_cast<std::uint16_t>(
      0x4400U | (static_cast<std::uint16_t>(opcode_value & 0x3U) << 8) |
      (static_cast<std::uint16_t>((rd >> 3) & 0x1U) << 7) |
      (static_cast<std::uint16_t>((rs >> 3) & 0x1U) << 6) |
      (static_cast<std::uint16_t>(rs & 0x7U) << 3) |
      static_cast<std::uint16_t>(rd & 0x7U));
}

constexpr std::uint16_t thumb_memory_register(std::uint8_t op, std::uint8_t ro,
                                              std::uint8_t rb, std::uint8_t rd) {
  return static_cast<std::uint16_t>(
      0x5000U | (static_cast<std::uint16_t>(op & 0x7U) << 9) |
      (static_cast<std::uint16_t>(ro & 0x7U) << 6) |
      (static_cast<std::uint16_t>(rb & 0x7U) << 3) |
      static_cast<std::uint16_t>(rd & 0x7U));
}

constexpr std::uint16_t thumb_memory_immediate(bool byte, bool load, std::uint8_t imm5,
                                               std::uint8_t rb, std::uint8_t rd) {
  return static_cast<std::uint16_t>(
      0x6000U | (static_cast<std::uint16_t>(byte ? 1U : 0U) << 12) |
      (static_cast<std::uint16_t>(load ? 1U : 0U) << 11) |
      (static_cast<std::uint16_t>(imm5 & 0x1FU) << 6) |
      (static_cast<std::uint16_t>(rb & 0x7U) << 3) |
      static_cast<std::uint16_t>(rd & 0x7U));
}

constexpr std::uint16_t thumb_memory_halfword_immediate(bool load, std::uint8_t imm5,
                                                        std::uint8_t rb, std::uint8_t rd) {
  return static_cast<std::uint16_t>(
      0x8000U | (static_cast<std::uint16_t>(load ? 1U : 0U) << 11) |
      (static_cast<std::uint16_t>(imm5 & 0x1FU) << 6) |
      (static_cast<std::uint16_t>(rb & 0x7U) << 3) |
      static_cast<std::uint16_t>(rd & 0x7U));
}

constexpr std::uint16_t thumb_sp_relative(bool load, std::uint8_t rd, std::uint8_t imm8) {
  return static_cast<std::uint16_t>(0x9000U |
                                    (static_cast<std::uint16_t>(load ? 1U : 0U) << 11) |
                                    (static_cast<std::uint16_t>(rd & 0x7U) << 8) |
                                    imm8);
}

constexpr std::uint16_t thumb_block_transfer(bool load, std::uint8_t rb,
                                             std::uint8_t list) {
  return static_cast<std::uint16_t>(0xC000U |
                                    (static_cast<std::uint16_t>(load ? 1U : 0U) << 11) |
                                    (static_cast<std::uint16_t>(rb & 0x7U) << 8) |
                                    list);
}

constexpr std::uint16_t thumb_stack(bool load, bool extra_register, std::uint8_t list) {
  return static_cast<std::uint16_t>((load ? 0xBC00U : 0xB400U) |
                                    (static_cast<std::uint16_t>(extra_register ? 1U : 0U)
                                     << 8) |
                                    list);
}

constexpr std::uint16_t thumb_sp_adjust(bool subtract, std::uint8_t word_offset) {
  return static_cast<std::uint16_t>(0xB000U |
                                    (static_cast<std::uint16_t>(subtract ? 1U : 0U)
                                     << 7) |
                                    (word_offset & 0x7FU));
}

constexpr std::uint16_t thumb_load_address(bool base_is_sp, std::uint8_t rd,
                                           std::uint8_t word_offset) {
  return static_cast<std::uint16_t>(0xA000U |
                                    (static_cast<std::uint16_t>(base_is_sp ? 1U : 0U)
                                     << 11) |
                                    (static_cast<std::uint16_t>(rd & 0x7U) << 8) |
                                    word_offset);
}

constexpr std::uint16_t thumb_swi(std::uint8_t comment) {
  return static_cast<std::uint16_t>(0xDF00U | comment);
}

std::vector<std::uint8_t> rom_with_word(std::uint32_t offset, std::uint32_t word) {
  std::vector<std::uint8_t> rom(256);
  rom.at(offset + 0U) = static_cast<std::uint8_t>(word & 0xFFU);
  rom.at(offset + 1U) = static_cast<std::uint8_t>((word >> 8) & 0xFFU);
  rom.at(offset + 2U) = static_cast<std::uint8_t>((word >> 16) & 0xFFU);
  rom.at(offset + 3U) = static_cast<std::uint8_t>((word >> 24) & 0xFFU);
  return rom;
}

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void expect_cycles(const gba::core::ArmCycleEstimate& actual, std::uint8_t sequential,
                   std::uint8_t nonsequential, std::uint8_t internal,
                   bool data_dependent, std::string_view message) {
  expect(actual.sequential == sequential, message);
  expect(actual.nonsequential == nonsequential, message);
  expect(actual.internal == internal, message);
  expect(actual.data_dependent == data_dependent, message);
}

void expect_elapsed(const gba::core::ArmElapsedCycleEstimate& actual, std::uint32_t cycles,
                    bool data_dependent, bool memory_timing_applied,
                    std::string_view message) {
  expect(actual.cycles == cycles, message);
  expect(actual.data_dependent == data_dependent, message);
  expect(actual.memory_timing_applied == memory_timing_applied, message);
}

void expect_step(const gba::core::ArmStepResult& actual, gba::core::ExecuteStatus status,
                 std::uint32_t elapsed_cycles, std::uint64_t total_elapsed_cycles,
                 bool data_dependent, bool memory_timing_applied,
                 std::string_view message) {
  expect(actual.status == status, message);
  expect(actual.elapsed_cycles == elapsed_cycles, message);
  expect(actual.total_elapsed_cycles == total_elapsed_cycles, message);
  expect(actual.data_dependent == data_dependent, message);
  expect(actual.memory_timing_applied == memory_timing_applied, message);
}

}  // namespace

int main() {
  using gba::core::Arm7tdmi;
  using gba::core::ArmOpcode;
  using gba::core::ArmShiftType;
  using gba::core::CpuMode;
  using gba::core::ExceptionKind;
  using gba::core::ExecuteStatus;
  using gba::core::MemoryBus;
  using gba::core::ThumbAluOpcode;
  using gba::core::ThumbHighRegisterOpcode;
  using gba::core::ThumbMemoryTransferKind;
  using gba::core::ThumbOpcode;
  using gba::core::WaitStateControl;

  constexpr std::uint32_t mov_r1_7 =
      kCondAl | kDataProcessingImmediate | opcode(0xD) | rd(1) | imm(7);
  constexpr std::uint32_t add_r2_r1_5 =
      kCondAl | kDataProcessingImmediate | opcode(0x4) | rn(1) | rd(2) | imm(5);
  constexpr std::uint32_t cmp_r2_12 =
      kCondAl | kDataProcessingImmediate | opcode(0xA) | kSetFlags | rn(2) | imm(12);
  constexpr std::uint32_t subeq_r3_r2_2 =
      kCondEq | kDataProcessingImmediate | opcode(0x2) | rn(2) | rd(3) | imm(2);
  constexpr std::uint32_t addne_r4_r2_1 =
      kCondNe | kDataProcessingImmediate | opcode(0x4) | rn(2) | rd(4) | imm(1);

  expect(Arm7tdmi::can_decode_data_processing_immediate(mov_r1_7), "MOV immediate decodes");
  const auto decoded = Arm7tdmi::decode_data_processing_immediate(add_r2_r1_5);
  expect(decoded.opcode == ArmOpcode::add, "ADD opcode decodes");
  expect(decoded.rn == 1, "ADD Rn decodes");
  expect(decoded.rd == 2, "ADD Rd decodes");
  expect(decoded.operand2 == 5, "ADD immediate decodes");

  Arm7tdmi cpu;
  expect(cpu.current_mode() == CpuMode::supervisor, "reset starts in supervisor mode");
  expect(!cpu.thumb_state(), "reset starts in ARM state");
  expect(!cpu.irq_disabled(), "reset CPSR IRQ mask starts clear in this scaffold");
  expect(!cpu.fiq_disabled(), "reset CPSR FIQ mask starts clear in this scaffold");
  expect(cpu.cpsr() == static_cast<std::uint32_t>(CpuMode::supervisor),
         "reset CPSR encodes supervisor mode");
  expect(cpu.has_spsr(), "supervisor mode exposes SPSR slot");
  expect(cpu.spsr().has_value(), "supervisor mode reads SPSR slot");
  expect(cpu.spsr().value() == 0, "supervisor SPSR resets to zero");
  expect(cpu.set_spsr(0x60000010), "supervisor SPSR writes");
  expect(cpu.spsr().value() == 0x60000010, "supervisor SPSR retains value");
  expect(cpu.set_cpsr(0xF0000032), "CPSR accepts IRQ Thumb state with flags");
  expect(cpu.current_mode() == CpuMode::irq, "CPSR mode bits switch to IRQ");
  expect(cpu.thumb_state(), "CPSR T bit sets Thumb state");
  expect(cpu.negative(), "CPSR N flag imports");
  expect(cpu.zero(), "CPSR Z flag imports");
  expect(cpu.carry(), "CPSR C flag imports");
  expect(cpu.overflow(), "CPSR V flag imports");
  expect(cpu.cpsr() == 0xF0000032, "CPSR exports flags, Thumb bit, and mode");
  expect(cpu.has_spsr(), "IRQ mode exposes SPSR slot");
  expect(cpu.spsr().value() == 0, "IRQ SPSR is distinct from supervisor SPSR");
  expect(cpu.set_spsr(0xA0000013), "IRQ SPSR writes");
  expect(cpu.spsr().value() == 0xA0000013, "IRQ SPSR retains value");
  expect(cpu.set_cpsr(0x000000D2), "CPSR accepts IRQ and FIQ disable bits");
  expect(cpu.irq_disabled(), "CPSR I bit imports");
  expect(cpu.fiq_disabled(), "CPSR F bit imports");
  expect(cpu.cpsr() == 0x000000D2, "CPSR exports I and F bits");
  expect(cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::supervisor)),
         "CPSR switches back to supervisor");
  expect(!cpu.thumb_state(), "cleared CPSR T bit returns to ARM state");
  expect(!cpu.irq_disabled(), "cleared CPSR I bit imports");
  expect(!cpu.fiq_disabled(), "cleared CPSR F bit imports");
  expect(!cpu.negative(), "cleared CPSR N flag imports");
  expect(cpu.spsr().value() == 0x60000010, "supervisor SPSR value is preserved");
  expect(cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::fiq)), "CPSR switches to FIQ");
  expect(cpu.has_spsr(), "FIQ mode exposes SPSR slot");
  expect(cpu.set_spsr(0x10000013), "FIQ SPSR writes");
  expect(cpu.spsr().value() == 0x10000013, "FIQ SPSR retains value");
  expect(cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::abort)), "CPSR switches to abort");
  expect(cpu.has_spsr(), "abort mode exposes SPSR slot");
  expect(cpu.set_spsr(0x20000010), "abort SPSR writes");
  expect(cpu.spsr().value() == 0x20000010, "abort SPSR retains value");
  expect(cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::undefined)),
         "CPSR switches to undefined mode");
  expect(cpu.has_spsr(), "undefined mode exposes SPSR slot");
  expect(cpu.set_spsr(0x30000010), "undefined SPSR writes");
  expect(cpu.spsr().value() == 0x30000010, "undefined SPSR retains value");
  expect(cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::system)),
         "CPSR switches to system mode");
  expect(!cpu.has_spsr(), "system mode has no SPSR");
  expect(!cpu.spsr().has_value(), "system mode SPSR read is unavailable");
  expect(!cpu.set_spsr(0xFFFFFFFF), "system mode SPSR write is rejected");
  expect(!cpu.set_cpsr(0x00000000), "CPSR rejects invalid mode bits");
  expect(cpu.current_mode() == CpuMode::system, "invalid CPSR mode does not change mode");

  Arm7tdmi bank_cpu;
  expect(bank_cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::user)),
         "bank seed switches to user mode");
  bank_cpu.set_register(8, 0x00000088);
  bank_cpu.set_register(12, 0x000000CC);
  bank_cpu.set_register(13, 0x03007F00);
  bank_cpu.set_register(14, 0x08001000);
  expect(bank_cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::system)),
         "bank seed switches to system mode");
  expect(bank_cpu.register_value(8) == 0x00000088, "user/system share R8");
  expect(bank_cpu.register_value(13) == 0x03007F00, "user/system share SP");
  expect(bank_cpu.register_value(14) == 0x08001000, "user/system share LR");
  bank_cpu.set_register(13, 0x03007E00);
  bank_cpu.set_register(14, 0x08002000);
  expect(bank_cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::supervisor)),
         "bank seed switches to supervisor mode");
  bank_cpu.set_register(13, 0x03007D00);
  bank_cpu.set_register(14, 0x08003000);
  expect(bank_cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::system)),
         "bank seed returns to system mode");
  expect(bank_cpu.register_value(13) == 0x03007E00, "system SP survives SVC bank");
  expect(bank_cpu.register_value(14) == 0x08002000, "system LR survives SVC bank");
  expect(bank_cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::supervisor)),
         "bank seed returns to supervisor mode");
  expect(bank_cpu.register_value(13) == 0x03007D00, "SVC SP is banked");
  expect(bank_cpu.register_value(14) == 0x08003000, "SVC LR is banked");
  expect(bank_cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::fiq)),
         "bank seed switches to FIQ mode");
  bank_cpu.set_register(8, 0x00000108);
  bank_cpu.set_register(12, 0x0000010C);
  bank_cpu.set_register(13, 0x03007C00);
  bank_cpu.set_register(14, 0x08004000);
  expect(bank_cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::system)),
         "bank seed returns from FIQ to system mode");
  expect(bank_cpu.register_value(8) == 0x00000088, "system R8 survives FIQ bank");
  expect(bank_cpu.register_value(12) == 0x000000CC, "system R12 survives FIQ bank");
  expect(bank_cpu.register_value(13) == 0x03007E00, "system SP survives FIQ bank");
  expect(bank_cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::fiq)),
         "bank seed re-enters FIQ mode");
  expect(bank_cpu.register_value(8) == 0x00000108, "FIQ R8 is banked");
  expect(bank_cpu.register_value(12) == 0x0000010C, "FIQ R12 is banked");
  expect(bank_cpu.register_value(13) == 0x03007C00, "FIQ SP is banked");
  expect(bank_cpu.register_value(14) == 0x08004000, "FIQ LR is banked");

  expect(cpu.set_cpsr(0xA0000013), "PSR transfer seed starts in supervisor mode");
  expect(cpu.execute_arm(arm_mrs(false, 4)) == ExecuteStatus::executed,
         "MRS CPSR executes");
  expect(cpu.register_value(4) == 0xA0000013, "MRS CPSR exports modeled CPSR bits");
  expect(cpu.set_spsr(0x60000010), "prepare supervisor SPSR for MRS");
  expect(cpu.execute_arm(arm_mrs(true, 5)) == ExecuteStatus::executed,
         "MRS SPSR executes in exception mode");
  expect(cpu.register_value(5) == 0x60000010, "MRS SPSR exports current mode SPSR");
  cpu.set_register(6, 0xF0000000);
  expect(cpu.execute_arm(arm_msr_register(false, 0x8, 6)) == ExecuteStatus::executed,
         "MSR CPSR_f register form executes");
  expect(cpu.current_mode() == CpuMode::supervisor, "MSR CPSR_f preserves mode bits");
  expect(cpu.negative() && cpu.zero() && cpu.carry() && cpu.overflow(),
         "MSR CPSR_f imports NZCV flags");
  expect(cpu.execute_arm(arm_msr_immediate(false, 0x1, 0xD2)) == ExecuteStatus::executed,
         "MSR CPSR_c immediate form executes");
  expect(cpu.current_mode() == CpuMode::irq, "MSR CPSR_c imports mode bits");
  expect(cpu.irq_disabled() && cpu.fiq_disabled(), "MSR CPSR_c imports interrupt masks");
  expect(cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::system)),
         "switch to system mode for SPSR negative tests");
  expect(cpu.execute_arm(arm_mrs(true, 7)) == ExecuteStatus::unsupported,
         "MRS SPSR rejects modes without SPSR");
  cpu.set_register(8, 0xF0000013);
  expect(cpu.execute_arm(arm_msr_register(true, 0xF, 8)) == ExecuteStatus::unsupported,
         "MSR SPSR rejects modes without SPSR");
  expect(cpu.return_from_exception(4) == ExecuteStatus::unsupported,
         "exception return rejects modes without SPSR");
  expect(cpu.execute_arm(arm_msr_immediate(false, 0x1, 0x00)) == ExecuteStatus::unsupported,
         "MSR CPSR_c rejects invalid mode bits without mutating");
  expect(cpu.current_mode() == CpuMode::system, "failed MSR CPSR_c preserves prior mode");
  expect(cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::supervisor)),
         "return to supervisor after PSR transfer tests");

  const auto irq_vector = Arm7tdmi::exception_vector(ExceptionKind::irq);
  expect(irq_vector.vector_address == 0x00000018, "IRQ vector address is fixed");
  expect(irq_vector.mode == CpuMode::irq, "IRQ vector enters IRQ mode");
  expect(irq_vector.link_offset == 4, "IRQ vector link offset is PC plus four");
  expect(irq_vector.save_cpsr, "IRQ vector saves CPSR to SPSR");
  expect(irq_vector.disable_irq, "IRQ vector disables further IRQs");
  expect(!irq_vector.disable_fiq, "IRQ vector leaves FIQ mask unchanged");
  const auto data_abort_vector = Arm7tdmi::exception_vector(ExceptionKind::data_abort);
  expect(data_abort_vector.vector_address == 0x00000010, "data abort vector address is fixed");
  expect(data_abort_vector.mode == CpuMode::abort, "data abort vector enters abort mode");
  expect(data_abort_vector.link_offset == 8, "data abort vector link offset is PC plus eight");
  expect(data_abort_vector.save_cpsr, "data abort vector saves CPSR to SPSR");

  Arm7tdmi exception_cpu;
  exception_cpu.set_register(Arm7tdmi::kPc, 0x08000100);
  expect(exception_cpu.set_cpsr(0xA0000030), "exception seed starts from user Thumb state");
  expect(exception_cpu.enter_exception(ExceptionKind::irq) == ExecuteStatus::executed,
         "IRQ exception entry executes");
  expect(exception_cpu.current_mode() == CpuMode::irq, "IRQ entry switches mode");
  expect(exception_cpu.register_value(Arm7tdmi::kPc) == 0x00000018,
         "IRQ entry vectors PC");
  expect(exception_cpu.register_value(Arm7tdmi::kLinkRegister) == 0x08000104,
         "IRQ entry writes LR");
  expect(!exception_cpu.thumb_state(), "IRQ entry clears Thumb state");
  expect(exception_cpu.irq_disabled(), "IRQ entry disables IRQ");
  expect(!exception_cpu.fiq_disabled(), "IRQ entry preserves clear FIQ mask");
  expect(exception_cpu.spsr().value() == 0xA0000030, "IRQ entry saves prior CPSR");
  exception_cpu.set_register(13, 0x03007000);
  expect(exception_cpu.return_from_exception(4) == ExecuteStatus::executed,
         "IRQ return restores CPSR and PC through modeled path");
  expect(exception_cpu.current_mode() == CpuMode::user, "IRQ return restores user mode");
  expect(exception_cpu.thumb_state(), "IRQ return restores Thumb state");
  expect(exception_cpu.register_value(Arm7tdmi::kPc) == 0x08000100,
         "IRQ return subtracts LR adjustment");
  expect(exception_cpu.register_value(Arm7tdmi::kLinkRegister) == 0,
         "IRQ return restores user LR bank");
  expect(exception_cpu.enter_exception(ExceptionKind::irq) == ExecuteStatus::executed,
         "IRQ exception can re-enter after modeled return");
  expect(exception_cpu.register_value(13) == 0x03007000,
         "IRQ SP bank survives exception return and re-entry");

  exception_cpu.set_register(Arm7tdmi::kPc, 0x08000200);
  expect(exception_cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::user)),
         "exception seed resets to user ARM state");
  expect(exception_cpu.enter_exception(ExceptionKind::fiq) == ExecuteStatus::executed,
         "FIQ exception entry executes");
  expect(exception_cpu.current_mode() == CpuMode::fiq, "FIQ entry switches mode");
  expect(exception_cpu.register_value(Arm7tdmi::kPc) == 0x0000001C,
         "FIQ entry vectors PC");
  expect(exception_cpu.register_value(Arm7tdmi::kLinkRegister) == 0x08000204,
         "FIQ entry writes LR");
  expect(exception_cpu.irq_disabled(), "FIQ entry disables IRQ");
  expect(exception_cpu.fiq_disabled(), "FIQ entry disables FIQ");
  expect(exception_cpu.spsr().value() == static_cast<std::uint32_t>(CpuMode::user),
         "FIQ entry saves prior CPSR");

  exception_cpu.set_register(Arm7tdmi::kPc, 0x08000300);
  expect(exception_cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::user)),
         "exception seed returns to user before data abort");
  expect(exception_cpu.enter_exception(ExceptionKind::data_abort) == ExecuteStatus::executed,
         "data abort exception entry executes");
  expect(exception_cpu.current_mode() == CpuMode::abort, "data abort entry switches mode");
  expect(exception_cpu.register_value(Arm7tdmi::kPc) == 0x00000010,
         "data abort entry vectors PC");
  expect(exception_cpu.register_value(Arm7tdmi::kLinkRegister) == 0x08000308,
         "data abort entry writes LR with abort offset");
  expect(exception_cpu.spsr().value() == static_cast<std::uint32_t>(CpuMode::user),
         "data abort entry saves prior CPSR");

  exception_cpu.set_register(Arm7tdmi::kPc, 0x08000400);
  expect(exception_cpu.set_cpsr(0x40000030), "exception seed prepares SWI from Thumb user");
  expect(exception_cpu.enter_exception(ExceptionKind::software_interrupt) ==
             ExecuteStatus::executed,
         "software interrupt entry executes");
  expect(exception_cpu.current_mode() == CpuMode::supervisor, "SWI entry switches mode");
  expect(exception_cpu.register_value(Arm7tdmi::kPc) == 0x00000008,
         "SWI entry vectors PC");
  expect(exception_cpu.register_value(Arm7tdmi::kLinkRegister) == 0x08000404,
         "SWI entry writes LR");
  expect(!exception_cpu.thumb_state(), "SWI entry clears Thumb state");
  expect(exception_cpu.spsr().value() == 0x40000030, "SWI entry saves prior CPSR");

  Arm7tdmi arm_swi_cpu;
  arm_swi_cpu.set_register(Arm7tdmi::kPc, 0x08000500);
  expect(arm_swi_cpu.set_cpsr(0x10000010), "prepare ARM SWI from user ARM state");
  expect(arm_swi_cpu.execute_arm(kCondEq | kSoftwareInterrupt | 0x12U) ==
             ExecuteStatus::skipped_condition,
         "conditional ARM SWI skips when condition fails");
  expect(arm_swi_cpu.register_value(Arm7tdmi::kPc) == 0x08000500,
         "skipped ARM SWI preserves PC");
  expect(arm_swi_cpu.execute_arm(arm_swi(0x123456)) == ExecuteStatus::executed,
         "ARM SWI executes through exception path");
  expect(arm_swi_cpu.current_mode() == CpuMode::supervisor,
         "ARM SWI enters supervisor mode");
  expect(arm_swi_cpu.register_value(Arm7tdmi::kPc) == 0x00000008,
         "ARM SWI vectors PC");
  expect(arm_swi_cpu.register_value(Arm7tdmi::kLinkRegister) == 0x08000504,
         "ARM SWI writes LR");
  expect(arm_swi_cpu.spsr().value() == 0x10000010, "ARM SWI saves prior CPSR");

  exception_cpu.set_register(Arm7tdmi::kLinkRegister, 0xDEADBEEF);
  exception_cpu.set_register(Arm7tdmi::kPc, 0x08000500);
  expect(exception_cpu.set_cpsr(0xF0000032), "exception seed prepares reset entry state");
  expect(exception_cpu.enter_exception(ExceptionKind::reset) == ExecuteStatus::executed,
         "reset exception entry executes");
  expect(exception_cpu.current_mode() == CpuMode::supervisor, "reset entry switches mode");
  expect(exception_cpu.register_value(Arm7tdmi::kPc) == 0x00000000,
         "reset entry vectors PC");
  expect(exception_cpu.register_value(Arm7tdmi::kLinkRegister) == 0xDEADBEEF,
         "reset entry does not write LR");
  expect(!exception_cpu.thumb_state(), "reset entry clears Thumb state");
  expect(exception_cpu.irq_disabled(), "reset entry disables IRQ");
  expect(exception_cpu.fiq_disabled(), "reset entry disables FIQ");
  cpu.reset();
  expect(cpu.execute_arm(mov_r1_7) == ExecuteStatus::executed, "execute MOV immediate");
  expect(Arm7tdmi::estimate_arm_cycles(mov_r1_7).has_value(),
         "MOV immediate cycle estimate exists");
  expect_cycles(Arm7tdmi::estimate_arm_cycles(mov_r1_7).value(), 1, 0, 0, false,
                "MOV immediate cycle estimate matches data-processing base");
  expect_elapsed(Arm7tdmi::estimate_arm_elapsed_cycles(mov_r1_7, 0x02000000).value(), 1,
                 false, false, "MOV elapsed cycle estimate uses unit timing");
  expect(cpu.register_value(1) == 7, "MOV writes Rd");
  expect(cpu.execute_arm(add_r2_r1_5) == ExecuteStatus::executed, "execute ADD immediate");
  expect(cpu.register_value(2) == 12, "ADD writes Rd");
  constexpr std::uint32_t mov_r0_pc = kCondAl | opcode(0xDU) | rd(0) | rm(Arm7tdmi::kPc);
  constexpr std::uint32_t add_r1_pc_4 = kCondAl | kDataProcessingImmediate |
                                        opcode(0x4U) | rn(Arm7tdmi::kPc) | rd(1) |
                                        imm(4);
  cpu.set_register(Arm7tdmi::kPc, 0x08000000);
  expect(cpu.execute_arm(mov_r0_pc) == ExecuteStatus::executed,
         "execute ARM MOV from pipeline-visible PC");
  expect(cpu.register_value(0) == 0x08000008,
         "ARM data-processing reads PC as current instruction address plus 8");
  expect(cpu.execute_arm(add_r1_pc_4) == ExecuteStatus::executed,
         "execute ARM ADD from pipeline-visible PC");
  expect(cpu.register_value(1) == 0x0800000C,
         "ARM data-processing Rn reads PC as current instruction address plus 8");
  constexpr std::uint32_t mov_r0_pc_lsl_r1 =
      kCondAl | opcode(0xDU) | rd(0) | rs(1) | shift_type(0) | 0x10 |
      rm(Arm7tdmi::kPc);
  constexpr std::uint32_t add_r2_pc_r3_lsl_r4 =
      kCondAl | opcode(0x4U) | rn(Arm7tdmi::kPc) | rd(2) | rs(4) | shift_type(0) |
      0x10 | rm(3);
  cpu.set_register(Arm7tdmi::kPc, 0x08000000);
  cpu.set_register(1, 0);
  expect(cpu.execute_arm(mov_r0_pc_lsl_r1) == ExecuteStatus::executed,
         "execute ARM MOV from register-shift pipeline-visible PC");
  expect(cpu.register_value(0) == 0x0800000C,
         "ARM register-shift operand reads PC as current instruction address plus 12");
  cpu.set_register(Arm7tdmi::kPc, 0x08000000);
  cpu.set_register(3, 1);
  cpu.set_register(4, 0);
  expect(cpu.execute_arm(add_r2_pc_r3_lsl_r4) == ExecuteStatus::executed,
         "execute ARM ADD Rn PC with register-controlled shift operand");
  expect(cpu.register_value(2) == 0x0800000D,
         "ARM register-shift Rn PC reads as current instruction address plus 12");
  cpu.set_register(2, 12);
  expect(cpu.execute_arm(cmp_r2_12) == ExecuteStatus::executed, "execute CMP immediate");
  expect(cpu.zero(), "CMP updates Z flag");
  expect(cpu.carry(), "CMP sets carry for no borrow");

  expect(cpu.execute_arm(subeq_r3_r2_2) == ExecuteStatus::executed, "EQ condition executes when Z set");
  expect(cpu.register_value(3) == 10, "SUB writes Rd");
  expect(cpu.execute_arm(addne_r4_r2_1) == ExecuteStatus::skipped_condition,
         "NE condition skips when Z set");
  expect(cpu.register_value(4) == 0, "skipped instruction preserves Rd");

  constexpr std::uint16_t thumb_mov_r0_0x80 = thumb_immediate(0x0, 0, 0x80);
  constexpr std::uint16_t thumb_add_r0_0x7f = thumb_immediate(0x2, 0, 0x7F);
  constexpr std::uint16_t thumb_cmp_r0_0xff = thumb_immediate(0x1, 0, 0xFF);
  constexpr std::uint16_t thumb_sub_r1_1 = thumb_immediate(0x3, 1, 1);
  constexpr std::uint16_t thumb_lsl_r0_r0_5 = thumb_shift_immediate(0x0, 5, 0, 0);
  constexpr std::uint16_t thumb_lsr_r2_r1_1 = thumb_shift_immediate(0x1, 1, 1, 2);
  constexpr std::uint16_t thumb_asr_r4_r3_0 = thumb_shift_immediate(0x2, 0, 3, 4);
  constexpr std::uint16_t thumb_bic_r1_r2 = thumb_alu(0xE, 2, 1);
  constexpr std::uint16_t thumb_tst_r1_r2 = thumb_alu(0x8, 2, 1);
  constexpr std::uint16_t thumb_neg_r5_r2 = thumb_alu(0x9, 2, 5);
  constexpr std::uint16_t thumb_mul_r1_r2 = thumb_alu(0xD, 2, 1);
  constexpr std::uint16_t thumb_add_r2_r0_r1 = thumb_add_sub(false, false, 1, 0, 2);
  constexpr std::uint16_t thumb_sub_r3_r2_7 = thumb_add_sub(true, true, 7, 2, 3);
  constexpr std::uint16_t thumb_b_forward = thumb_unconditional_branch(2);
  constexpr std::uint16_t thumb_beq_backward = thumb_conditional_branch(0, -2);
  constexpr std::uint16_t thumb_bne_forward = thumb_conditional_branch(1, 3);
  constexpr std::uint16_t thumb_bcs_forward = thumb_conditional_branch(2, 2);
  constexpr std::uint16_t thumb_bmi_forward = thumb_conditional_branch(4, 2);
  constexpr std::uint16_t thumb_bl_prefix_0 = thumb_bl_prefix(0);
  constexpr std::uint16_t thumb_bl_suffix_0x2c = thumb_bl_suffix(0x2C);
  constexpr std::uint16_t thumb_mov_r8_r1 = thumb_high_register(0x2, 8, 1);
  constexpr std::uint16_t thumb_add_r8_r2 = thumb_high_register(0x0, 8, 2);
  constexpr std::uint16_t thumb_cmp_r8_r3 = thumb_high_register(0x1, 8, 3);
  constexpr std::uint16_t thumb_mov_lr_sp = thumb_high_register(0x2, 14, 13);
  constexpr std::uint16_t thumb_mov_r0_pc = thumb_high_register(0x2, 0, 15);
  constexpr std::uint16_t thumb_add_r1_pc = thumb_high_register(0x0, 1, 15);
  constexpr std::uint16_t thumb_add_sp_r8 = thumb_high_register(0x0, 13, 8);
  constexpr std::uint16_t thumb_mov_pc_lr = thumb_high_register(0x2, 15, 14);
  constexpr std::uint16_t thumb_bx_r4 = thumb_high_register(0x3, 0, 4);
  constexpr std::uint16_t thumb_ldr_literal_r0_1 = thumb_ldr_literal(0, 1);
  constexpr std::uint16_t thumb_str_r1_r2_imm4_words =
      thumb_memory_immediate(false, false, 4, 2, 1);
  constexpr std::uint16_t thumb_ldr_r3_r2_imm4_words =
      thumb_memory_immediate(false, true, 4, 2, 3);
  constexpr std::uint16_t thumb_strb_r4_r2_imm1 =
      thumb_memory_immediate(true, false, 1, 2, 4);
  constexpr std::uint16_t thumb_ldrb_r5_r2_imm1 =
      thumb_memory_immediate(true, true, 1, 2, 5);
  constexpr std::uint16_t thumb_strh_r6_r2_imm2_halfwords =
      thumb_memory_halfword_immediate(false, 2, 2, 6);
  constexpr std::uint16_t thumb_ldrh_r7_r2_imm2_halfwords =
      thumb_memory_halfword_immediate(true, 2, 2, 7);
  constexpr std::uint16_t thumb_str_r0_r2_r1 = thumb_memory_register(0x0, 1, 2, 0);
  constexpr std::uint16_t thumb_ldr_r3_r2_r1 = thumb_memory_register(0x4, 1, 2, 3);
  constexpr std::uint16_t thumb_ldrsb_r4_r2_r1 = thumb_memory_register(0x3, 1, 2, 4);
  constexpr std::uint16_t thumb_ldrsh_r5_r2_r1 = thumb_memory_register(0x7, 1, 2, 5);
  constexpr std::uint16_t thumb_str_sp_r0_imm2_words = thumb_sp_relative(false, 0, 2);
  constexpr std::uint16_t thumb_ldr_sp_r6_imm2_words = thumb_sp_relative(true, 6, 2);
  constexpr std::uint16_t thumb_stmia_r0_r2 = thumb_block_transfer(false, 0, 0x04);
  constexpr std::uint16_t thumb_ldmia_r0_r2_r3 = thumb_block_transfer(true, 0, 0x0C);
  constexpr std::uint16_t thumb_push_r0_r1_lr = thumb_stack(false, true, 0x03);
  constexpr std::uint16_t thumb_pop_r2_r3_pc = thumb_stack(true, true, 0x0C);
  constexpr std::uint16_t thumb_empty_push = thumb_stack(false, false, 0x00);
  constexpr std::uint16_t thumb_sub_sp_0x34 = thumb_sp_adjust(true, 0x0D);
  constexpr std::uint16_t thumb_add_sp_0x14 = thumb_sp_adjust(false, 0x05);
  constexpr std::uint16_t thumb_add_r3_sp_0xa8 = thumb_load_address(true, 3, 0x2A);
  constexpr std::uint16_t thumb_add_r4_pc_0x10 = thumb_load_address(false, 4, 0x04);
  constexpr std::uint16_t thumb_swi_7 = thumb_swi(7);
  constexpr std::uint16_t unsupported_thumb_condition = thumb_conditional_branch(14, 1);
  constexpr std::uint16_t unsupported_low_only_high_add = thumb_high_register(0x0, 0, 1);
  constexpr std::uint16_t unsupported_thumb = 0xB100;
  expect(Arm7tdmi::can_decode_thumb_immediate(thumb_mov_r0_0x80),
         "Thumb MOV immediate decodes");
  const auto decoded_thumb_mov = Arm7tdmi::decode_thumb_immediate(thumb_mov_r0_0x80);
  expect(decoded_thumb_mov.opcode == ThumbOpcode::mov, "Thumb MOV opcode decodes");
  expect(decoded_thumb_mov.rd == 0, "Thumb MOV Rd decodes");
  expect(decoded_thumb_mov.operand == 0x80, "Thumb MOV immediate decodes");
  expect(!decoded_thumb_mov.operand_is_register, "Thumb MOV immediate operand is immediate");
  expect(Arm7tdmi::can_decode_thumb_add_subtract(thumb_add_r2_r0_r1),
         "Thumb ADD register decodes");
  const auto decoded_thumb_add = Arm7tdmi::decode_thumb_add_subtract(thumb_add_r2_r0_r1);
  expect(decoded_thumb_add.opcode == ThumbOpcode::add, "Thumb ADD opcode decodes");
  expect(decoded_thumb_add.rd == 2, "Thumb ADD Rd decodes");
  expect(decoded_thumb_add.rs == 0, "Thumb ADD source decodes");
  expect(decoded_thumb_add.operand == 1, "Thumb ADD operand register decodes");
  expect(decoded_thumb_add.operand_is_register, "Thumb ADD register operand is marked");
  expect(Arm7tdmi::can_decode_thumb_shift_immediate(thumb_lsl_r0_r0_5),
         "Thumb LSL immediate decodes");
  const auto decoded_thumb_lsl =
      Arm7tdmi::decode_thumb_shift_immediate(thumb_lsl_r0_r0_5);
  expect(decoded_thumb_lsl.type == ArmShiftType::lsl, "Thumb LSL type decodes");
  expect(decoded_thumb_lsl.rd == 0, "Thumb LSL Rd decodes");
  expect(decoded_thumb_lsl.rs == 0, "Thumb LSL Rs decodes");
  expect(decoded_thumb_lsl.amount == 5, "Thumb LSL amount decodes");
  expect(Arm7tdmi::can_decode_thumb_alu(thumb_bic_r1_r2), "Thumb BIC decodes");
  const auto decoded_thumb_bic = Arm7tdmi::decode_thumb_alu(thumb_bic_r1_r2);
  expect(decoded_thumb_bic.opcode == ThumbAluOpcode::bic, "Thumb BIC opcode decodes");
  expect(decoded_thumb_bic.rd == 1, "Thumb BIC Rd decodes");
  expect(decoded_thumb_bic.rs == 2, "Thumb BIC Rs decodes");
  expect(Arm7tdmi::can_decode_thumb_unconditional_branch(thumb_b_forward),
         "Thumb B decodes");
  expect(Arm7tdmi::decode_thumb_unconditional_branch(thumb_b_forward).offset == 4,
         "Thumb B offset decodes");
  expect(Arm7tdmi::can_decode_thumb_conditional_branch(thumb_bne_forward),
         "Thumb BNE decodes");
  expect(Arm7tdmi::can_decode_thumb_conditional_branch(thumb_bcs_forward),
         "Thumb BCS decodes");
  expect(Arm7tdmi::decode_thumb_conditional_branch(thumb_beq_backward).offset == -4,
         "Thumb conditional branch sign-extends offset");
  expect(Arm7tdmi::can_decode_thumb_long_branch_link(thumb_bl_prefix_0),
         "Thumb BL prefix decodes");
  expect(!Arm7tdmi::decode_thumb_long_branch_link(thumb_bl_prefix_0).second_half,
         "Thumb BL prefix marks first half");
  expect(Arm7tdmi::decode_thumb_long_branch_link(thumb_bl_prefix_0).offset == 0,
         "Thumb BL prefix offset decodes");
  expect(Arm7tdmi::can_decode_thumb_long_branch_link(thumb_bl_suffix_0x2c),
         "Thumb BL suffix decodes");
  expect(Arm7tdmi::decode_thumb_long_branch_link(thumb_bl_suffix_0x2c).second_half,
         "Thumb BL suffix marks second half");
  expect(Arm7tdmi::decode_thumb_long_branch_link(thumb_bl_suffix_0x2c).offset == 0x58,
         "Thumb BL suffix offset decodes");
  expect(Arm7tdmi::can_decode_thumb_high_register(thumb_mov_r8_r1),
         "Thumb high-register MOV decodes");
  const auto decoded_thumb_high_mov =
      Arm7tdmi::decode_thumb_high_register(thumb_mov_r8_r1);
  expect(decoded_thumb_high_mov.opcode == ThumbHighRegisterOpcode::mov,
         "Thumb high-register MOV opcode decodes");
  expect(decoded_thumb_high_mov.rd == 8, "Thumb high-register Rd decodes");
  expect(decoded_thumb_high_mov.rs == 1, "Thumb high-register Rs decodes");
  expect(!Arm7tdmi::can_decode_thumb_conditional_branch(unsupported_thumb_condition),
         "unsupported Thumb branch condition does not decode");
  expect(!Arm7tdmi::can_decode_thumb_high_register(unsupported_low_only_high_add),
         "Thumb high-register ALU rejects low-register-only alias");
  expect(Arm7tdmi::can_decode_thumb_memory_transfer(thumb_str_r1_r2_imm4_words),
         "Thumb STR immediate decodes");
  const auto decoded_thumb_str =
      Arm7tdmi::decode_thumb_memory_transfer(thumb_str_r1_r2_imm4_words);
  expect(!decoded_thumb_str.load, "Thumb STR immediate decodes store");
  expect(decoded_thumb_str.kind == ThumbMemoryTransferKind::word,
         "Thumb STR immediate decodes word kind");
  expect(decoded_thumb_str.rd == 1, "Thumb STR immediate decodes Rd");
  expect(decoded_thumb_str.rb == 2, "Thumb STR immediate decodes base register");
  expect(decoded_thumb_str.offset == 16, "Thumb STR immediate scales word offset");
  expect(!decoded_thumb_str.offset_is_register, "Thumb STR immediate marks immediate offset");
  expect(Arm7tdmi::can_decode_thumb_memory_transfer(thumb_ldr_literal_r0_1),
         "Thumb LDR literal decodes");
  const auto decoded_thumb_ldr_literal =
      Arm7tdmi::decode_thumb_memory_transfer(thumb_ldr_literal_r0_1);
  expect(decoded_thumb_ldr_literal.load, "Thumb LDR literal decodes load");
  expect(decoded_thumb_ldr_literal.kind == ThumbMemoryTransferKind::word,
         "Thumb LDR literal decodes word kind");
  expect(decoded_thumb_ldr_literal.rd == 0, "Thumb LDR literal decodes Rd");
  expect(decoded_thumb_ldr_literal.rb == Arm7tdmi::kPc, "Thumb LDR literal uses PC base");
  expect(decoded_thumb_ldr_literal.offset == 4, "Thumb LDR literal scales word offset");
  expect(!decoded_thumb_ldr_literal.offset_is_register,
         "Thumb LDR literal marks immediate offset");
  expect(Arm7tdmi::can_decode_thumb_memory_transfer(thumb_ldrsb_r4_r2_r1),
         "Thumb LDRSB register decodes");
  const auto decoded_thumb_ldrsb =
      Arm7tdmi::decode_thumb_memory_transfer(thumb_ldrsb_r4_r2_r1);
  expect(decoded_thumb_ldrsb.load, "Thumb LDRSB decodes load");
  expect(decoded_thumb_ldrsb.kind == ThumbMemoryTransferKind::signed_byte,
         "Thumb LDRSB decodes signed-byte kind");
  expect(decoded_thumb_ldrsb.offset == 1, "Thumb LDRSB decodes offset register");
  expect(decoded_thumb_ldrsb.offset_is_register, "Thumb LDRSB marks register offset");
  expect(Arm7tdmi::can_decode_thumb_block_transfer(thumb_stmia_r0_r2),
         "Thumb STMIA decodes");
  const auto decoded_thumb_stmia =
      Arm7tdmi::decode_thumb_block_transfer(thumb_stmia_r0_r2);
  expect(!decoded_thumb_stmia.load, "Thumb STMIA decodes store");
  expect(decoded_thumb_stmia.rb == 0, "Thumb STMIA decodes base register");
  expect(decoded_thumb_stmia.register_list == 0x04, "Thumb STMIA decodes register list");
  expect(Arm7tdmi::can_decode_thumb_stack_transfer(thumb_push_r0_r1_lr),
         "Thumb PUSH decodes");
  const auto decoded_thumb_push =
      Arm7tdmi::decode_thumb_stack_transfer(thumb_push_r0_r1_lr);
  expect(!decoded_thumb_push.load, "Thumb PUSH decodes store");
  expect(decoded_thumb_push.extra_register, "Thumb PUSH decodes LR bit");
  expect(decoded_thumb_push.register_list == 0x03, "Thumb PUSH decodes low register list");
  expect(Arm7tdmi::can_decode_thumb_stack_pointer_adjust(thumb_sub_sp_0x34),
         "Thumb SUB SP immediate decodes");
  const auto decoded_thumb_sub_sp =
      Arm7tdmi::decode_thumb_stack_pointer_adjust(thumb_sub_sp_0x34);
  expect(decoded_thumb_sub_sp.subtract, "Thumb SUB SP immediate decodes subtract bit");
  expect(decoded_thumb_sub_sp.offset == 0x34, "Thumb SUB SP immediate decodes word offset");
  expect(Arm7tdmi::can_decode_thumb_load_address(thumb_add_r3_sp_0xa8),
         "Thumb ADD Rd, SP immediate decodes");
  const auto decoded_thumb_add_sp_address =
      Arm7tdmi::decode_thumb_load_address(thumb_add_r3_sp_0xa8);
  expect(decoded_thumb_add_sp_address.base_is_sp, "Thumb load-address decodes SP base");
  expect(decoded_thumb_add_sp_address.rd == 3, "Thumb load-address decodes Rd");
  expect(decoded_thumb_add_sp_address.offset == 0xA8,
         "Thumb load-address decodes scaled offset");
  expect(Arm7tdmi::can_decode_thumb_software_interrupt(thumb_swi_7),
         "Thumb SWI decodes");

  Arm7tdmi thumb_cpu;
  MemoryBus thumb_memory;
  expect(thumb_cpu.execute_thumb(thumb_mov_r0_0x80) == ExecuteStatus::executed,
         "execute Thumb MOV immediate");
  expect(thumb_cpu.register_value(0) == 0x80, "Thumb MOV writes Rd");
  expect(!thumb_cpu.negative(), "Thumb MOV clears N flag for zero-extended immediate");
  expect(!thumb_cpu.zero(), "Thumb MOV clears Z flag for nonzero");
  expect(thumb_cpu.execute_thumb(thumb_add_r0_0x7f) == ExecuteStatus::executed,
         "execute Thumb ADD immediate");
  expect(thumb_cpu.register_value(0) == 0xFF, "Thumb ADD immediate writes Rd");
  expect(!thumb_cpu.negative(), "Thumb ADD clears N for positive result");
  expect(!thumb_cpu.zero(), "Thumb ADD clears Z for nonzero result");
  expect(!thumb_cpu.carry(), "Thumb ADD clears carry when no carry out");
  expect(thumb_cpu.execute_thumb(thumb_cmp_r0_0xff) == ExecuteStatus::executed,
         "execute Thumb CMP immediate");
  expect(thumb_cpu.zero(), "Thumb CMP sets Z flag");
  expect(thumb_cpu.carry(), "Thumb CMP sets carry for no borrow");
  thumb_cpu.set_register(1, 0);
  expect(thumb_cpu.execute_thumb(thumb_sub_r1_1) == ExecuteStatus::executed,
         "execute Thumb SUB immediate");
  expect(thumb_cpu.register_value(1) == 0xFFFFFFFF, "Thumb SUB immediate wraps");
  expect(thumb_cpu.negative(), "Thumb SUB updates N flag");
  expect(!thumb_cpu.carry(), "Thumb SUB clears carry for borrow");
  thumb_cpu.set_register(0, 0x08000000);
  expect(thumb_cpu.execute_thumb(thumb_lsl_r0_r0_5) == ExecuteStatus::executed,
         "execute Thumb LSL immediate");
  expect(thumb_cpu.register_value(0) == 0, "Thumb LSL immediate shifts result");
  expect(thumb_cpu.zero(), "Thumb LSL immediate updates Z flag");
  expect(thumb_cpu.carry(), "Thumb LSL immediate updates carry");
  thumb_cpu.set_register(1, 0x3);
  expect(thumb_cpu.execute_thumb(thumb_lsr_r2_r1_1) == ExecuteStatus::executed,
         "execute Thumb LSR immediate");
  expect(thumb_cpu.register_value(2) == 0x1, "Thumb LSR immediate shifts result");
  expect(thumb_cpu.carry(), "Thumb LSR immediate captures shifted-out bit");
  thumb_cpu.set_register(3, 0x80000000);
  expect(thumb_cpu.execute_thumb(thumb_asr_r4_r3_0) == ExecuteStatus::executed,
         "execute Thumb ASR immediate zero-as-32");
  expect(thumb_cpu.register_value(4) == 0xFFFFFFFF,
         "Thumb ASR immediate zero amount sign-fills");
  expect(thumb_cpu.carry(), "Thumb ASR immediate zero amount carries sign bit");
  thumb_cpu.set_register(1, 0xFFFF00FF);
  thumb_cpu.set_register(2, 0x000000F0);
  expect(thumb_cpu.execute_thumb(thumb_bic_r1_r2) == ExecuteStatus::executed,
         "execute Thumb BIC");
  expect(thumb_cpu.register_value(1) == 0xFFFF000F, "Thumb BIC clears selected bits");
  expect(thumb_cpu.execute_thumb(thumb_tst_r1_r2) == ExecuteStatus::executed,
         "execute Thumb TST");
  expect(thumb_cpu.register_value(1) == 0xFFFF000F, "Thumb TST does not write Rd");
  expect(thumb_cpu.zero(), "Thumb TST updates flags");
  expect(thumb_cpu.execute_thumb(thumb_neg_r5_r2) == ExecuteStatus::executed,
         "execute Thumb NEG");
  expect(thumb_cpu.register_value(5) == 0xFFFFFF10, "Thumb NEG subtracts from zero");
  thumb_cpu.set_register(1, 7);
  thumb_cpu.set_register(2, 6);
  expect(thumb_cpu.execute_thumb(thumb_mul_r1_r2) == ExecuteStatus::executed,
         "execute Thumb MUL");
  expect(thumb_cpu.register_value(1) == 42, "Thumb MUL writes Rd");
  thumb_cpu.reset_elapsed_cycles();
  thumb_cpu.set_register(1, 0x12345678);
  thumb_cpu.set_register(2, 0xFF);
  expect(thumb_cpu.step_thumb(thumb_mul_r1_r2).elapsed_cycles == 5,
         "Thumb MUL step uses signed early-out destination timing");
  thumb_cpu.set_register(0, 5);
  thumb_cpu.set_register(1, 6);
  expect(thumb_cpu.execute_thumb(thumb_add_r2_r0_r1) == ExecuteStatus::executed,
         "execute Thumb ADD register");
  expect(thumb_cpu.register_value(2) == 11, "Thumb ADD register writes Rd");
  expect(thumb_cpu.execute_thumb(thumb_sub_r3_r2_7) == ExecuteStatus::executed,
         "execute Thumb SUB immediate3");
  expect(thumb_cpu.register_value(3) == 4, "Thumb SUB immediate3 writes Rd");
  thumb_cpu.set_register(Arm7tdmi::kPc, 0x08000000);
  expect(thumb_cpu.execute_thumb(thumb_b_forward) == ExecuteStatus::executed,
         "execute Thumb B");
  expect(thumb_cpu.register_value(Arm7tdmi::kPc) == 0x08000008,
         "Thumb B applies signed halfword offset");
  thumb_cpu.set_register(0, 0);
  expect(thumb_cpu.execute_thumb(thumb_immediate(0x1, 0, 0)) == ExecuteStatus::executed,
         "prepare Thumb branch Z flag");
  thumb_cpu.set_register(Arm7tdmi::kPc, 0x08000010);
  expect(thumb_cpu.execute_thumb(thumb_beq_backward) == ExecuteStatus::executed,
         "execute taken Thumb BEQ");
  expect(thumb_cpu.register_value(Arm7tdmi::kPc) == 0x08000010,
         "Thumb BEQ applies negative offset");
  expect(thumb_cpu.execute_thumb(thumb_bne_forward) == ExecuteStatus::skipped_condition,
         "non-taken Thumb BNE reports skipped condition");
  thumb_cpu.set_register(0, 1);
  expect(thumb_cpu.execute_thumb(thumb_immediate(0x1, 0, 0)) == ExecuteStatus::executed,
         "prepare Thumb carry branch flags");
  thumb_cpu.set_register(Arm7tdmi::kPc, 0x08000020);
  expect(thumb_cpu.execute_thumb(thumb_bcs_forward) == ExecuteStatus::executed,
         "execute taken Thumb BCS");
  expect(thumb_cpu.register_value(Arm7tdmi::kPc) == 0x08000028,
         "Thumb BCS applies positive offset");
  expect(thumb_cpu.execute_thumb(thumb_bmi_forward) == ExecuteStatus::skipped_condition,
         "non-taken Thumb BMI reports skipped condition");
  thumb_cpu.set_register(Arm7tdmi::kPc, 0x0800012C);
  expect(thumb_cpu.execute_thumb(thumb_bl_prefix_0) == ExecuteStatus::executed,
         "execute Thumb BL prefix");
  expect(thumb_cpu.register_value(Arm7tdmi::kPc) == 0x0800012C,
         "Thumb BL prefix preserves PC for scheduler advance");
  expect(thumb_cpu.register_value(Arm7tdmi::kLinkRegister) == 0x08000130,
         "Thumb BL prefix seeds LR from visible PC");
  thumb_cpu.set_register(Arm7tdmi::kPc, 0x0800012E);
  expect(thumb_cpu.execute_thumb(thumb_bl_suffix_0x2c) == ExecuteStatus::executed,
         "execute Thumb BL suffix");
  expect(thumb_cpu.register_value(Arm7tdmi::kPc) == 0x08000188,
         "Thumb BL suffix branches to LR plus offset");
  expect(thumb_cpu.register_value(Arm7tdmi::kLinkRegister) == 0x08000131,
         "Thumb BL suffix writes Thumb return address");
  thumb_cpu.set_register(1, 0x12345678);
  expect(thumb_cpu.execute_thumb(thumb_mov_r8_r1) == ExecuteStatus::executed,
         "execute Thumb high-register MOV");
  expect(thumb_cpu.register_value(8) == 0x12345678, "Thumb MOV high writes R8");
  thumb_cpu.set_register(2, 2);
  expect(thumb_cpu.execute_thumb(thumb_add_r8_r2) == ExecuteStatus::executed,
         "execute Thumb high-register ADD");
  expect(thumb_cpu.register_value(8) == 0x1234567A, "Thumb ADD high writes R8");
  thumb_cpu.set_register(3, 0x1234567A);
  expect(thumb_cpu.execute_thumb(thumb_cmp_r8_r3) == ExecuteStatus::executed,
         "execute Thumb high-register CMP");
  expect(thumb_cpu.zero(), "Thumb CMP high updates Z flag");
  thumb_cpu.set_register(13, 0x03007F00);
  expect(thumb_cpu.execute_thumb(thumb_mov_lr_sp) == ExecuteStatus::executed,
         "execute Thumb high-register MOV LR, SP");
  expect(thumb_cpu.register_value(Arm7tdmi::kLinkRegister) == 0x03007F00,
         "Thumb high-register MOV can write LR");
  thumb_cpu.set_register(Arm7tdmi::kPc, 0x08000100);
  expect(thumb_cpu.execute_thumb(thumb_mov_r0_pc) == ExecuteStatus::executed,
         "execute Thumb high-register MOV from pipeline-visible PC");
  expect(thumb_cpu.register_value(0) == 0x08000104,
         "Thumb high-register source reads PC as current instruction address plus 4");
  thumb_cpu.set_register(1, 0);
  expect(thumb_cpu.execute_thumb(thumb_add_r1_pc) == ExecuteStatus::executed,
         "execute Thumb high-register ADD from pipeline-visible PC");
  expect(thumb_cpu.register_value(1) == 0x08000104,
         "Thumb high-register ADD uses pipeline-visible PC source");
  thumb_cpu.set_register(8, 0x20);
  expect(thumb_cpu.execute_thumb(thumb_add_sp_r8) == ExecuteStatus::executed,
         "execute Thumb high-register ADD SP, R8");
  expect(thumb_cpu.register_value(13) == 0x03007F20,
         "Thumb high-register ADD can update SP");
  thumb_cpu.set_register(Arm7tdmi::kLinkRegister, 0x08000303);
  expect(thumb_cpu.execute_thumb(thumb_mov_pc_lr) == ExecuteStatus::executed,
         "execute Thumb high-register MOV PC, LR");
  expect(thumb_cpu.register_value(Arm7tdmi::kPc) == 0x08000302,
         "Thumb high-register MOV PC clears low target bit");
  thumb_cpu.set_register(4, 0x08000101);
  expect(thumb_cpu.execute_thumb(thumb_bx_r4) == ExecuteStatus::executed,
         "execute Thumb BX to Thumb target");
  expect(thumb_cpu.thumb_state(), "BX keeps Thumb state when target bit zero is set");
  expect(thumb_cpu.register_value(Arm7tdmi::kPc) == 0x08000100,
         "BX clears low target bit into PC");
  thumb_cpu.set_register(4, 0x08000200);
  expect(thumb_cpu.execute_thumb(thumb_bx_r4) == ExecuteStatus::executed,
         "execute Thumb BX to ARM target");
  expect(!thumb_cpu.thumb_state(), "BX switches to ARM state when target bit zero is clear");
  expect(thumb_cpu.register_value(Arm7tdmi::kPc) == 0x08000200,
         "BX preserves aligned ARM target");
  thumb_cpu.reset();
  thumb_memory.reset();
  thumb_cpu.set_register(2, 0x02000000);
  thumb_cpu.set_register(1, 0xAABBCCDD);
  expect(thumb_cpu.execute_thumb(thumb_str_r1_r2_imm4_words, thumb_memory) ==
             ExecuteStatus::executed,
         "execute Thumb STR immediate");
  expect(thumb_memory.read32(0x02000010).value() == 0xAABBCCDD,
         "Thumb STR immediate stores word");
  expect(thumb_cpu.execute_thumb(thumb_ldr_r3_r2_imm4_words, thumb_memory) ==
             ExecuteStatus::executed,
         "execute Thumb LDR immediate");
  expect(thumb_cpu.register_value(3) == 0xAABBCCDD, "Thumb LDR immediate loads word");
  expect(thumb_memory.write32(0x02000008, 0xCAFEBABE),
         "seed Thumb LDR literal pool word");
  thumb_cpu.set_register(Arm7tdmi::kPc, 0x02000000);
  expect(thumb_cpu.execute_thumb(thumb_ldr_literal_r0_1, thumb_memory) ==
             ExecuteStatus::executed,
         "execute Thumb LDR literal");
  expect(thumb_cpu.register_value(0) == 0xCAFEBABE,
         "Thumb LDR literal reads from aligned PC plus scaled offset");
  thumb_cpu.set_register(4, 0x123456EF);
  expect(thumb_cpu.execute_thumb(thumb_strb_r4_r2_imm1, thumb_memory) ==
             ExecuteStatus::executed,
         "execute Thumb STRB immediate");
  expect(thumb_memory.read8(0x02000001).value() == 0xEF, "Thumb STRB stores byte");
  expect(thumb_cpu.execute_thumb(thumb_ldrb_r5_r2_imm1, thumb_memory) ==
             ExecuteStatus::executed,
         "execute Thumb LDRB immediate");
  expect(thumb_cpu.register_value(5) == 0xEF, "Thumb LDRB zero-extends byte");
  thumb_cpu.set_register(6, 0xCAFE9001);
  expect(thumb_cpu.execute_thumb(thumb_strh_r6_r2_imm2_halfwords, thumb_memory) ==
             ExecuteStatus::executed,
         "execute Thumb STRH immediate");
  expect(thumb_memory.read16(0x02000004).value() == 0x9001, "Thumb STRH stores halfword");
  thumb_cpu.set_register(1, 5);
  thumb_cpu.set_register(6, 0xCAFE1122);
  expect(thumb_cpu.execute_thumb(thumb_memory_register(0x1, 1, 2, 6), thumb_memory) ==
             ExecuteStatus::executed,
         "execute Thumb unaligned STRH register-offset");
  expect(thumb_memory.read16(0x02000004).value() == 0x1122,
         "Thumb unaligned STRH aligns destination down");
  expect(thumb_cpu.execute_thumb(thumb_ldrh_r7_r2_imm2_halfwords, thumb_memory) ==
             ExecuteStatus::executed,
         "execute Thumb LDRH immediate");
  expect(thumb_cpu.register_value(7) == 0x1122, "Thumb LDRH zero-extends halfword");
  thumb_cpu.set_register(0, 0x01020304);
  thumb_cpu.set_register(1, 0x20);
  expect(thumb_cpu.execute_thumb(thumb_str_r0_r2_r1, thumb_memory) ==
             ExecuteStatus::executed,
         "execute Thumb STR register-offset");
  expect(thumb_memory.read32(0x02000020).value() == 0x01020304,
         "Thumb STR register-offset stores word");
  expect(thumb_cpu.execute_thumb(thumb_ldr_r3_r2_r1, thumb_memory) ==
             ExecuteStatus::executed,
         "execute Thumb LDR register-offset");
  expect(thumb_cpu.register_value(3) == 0x01020304,
         "Thumb LDR register-offset loads word");
  thumb_cpu.set_register(0, 0xA1B2C3D4);
  thumb_cpu.set_register(1, 0x21);
  expect(thumb_cpu.execute_thumb(thumb_str_r0_r2_r1, thumb_memory) ==
             ExecuteStatus::executed,
         "execute Thumb unaligned STR register-offset");
  expect(thumb_memory.read32(0x02000020).value() == 0xA1B2C3D4,
         "Thumb unaligned STR aligns destination down");
  thumb_cpu.set_register(1, 0x20);
  expect(thumb_memory.write8(0x02000020, 0x80), "seed Thumb LDRSB byte");
  expect(thumb_cpu.execute_thumb(thumb_ldrsb_r4_r2_r1, thumb_memory) ==
             ExecuteStatus::executed,
         "execute Thumb LDRSB register-offset");
  expect(thumb_cpu.register_value(4) == 0xFFFFFF80,
         "Thumb LDRSB sign-extends byte");
  expect(thumb_memory.write16(0x02000020, 0x8001), "seed Thumb LDRSH halfword");
  expect(thumb_cpu.execute_thumb(thumb_ldrsh_r5_r2_r1, thumb_memory) ==
             ExecuteStatus::executed,
         "execute Thumb LDRSH register-offset");
  expect(thumb_cpu.register_value(5) == 0xFFFF8001,
         "Thumb LDRSH sign-extends halfword");
  thumb_cpu.set_register(13, 0x03000080);
  thumb_cpu.set_register(0, 0x11223344);
  expect(thumb_cpu.execute_thumb(thumb_str_sp_r0_imm2_words, thumb_memory) ==
             ExecuteStatus::executed,
         "execute Thumb SP-relative STR");
  expect(thumb_memory.read32(0x03000088).value() == 0x11223344,
         "Thumb SP-relative STR stores word");
  expect(thumb_cpu.execute_thumb(thumb_ldr_sp_r6_imm2_words, thumb_memory) ==
             ExecuteStatus::executed,
         "execute Thumb SP-relative LDR");
  expect(thumb_cpu.register_value(6) == 0x11223344,
         "Thumb SP-relative LDR loads word");
  thumb_cpu.set_register(0, 0x030000A0);
  thumb_cpu.set_register(2, 0x55667788);
  expect(thumb_cpu.execute_thumb(thumb_stmia_r0_r2, thumb_memory) == ExecuteStatus::executed,
         "execute Thumb STMIA");
  expect(thumb_memory.read32(0x030000A0).value() == 0x55667788,
         "Thumb STMIA stores listed register");
  expect(thumb_cpu.register_value(0) == 0x030000A4, "Thumb STMIA writes back base");
  expect(thumb_memory.write32(0x030000A4, 0x01020304), "seed Thumb LDMIA first word");
  expect(thumb_memory.write32(0x030000A8, 0x05060708), "seed Thumb LDMIA second word");
  expect(thumb_cpu.execute_thumb(thumb_ldmia_r0_r2_r3, thumb_memory) ==
             ExecuteStatus::executed,
         "execute Thumb LDMIA");
  expect(thumb_cpu.register_value(2) == 0x01020304, "Thumb LDMIA loads first register");
  expect(thumb_cpu.register_value(3) == 0x05060708, "Thumb LDMIA loads second register");
  expect(thumb_cpu.register_value(0) == 0x030000AC, "Thumb LDMIA writes back base");
  expect(thumb_memory.load_game_pak_rom(rom_with_word(4, 0x428A428AU)),
         "Thumb LDMIA open-bus test ROM with pipeline word");
  Arm7tdmi thumb_open_bus_cpu;
  expect(thumb_open_bus_cpu.set_cpsr(0x20U | static_cast<std::uint32_t>(CpuMode::system)),
         "Thumb LDMIA open-bus fixture enters Thumb state");
  thumb_open_bus_cpu.set_register(Arm7tdmi::kPc, 0x08000000);
  thumb_open_bus_cpu.set_register(3, 0x10000000);
  expect(thumb_open_bus_cpu.execute_thumb(thumb_block_transfer(true, 3, 0x04),
                                          thumb_memory) == ExecuteStatus::executed,
         "Thumb LDMIA unmapped read uses pipeline open bus");
  expect(thumb_open_bus_cpu.register_value(2) == 0x428A428A,
         "Thumb LDMIA loads instruction-stream open bus");
  expect(thumb_open_bus_cpu.register_value(3) == 0x10000004,
         "Thumb LDMIA unmapped read writes back base");
  thumb_memory.drive_open_bus(0xDEAD0000);
  Arm7tdmi thumb_latched_bus_cpu;
  expect(thumb_latched_bus_cpu.set_cpsr(0x20U | static_cast<std::uint32_t>(CpuMode::system)),
         "Thumb LDMIA latched-bus fixture enters Thumb state");
  thumb_latched_bus_cpu.set_register(Arm7tdmi::kPc, 0x08000000);
  thumb_latched_bus_cpu.set_register(3, 0x10000000);
  expect(thumb_latched_bus_cpu.execute_thumb(thumb_block_transfer(true, 3, 0x04),
                                             thumb_memory) == ExecuteStatus::executed,
         "Thumb LDMIA unmapped read uses latched data bus");
  expect(thumb_latched_bus_cpu.register_value(2) == 0xDEAD0000,
         "Thumb LDMIA loads DMA-driven open bus when present");
  expect(thumb_latched_bus_cpu.register_value(3) == 0x10000004,
         "Thumb LDMIA latched-bus read writes back base");
  thumb_cpu.set_register(13, 0x03000100);
  thumb_cpu.set_register(0, 0xAAAA0000);
  thumb_cpu.set_register(1, 0xBBBB1111);
  thumb_cpu.set_register(Arm7tdmi::kLinkRegister, 0x08000005);
  expect(thumb_cpu.execute_thumb(thumb_push_r0_r1_lr, thumb_memory) ==
             ExecuteStatus::executed,
         "execute Thumb PUSH with LR");
  expect(thumb_cpu.register_value(13) == 0x030000F4, "Thumb PUSH decrements SP");
  expect(thumb_memory.read32(0x030000F4).value() == 0xAAAA0000,
         "Thumb PUSH stores R0 first");
  expect(thumb_memory.read32(0x030000F8).value() == 0xBBBB1111,
         "Thumb PUSH stores R1 second");
  expect(thumb_memory.read32(0x030000FC).value() == 0x08000005,
         "Thumb PUSH stores LR last");
  expect(thumb_cpu.execute_thumb(thumb_pop_r2_r3_pc, thumb_memory) ==
             ExecuteStatus::executed,
         "execute Thumb POP with PC");
  expect(thumb_cpu.register_value(2) == 0xAAAA0000, "Thumb POP loads R2");
  expect(thumb_cpu.register_value(3) == 0xBBBB1111, "Thumb POP loads R3");
  expect(thumb_cpu.register_value(Arm7tdmi::kPc) == 0x08000004,
         "Thumb POP PC clears low target bit");
  expect(thumb_cpu.register_value(13) == 0x03000100, "Thumb POP increments SP");
  expect(thumb_cpu.execute_thumb(thumb_empty_push, thumb_memory) == ExecuteStatus::unsupported,
         "Thumb empty PUSH remains unsupported");
  thumb_cpu.set_register(13, 0x03007EDC);
  expect(thumb_cpu.execute_thumb(thumb_sub_sp_0x34) == ExecuteStatus::executed,
         "execute Thumb SUB SP immediate");
  expect(thumb_cpu.register_value(13) == 0x03007EA8,
         "Thumb SUB SP immediate subtracts scaled offset");
  expect(thumb_cpu.execute_thumb(thumb_add_sp_0x14) == ExecuteStatus::executed,
         "execute Thumb ADD SP immediate");
  expect(thumb_cpu.register_value(13) == 0x03007EBC,
         "Thumb ADD SP immediate adds scaled offset");
  thumb_cpu.set_register(13, 0x03007DF0);
  expect(thumb_cpu.execute_thumb(thumb_add_r3_sp_0xa8) == ExecuteStatus::executed,
         "execute Thumb ADD Rd, SP immediate");
  expect(thumb_cpu.register_value(3) == 0x03007E98,
         "Thumb ADD Rd, SP immediate writes computed address");
  thumb_cpu.set_register(Arm7tdmi::kPc, 0x08000002);
  expect(thumb_cpu.execute_thumb(thumb_add_r4_pc_0x10) == ExecuteStatus::executed,
         "execute Thumb ADD Rd, PC immediate");
  expect(thumb_cpu.register_value(4) == 0x08000014,
         "Thumb ADD Rd, PC immediate uses aligned visible PC");
  thumb_cpu.reset();
  expect(thumb_cpu.set_cpsr(0x40000030), "prepare Thumb SWI from user state");
  thumb_cpu.set_register(Arm7tdmi::kPc, 0x08000400);
  expect(thumb_cpu.execute_thumb(thumb_swi_7) == ExecuteStatus::executed,
         "execute Thumb SWI");
  expect(thumb_cpu.current_mode() == CpuMode::supervisor, "Thumb SWI enters supervisor");
  expect(thumb_cpu.register_value(Arm7tdmi::kPc) == 0x00000008,
         "Thumb SWI vectors PC");
  expect(thumb_cpu.register_value(Arm7tdmi::kLinkRegister) == 0x08000404,
         "Thumb SWI writes LR through exception path");
  expect(!thumb_cpu.thumb_state(), "Thumb SWI clears Thumb state");
  expect(thumb_cpu.spsr().value() == 0x40000030, "Thumb SWI saves prior CPSR");
  expect(thumb_cpu.execute_thumb(unsupported_thumb_condition) == ExecuteStatus::unsupported,
         "unsupported Thumb branch condition is reported");
  expect(thumb_cpu.execute_thumb(unsupported_low_only_high_add) == ExecuteStatus::unsupported,
         "unsupported low-only Thumb high-register alias is reported");
  expect(thumb_cpu.execute_thumb(unsupported_thumb) == ExecuteStatus::unsupported,
         "unsupported Thumb instruction is reported");

  constexpr std::uint32_t b_forward = kCondAl | kBranch | branch_offset(2);
  cpu.set_register(Arm7tdmi::kPc, 0x08000000);
  expect(Arm7tdmi::can_decode_branch(b_forward), "B decodes");
  expect_cycles(Arm7tdmi::estimate_arm_cycles(b_forward).value(), 2, 1, 0, false,
                "B cycle estimate matches pipeline refill shape");
  expect(Arm7tdmi::decode_branch(b_forward).offset == 8, "B offset decodes");
  expect(cpu.execute_arm(b_forward) == ExecuteStatus::executed, "execute B");
  expect(cpu.register_value(Arm7tdmi::kPc) == 0x08000010, "B updates PC with pipeline offset");

  constexpr std::uint32_t bl_backward = kCondAl | kBranchLink | branch_offset(-4);
  cpu.set_register(Arm7tdmi::kPc, 0x08000020);
  expect(Arm7tdmi::decode_branch(bl_backward).link, "BL decodes link bit");
  expect(Arm7tdmi::decode_branch(bl_backward).offset == -16, "BL signed offset decodes");
  expect(cpu.execute_arm(bl_backward) == ExecuteStatus::executed, "execute BL");
  expect(cpu.register_value(Arm7tdmi::kLinkRegister) == 0x08000024, "BL writes link register");
  expect(cpu.register_value(Arm7tdmi::kPc) == 0x08000018, "BL applies signed offset");

  constexpr std::uint32_t bx_r3 = arm_bx(kCondAl, 3);
  expect(Arm7tdmi::can_decode_branch_exchange(bx_r3), "BX decodes");
  expect(Arm7tdmi::decode_branch_exchange(bx_r3).rm == 3, "BX source register decodes");
  expect_cycles(Arm7tdmi::estimate_arm_cycles(bx_r3).value(), 2, 1, 0, false,
                "BX cycle estimate matches pipeline refill shape");
  cpu.set_register(3, 0x08000201);
  expect(cpu.execute_arm(bx_r3) == ExecuteStatus::executed, "execute BX to Thumb");
  expect(cpu.thumb_state(), "BX sets Thumb state from target bit");
  expect(cpu.register_value(Arm7tdmi::kPc) == 0x08000200, "BX clears target state bit");

  constexpr std::uint32_t bx_r4 = arm_bx(kCondAl, 4);
  cpu.set_register(4, 0x08000300);
  expect(cpu.execute_arm(bx_r4) == ExecuteStatus::executed, "execute BX to ARM");
  expect(!cpu.thumb_state(), "BX clears Thumb state from aligned target");
  expect(cpu.register_value(Arm7tdmi::kPc) == 0x08000300, "BX keeps aligned ARM target");

  constexpr std::uint32_t bne_skipped = kCondNe | kBranch | branch_offset(1);
  cpu.set_register(Arm7tdmi::kPc, 0x08000100);
  expect(cpu.execute_arm(bne_skipped) == ExecuteStatus::skipped_condition,
         "conditional branch skips when condition fails");
  expect(cpu.register_value(Arm7tdmi::kPc) == 0x08000100, "skipped branch preserves PC");
  constexpr std::uint32_t bge_forward = kCondGe | kBranch | branch_offset(1);
  constexpr std::uint32_t blt_forward = kCondLt | kBranch | branch_offset(1);
  expect(cpu.set_cpsr(0x90000013), "prepare signed condition flags with N == V");
  cpu.set_register(Arm7tdmi::kPc, 0x08000120);
  expect(cpu.execute_arm(bge_forward) == ExecuteStatus::executed,
         "GE branch executes when N equals V");
  expect(cpu.register_value(Arm7tdmi::kPc) == 0x0800012C, "GE branch updates PC");
  expect(cpu.execute_arm(blt_forward) == ExecuteStatus::skipped_condition,
         "LT branch skips when N equals V");
  expect(cpu.register_value(Arm7tdmi::kPc) == 0x0800012C, "skipped LT preserves PC");
  constexpr std::uint32_t bxlt_r4 = arm_bx(kCondLt, 4);
  expect(cpu.execute_arm(bxlt_r4) == ExecuteStatus::skipped_condition,
         "conditional BX skips when condition fails");
  expect(cpu.register_value(Arm7tdmi::kPc) == 0x0800012C, "skipped BX preserves PC");
  expect(cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::supervisor)),
         "restore supervisor CPSR after condition tests");
  Arm7tdmi branch_step_cpu;
  branch_step_cpu.set_register(Arm7tdmi::kPc, 0x08000000);
  expect_step(branch_step_cpu.step_arm(b_forward), ExecuteStatus::executed, 3, 3, false, false,
              "scheduler step charges branch");
  expect(branch_step_cpu.register_value(Arm7tdmi::kPc) == 0x08000010,
         "scheduler step executes branch");
  Arm7tdmi bx_step_cpu;
  bx_step_cpu.set_register(3, 0x08000401);
  expect_step(bx_step_cpu.step_arm(bx_r3), ExecuteStatus::executed, 3, 3, false, false,
              "scheduler step charges BX");
  expect(bx_step_cpu.thumb_state(), "scheduler step executes BX state switch");
  expect(bx_step_cpu.register_value(Arm7tdmi::kPc) == 0x08000400,
         "scheduler step executes BX target");
  branch_step_cpu.set_register(2, 12);
  expect_step(branch_step_cpu.step_arm(cmp_r2_12), ExecuteStatus::executed, 1, 4, false, false,
              "scheduler step charges CMP");
  expect_step(branch_step_cpu.step_arm(bne_skipped), ExecuteStatus::skipped_condition, 0, 4,
              false, false, "scheduler step charges skipped condition");

  constexpr std::uint32_t str_r1_base_plus_4 =
      kCondAl | kSingleDataTransferImmediate | kPreIndexed | kUp | rn(6) | rd(1) | offset12(4);
  constexpr std::uint32_t ldr_r5_base_plus_4 = kCondAl | kSingleDataTransferImmediate |
                                               kPreIndexed | kUp | kLoad | rn(6) | rd(5) |
                                               offset12(4);
  constexpr std::uint32_t ldr_r7_base_minus_4 =
      kCondAl | kSingleDataTransferImmediate | kPreIndexed | kLoad | rn(6) | rd(7) | offset12(4);
  MemoryBus memory;
  cpu.set_register(1, 0xAABBCCDD);
  cpu.set_register(6, 0x02000000);
  expect(Arm7tdmi::can_decode_single_data_transfer_immediate(str_r1_base_plus_4),
         "STR immediate decodes");
  expect_cycles(Arm7tdmi::estimate_arm_cycles(str_r1_base_plus_4).value(), 0, 2, 0, false,
                "STR cycle estimate matches store transfer shape");
  expect_cycles(Arm7tdmi::estimate_arm_cycles(ldr_r5_base_plus_4).value(), 1, 1, 1, false,
                "LDR cycle estimate matches load transfer shape");
  expect_elapsed(Arm7tdmi::estimate_arm_elapsed_cycles(str_r1_base_plus_4, 0x02000004).value(),
                 12, false, true, "STR EWRAM word elapsed cycle estimate");
  expect_elapsed(Arm7tdmi::estimate_arm_elapsed_cycles(ldr_r5_base_plus_4, 0x03000004).value(),
                 3, false, true, "LDR IWRAM word elapsed cycle estimate");
  WaitStateControl waitcnt;
  expect_elapsed(Arm7tdmi::estimate_arm_elapsed_cycles(ldr_r5_base_plus_4, 0x08000004,
                                                       waitcnt)
                     .value(),
                 10, false, true,
                 "WAITCNT-aware LDR estimate uses default ROM wait0 timing");
  waitcnt.write_control(WaitStateControl::kStandardGamePakSetting);
  expect_elapsed(Arm7tdmi::estimate_arm_elapsed_cycles(ldr_r5_base_plus_4, 0x03000004,
                                                       waitcnt)
                     .value(),
                 2, false, true,
                 "WAITCNT prefetch overlaps IWRAM LDR internal cycle");
  expect_elapsed(Arm7tdmi::estimate_arm_elapsed_cycles(ldr_r5_base_plus_4, 0x08000004,
                                                       waitcnt)
                     .value(),
                 8, false, true,
                 "WAITCNT-aware LDR estimate uses standard ROM wait0 timing");
  expect_elapsed(Arm7tdmi::estimate_arm_elapsed_cycles(ldr_r5_base_plus_4, 0x0C000004,
                                                       waitcnt)
                     .value(),
                 20, false, true,
                 "WAITCNT-aware LDR estimate uses standard ROM wait2 timing");
  expect_elapsed(Arm7tdmi::estimate_arm_elapsed_cycles(ldr_r5_base_plus_4, 0x0E000004,
                                                       waitcnt)
                     .value(),
                 17, false, true,
                 "WAITCNT-aware LDR estimate uses standard save timing metadata");
  expect_elapsed(Arm7tdmi::estimate_arm_elapsed_cycles(mov_r1_7, 0x08000000, waitcnt).value(),
                 1, false, false,
                 "WAITCNT-aware MOV elapsed estimate does not apply memory timing");
  expect_elapsed(Arm7tdmi::estimate_arm_elapsed_cycles(ldr_r5_base_plus_4, 0x00000000,
                                                       waitcnt)
                     .value(),
                 3, false, true,
                 "WAITCNT-aware LDR estimate accepts HLE BIOS vector timing");
  Arm7tdmi scheduler_cpu;
  MemoryBus scheduler_memory;
  expect(scheduler_cpu.elapsed_cycles() == 0, "scheduler elapsed cycles start at zero");
  expect_step(scheduler_cpu.step_arm(mov_r1_7), ExecuteStatus::executed, 1, 1, false, false,
              "scheduler step charges MOV");
  expect(scheduler_cpu.register_value(1) == 7, "scheduler step executes MOV");
  scheduler_cpu.set_register(1, 0x11223344);
  scheduler_cpu.set_register(6, 0x02000000);
  expect_step(scheduler_cpu.step_arm(str_r1_base_plus_4, scheduler_memory),
              ExecuteStatus::executed, 12, 13, false, true,
              "scheduler step charges STR at effective EWRAM address");
  expect(scheduler_memory.read32(0x02000004).value() == 0x11223344,
         "scheduler step executes STR");
  expect(scheduler_memory.write32(0x03000004, 0x55667788), "seed scheduler IWRAM word");
  scheduler_cpu.set_register(6, 0x03000000);
  expect_step(scheduler_cpu.step_arm(ldr_r5_base_plus_4, scheduler_memory),
              ExecuteStatus::executed, 3, 16, false, true,
              "scheduler step charges LDR at effective IWRAM address");
  expect(scheduler_cpu.register_value(5) == 0x55667788, "scheduler step executes LDR");
  scheduler_cpu.reset_elapsed_cycles();
  expect(scheduler_cpu.elapsed_cycles() == 0, "scheduler elapsed cycles reset independently");
  scheduler_cpu.set_register(6, 0x08000000);
  expect_step(scheduler_cpu.step_arm(ldr_r5_base_plus_4, scheduler_memory),
              ExecuteStatus::unsupported, 0, 0, false, false,
              "scheduler step does not charge unsupported cartridge access");
  expect(scheduler_memory.load_game_pak_rom(rom_with_word(4, 0xA5A55A5A)),
         "seed explicit ROM bytes for untimed scheduler access");
  scheduler_cpu.set_register(5, 0xDEADBEEF);
  scheduler_cpu.set_register(6, 0x08000000);
  expect_step(scheduler_cpu.step_arm(ldr_r5_base_plus_4, scheduler_memory),
              ExecuteStatus::unsupported, 0, 0, false, false,
              "untimed scheduler step rejects loaded cartridge LDR before mutation");
  expect(scheduler_cpu.register_value(5) == 0xDEADBEEF,
         "untimed rejected cartridge LDR preserves destination register");
  scheduler_cpu.set_register(2, 12);
  expect(scheduler_cpu.execute_arm(cmp_r2_12) == ExecuteStatus::executed,
         "prepare false NE condition for memory timing preflight");
  constexpr std::uint32_t ldrne_r5_base_plus_4 =
      kCondNe | kSingleDataTransferImmediate | kPreIndexed | kUp | kLoad | rn(6) |
      rd(5) | offset12(4);
  expect_step(scheduler_cpu.step_arm(ldrne_r5_base_plus_4, scheduler_memory),
              ExecuteStatus::skipped_condition, 0, 0, false, false,
              "false condition is still skipped before memory timing preflight");
  const auto decoded_store = Arm7tdmi::decode_single_data_transfer_immediate(str_r1_base_plus_4);
  expect(!decoded_store.load, "STR load bit decodes");
  expect(decoded_store.pre_index, "STR pre-index bit decodes");
  expect(decoded_store.up, "STR up bit decodes");
  expect(decoded_store.rn == 6, "STR Rn decodes");
  expect(decoded_store.rd == 1, "STR Rd decodes");
  expect(decoded_store.offset == 4, "STR offset decodes");
  expect(cpu.execute_arm(str_r1_base_plus_4, memory) == ExecuteStatus::executed,
         "execute STR immediate");
  expect(memory.read32(0x02000004).value_or(0) == 0xAABBCCDD, "STR writes memory");
  expect(cpu.execute_arm(ldr_r5_base_plus_4, memory) == ExecuteStatus::executed,
         "execute LDR immediate");
  expect(cpu.register_value(5) == 0xAABBCCDD, "LDR reads memory");

  constexpr std::uint32_t swp_r9_r10_base_r6 = arm_swp(false, 6, 9, 10);
  expect(Arm7tdmi::can_decode_swap(swp_r9_r10_base_r6), "SWP decodes");
  expect_cycles(Arm7tdmi::estimate_arm_cycles(swp_r9_r10_base_r6).value(), 0, 2, 1,
                false, "SWP cycle estimate is available");
  cpu.set_register(6, 0x02000020);
  cpu.set_register(9, 0xDEADBEEF);
  cpu.set_register(10, 0x55667788);
  expect(memory.write32(0x02000020, 0x01020304), "prepare SWP word fixture");
  expect(cpu.execute_arm(swp_r9_r10_base_r6, memory) == ExecuteStatus::executed,
         "execute SWP word");
  expect(cpu.register_value(9) == 0x01020304, "SWP loads old word into Rd");
  expect(memory.read32(0x02000020).value() == 0x55667788,
         "SWP stores source word to memory");
  constexpr std::uint32_t swpb_r11_r12_base_r6 = arm_swp(true, 6, 11, 12);
  cpu.set_register(6, 0x02000024);
  cpu.set_register(11, 0xDEADBEEF);
  cpu.set_register(12, 0x123456AB);
  expect(memory.write8(0x02000024, 0x7E), "prepare SWPB byte fixture");
  expect(cpu.execute_arm(swpb_r11_r12_base_r6, memory) == ExecuteStatus::executed,
         "execute SWPB byte");
  expect(cpu.register_value(11) == 0x7E, "SWPB zero-extends old byte into Rd");
  expect(memory.read8(0x02000024).value() == 0xAB, "SWPB writes low source byte");
  MemoryBus swap_rom_memory;
  expect(swap_rom_memory.load_game_pak_rom(rom_with_word(0, 0xCAFEBABE)),
         "prepare read-only ROM for ignored-write SWP");
  cpu.set_register(6, 0x08000000);
  cpu.set_register(9, 0xDEADBEEF);
  cpu.set_register(10, 0x01010101);
  expect(cpu.execute_arm(swp_r9_r10_base_r6, swap_rom_memory) == ExecuteStatus::executed,
         "SWP to read-only ROM reads old value and ignores write");
  expect(cpu.register_value(9) == 0xCAFEBABE, "ROM SWP loads old ROM value into Rd");
  expect(swap_rom_memory.read32(0x08000000).value() == 0xCAFEBABE,
         "ROM SWP ignored write preserves ROM bytes");

  expect(memory.write32(0x020000FC, 0x11223344), "prepare LDR down fixture");
  cpu.set_register(6, 0x02000100);
  expect(cpu.execute_arm(ldr_r7_base_minus_4, memory) == ExecuteStatus::executed,
         "execute LDR immediate down");
  expect(cpu.register_value(7) == 0x11223344, "LDR down reads memory");

  constexpr std::uint32_t ldrne_skipped = kCondNe | kSingleDataTransferImmediate | kPreIndexed |
                                          kUp | kLoad | rn(6) | rd(8) | offset12(4);
  expect(cpu.set_cpsr(0x40000013), "prepare Z flag for skipped conditional LDR");
  expect(cpu.execute_arm(ldrne_skipped, memory) == ExecuteStatus::skipped_condition,
         "conditional LDR skips when condition fails");
  expect(cpu.register_value(8) == 0, "skipped LDR preserves Rd");

  constexpr std::uint32_t str_r1_pre_writeback =
      kCondAl | kSingleDataTransferImmediate | kPreIndexed | kUp | kWriteBack | rn(6) |
      rd(1) | offset12(8);
  cpu.set_register(1, 0x01020304);
  cpu.set_register(6, 0x02000060);
  expect(cpu.execute_arm(str_r1_pre_writeback, memory) == ExecuteStatus::executed,
         "execute STR pre-index writeback");
  expect(memory.read32(0x02000068).value_or(0) == 0x01020304,
         "STR pre-index writeback writes offset address");
  expect(cpu.register_value(6) == 0x02000068, "STR pre-index writeback updates Rn");

  expect(memory.write32(0x02000068, 0x0BADF00D), "prepare LDR post-index fixture");
  constexpr std::uint32_t ldr_r5_post_plus_4 =
      kCondAl | kSingleDataTransferImmediate | kUp | kLoad | rn(6) | rd(5) | offset12(4);
  expect(cpu.execute_arm(ldr_r5_post_plus_4, memory) == ExecuteStatus::executed,
         "execute LDR post-index");
  expect(cpu.register_value(5) == 0x0BADF00D, "LDR post-index reads base address");
  expect(cpu.register_value(6) == 0x0200006C, "LDR post-index updates Rn after load");

  constexpr std::uint32_t ldr_r6_pre_writeback_same_register =
      kCondAl | kSingleDataTransferImmediate | kPreIndexed | kUp | kWriteBack | kLoad |
      rn(6) | rd(6) | offset12(4);
  expect(cpu.execute_arm(ldr_r6_pre_writeback_same_register, memory) == ExecuteStatus::unsupported,
         "LDR writeback with Rn as Rd remains unsupported");

  constexpr std::uint32_t stmia_r6_writeback_r0_r2_r4 =
      kCondAl | kBlockDataTransfer | kUp | kWriteBack | rn(6) |
      reg_list((1U << 0U) | (1U << 2U) | (1U << 4U));
  cpu.set_register(0, 0x11111111);
  cpu.set_register(2, 0x22222222);
  cpu.set_register(4, 0x44444444);
  cpu.set_register(6, 0x02000100);
  expect(Arm7tdmi::can_decode_block_data_transfer(stmia_r6_writeback_r0_r2_r4),
         "STMIA block transfer decodes");
  expect_cycles(Arm7tdmi::estimate_arm_cycles(stmia_r6_writeback_r0_r2_r4).value(), 2, 2, 0,
                false, "STMIA cycle estimate scales with register count");
  expect_elapsed(
      Arm7tdmi::estimate_arm_elapsed_cycles(stmia_r6_writeback_r0_r2_r4, 0x02000100).value(),
      24, false, true, "STMIA EWRAM elapsed cycle estimate scales with register count");
  const auto decoded_stmia =
      Arm7tdmi::decode_block_data_transfer(stmia_r6_writeback_r0_r2_r4);
  expect(!decoded_stmia.load, "STMIA load bit decodes clear");
  expect(!decoded_stmia.pre_index, "STMIA after-index decodes");
  expect(decoded_stmia.up, "STMIA up bit decodes");
  expect(decoded_stmia.write_back, "STMIA writeback bit decodes");
  expect(decoded_stmia.rn == 6, "STMIA Rn decodes");
  expect(decoded_stmia.register_list == 0x0015, "STMIA register list decodes");
  expect(cpu.execute_arm(stmia_r6_writeback_r0_r2_r4, memory) == ExecuteStatus::executed,
         "execute STMIA writeback");
  expect(memory.read32(0x02000100).value_or(0) == 0x11111111,
         "STMIA stores lowest register at base");
  expect(memory.read32(0x02000104).value_or(0) == 0x22222222,
         "STMIA stores next selected register sequentially");
  expect(memory.read32(0x02000108).value_or(0) == 0x44444444,
         "STMIA stores highest selected register sequentially");
  expect(cpu.register_value(6) == 0x0200010C, "STMIA writeback advances Rn");

  constexpr std::uint32_t ldmia_r7_r1_r3 =
      kCondAl | kBlockDataTransfer | kUp | kLoad | rn(7) |
      reg_list((1U << 1U) | (1U << 3U));
  expect_cycles(Arm7tdmi::estimate_arm_cycles(ldmia_r7_r1_r3).value(), 2, 1, 1, false,
                "LDMIA cycle estimate scales with register count");
  expect(memory.write32(0x02000120, 0xABCDEF01), "prepare LDMIA first word");
  expect(memory.write32(0x02000124, 0x10203040), "prepare LDMIA second word");
  cpu.set_register(7, 0x02000120);
  expect(cpu.execute_arm(ldmia_r7_r1_r3, memory) == ExecuteStatus::executed,
         "execute LDMIA");
  expect(cpu.register_value(1) == 0xABCDEF01, "LDMIA loads first selected register");
  expect(cpu.register_value(3) == 0x10203040, "LDMIA loads second selected register");
  expect(cpu.register_value(7) == 0x02000120, "LDMIA without writeback preserves Rn");

  constexpr std::uint32_t stmdb_r8_writeback_r0_r1 =
      kCondAl | kBlockDataTransfer | kPreIndexed | kWriteBack | rn(8) |
      reg_list((1U << 0U) | (1U << 1U));
  cpu.set_register(0, 0x0A0B0C0D);
  cpu.set_register(1, 0x01020304);
  cpu.set_register(8, 0x02000140);
  expect(cpu.execute_arm(stmdb_r8_writeback_r0_r1, memory) == ExecuteStatus::executed,
         "execute STMDB writeback");
  expect(memory.read32(0x02000138).value_or(0) == 0x0A0B0C0D,
         "STMDB stores first register at descending block start");
  expect(memory.read32(0x0200013C).value_or(0) == 0x01020304,
         "STMDB stores second register sequentially");
  expect(cpu.register_value(8) == 0x02000138, "STMDB writeback subtracts block size");

  constexpr std::uint32_t stmdb_sp_writeback_sp_lr =
      kCondAl | kBlockDataTransfer | kPreIndexed | kWriteBack | rn(13) |
      reg_list((1U << 13U) | (1U << 14U));
  cpu.set_register(13, 0x02000160);
  cpu.set_register(14, 0x08001234);
  expect(cpu.execute_arm(stmdb_sp_writeback_sp_lr, memory) == ExecuteStatus::executed,
         "execute STMDB writeback with base in store list");
  expect(memory.read32(0x02000158).value_or(0) == 0x02000160,
         "STMDB stores original SP when base is in store list");
  expect(memory.read32(0x0200015C).value_or(0) == 0x08001234,
         "STMDB stores LR after original SP");
  expect(cpu.register_value(13) == 0x02000158,
         "STMDB writeback with base in store list updates SP after stores");

  constexpr std::uint32_t ldmne_skipped =
      kCondNe | kBlockDataTransfer | kUp | kLoad | rn(7) | reg_list(1U << 9U);
  cpu.set_register(9, 0xFEEDFACE);
  expect(cpu.execute_arm(ldmne_skipped, memory) == ExecuteStatus::skipped_condition,
         "conditional LDM skips when condition fails");
  expect(cpu.register_value(9) == 0xFEEDFACE, "skipped LDM preserves selected register");

  constexpr std::uint32_t ldmia_with_pc =
      kCondAl | kBlockDataTransfer | kUp | kLoad | rn(7) | reg_list(1U << 15U);
  expect(!Arm7tdmi::can_decode_block_data_transfer(ldmia_with_pc),
         "LDM with PC remains unsupported");
  expect(cpu.execute_arm(ldmia_with_pc, memory) == ExecuteStatus::unsupported,
         "LDM with PC execution remains unsupported");
  expect(!Arm7tdmi::estimate_arm_elapsed_cycles(ldmia_with_pc, 0x02000120).has_value(),
         "unsupported LDM with PC has no elapsed cycle estimate");

  constexpr std::uint32_t ldmia_writeback_base_in_list =
      kCondAl | kBlockDataTransfer | kUp | kLoad | kWriteBack | rn(7) |
      reg_list((1U << 1U) | (1U << 7U));
  expect(cpu.execute_arm(ldmia_writeback_base_in_list, memory) == ExecuteStatus::unsupported,
         "LDM writeback with base in register list remains unsupported");

  constexpr std::uint32_t str_r1_base_plus_r2 =
      kCondAl | kSingleDataTransferRegister | kPreIndexed | kUp | rn(6) | rd(1) | rm(2);
  constexpr std::uint32_t ldr_r14_base_minus_r2 = kCondAl | kSingleDataTransferRegister |
                                                  kPreIndexed | kLoad | rn(6) | rd(14) |
                                                  rm(2);
  cpu.set_register(1, 0x55667788);
  cpu.set_register(2, 12);
  cpu.set_register(6, 0x02000020);
  expect(Arm7tdmi::can_decode_single_data_transfer_register(str_r1_base_plus_r2),
         "STR register-offset decodes");
  const auto decoded_register_store =
      Arm7tdmi::decode_single_data_transfer_register(str_r1_base_plus_r2,
                                                     cpu.register_value(2));
  expect(!decoded_register_store.load, "STR register-offset load bit decodes");
  expect(decoded_register_store.pre_index, "STR register-offset pre-index bit decodes");
  expect(decoded_register_store.up, "STR register-offset up bit decodes");
  expect(decoded_register_store.offset == 12, "STR register-offset computes offset");
  expect(cpu.execute_arm(str_r1_base_plus_r2, memory) == ExecuteStatus::executed,
         "execute STR register-offset");
  expect(memory.read32(0x0200002C).value_or(0) == 0x55667788,
         "STR register-offset writes memory");
  cpu.set_register(1, 0x11223344);
  cpu.set_register(2, 13);
  expect(cpu.execute_arm(str_r1_base_plus_r2, memory) == ExecuteStatus::executed,
         "execute unaligned STR register-offset");
  expect(memory.read32(0x0200002C).value_or(0) == 0x11223344,
         "unaligned STR register-offset aligns destination down");

  cpu.set_register(2, 12);
  expect(memory.write32(0x02000014, 0xCAFEBABE), "prepare LDR register-offset fixture");
  expect(cpu.execute_arm(ldr_r14_base_minus_r2, memory) == ExecuteStatus::executed,
         "execute LDR register-offset down");
  expect(cpu.register_value(14) == 0xCAFEBABE, "LDR register-offset down reads memory");

  constexpr std::uint32_t strb_r1_base_plus_r3_lsl_1 =
      kCondAl | kSingleDataTransferRegister | kPreIndexed | kUp | kByteTransfer | rn(6) |
      rd(1) | shift_imm(1) | shift_type(0) | rm(3);
  constexpr std::uint32_t ldrb_r12_base_plus_r3_lsl_1 =
      kCondAl | kSingleDataTransferRegister | kPreIndexed | kUp | kByteTransfer | kLoad |
      rn(6) | rd(12) | shift_imm(1) | shift_type(0) | rm(3);
  cpu.set_register(1, 0xAABBCCDD);
  cpu.set_register(3, 6);
  expect(cpu.execute_arm(strb_r1_base_plus_r3_lsl_1, memory) == ExecuteStatus::executed,
         "execute STRB shifted register-offset");
  expect(memory.read8(0x0200002C).value_or(0) == 0xDD,
         "STRB shifted register-offset writes low byte");
  expect(cpu.execute_arm(ldrb_r12_base_plus_r3_lsl_1, memory) == ExecuteStatus::executed,
         "execute LDRB shifted register-offset");
  expect(cpu.register_value(12) == 0xDD, "LDRB shifted register-offset zero-extends byte");
  constexpr std::uint32_t ldrb_r3_bios =
      kCondAl | kSingleDataTransferImmediate | kPreIndexed | kUp | kByteTransfer | kLoad |
      rn(0) | rd(3);
  cpu.set_register(0, 0);
  expect(cpu.execute_arm(ldrb_r3_bios, memory) == ExecuteStatus::executed,
         "execute LDRB from HLE BIOS vector");
  expect(cpu.register_value(3) == 0x04, "LDRB from HLE BIOS vector reads byte zero");

  MemoryBus open_bus_memory;
  expect(open_bus_memory.load_game_pak_rom(rom_with_word(8, 0xE3A02001)),
         "open-bus test ROM with pipeline word loads");
  Arm7tdmi open_bus_cpu;
  open_bus_cpu.set_register(Arm7tdmi::kPc, 0x08000000);
  open_bus_cpu.set_register(0, 0x00010000);
  expect(open_bus_cpu.execute_arm(ldrb_r3_bios, open_bus_memory) == ExecuteStatus::executed,
         "protected BIOS LDRB uses pipeline open bus");
  expect(open_bus_cpu.register_value(3) == 0x01,
         "protected BIOS LDRB reads low byte from PC+8 instruction");

  expect(open_bus_memory.load_game_pak_rom(rom_with_word(8, 0xE3A02004)),
         "open-bus test ROM with halfword pipeline word loads");
  constexpr std::uint32_t ldrh_r10_protected_bios_odd =
      kCondAl | kHalfwordDataTransferImmediate | kPreIndexed | kUp | kLoad | rn(0) |
      rd(10) | halfword_offset(1);
  open_bus_cpu.set_register(Arm7tdmi::kPc, 0x08000000);
  open_bus_cpu.set_register(0, 0x00010000);
  expect(open_bus_cpu.execute_arm(ldrh_r10_protected_bios_odd, open_bus_memory) ==
             ExecuteStatus::executed,
         "protected BIOS odd LDRH uses pipeline open bus");
  expect(open_bus_cpu.register_value(10) == 0x04000020,
         "protected BIOS odd LDRH rotates PC+8 halfword");

  expect(open_bus_memory.load_game_pak_rom(rom_with_word(8, 0xE3A02008)),
         "open-bus test ROM with word pipeline word loads");
  constexpr std::uint32_t ldr_r4_protected_bios_unaligned =
      kCondAl | kSingleDataTransferImmediate | kPreIndexed | kUp | kLoad | rn(0) |
      rd(4) | offset12(1);
  open_bus_cpu.set_register(Arm7tdmi::kPc, 0x08000000);
  open_bus_cpu.set_register(0, 0x00010000);
  expect(open_bus_cpu.execute_arm(ldr_r4_protected_bios_unaligned, open_bus_memory) ==
             ExecuteStatus::executed,
         "protected BIOS unaligned LDR uses pipeline open bus");
  expect(open_bus_cpu.register_value(4) == 0x08E3A020,
         "protected BIOS unaligned LDR rotates PC+8 word");

  constexpr std::uint32_t str_r1_base_plus_r2_rrx =
      kCondAl | kSingleDataTransferRegister | kPreIndexed | kUp | rn(6) | rd(1) |
      shift_type(3) | rm(2);
  expect(Arm7tdmi::can_decode_single_data_transfer_register(str_r1_base_plus_r2_rrx),
         "RRX register-offset decodes");
  expect(cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::supervisor)),
         "clear carry before RRX register-offset execution");
  cpu.set_register(1, 0x13579BDF);
  cpu.set_register(2, 8);
  cpu.set_register(6, 0x02000040);
  expect(cpu.execute_arm(str_r1_base_plus_r2_rrx, memory) == ExecuteStatus::executed,
         "execute STR with RRX register-offset");
  expect(memory.read32(0x02000044).value() == 0x13579BDF,
         "RRX register-offset uses carry-extended right shift");

  constexpr std::uint32_t strb_r1_base_plus_5 = kCondAl | kSingleDataTransferImmediate |
                                                kPreIndexed | kUp | kByteTransfer | rn(6) |
                                                rd(1) | offset12(5);
  constexpr std::uint32_t ldrb_r9_base_plus_5 = kCondAl | kSingleDataTransferImmediate |
                                                kPreIndexed | kUp | kByteTransfer | kLoad |
                                                rn(6) | rd(9) | offset12(5);
  cpu.set_register(1, 0xAABBCCDD);
  cpu.set_register(6, 0x02000000);
  expect(cpu.execute_arm(strb_r1_base_plus_5, memory) == ExecuteStatus::executed,
         "execute STRB immediate");
  expect_elapsed(Arm7tdmi::estimate_arm_elapsed_cycles(strb_r1_base_plus_5, 0x05000005).value(),
                 2, false, true, "STRB palette elapsed cycle estimate uses byte timing");
  expect(memory.read8(0x02000005).value_or(0) == 0xDD, "STRB writes low byte");
  expect(cpu.execute_arm(ldrb_r9_base_plus_5, memory) == ExecuteStatus::executed,
         "execute LDRB immediate");
  expect(cpu.register_value(9) == 0xDD, "LDRB zero-extends byte");

  constexpr std::uint32_t strh_r1_base_plus_6 =
      kCondAl | kHalfwordDataTransferImmediate | kPreIndexed | kUp | rn(6) | rd(1) |
      halfword_offset(6);
  constexpr std::uint32_t ldrh_r10_base_plus_6 = kCondAl | kHalfwordDataTransferImmediate |
                                                 kPreIndexed | kUp | kLoad | rn(6) | rd(10) |
                                                 halfword_offset(6);
  expect(Arm7tdmi::can_decode_halfword_data_transfer_immediate(strh_r1_base_plus_6),
         "STRH immediate decodes");
  const auto decoded_halfword =
      Arm7tdmi::decode_halfword_data_transfer_immediate(strh_r1_base_plus_6);
  expect(!decoded_halfword.load, "STRH load bit decodes");
  expect(!decoded_halfword.signed_transfer, "STRH signed bit decodes");
  expect(decoded_halfword.halfword, "STRH halfword bit decodes");
  expect(decoded_halfword.pre_index, "STRH pre-index bit decodes");
  expect(decoded_halfword.up, "STRH up bit decodes");
  expect(decoded_halfword.rn == 6, "STRH Rn decodes");
  expect(decoded_halfword.rd == 1, "STRH Rd decodes");
  expect(decoded_halfword.offset == 6, "STRH offset decodes");
  expect(cpu.execute_arm(strh_r1_base_plus_6, memory) == ExecuteStatus::executed,
         "execute STRH immediate");
  expect(memory.read16(0x02000006).value_or(0) == 0xCCDD, "STRH writes low halfword");
  expect(cpu.execute_arm(ldrh_r10_base_plus_6, memory) == ExecuteStatus::executed,
         "execute LDRH immediate");
  expect(cpu.register_value(10) == 0xCCDD, "LDRH zero-extends halfword");
  constexpr std::uint32_t ldrh_r10_base_plus_7 = kCondAl | kHalfwordDataTransferImmediate |
                                                 kPreIndexed | kUp | kLoad | rn(6) | rd(10) |
                                                 halfword_offset(7);
  expect(cpu.execute_arm(ldrh_r10_base_plus_7, memory) == ExecuteStatus::executed,
         "execute unaligned LDRH immediate");
  expect(cpu.register_value(10) == 0xDD0000CC,
         "unaligned LDRH rotates aligned halfword right by 8");

  constexpr std::uint32_t strh_unaligned =
      kCondAl | kHalfwordDataTransferImmediate | kPreIndexed | kUp | rn(6) | rd(1) |
      halfword_offset(7);
  expect(cpu.execute_arm(strh_unaligned, memory) == ExecuteStatus::executed,
         "execute unaligned STRH immediate");
  expect(memory.read16(0x02000006).value_or(0) == 0xCCDD,
         "unaligned STRH aligns destination down");

  expect(memory.write8(0x02000008, 0x80), "prepare LDRSB fixture");
  constexpr std::uint32_t ldrsb_r11_base_plus_8 =
      kCondAl | kSignedByteDataTransferImmediate | kPreIndexed | kUp | kLoad | rn(6) | rd(11) |
      halfword_offset(8);
  expect(Arm7tdmi::can_decode_halfword_data_transfer_immediate(ldrsb_r11_base_plus_8),
         "LDRSB immediate decodes");
  const auto decoded_signed_byte =
      Arm7tdmi::decode_halfword_data_transfer_immediate(ldrsb_r11_base_plus_8);
  expect(decoded_signed_byte.load, "LDRSB load bit decodes");
  expect(decoded_signed_byte.signed_transfer, "LDRSB signed bit decodes");
  expect(!decoded_signed_byte.halfword, "LDRSB byte shape decodes");
  expect(cpu.execute_arm(ldrsb_r11_base_plus_8, memory) == ExecuteStatus::executed,
         "execute LDRSB immediate");
  expect(cpu.register_value(11) == 0xFFFFFF80, "LDRSB sign-extends byte");

  expect(memory.write16(0x0200000A, 0x8001), "prepare LDRSH fixture");
  constexpr std::uint32_t ldrsh_r12_base_plus_10 =
      kCondAl | kSignedHalfwordDataTransferImmediate | kPreIndexed | kUp | kLoad | rn(6) |
      rd(12) | halfword_offset(10);
  expect(Arm7tdmi::can_decode_halfword_data_transfer_immediate(ldrsh_r12_base_plus_10),
         "LDRSH immediate decodes");
  const auto decoded_signed_halfword =
      Arm7tdmi::decode_halfword_data_transfer_immediate(ldrsh_r12_base_plus_10);
  expect(decoded_signed_halfword.load, "LDRSH load bit decodes");
  expect(decoded_signed_halfword.signed_transfer, "LDRSH signed bit decodes");
  expect(decoded_signed_halfword.halfword, "LDRSH halfword shape decodes");
  expect(cpu.execute_arm(ldrsh_r12_base_plus_10, memory) == ExecuteStatus::executed,
         "execute LDRSH immediate");
  expect(cpu.register_value(12) == 0xFFFF8001, "LDRSH sign-extends halfword");

  constexpr std::uint32_t ldrsh_unaligned =
      kCondAl | kSignedHalfwordDataTransferImmediate | kPreIndexed | kUp | kLoad | rn(6) |
      rd(13) | halfword_offset(11);
  expect(cpu.execute_arm(ldrsh_unaligned, memory) == ExecuteStatus::executed,
         "execute unaligned LDRSH immediate");
  expect(cpu.register_value(13) == 0xFFFFFF80,
         "unaligned LDRSH sign-extends addressed byte");

  constexpr std::uint32_t strh_r1_pre_writeback =
      kCondAl | kHalfwordDataTransferImmediate | kPreIndexed | kUp | kWriteBack | rn(6) |
      rd(1) | halfword_offset(2);
  cpu.set_register(1, 0x1234ABCD);
  cpu.set_register(6, 0x02000090);
  expect(cpu.execute_arm(strh_r1_pre_writeback, memory) == ExecuteStatus::executed,
         "execute STRH pre-index writeback");
  expect(memory.read16(0x02000092).value_or(0) == 0xABCD,
         "STRH pre-index writeback writes offset address");
  expect(cpu.register_value(6) == 0x02000092, "STRH pre-index writeback updates Rn");

  expect(memory.write16(0x02000092, 0x2468), "prepare LDRH post-index fixture");
  constexpr std::uint32_t ldrh_r10_post_plus_4 =
      kCondAl | kHalfwordDataTransferImmediate | kUp | kLoad | rn(6) | rd(10) |
      halfword_offset(4);
  expect(cpu.execute_arm(ldrh_r10_post_plus_4, memory) == ExecuteStatus::executed,
         "execute LDRH post-index");
  expect(cpu.register_value(10) == 0x2468, "LDRH post-index reads base address");
  expect(cpu.register_value(6) == 0x02000096, "LDRH post-index updates Rn after load");

  constexpr std::uint32_t strh_r1_base_plus_r2 =
      kCondAl | kHalfwordDataTransferRegister | kPreIndexed | kUp | rn(6) | rd(1) | rm(2);
  constexpr std::uint32_t ldrh_r10_base_minus_r2 =
      kCondAl | kHalfwordDataTransferRegister | kPreIndexed | kLoad | rn(6) | rd(10) | rm(2);
  cpu.set_register(1, 0xDEADBEEF);
  cpu.set_register(2, 8);
  cpu.set_register(6, 0x02000040);
  expect(Arm7tdmi::can_decode_halfword_data_transfer_register(strh_r1_base_plus_r2),
         "STRH register-offset decodes");
  const auto decoded_halfword_register =
      Arm7tdmi::decode_halfword_data_transfer_register(strh_r1_base_plus_r2,
                                                       cpu.register_value(2));
  expect(!decoded_halfword_register.load, "STRH register-offset load bit decodes");
  expect(!decoded_halfword_register.signed_transfer,
         "STRH register-offset signed bit decodes");
  expect(decoded_halfword_register.halfword, "STRH register-offset halfword bit decodes");
  expect(decoded_halfword_register.pre_index, "STRH register-offset pre-index bit decodes");
  expect(decoded_halfword_register.up, "STRH register-offset up bit decodes");
  expect(decoded_halfword_register.offset == 8, "STRH register-offset computes offset");
  expect(cpu.execute_arm(strh_r1_base_plus_r2, memory) == ExecuteStatus::executed,
         "execute STRH register-offset");
  expect(memory.read16(0x02000048).value_or(0) == 0xBEEF,
         "STRH register-offset writes low halfword");

  expect(memory.write16(0x02000038, 0x1357), "prepare LDRH register-offset fixture");
  expect(cpu.execute_arm(ldrh_r10_base_minus_r2, memory) == ExecuteStatus::executed,
         "execute LDRH register-offset down");
  expect(cpu.register_value(10) == 0x1357, "LDRH register-offset zero-extends halfword");

  constexpr std::uint32_t ldrsb_r11_base_plus_r3 =
      kCondAl | kSignedByteDataTransferRegister | kPreIndexed | kUp | kLoad | rn(6) |
      rd(11) | rm(3);
  cpu.set_register(3, 12);
  expect(memory.write8(0x0200004C, 0xF0), "prepare LDRSB register-offset fixture");
  expect(Arm7tdmi::can_decode_halfword_data_transfer_register(ldrsb_r11_base_plus_r3),
         "LDRSB register-offset decodes");
  const auto decoded_signed_byte_register =
      Arm7tdmi::decode_halfword_data_transfer_register(ldrsb_r11_base_plus_r3,
                                                       cpu.register_value(3));
  expect(decoded_signed_byte_register.load, "LDRSB register-offset load bit decodes");
  expect(decoded_signed_byte_register.signed_transfer,
         "LDRSB register-offset signed bit decodes");
  expect(!decoded_signed_byte_register.halfword, "LDRSB register-offset byte shape decodes");
  expect(decoded_signed_byte_register.offset == 12, "LDRSB register-offset computes offset");
  expect(cpu.execute_arm(ldrsb_r11_base_plus_r3, memory) == ExecuteStatus::executed,
         "execute LDRSB register-offset");
  expect(cpu.register_value(11) == 0xFFFFFFF0, "LDRSB register-offset sign-extends byte");

  constexpr std::uint32_t ldrsh_r12_base_minus_r4 =
      kCondAl | kSignedHalfwordDataTransferRegister | kPreIndexed | kLoad | rn(6) |
      rd(12) | rm(4);
  cpu.set_register(4, 12);
  expect(memory.write16(0x02000034, 0x9001), "prepare LDRSH register-offset fixture");
  expect(Arm7tdmi::can_decode_halfword_data_transfer_register(ldrsh_r12_base_minus_r4),
         "LDRSH register-offset decodes");
  const auto decoded_signed_halfword_register =
      Arm7tdmi::decode_halfword_data_transfer_register(ldrsh_r12_base_minus_r4,
                                                       cpu.register_value(4));
  expect(decoded_signed_halfword_register.load, "LDRSH register-offset load bit decodes");
  expect(decoded_signed_halfword_register.signed_transfer,
         "LDRSH register-offset signed bit decodes");
  expect(decoded_signed_halfword_register.halfword,
         "LDRSH register-offset halfword shape decodes");
  expect(decoded_signed_halfword_register.offset == 12, "LDRSH register-offset computes offset");
  expect(cpu.execute_arm(ldrsh_r12_base_minus_r4, memory) == ExecuteStatus::executed,
         "execute LDRSH register-offset");
  expect(cpu.register_value(12) == 0xFFFF9001,
         "LDRSH register-offset sign-extends halfword");

  constexpr std::uint32_t add_r9_r1_r2_lsl_2 =
      kCondAl | opcode(0x4) | rn(1) | rd(9) | shift_imm(2) | shift_type(0) | rm(2);
  cpu.set_register(1, 10);
  cpu.set_register(2, 3);
  expect(Arm7tdmi::can_decode_data_processing_register_shift(add_r9_r1_r2_lsl_2),
         "ADD register-shift decodes");
  expect_cycles(Arm7tdmi::estimate_arm_cycles(add_r9_r1_r2_lsl_2).value(), 1, 0, 0, false,
                "ADD immediate-shift cycle estimate has no extra internal cycle");
  const auto decoded_shift =
      Arm7tdmi::decode_data_processing_register_shift(add_r9_r1_r2_lsl_2, cpu.register_value(2));
  expect(decoded_shift.operand2 == 12, "LSL immediate shift computes operand2");
  expect(cpu.execute_arm(add_r9_r1_r2_lsl_2) == ExecuteStatus::executed,
         "execute ADD register-shift");
  expect(cpu.register_value(9) == 22, "ADD register-shift writes Rd");

  constexpr std::uint32_t movs_r10_r11_lsr_1 =
      kCondAl | opcode(0xD) | kSetFlags | rd(10) | shift_imm(1) | shift_type(1) | rm(11);
  cpu.set_register(11, 3);
  expect(cpu.execute_arm(movs_r10_r11_lsr_1) == ExecuteStatus::executed,
         "execute MOVS LSR immediate");
  expect(cpu.register_value(10) == 1, "MOVS LSR writes shifted value");
  expect(cpu.carry(), "MOVS LSR updates carry from shifter");

  constexpr std::uint32_t movs_r12_r13_asr_1 =
      kCondAl | opcode(0xD) | kSetFlags | rd(12) | shift_imm(1) | shift_type(2) | rm(13);
  cpu.set_register(13, 0x80000000);
  expect(cpu.execute_arm(movs_r12_r13_asr_1) == ExecuteStatus::executed,
         "execute MOVS ASR immediate");
  expect(cpu.register_value(12) == 0xC0000000, "MOVS ASR sign-extends");
  expect(cpu.negative(), "MOVS ASR updates N flag");
  expect(!cpu.carry(), "MOVS ASR updates carry from shifted-out bit");

  constexpr std::uint32_t movs_r0_r1_ror_1 =
      kCondAl | opcode(0xD) | kSetFlags | rd(0) | shift_imm(1) | shift_type(3) | rm(1);
  cpu.set_register(1, 1);
  expect(cpu.execute_arm(movs_r0_r1_ror_1) == ExecuteStatus::executed,
         "execute MOVS ROR immediate");
  expect(cpu.register_value(0) == 0x80000000, "MOVS ROR rotates value");
  expect(cpu.carry(), "MOVS ROR updates carry from result bit 31");

  constexpr std::uint32_t movs_r0_r1_rrx =
      kCondAl | opcode(0xD) | kSetFlags | rd(0) | shift_type(3) | rm(1);
  expect(Arm7tdmi::can_decode_data_processing_register_shift(movs_r0_r1_rrx),
         "RRX data-processing shift decodes");
  expect(cpu.set_cpsr(0x20000013), "prepare carry for RRX data-processing");
  cpu.set_register(1, 3);
  expect(cpu.execute_arm(movs_r0_r1_rrx) == ExecuteStatus::executed,
         "execute MOVS RRX immediate-shift alias");
  expect(cpu.register_value(0) == 0x80000001, "RRX shifts carry into bit 31");
  expect(cpu.carry(), "RRX carry-out uses old bit 0");
  expect(cpu.negative(), "RRX updates N flag through MOVS");

  constexpr std::uint32_t add_r2_r1_r3_lsl_r4 =
      kCondAl | opcode(0x4) | rn(1) | rd(2) | rs(4) | shift_type(0) | 0x10 | rm(3);
  cpu.set_register(1, 1);
  cpu.set_register(3, 2);
  cpu.set_register(4, 5);
  expect(Arm7tdmi::can_decode_data_processing_register_shift(add_r2_r1_r3_lsl_r4),
         "ADD register-controlled shift decodes");
  expect_cycles(Arm7tdmi::estimate_arm_cycles(add_r2_r1_r3_lsl_r4).value(), 1, 0, 1,
                false, "ADD register-controlled shift cycle estimate charges internal cycle");
  const auto decoded_register_shift =
      Arm7tdmi::decode_data_processing_register_shift(add_r2_r1_r3_lsl_r4,
                                                      cpu.register_value(3),
                                                      cpu.register_value(4));
  expect(decoded_register_shift.operand2 == 64, "register LSL computes operand2");
  expect(cpu.execute_arm(add_r2_r1_r3_lsl_r4) == ExecuteStatus::executed,
         "execute ADD register-controlled shift");
  expect(cpu.register_value(2) == 65, "ADD register-controlled shift writes Rd");

  constexpr std::uint32_t movs_r5_r6_lsr_r7 =
      kCondAl | opcode(0xD) | kSetFlags | rd(5) | rs(7) | shift_type(1) | 0x10 | rm(6);
  cpu.set_register(6, 0x80000000);
  cpu.set_register(7, 32);
  expect(cpu.execute_arm(movs_r5_r6_lsr_r7) == ExecuteStatus::executed,
         "execute MOVS register LSR");
  expect(cpu.register_value(5) == 0, "register LSR by 32 produces zero");
  expect(cpu.zero(), "register LSR updates Z flag");
  expect(cpu.carry(), "register LSR by 32 updates carry from bit 31");

  constexpr std::uint32_t movs_r8_r9_asr_r10 =
      kCondAl | opcode(0xD) | kSetFlags | rd(8) | rs(10) | shift_type(2) | 0x10 | rm(9);
  cpu.set_register(9, 0x80000000);
  cpu.set_register(10, 40);
  expect(cpu.execute_arm(movs_r8_r9_asr_r10) == ExecuteStatus::executed,
         "execute MOVS register ASR");
  expect(cpu.register_value(8) == 0xFFFFFFFF, "register ASR >=32 sign-fills");
  expect(cpu.negative(), "register ASR updates N flag");
  expect(cpu.carry(), "register ASR >=32 updates carry from sign bit");

  constexpr std::uint32_t cmp_r0_0_for_shift =
      kCondAl | kDataProcessingImmediate | opcode(0xA) | kSetFlags | rn(0) | imm(0);
  constexpr std::uint32_t movs_r11_r12_lsl_r13 =
      kCondAl | opcode(0xD) | kSetFlags | rd(11) | rs(13) | shift_type(0) | 0x10 | rm(12);
  cpu.set_register(0, 0);
  cpu.set_register(12, 0x12345678);
  cpu.set_register(13, 0);
  expect(cpu.execute_arm(cmp_r0_0_for_shift) == ExecuteStatus::executed,
         "prepare carry set for zero register shift");
  expect(cpu.carry(), "carry is set before zero register shift");
  expect(cpu.execute_arm(movs_r11_r12_lsl_r13) == ExecuteStatus::executed,
         "execute MOVS zero register shift");
  expect(cpu.register_value(11) == 0x12345678, "zero register shift preserves operand");
  expect(cpu.carry(), "zero register shift preserves carry");

  constexpr std::uint32_t orr_r6_r1_0f =
      kCondAl | kDataProcessingImmediate | opcode(0xC) | rn(1) | rd(6) | imm(0x0F);
  cpu.set_register(1, 0xF0);
  expect(Arm7tdmi::can_decode_data_processing_immediate(orr_r6_r1_0f), "ORR immediate decodes");
  expect(Arm7tdmi::decode_data_processing_immediate(orr_r6_r1_0f).opcode == ArmOpcode::orr,
         "ORR opcode decodes");
  expect(cpu.execute_arm(orr_r6_r1_0f) == ExecuteStatus::executed, "execute ORR immediate");
  expect(cpu.register_value(6) == 0xFF, "ORR writes Rd");

  constexpr std::uint32_t eor_r7_r1_r2_lsl_4 =
      kCondAl | opcode(0x1) | rn(1) | rd(7) | shift_imm(4) | shift_type(0) | rm(2);
  cpu.set_register(1, 0xAA);
  cpu.set_register(2, 0x0F);
  expect(cpu.execute_arm(eor_r7_r1_r2_lsl_4) == ExecuteStatus::executed,
         "execute EOR register-shift");
  expect(cpu.register_value(7) == 0x5A, "EOR register-shift writes Rd");

  constexpr std::uint32_t bic_r8_r1_0f =
      kCondAl | kDataProcessingImmediate | opcode(0xE) | rn(1) | rd(8) | imm(0x0F);
  cpu.set_register(1, 0xFF);
  expect(cpu.execute_arm(bic_r8_r1_0f) == ExecuteStatus::executed, "execute BIC immediate");
  expect(cpu.register_value(8) == 0xF0, "BIC writes Rd");

  constexpr std::uint32_t mvn_r9_0f =
      kCondAl | kDataProcessingImmediate | opcode(0xF) | rd(9) | imm(0x0F);
  expect(cpu.execute_arm(mvn_r9_0f) == ExecuteStatus::executed, "execute MVN immediate");
  expect(cpu.register_value(9) == 0xFFFFFFF0, "MVN writes inverted operand2");

  constexpr std::uint32_t tst_r1_0f =
      kCondAl | kDataProcessingImmediate | opcode(0x8) | kSetFlags | rn(1) | rd(10) | imm(0x0F);
  cpu.set_register(1, 0xF0);
  cpu.set_register(10, 0x12345678);
  expect(cpu.execute_arm(tst_r1_0f) == ExecuteStatus::executed, "execute TST immediate");
  expect(cpu.zero(), "TST updates Z flag");
  expect(cpu.register_value(10) == 0x12345678, "TST does not write Rd");

  constexpr std::uint32_t teq_r1_f0 =
      kCondAl | kDataProcessingImmediate | opcode(0x9) | kSetFlags | rn(1) | rd(10) | imm(0xF0);
  expect(cpu.execute_arm(teq_r1_f0) == ExecuteStatus::executed, "execute TEQ immediate");
  expect(cpu.zero(), "TEQ updates Z flag");
  expect(cpu.register_value(10) == 0x12345678, "TEQ does not write Rd");

  constexpr std::uint32_t ands_r11_r1_r2_lsr_1 =
      kCondAl | opcode(0x0) | kSetFlags | rn(1) | rd(11) | shift_imm(1) | shift_type(1) | rm(2);
  cpu.set_register(1, 0xFF);
  cpu.set_register(2, 3);
  expect(cpu.execute_arm(ands_r11_r1_r2_lsr_1) == ExecuteStatus::executed,
         "execute ANDS register-shift");
  expect(cpu.register_value(11) == 1, "ANDS register-shift writes Rd");
  expect(!cpu.zero(), "ANDS updates Z flag");
  expect(cpu.carry(), "ANDS updates carry from shifter");

  constexpr std::uint32_t rsb_r12_r1_10 =
      kCondAl | kDataProcessingImmediate | opcode(0x3) | rn(1) | rd(12) | imm(10);
  cpu.set_register(1, 3);
  expect(cpu.execute_arm(rsb_r12_r1_10) == ExecuteStatus::executed, "execute RSB immediate");
  expect(cpu.register_value(12) == 7, "RSB subtracts Rn from operand2");

  constexpr std::uint32_t cmp_r0_0 =
      kCondAl | kDataProcessingImmediate | opcode(0xA) | kSetFlags | rn(0) | imm(0);
  constexpr std::uint32_t adcs_r13_r1_1 =
      kCondAl | kDataProcessingImmediate | opcode(0x5) | kSetFlags | rn(1) | rd(13) | imm(1);
  cpu.set_register(0, 0);
  cpu.set_register(1, 0xFFFFFFFF);
  expect(cpu.execute_arm(cmp_r0_0) == ExecuteStatus::executed, "prepare carry set for ADC");
  expect(cpu.carry(), "CMP prepares carry set");
  expect(cpu.execute_arm(adcs_r13_r1_1) == ExecuteStatus::executed, "execute ADCS immediate");
  expect(cpu.register_value(13) == 1, "ADCS includes carry input");
  expect(cpu.carry(), "ADCS sets carry output");
  expect(!cpu.zero(), "ADCS updates Z flag");

  constexpr std::uint32_t sbcs_r14_r1_0 =
      kCondAl | kDataProcessingImmediate | opcode(0x6) | kSetFlags | rn(1) | rd(14) | imm(0);
  constexpr std::uint32_t cmp_r0_2 =
      kCondAl | kDataProcessingImmediate | opcode(0xA) | kSetFlags | rn(0) | imm(2);
  cpu.set_register(0, 1);
  cpu.set_register(1, 0);
  expect(cpu.execute_arm(cmp_r0_2) == ExecuteStatus::executed, "prepare carry clear for SBC");
  expect(!cpu.carry(), "CMP prepares carry clear");
  expect(cpu.execute_arm(sbcs_r14_r1_0) == ExecuteStatus::executed, "execute SBCS immediate");
  expect(cpu.register_value(14) == 0xFFFFFFFF, "SBC subtracts inverted carry borrow");
  expect(cpu.negative(), "SBCS updates N flag");
  expect(!cpu.carry(), "SBCS clears carry when borrow occurs");

  constexpr std::uint32_t rsc_r4_r1_10 =
      kCondAl | kDataProcessingImmediate | opcode(0x7) | rn(1) | rd(4) | imm(10);
  cpu.set_register(0, 0);
  cpu.set_register(1, 3);
  expect(cpu.execute_arm(cmp_r0_0) == ExecuteStatus::executed, "prepare carry set for RSC");
  expect(cpu.execute_arm(rsc_r4_r1_10) == ExecuteStatus::executed, "execute RSC immediate");
  expect(cpu.register_value(4) == 7, "RSC subtracts Rn from operand2 with carry set");

  constexpr std::uint32_t cmn_r1_1 =
      kCondAl | kDataProcessingImmediate | opcode(0xB) | kSetFlags | rn(1) | rd(5) | imm(1);
  cpu.set_register(1, 0xFFFFFFFF);
  cpu.set_register(5, 0x12345678);
  expect(cpu.execute_arm(cmn_r1_1) == ExecuteStatus::executed, "execute CMN immediate");
  expect(cpu.zero(), "CMN updates Z flag");
  expect(cpu.carry(), "CMN sets carry for addition overflow");
  expect(cpu.register_value(5) == 0x12345678, "CMN does not write Rd");

  constexpr std::uint32_t mul_r2_r3_r4 =
      kCondAl | kMultiply | multiply_rd(2) | multiply_rs(4) | rm(3);
  cpu.set_register(3, 7);
  cpu.set_register(4, 6);
  expect(Arm7tdmi::can_decode_multiply(mul_r2_r3_r4), "MUL decodes");
  expect_cycles(Arm7tdmi::estimate_arm_cycles(mul_r2_r3_r4).value(), 1, 0, 0, true,
                "MUL cycle estimate is data-dependent");
  expect_elapsed(Arm7tdmi::estimate_arm_elapsed_cycles(mul_r2_r3_r4, 0x02000000).value(), 1,
                 true, false, "MUL elapsed cycle estimate preserves data-dependent flag");
  const auto decoded_multiply = Arm7tdmi::decode_multiply(mul_r2_r3_r4);
  expect(!decoded_multiply.accumulate, "MUL accumulate bit decodes clear");
  expect(decoded_multiply.rd == 2, "MUL Rd decodes");
  expect(decoded_multiply.rs == 4, "MUL Rs decodes");
  expect(decoded_multiply.rm == 3, "MUL Rm decodes");
  expect(cpu.execute_arm(mul_r2_r3_r4) == ExecuteStatus::executed, "execute MUL");
  expect(cpu.register_value(2) == 42, "MUL writes low 32-bit product");
  cpu.reset_elapsed_cycles();
  cpu.set_register(3, 2);
  cpu.set_register(4, 0x00345678);
  expect(cpu.step_arm(mul_r2_r3_r4).elapsed_cycles == 4,
         "MUL step uses signed early-out multiplier timing");

  constexpr std::uint32_t mla_r5_r7_r8_r6 = kCondAl | kMultiplyAccumulate |
                                            multiply_rd(5) | multiply_rn(6) |
                                            multiply_rs(8) | rm(7);
  cpu.set_register(6, 3);
  cpu.set_register(7, 4);
  cpu.set_register(8, 5);
  const auto decoded_accumulate = Arm7tdmi::decode_multiply(mla_r5_r7_r8_r6);
  expect(decoded_accumulate.accumulate, "MLA accumulate bit decodes set");
  expect(decoded_accumulate.rn == 6, "MLA Rn decodes");
  expect(cpu.execute_arm(mla_r5_r7_r8_r6) == ExecuteStatus::executed, "execute MLA");
  expect(cpu.register_value(5) == 23, "MLA adds accumulator");
  cpu.reset_elapsed_cycles();
  cpu.set_register(6, 3);
  cpu.set_register(7, 4);
  cpu.set_register(8, 0xFF);
  expect(cpu.step_arm(mla_r5_r7_r8_r6).elapsed_cycles == 3,
         "MLA step includes accumulate internal cycle");

  constexpr std::uint32_t muls_r9_r10_r11 =
      kCondAl | kMultiply | kSetFlags | multiply_rd(9) | multiply_rs(10) | rm(11);
  cpu.set_register(10, 123);
  cpu.set_register(11, 0);
  expect(cpu.execute_arm(muls_r9_r10_r11) == ExecuteStatus::executed, "execute MULS");
  expect(cpu.register_value(9) == 0, "MULS writes product");
  expect(cpu.zero(), "MULS updates Z flag");
  expect(!cpu.negative(), "MULS clears N flag for zero result");

  constexpr std::uint32_t mulne_r12_r3_r4 =
      kCondNe | kMultiply | multiply_rd(12) | multiply_rs(4) | rm(3);
  cpu.set_register(12, 0xFEEDFACE);
  expect(cpu.execute_arm(mulne_r12_r3_r4) == ExecuteStatus::skipped_condition,
         "conditional MUL skips when condition fails");
  expect(cpu.register_value(12) == 0xFEEDFACE, "skipped MUL preserves Rd");

  constexpr std::uint32_t umull_r2_r3_r4_r5 = kCondAl | kMultiplyLong |
                                              multiply_long_rd_hi(3) |
                                              multiply_long_rd_lo(2) |
                                              multiply_rs(5) | rm(4);
  cpu.set_register(4, 0xFFFFFFFF);
  cpu.set_register(5, 2);
  expect(Arm7tdmi::can_decode_multiply_long(umull_r2_r3_r4_r5), "UMULL decodes");
  expect_cycles(Arm7tdmi::estimate_arm_cycles(umull_r2_r3_r4_r5).value(), 1, 0, 1, true,
                "UMULL cycle estimate includes long multiply internal cycle");
  const auto decoded_umull = Arm7tdmi::decode_multiply_long(umull_r2_r3_r4_r5);
  expect(!decoded_umull.signed_multiply, "UMULL signed bit decodes clear");
  expect(!decoded_umull.accumulate, "UMULL accumulate bit decodes clear");
  expect(decoded_umull.rd_lo == 2, "UMULL RdLo decodes");
  expect(decoded_umull.rd_hi == 3, "UMULL RdHi decodes");
  expect(decoded_umull.rs == 5, "UMULL Rs decodes");
  expect(decoded_umull.rm == 4, "UMULL Rm decodes");
  expect(!Arm7tdmi::can_decode_multiply(umull_r2_r3_r4_r5),
         "multiply-long shape does not decode as basic multiply");
  expect(cpu.execute_arm(umull_r2_r3_r4_r5) == ExecuteStatus::executed, "execute UMULL");
  expect(cpu.register_value(2) == 0xFFFFFFFE, "UMULL writes low word");
  expect(cpu.register_value(3) == 0x00000001, "UMULL writes high word");
  cpu.reset_elapsed_cycles();
  cpu.set_register(4, 2);
  cpu.set_register(5, 0xFF000000);
  expect(cpu.step_arm(umull_r2_r3_r4_r5).elapsed_cycles == 6,
         "UMULL step uses unsigned early-out multiplier timing");
  constexpr std::uint32_t umulls_r2_r3_r4_r5 = umull_r2_r3_r4_r5 | kSetFlags;
  cpu.set_register(4, 0xFFFFFFFF);
  cpu.set_register(5, 0xFFFFFFFF);
  expect(cpu.set_cpsr(0x00000013), "clear flags before UMULLS carry-one case");
  expect(cpu.execute_arm(umulls_r2_r3_r4_r5) == ExecuteStatus::executed,
         "execute UMULLS carry-one case");
  expect(cpu.carry(), "UMULLS sets ARM7TDMI multiply carry quirk");
  cpu.set_register(4, 0x80000000);
  cpu.set_register(5, 0xFFFFFFFF);
  expect(cpu.set_cpsr(0x20000013), "seed carry before UMULLS carry-zero case");
  expect(cpu.execute_arm(umulls_r2_r3_r4_r5) == ExecuteStatus::executed,
         "execute UMULLS carry-zero case");
  expect(!cpu.carry(), "UMULLS overwrites carry with ARM7TDMI multiply carry quirk");

  constexpr std::uint32_t umlal_r2_r3_r4_r5 = kCondAl | kMultiplyLongAccumulate |
                                              multiply_long_rd_hi(3) |
                                              multiply_long_rd_lo(2) |
                                              multiply_rs(5) | rm(4);
  cpu.set_register(2, 3);
  cpu.set_register(3, 0);
  cpu.set_register(4, 4);
  cpu.set_register(5, 5);
  const auto decoded_umlal = Arm7tdmi::decode_multiply_long(umlal_r2_r3_r4_r5);
  expect(decoded_umlal.accumulate, "UMLAL accumulate bit decodes set");
  expect(cpu.execute_arm(umlal_r2_r3_r4_r5) == ExecuteStatus::executed, "execute UMLAL");
  expect(cpu.register_value(2) == 23, "UMLAL adds low accumulator");
  expect(cpu.register_value(3) == 0, "UMLAL writes high accumulator");

  constexpr std::uint32_t smull_r6_r7_r8_r9 = kCondAl | kSignedMultiplyLong |
                                              multiply_long_rd_hi(7) |
                                              multiply_long_rd_lo(6) |
                                              multiply_rs(9) | rm(8);
  cpu.set_register(8, 0xFFFFFFFF);
  cpu.set_register(9, 2);
  const auto decoded_smull = Arm7tdmi::decode_multiply_long(smull_r6_r7_r8_r9);
  expect(decoded_smull.signed_multiply, "SMULL signed bit decodes set");
  expect(cpu.execute_arm(smull_r6_r7_r8_r9) == ExecuteStatus::executed, "execute SMULL");
  expect(cpu.register_value(6) == 0xFFFFFFFE, "SMULL writes signed low word");
  expect(cpu.register_value(7) == 0xFFFFFFFF, "SMULL sign-extends high word");
  cpu.reset_elapsed_cycles();
  cpu.set_register(8, 2);
  cpu.set_register(9, 0xFF000000);
  expect(cpu.step_arm(smull_r6_r7_r8_r9).elapsed_cycles == 5,
         "SMULL step uses signed early-out multiplier timing");
  constexpr std::uint32_t smulls_r6_r7_r8_r9 = smull_r6_r7_r8_r9 | kSetFlags;
  cpu.set_register(8, 0);
  cpu.set_register(9, 0x80000000);
  expect(cpu.set_cpsr(0x00000013), "clear flags before SMULLS carry-one case");
  expect(cpu.execute_arm(smulls_r6_r7_r8_r9) == ExecuteStatus::executed,
         "execute SMULLS carry-one case");
  expect(cpu.zero(), "SMULLS updates Z for zero 64-bit result");
  expect(cpu.carry(), "SMULLS sets ARM7TDMI multiply carry quirk");
  cpu.set_register(8, 0xFFFFFFFF);
  cpu.set_register(9, 0x80000000);
  expect(cpu.set_cpsr(0x20000013), "seed carry before SMULLS carry-zero case");
  expect(cpu.execute_arm(smulls_r6_r7_r8_r9) == ExecuteStatus::executed,
         "execute SMULLS carry-zero case");
  expect(!cpu.carry(), "SMULLS overwrites carry with ARM7TDMI multiply carry quirk");

  constexpr std::uint32_t smlals_r6_r7_r8_r9 = kCondAl | kSignedMultiplyLongAccumulate |
                                               kSetFlags | multiply_long_rd_hi(7) |
                                               multiply_long_rd_lo(6) |
                                               multiply_rs(9) | rm(8);
  cpu.set_register(6, 1);
  cpu.set_register(7, 0);
  cpu.set_register(8, 0xFFFFFFFF);
  cpu.set_register(9, 2);
  expect(cpu.execute_arm(smlals_r6_r7_r8_r9) == ExecuteStatus::executed, "execute SMLALS");
  expect(cpu.register_value(6) == 0xFFFFFFFF, "SMLAL adds signed low word");
  expect(cpu.register_value(7) == 0xFFFFFFFF, "SMLAL adds signed high word");
  expect(cpu.negative(), "SMLALS updates N flag from 64-bit result");
  expect(!cpu.zero(), "SMLALS clears Z for nonzero result");

  constexpr std::uint32_t umulleq_r10_r11_r4_r5 = kCondEq | kMultiplyLong |
                                                  multiply_long_rd_hi(11) |
                                                  multiply_long_rd_lo(10) |
                                                  multiply_rs(5) | rm(4);
  cpu.set_register(10, 0xAAAAAAAA);
  cpu.set_register(11, 0xBBBBBBBB);
  expect(cpu.execute_arm(umulleq_r10_r11_r4_r5) == ExecuteStatus::skipped_condition,
         "conditional UMULL skips when condition fails");
  expect(cpu.register_value(10) == 0xAAAAAAAA, "skipped UMULL preserves RdLo");
  expect(cpu.register_value(11) == 0xBBBBBBBB, "skipped UMULL preserves RdHi");

  constexpr std::uint32_t unsupported_instruction = kCondAl | 0x0C000000;
  constexpr std::uint32_t unsupported_branch_condition = kCondNv | kBranch | branch_offset(1);
  constexpr std::uint32_t unsupported_condition =
      kCondNv | kDataProcessingImmediate | opcode(0xD) | rd(5) | imm(1);
  expect(cpu.execute_arm(unsupported_instruction) == ExecuteStatus::unsupported,
         "unsupported instruction is reported");
  expect(!Arm7tdmi::estimate_arm_cycles(unsupported_instruction).has_value(),
         "unsupported instruction has no cycle estimate");
  expect(!Arm7tdmi::estimate_arm_elapsed_cycles(ldr_r5_base_plus_4, 0x08000000).has_value(),
         "cartridge-space LDR has no elapsed cycle estimate while cartridge bus is out of scope");
  expect(cpu.execute_arm(unsupported_branch_condition) == ExecuteStatus::unsupported,
         "reserved branch condition is reported");
  expect(cpu.execute_arm(unsupported_condition) == ExecuteStatus::unsupported,
         "reserved condition is reported");

  std::cout << "arm7tdmi_test: PASS\n";
  return 0;
}
