#pragma once

#include <cstdint>

namespace gba::core {

enum class BiosExecutionMode : std::uint8_t {
  no_bios,
  caller_provided_bios,
  hle,
};

enum class BiosSwiSource : std::uint8_t {
  arm,
  thumb,
};

enum class BiosSwiStatus : std::uint8_t {
  trap_to_vector,
  handled,
  unimplemented_service,
};

struct BiosSwiCall {
  BiosSwiSource source;
  std::uint32_t raw_comment;
  std::uint8_t service;
};

struct BiosSwiResult {
  BiosExecutionMode mode;
  BiosSwiStatus status;
  BiosSwiCall call;
  bool handled;
  bool requires_bios_bytes;
};

// GBA BIOS / libgba conventions used by IRQ-dispatch and SWI HLE (no BIOS bytes bundled).
struct BiosHleConstants {
  static constexpr std::uint32_t kIrqVectorAddress = 0x00000018U;
  static constexpr std::uint32_t kSoftwareInterruptVectorAddress = 0x00000008U;
  static constexpr std::uint32_t kUserIrqHandlerPointer = 0x03007FFCU;
  static constexpr std::uint32_t kIrqReturnSentinelPc = 0x0FFFFF00U;

  static constexpr std::uint32_t kIrqDispatchCycles = 21;
  static constexpr std::uint32_t kIrqPostReturnDispatchCycles = 24;
  static constexpr std::uint32_t kIrqChainedPostReturnDispatchCycles = 26;
  static constexpr std::uint32_t kIrqReentryDispatchCycles = 29;
  static constexpr std::uint32_t kIrqReturnCycles = 3;
  static constexpr std::uint32_t kIntrWaitReturnCycles = 57;
  static constexpr std::uint32_t kVBlankIntrWaitReturnCycles = 515;
  static constexpr std::uint32_t kHblankHaltReturnCycles = 83;
};

struct BiosIrqDispatchTiming {
  bool reentry = false;
  bool post_return = false;
  bool chained_post_return = false;
  bool chained_post_return_data = false;
  bool chained_post_return_spaced_data = false;
  bool timer0_interrupt_requested = false;
  std::uint32_t timer0_reload = 0;
  std::uint32_t timer_io_access_gap_cycles = 0;
};

class BiosController {
 public:
  BiosController();

  void reset();
  void set_mode(BiosExecutionMode mode);
  [[nodiscard]] BiosExecutionMode mode() const;

  [[nodiscard]] BiosSwiResult handle_swi(BiosSwiCall call) const;

  [[nodiscard]] static BiosSwiCall decode_arm_swi(std::uint32_t instruction);
  [[nodiscard]] static BiosSwiCall decode_thumb_swi(std::uint16_t instruction);
  [[nodiscard]] static bool known_gba_service(std::uint8_t service);

  [[nodiscard]] static std::uint32_t irq_dispatch_cycles(
      const BiosIrqDispatchTiming& timing);

  [[nodiscard]] static std::uint32_t irq_return_cycles(bool long_timer_chained_return,
                                                       bool slow_timer0_active_return);

  [[nodiscard]] static std::int32_t hle_div_quotient(std::int32_t numerator,
                                                     std::int32_t denominator);
  [[nodiscard]] static std::int32_t hle_div_remainder(std::int32_t numerator,
                                                      std::int32_t denominator);
  [[nodiscard]] static std::uint32_t hle_div_abs_scratch(std::int32_t numerator,
                                                         std::int32_t denominator);
  [[nodiscard]] static std::uint32_t hle_div_base_cycles(std::int32_t numerator,
                                                         std::int32_t denominator);
  [[nodiscard]] static std::uint32_t hle_sqrt(std::uint32_t input);
  [[nodiscard]] static std::uint32_t hle_sqrt_base_cycles(std::uint32_t value);
  [[nodiscard]] static std::int32_t hle_arc_tan(std::int32_t value,
                                                std::int32_t* scratch_r1,
                                                std::int32_t* scratch_r3);
  [[nodiscard]] static std::int32_t hle_arc_tan2(std::int32_t x, std::int32_t y,
                                                 std::int32_t* scratch_r1);

  struct AffineMatrix {
    std::int16_t pa = 0;
    std::int16_t pb = 0;
    std::int16_t pc = 0;
    std::int16_t pd = 0;
  };

  [[nodiscard]] static AffineMatrix hle_affine_matrix(std::int16_t scale_x,
                                                      std::int16_t scale_y,
                                                      std::uint16_t rotation);

 private:
  BiosExecutionMode mode_;
};

}  // namespace gba::core
