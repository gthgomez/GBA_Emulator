#include "gba/core/bios.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  using gba::core::BiosController;
  using gba::core::BiosExecutionMode;
  using gba::core::BiosHleConstants;
  using gba::core::BiosIrqDispatchTiming;
  using gba::core::BiosSwiSource;
  using gba::core::BiosSwiStatus;

  constexpr std::uint32_t arm_swi_div = 0xEF060000U;
  constexpr std::uint32_t arm_swi_low_comment_only = 0xEF000006U;
  constexpr std::uint16_t thumb_swi_div = 0xDF06U;
  constexpr std::uint16_t thumb_swi_unknown = 0xDF80U;

  const auto arm_div = BiosController::decode_arm_swi(arm_swi_div);
  expect(arm_div.source == BiosSwiSource::arm, "ARM SWI source decodes");
  expect(arm_div.raw_comment == 0x00060000U, "ARM SWI raw comment preserves 24 bits");
  expect(arm_div.service == 0x06, "ARM SWI service decodes from upper comment byte");
  expect(BiosController::decode_arm_swi(arm_swi_low_comment_only).service == 0,
         "ARM SWI low comment byte is not the GBA service number");

  const auto thumb_div = BiosController::decode_thumb_swi(thumb_swi_div);
  expect(thumb_div.source == BiosSwiSource::thumb, "Thumb SWI source decodes");
  expect(thumb_div.raw_comment == 0x06, "Thumb SWI raw comment preserves imm8");
  expect(thumb_div.service == 0x06, "Thumb SWI service decodes from imm8");
  expect(BiosController::known_gba_service(0x00), "SoftReset service is known");
  expect(BiosController::known_gba_service(0x2A), "highest documented GBA service is known");
  expect(!BiosController::known_gba_service(0x80), "out-of-range GBA service is unknown");

  BiosController bios;
  expect(bios.mode() == BiosExecutionMode::no_bios, "BIOS policy defaults to no BIOS");
  const auto no_bios = bios.handle_swi(thumb_div);
  expect(no_bios.status == BiosSwiStatus::trap_to_vector,
         "no-BIOS SWI policy traps to vector");
  expect(!no_bios.handled, "no-BIOS SWI policy does not claim service handled");
  expect(!no_bios.requires_bios_bytes, "no-BIOS SWI policy does not require BIOS bytes");

  bios.set_mode(BiosExecutionMode::caller_provided_bios);
  const auto caller_bios = bios.handle_swi(arm_div);
  expect(caller_bios.status == BiosSwiStatus::trap_to_vector,
         "caller-provided BIOS policy traps to vector");
  expect(!caller_bios.handled, "caller-provided BIOS policy does not emulate service");
  expect(caller_bios.requires_bios_bytes,
         "caller-provided BIOS policy explicitly requires external BIOS bytes later");

  bios.set_mode(BiosExecutionMode::hle);
  const auto hle_known = bios.handle_swi(thumb_div);
  expect(hle_known.status == BiosSwiStatus::handled,
         "HLE policy claims known service for scheduler dispatch");
  expect(hle_known.handled, "HLE policy reports known service handled");
  expect(!hle_known.requires_bios_bytes, "HLE policy does not require BIOS bytes");
  const auto hle_unknown = bios.handle_swi(BiosController::decode_thumb_swi(thumb_swi_unknown));
  expect(hle_unknown.status == BiosSwiStatus::unimplemented_service,
         "HLE policy fails unknown service cleanly");
  expect(hle_unknown.call.service == 0x80, "unknown service is reported to caller");

  expect(BiosHleConstants::kIrqVectorAddress == 0x00000018U,
         "BIOS IRQ vector address matches hardware");
  expect(BiosHleConstants::kUserIrqHandlerPointer == 0x03007FFCU,
         "libgba IRQ handler pointer address matches IWRAM convention");
  expect(BiosHleConstants::kIrqDispatchCycles == 21U,
         "BIOS IRQ dispatch HLE baseline cycle budget");

  BiosIrqDispatchTiming baseline_timing{};
  expect(BiosController::irq_dispatch_cycles(baseline_timing) == 21U,
         "baseline IRQ dispatch uses first-entry cycle budget");
  BiosIrqDispatchTiming reentry_timing{.reentry = true};
  expect(BiosController::irq_dispatch_cycles(reentry_timing) == 29U,
         "IRQ reentry dispatch uses extended cycle budget");

  expect(BiosController::hle_div_quotient(7, 3) == 2, "SWI Div HLE quotient");
  expect(BiosController::hle_div_remainder(7, 3) == 1, "SWI Div HLE remainder");
  expect(BiosController::hle_div_quotient(0xFFFFFFFF, 0) == -1,
         "SWI Div HLE denominator-zero negative numerator saturates quotient");
  expect(BiosController::hle_sqrt(16) == 4, "SWI Sqrt HLE returns integer root");

  std::int32_t arc_scratch_r1 = 0;
  std::int32_t arc_scratch_r3 = 0;
  expect(BiosController::hle_arc_tan(0, &arc_scratch_r1, &arc_scratch_r3) == 0,
         "SWI ArcTan HLE zero input returns zero");
  expect(arc_scratch_r3 == 0xA2F9, "SWI ArcTan HLE writes BIOS polynomial factor scratch");

  std::int32_t arc2_scratch_r1 = 0;
  expect(BiosController::hle_arc_tan2(1, 1, &arc2_scratch_r1) == 0x2000,
         "SWI ArcTan2 HLE 1,1 returns eighth-turn angle");

  std::cout << "bios_test: PASS\n";
  return 0;
}
