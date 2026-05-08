#pragma once

#include <cstddef>
#include <cstdint>

namespace gba::core {

enum class AndroidBridgeStatus : std::uint8_t {
  ok,
  null_handle,
  invalid_argument,
  rom_rejected,
};

struct AndroidBridgeRunResult {
  AndroidBridgeStatus status = AndroidBridgeStatus::ok;
  std::uint32_t executed_steps = 0;
  std::uint32_t final_pc = 0;
  std::uint64_t state_hash = 0;
};

extern "C" {

void* gba_android_core_create();
void gba_android_core_destroy(void* handle);
AndroidBridgeStatus gba_android_core_reset(void* handle);
AndroidBridgeStatus gba_android_core_load_rom(void* handle, const std::uint8_t* bytes,
                                              std::size_t size);
AndroidBridgeStatus gba_android_core_run(void* handle, std::uint32_t max_steps,
                                         AndroidBridgeRunResult* result);
std::uint64_t gba_android_core_state_hash(void* handle);

}

}  // namespace gba::core
