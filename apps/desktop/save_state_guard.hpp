#pragma once

// Host-side save-state load policy for the desktop frontend.
//
// The core SaveStateCodec is a complete-machine snapshot: a blob carries its
// own ROM bytes, so decode_into() happily materializes a state captured from a
// *different* ROM into any session. The desktop host always has a specific ROM
// loaded, so it must additionally refuse a state that belongs to another game
// or changes the selected cartridge-save protocol.
//
// SDL-free on purpose so tests/desktop_save_state_guard_test.cpp can verify
// the policy headlessly with core sources only.

#include "gba/core/core_session.hpp"
#include "gba/core/save_state_codec.hpp"

#include <cstdint>
#include <vector>

namespace gba::desktop {

enum class SaveStateLoadStatus : std::uint8_t {
  ok,
  decode_failed,  // corrupt payload, bad magic, unsupported version, hash mismatch
  wrong_rom,      // valid state, but captured from a different ROM
  wrong_save_type,  // would replace the host's selected backup protocol
};

// Decodes `blob` into `scratch` and commits it into `live` only when it both
// decodes successfully and matches the ROM and save type of `live`.
//
// `live` is never mutated on failure: decode_into() itself is transactional,
// and the ROM-identity check runs against `scratch` before any commit.
inline SaveStateLoadStatus load_save_state_for_session(
    gba::core::CoreSession& live, const std::vector<std::uint8_t>& blob,
    gba::core::CoreSession& scratch) {
  const gba::core::SaveStateDecodeResult decoded =
      gba::core::SaveStateCodec::decode_into(scratch, blob);
  if (decoded.status != gba::core::SaveStateDecodeStatus::ok) {
    return SaveStateLoadStatus::decode_failed;
  }
  if (scratch.memory().export_game_pak_rom() !=
      live.memory().export_game_pak_rom()) {
    return SaveStateLoadStatus::wrong_rom;
  }
  if (scratch.memory().game_pak_save_type() != live.memory().game_pak_save_type()) {
    return SaveStateLoadStatus::wrong_save_type;
  }
  live.load_state(scratch.save_state());
  return SaveStateLoadStatus::ok;
}

}  // namespace gba::desktop
