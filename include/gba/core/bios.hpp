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

 private:
  BiosExecutionMode mode_;
};

}  // namespace gba::core
