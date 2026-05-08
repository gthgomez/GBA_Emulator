#include "gba/core/bios.hpp"

namespace gba::core {

BiosController::BiosController() {
  reset();
}

void BiosController::reset() {
  mode_ = BiosExecutionMode::no_bios;
}

void BiosController::set_mode(BiosExecutionMode mode) {
  mode_ = mode;
}

BiosExecutionMode BiosController::mode() const {
  return mode_;
}

BiosSwiResult BiosController::handle_swi(BiosSwiCall call) const {
  switch (mode_) {
    case BiosExecutionMode::no_bios:
      return {mode_, BiosSwiStatus::trap_to_vector, call, false, false};
    case BiosExecutionMode::caller_provided_bios:
      return {mode_, BiosSwiStatus::trap_to_vector, call, false, true};
    case BiosExecutionMode::hle:
      return {mode_, known_gba_service(call.service) ? BiosSwiStatus::handled
                                                     : BiosSwiStatus::unimplemented_service,
              call, known_gba_service(call.service), false};
  }
  return {mode_, BiosSwiStatus::unimplemented_service, call, false, false};
}

BiosSwiCall BiosController::decode_arm_swi(std::uint32_t instruction) {
  const std::uint32_t comment = instruction & 0x00FFFFFFU;
  return {BiosSwiSource::arm, comment,
          static_cast<std::uint8_t>((comment >> 16) & 0xFFU)};
}

BiosSwiCall BiosController::decode_thumb_swi(std::uint16_t instruction) {
  const std::uint8_t comment = static_cast<std::uint8_t>(instruction & 0x00FFU);
  return {BiosSwiSource::thumb, comment, comment};
}

bool BiosController::known_gba_service(std::uint8_t service) {
  return service <= 0x2AU;
}

}  // namespace gba::core
