#include "gba/core/android_core_bridge.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void write_word(std::vector<std::uint8_t>& bytes, std::size_t offset,
                std::uint32_t value) {
  bytes.at(offset + 0U) = static_cast<std::uint8_t>(value & 0xFFU);
  bytes.at(offset + 1U) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  bytes.at(offset + 2U) = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  bytes.at(offset + 3U) = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

}  // namespace

int main() {
  using gba::core::AndroidBridgeStatus;

  expect(gba::core::gba_android_core_reset(nullptr) == AndroidBridgeStatus::null_handle,
         "null reset rejects");
  expect(gba::core::gba_android_core_state_hash(nullptr) == 0,
         "null state hash returns zero");
  expect(gba::core::gba_android_core_run(nullptr, 1, nullptr) ==
             AndroidBridgeStatus::invalid_argument,
         "null run result rejects");

  void* handle = gba::core::gba_android_core_create();
  expect(handle != nullptr, "bridge handle creates");
  expect(gba::core::gba_android_core_load_rom(handle, nullptr, 4) ==
             AndroidBridgeStatus::invalid_argument,
         "null ROM bytes reject");

  constexpr std::uint32_t kAddR0R0Imm1 = 0xE2800001U;
  std::vector<std::uint8_t> rom(12);
  write_word(rom, 0, kAddR0R0Imm1);
  write_word(rom, 4, kAddR0R0Imm1);
  write_word(rom, 8, kAddR0R0Imm1);
  expect(gba::core::gba_android_core_load_rom(handle, rom.data(), rom.size()) ==
             AndroidBridgeStatus::ok,
         "bridge loads explicit ROM bytes");
  const std::uint64_t loaded_hash = gba::core::gba_android_core_state_hash(handle);
  gba::core::AndroidBridgeRunResult run;
  expect(gba::core::gba_android_core_run(handle, 3, &run) == AndroidBridgeStatus::ok,
         "bridge run returns ok status");
  expect(run.status == AndroidBridgeStatus::ok, "bridge run succeeds");
  expect(run.executed_steps == 3, "bridge run executes bounded steps");
  expect(run.final_pc == 0x0800000CU, "bridge final PC advances deterministically");
  expect(run.state_hash != 0 && run.state_hash != loaded_hash,
         "bridge reports updated state hash");
  expect(gba::core::gba_android_core_reset(handle) == AndroidBridgeStatus::ok,
         "bridge reset succeeds");
  gba::core::gba_android_core_destroy(handle);

  std::cout << "android_core_bridge_test: PASS\n";
  return 0;
}
