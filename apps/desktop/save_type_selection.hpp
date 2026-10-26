#pragma once

// Host policy only: the core still owns detection and backup-memory protocols.
#include "desktop_persistence.hpp"
#include "gba/core/memory_bus.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace gba::desktop {

inline const char* save_kind_name(gba::core::GamePakSaveType type) {
  using gba::core::GamePakSaveType;
  switch (type) {
    case GamePakSaveType::none: return "none";
    case GamePakSaveType::sram32k: return "sram32k";
    case GamePakSaveType::flash64k: return "flash64k";
    case GamePakSaveType::flash128k: return "flash128k";
    case GamePakSaveType::eeprom512: return "eeprom512";
    case GamePakSaveType::eeprom8k: return "eeprom8k";
    default: return "unknown";
  }
}

inline bool parse_save_type(std::string_view text,
                            std::optional<gba::core::GamePakSaveType>& selection) {
  if (text == "auto") {
    selection.reset();
    return true;
  }
  for (const auto type : {gba::core::GamePakSaveType::none,
                          gba::core::GamePakSaveType::sram32k,
                          gba::core::GamePakSaveType::flash64k,
                          gba::core::GamePakSaveType::flash128k,
                          gba::core::GamePakSaveType::eeprom512,
                          gba::core::GamePakSaveType::eeprom8k}) {
    if (text == save_kind_name(type)) {
      selection = type;
      return true;
    }
  }
  return false;
}

// Bind the CLI selection to the original file, including equivalent path
// spellings. A dialog/drop/switch to a different file goes back to auto.
inline std::optional<gba::core::GamePakSaveType> save_type_for_rom(
    const std::string& original_path, const std::string& incoming_path,
    std::optional<gba::core::GamePakSaveType> selection) {
  if (!selection || original_path.empty()) {
    return std::nullopt;
  }
  // The initial load must honor the explicit option even on filesystems that
  // cannot report file identity. Metadata is only needed for path aliases.
  if (original_path == incoming_path) {
    return selection;
  }
  std::error_code ec;
  const bool same_file = std::filesystem::equivalent(
      persistence::native_path(original_path), persistence::native_path(incoming_path), ec);
  return same_file && !ec ? selection : std::nullopt;
}

inline bool apply_save_type(gba::core::MemoryBus& memory,
                           std::optional<gba::core::GamePakSaveType> selection) {
  return !selection || memory.configure_game_pak_save(*selection);
}

}  // namespace gba::desktop
