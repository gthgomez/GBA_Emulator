#include "gba/core/android_core_bridge.hpp"
#include "gba/core/memory_bus.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

#include "test_helpers.hpp"

int main() {
  using gba::core::AndroidBridgeStatus;

  expect(gba::core::gba_android_core_reset(nullptr) == AndroidBridgeStatus::null_handle,
         "null reset rejects");
  expect(gba::core::gba_android_core_run(nullptr, 1, nullptr) ==
             AndroidBridgeStatus::invalid_argument,
         "null run result rejects");

  // T9: the hash accessor reports a dedicated status for a bad handle and
  // never asks callers to interpret a zero hash as failure.
  std::uint64_t null_hash = 0xABCD'1234ULL;
  expect(gba::core::gba_android_core_state_hash(nullptr, &null_hash) ==
             AndroidBridgeStatus::invalid_handle,
         "null state hash returns invalid_handle");
  expect(null_hash == 0xABCD'1234ULL,
         "failed state hash leaves out-param unmodified");
  expect(gba::core::gba_android_core_state_hash(nullptr, nullptr) ==
             AndroidBridgeStatus::invalid_argument,
         "null out-param rejects");

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
  std::uint64_t loaded_hash = 0;
  expect(gba::core::gba_android_core_state_hash(handle, &loaded_hash) ==
             AndroidBridgeStatus::ok,
         "state hash succeeds on live handle");
  {
    const std::vector<std::uint8_t> oversized(gba::core::MemoryBus::kGamePakRomWindowSize + 1);
    expect(gba::core::gba_android_core_load_rom(handle, oversized.data(), oversized.size()) ==
               AndroidBridgeStatus::rom_rejected,
           "oversized ROM rejects");
    std::uint64_t hash_after_rejection = 0;
    expect(gba::core::gba_android_core_state_hash(handle, &hash_after_rejection) ==
               AndroidBridgeStatus::ok && hash_after_rejection == loaded_hash,
           "oversized ROM preserves the active session");

    // An impossible size must be rejected before forming the input range.
    const std::uint8_t sentinel = 0;
    expect(gba::core::gba_android_core_load_rom(
               handle, &sentinel, std::numeric_limits<std::size_t>::max()) ==
               AndroidBridgeStatus::rom_rejected,
           "unrepresentable ROM length is rejected before access");
  }
  gba::core::AndroidBridgeRunResult run;
  expect(gba::core::gba_android_core_run(handle, 3, &run) == AndroidBridgeStatus::ok,
         "bridge run returns ok status");
  expect(run.status == AndroidBridgeStatus::ok, "bridge run succeeds");
  expect(run.executed_steps == 3, "bridge run executes bounded steps");
  expect(run.final_pc == 0x0800000CU, "bridge final PC advances deterministically");
  expect(run.state_hash != 0 && run.state_hash != loaded_hash,
         "bridge reports updated state hash");
  std::uint64_t stepped_hash = 0;
  expect(gba::core::gba_android_core_state_hash(handle, &stepped_hash) ==
             AndroidBridgeStatus::ok,
         "post-run state hash succeeds");
  expect(stepped_hash == run.state_hash,
         "accessor hash matches run-reported hash");
  expect(gba::core::gba_android_core_reset(handle) == AndroidBridgeStatus::ok,
         "bridge reset succeeds");
  gba::core::gba_android_core_destroy(handle);
  gba::core::gba_android_core_destroy(nullptr);

  std::cout << "android_core_bridge_test: PASS\n";
  return 0;
}
