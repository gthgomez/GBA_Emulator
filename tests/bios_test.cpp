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
  expect(hle_known.status == BiosSwiStatus::unimplemented_service,
         "HLE policy fails known service until implemented");
  expect(!hle_known.handled, "HLE policy does not claim unimplemented service handled");
  expect(!hle_known.requires_bios_bytes, "HLE policy does not require BIOS bytes");
  const auto hle_unknown = bios.handle_swi(BiosController::decode_thumb_swi(thumb_swi_unknown));
  expect(hle_unknown.status == BiosSwiStatus::unimplemented_service,
         "HLE policy fails unknown service cleanly");
  expect(hle_unknown.call.service == 0x80, "unknown service is reported to caller");

  std::cout << "bios_test: PASS\n";
  return 0;
}
