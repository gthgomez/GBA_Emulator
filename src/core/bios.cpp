#include "gba/core/bios.hpp"

#include <cmath>
#include <limits>

namespace gba::core {

namespace {

constexpr std::uint32_t kBiosHleLongTimerNonDataIrqDispatchExtraCycles = 2;
constexpr std::uint32_t kBiosHleVeryLongTimerNonDataIrqDispatchAdvanceCycles = 3;
constexpr std::uint32_t kBiosHleSpacedTimerDataIrqDispatchAdvanceCycles = 1;
constexpr std::uint32_t kBiosHleLongTimerChainedPostReturnExtraCycles = 5;
constexpr std::uint32_t kBiosHleSlowTimer0ActiveReturnExtraCycles = 1;
constexpr std::uint32_t kLooseTimerIoIrqDispatchGapCycles = 12;

[[nodiscard]] std::int32_t wrap_i32(std::uint32_t value) {
  return static_cast<std::int32_t>(value);
}

[[nodiscard]] std::uint32_t wrap_u32(std::int32_t value) {
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::int32_t wrap_mul_i32(std::int32_t left, std::int32_t right) {
  return wrap_i32(wrap_u32(left) * wrap_u32(right));
}

[[nodiscard]] std::int32_t wrap_shl_i32(std::int32_t value, std::uint8_t shift) {
  return wrap_i32(wrap_u32(value) << shift);
}

}  // namespace

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

std::uint32_t BiosController::irq_dispatch_cycles(const BiosIrqDispatchTiming& timing) {
  const bool long_timer_chained_post_return_dispatch =
      timing.chained_post_return && timing.timer0_interrupt_requested &&
      timing.timer0_reload <= 0xFF80U;
  const bool spaced_long_timer_data_dispatch =
      long_timer_chained_post_return_dispatch &&
      timing.chained_post_return_spaced_data && timing.timer0_reload >= 0xF800U &&
      timing.timer0_reload < 0xFF80U;

  std::uint32_t chained_dispatch_cycles = BiosHleConstants::kIrqChainedPostReturnDispatchCycles;
  if (long_timer_chained_post_return_dispatch && !timing.chained_post_return_data) {
    chained_dispatch_cycles += kBiosHleLongTimerNonDataIrqDispatchExtraCycles;
    if (timing.timer0_reload <= 0x8000U &&
        timing.timer_io_access_gap_cycles >= kLooseTimerIoIrqDispatchGapCycles) {
      chained_dispatch_cycles -= kBiosHleVeryLongTimerNonDataIrqDispatchAdvanceCycles;
    }
  } else if (spaced_long_timer_data_dispatch) {
    chained_dispatch_cycles -= kBiosHleSpacedTimerDataIrqDispatchAdvanceCycles;
  }

  if (timing.reentry) {
    return BiosHleConstants::kIrqReentryDispatchCycles;
  }
  if (timing.chained_post_return) {
    return chained_dispatch_cycles;
  }
  if (timing.post_return) {
    return BiosHleConstants::kIrqPostReturnDispatchCycles;
  }
  return BiosHleConstants::kIrqDispatchCycles;
}

std::uint32_t BiosController::irq_return_cycles(bool long_timer_chained_return,
                                                 bool slow_timer0_active_return) {
  return BiosHleConstants::kIrqReturnCycles +
         (long_timer_chained_return ? kBiosHleLongTimerChainedPostReturnExtraCycles : 0U) +
         (slow_timer0_active_return ? kBiosHleSlowTimer0ActiveReturnExtraCycles : 0U);
}

std::int32_t BiosController::hle_div_quotient(std::int32_t numerator,
                                            std::int32_t denominator) {
  if (denominator == 0) {
    return numerator < 0 ? -1 : 1;
  }
  if (denominator == -1 && numerator == std::numeric_limits<std::int32_t>::min()) {
    return std::numeric_limits<std::int32_t>::min();
  }
  return static_cast<std::int32_t>(static_cast<std::int64_t>(numerator) /
                                   static_cast<std::int64_t>(denominator));
}

std::int32_t BiosController::hle_div_remainder(std::int32_t numerator,
                                               std::int32_t denominator) {
  if (denominator == 0) {
    return numerator;
  }
  if (denominator == -1 && numerator == std::numeric_limits<std::int32_t>::min()) {
    return 0;
  }
  return static_cast<std::int32_t>(static_cast<std::int64_t>(numerator) %
                                   static_cast<std::int64_t>(denominator));
}

std::uint32_t BiosController::hle_div_abs_scratch(std::int32_t numerator,
                                                  std::int32_t denominator) {
  if (denominator == 0) {
    return 1U;
  }
  if (denominator == -1 && numerator == std::numeric_limits<std::int32_t>::min()) {
    return wrap_u32(std::numeric_limits<std::int32_t>::min());
  }
  const std::int64_t quotient =
      static_cast<std::int64_t>(numerator) / static_cast<std::int64_t>(denominator);
  const std::int64_t absolute = quotient < 0 ? -quotient : quotient;
  return static_cast<std::uint32_t>(absolute);
}

std::uint32_t BiosController::hle_div_base_cycles(std::int32_t numerator,
                                                  std::int32_t denominator) {
  if (denominator == 0 ||
      (denominator == -1 && numerator == std::numeric_limits<std::int32_t>::min())) {
    return 330;
  }
  const auto abs64 = [](std::int32_t value) {
    const std::int64_t wide = value;
    return wide < 0 ? -wide : wide;
  };
  return abs64(numerator) < abs64(denominator) ? 70U : 330U;
}

std::uint32_t BiosController::hle_sqrt(std::uint32_t input) {
  return static_cast<std::uint32_t>(std::sqrt(static_cast<double>(input)));
}

std::uint32_t BiosController::hle_sqrt_base_cycles(std::uint32_t value) {
  if (value == 0) {
    return 99;
  }
  return value <= 0xFFU ? 214U : 1130U;
}

std::int32_t BiosController::hle_arc_tan(std::int32_t value, std::int32_t* scratch_r1,
                                         std::int32_t* scratch_r3) {
  const std::int32_t square = wrap_mul_i32(value, value);
  std::int32_t polynomial = -(square >> 14);
  std::int32_t factor = ((wrap_mul_i32(0xA9, polynomial)) >> 14) + 0x390;
  factor = ((wrap_mul_i32(factor, polynomial)) >> 14) + 0x91C;
  factor = ((wrap_mul_i32(factor, polynomial)) >> 14) + 0xFB6;
  factor = ((wrap_mul_i32(factor, polynomial)) >> 14) + 0x16AA;
  factor = ((wrap_mul_i32(factor, polynomial)) >> 14) + 0x2081;
  factor = ((wrap_mul_i32(factor, polynomial)) >> 14) + 0x3651;
  factor = ((wrap_mul_i32(factor, polynomial)) >> 14) + 0xA2F9;
  if (scratch_r1 != nullptr) {
    *scratch_r1 = polynomial;
  }
  if (scratch_r3 != nullptr) {
    *scratch_r3 = factor;
  }
  return static_cast<std::int16_t>(wrap_mul_i32(value, factor) >> 16);
}

std::int32_t BiosController::hle_arc_tan2(std::int32_t x, std::int32_t y,
                                        std::int32_t* scratch_r1) {
  const std::int64_t wide_x = x;
  const std::int64_t wide_y = y;
  if (y == 0) {
    return x >= 0 ? 0 : 0x8000;
  }
  if (x == 0) {
    return y >= 0 ? 0x4000 : 0xC000;
  }
  if (y >= 0) {
    if (x >= 0) {
      if (x >= y) {
        return hle_arc_tan(wrap_shl_i32(y, 14) / x, scratch_r1, nullptr);
      }
    } else if (-wide_x >= wide_y) {
      return hle_arc_tan(wrap_shl_i32(y, 14) / x, scratch_r1, nullptr) + 0x8000;
    }
    return 0x4000 - hle_arc_tan(wrap_shl_i32(x, 14) / y, scratch_r1, nullptr);
  }

  if (x <= 0) {
    if (-wide_x > -wide_y) {
      return hle_arc_tan(wrap_shl_i32(y, 14) / x, scratch_r1, nullptr) + 0x8000;
    }
  } else if (wide_x >= -wide_y) {
    return hle_arc_tan(wrap_shl_i32(y, 14) / x, scratch_r1, nullptr) + 0x10000;
  }
  return 0xC000 - hle_arc_tan(wrap_shl_i32(x, 14) / y, scratch_r1, nullptr);
}

BiosController::AffineMatrix BiosController::hle_affine_matrix(
    std::int16_t scale_x, std::int16_t scale_y, std::uint16_t rotation) {
  const double theta =
      static_cast<double>(rotation >> 8U) * (2.0 * 3.141592653589793 / 256.0);
  const double sin_t = std::sin(theta);
  const double cos_t = std::cos(theta);
  const double sx = static_cast<double>(scale_x);
  const double sy = static_cast<double>(scale_y);
  AffineMatrix matrix{};
  matrix.pa = static_cast<std::int16_t>(std::lround((sx * cos_t) / 256.0));
  matrix.pb = static_cast<std::int16_t>(std::lround(-(sx * sin_t) / 256.0));
  matrix.pc = static_cast<std::int16_t>(std::lround((sy * sin_t) / 256.0));
  matrix.pd = static_cast<std::int16_t>(std::lround((sy * cos_t) / 256.0));
  return matrix;
}

}  // namespace gba::core
