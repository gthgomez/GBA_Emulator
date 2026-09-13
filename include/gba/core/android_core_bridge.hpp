#pragma once

#include <cstddef>
#include <cstdint>

namespace gba::core {

enum class AndroidBridgeStatus : std::uint8_t {
  ok,
  null_handle,
  invalid_argument,
  rom_rejected,
  // Handle is non-null but refers to an object this bridge did not create;
  // returned instead of an ambiguous zero hash or partial result.
  invalid_handle,
  // An unexpected C++ exception escaped the internal call path and was
  // contained at the ABI boundary (e.g. bad_alloc while copying a ROM).
  internal_error,
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
// Writes the session state hash to *out_hash and returns ok. On failure
// (null/foreign handle) returns a status other than ok and leaves *out_hash
// unmodified; callers must never interpret a return of 0 as a failure signal.
AndroidBridgeStatus gba_android_core_state_hash(void* handle, std::uint64_t* out_hash);

}

}  // namespace gba::core
