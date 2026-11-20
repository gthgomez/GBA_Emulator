#pragma once

#include "desktop_persistence.hpp"
#include "gba/core/memory_bus.hpp"

namespace gba::desktop {

inline bool has_gba_cartridge_header(const std::vector<std::uint8_t>& rom) {
  return rom.size() >= 0xC0 && rom[0xB2] == 0x96 &&
         gba::core::MemoryBus::cartridge_complement_valid(rom);
}

inline std::vector<std::uint8_t> read_gba_rom(const std::string& path) {
  return persistence::read_binary_file(path, gba::core::MemoryBus::kGamePakRomWindowSize);
}

}  // namespace gba::desktop
