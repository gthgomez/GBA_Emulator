#pragma once

// Shared helpers for the core verifier binaries under tests/. Each helper
// replaces byte-identical per-file copies (anonymous-namespace definitions)
// that previously duplicated these semantics in every test translation unit.

#include "gba/core/interrupt_controller.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

inline void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

constexpr std::uint16_t irq_bit(gba::core::InterruptSource source) {
  return static_cast<std::uint16_t>(1U << static_cast<std::uint8_t>(source));
}

inline void write_word(std::vector<std::uint8_t>& bytes, std::size_t offset,
                       std::uint32_t value) {
  bytes.at(offset + 0U) = static_cast<std::uint8_t>(value & 0xFFU);
  bytes.at(offset + 1U) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  bytes.at(offset + 2U) = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  bytes.at(offset + 3U) = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

inline void put_rom_halfword(std::vector<std::uint8_t>& rom, std::size_t offset,
                             std::uint16_t value) {
  rom.at(offset) = static_cast<std::uint8_t>(value & 0xFFU);
  rom.at(offset + 1) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}
