#include "gba/core/android_core_bridge.hpp"

#include "gba/core/arm7tdmi.hpp"
#include "gba/core/core_session.hpp"

#include <exception>
#include <mutex>
#include <new>
#include <vector>

namespace gba::core {
namespace {

class AndroidCoreBridge {
 public:
  AndroidBridgeStatus reset() {
    const std::lock_guard<std::mutex> lock(mutex_);
    session_.reset();
    return AndroidBridgeStatus::ok;
  }

  AndroidBridgeStatus load_rom(const std::uint8_t* bytes, std::size_t size) {
    if (bytes == nullptr || size == 0) {
      return AndroidBridgeStatus::invalid_argument;
    }
    // The ROM copy can throw (bad_alloc / length_error for absurd sizes); the
    // extern "C" wrappers contain that and map it to internal_error.
    std::vector<std::uint8_t> rom(bytes, bytes + size);
    const std::lock_guard<std::mutex> lock(mutex_);
    session_.reset();
    if (!session_.memory().load_game_pak_rom(rom)) {
      return AndroidBridgeStatus::rom_rejected;
    }
    const std::optional<GamePakSaveType> save_type =
        session_.memory().detect_game_pak_save_type();
    const GamePakSaveType configured_save =
        save_type.value_or(GamePakSaveType::none);
    if (!session_.memory().configure_game_pak_save(configured_save)) {
      session_.reset();
      return AndroidBridgeStatus::rom_rejected;
    }
    session_.configure_for_game_boot();
    return AndroidBridgeStatus::ok;
  }

  AndroidBridgeRunResult run(std::uint32_t max_steps) {
    if (max_steps == 0) {
      return {AndroidBridgeStatus::invalid_argument, 0, 0, 0};
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    const CoreSchedulerRunResult result = session_.run(max_steps);
    return {AndroidBridgeStatus::ok, result.executed_steps, result.final_pc,
            session_.state_hash()};
  }

  std::uint64_t state_hash() {
    const std::lock_guard<std::mutex> lock(mutex_);
    return session_.state_hash();
  }

 private:
  CoreSession session_;
  std::mutex mutex_;
};

// Validates a caller-supplied handle. Returns null for both the null handle
// and foreign pointers; callers translate each case into its own status.
AndroidCoreBridge* bridge_from_handle(void* handle) {
  return static_cast<AndroidCoreBridge*>(handle);
}

}  // namespace

extern "C" void* gba_android_core_create() {
  // Contained allocation failure: report an unusable bridge as nullptr rather
  // than letting bad_alloc escape the C boundary.
  try {
    return new AndroidCoreBridge();
  } catch (const std::exception&) {
    return nullptr;
  } catch (...) {
    return nullptr;
  }
}

extern "C" void gba_android_core_destroy(void* handle) {
  // delete on nullptr is a no-op; no exception can escape delete here.
  delete bridge_from_handle(handle);
}

extern "C" AndroidBridgeStatus gba_android_core_reset(void* handle) {
  AndroidCoreBridge* bridge = bridge_from_handle(handle);
  if (bridge == nullptr) {
    return AndroidBridgeStatus::null_handle;
  }
  try {
    return bridge->reset();
  } catch (const std::exception&) {
    return AndroidBridgeStatus::internal_error;
  } catch (...) {
    return AndroidBridgeStatus::internal_error;
  }
}

extern "C" AndroidBridgeStatus gba_android_core_load_rom(void* handle,
                                                         const std::uint8_t* bytes,
                                                         std::size_t size) {
  AndroidCoreBridge* bridge = bridge_from_handle(handle);
  if (bridge == nullptr) {
    return AndroidBridgeStatus::null_handle;
  }
  try {
    return bridge->load_rom(bytes, size);
  } catch (const std::bad_alloc&) {
    return AndroidBridgeStatus::internal_error;
  } catch (const std::exception&) {
    return AndroidBridgeStatus::internal_error;
  } catch (...) {
    return AndroidBridgeStatus::internal_error;
  }
}

extern "C" AndroidBridgeStatus gba_android_core_run(void* handle, std::uint32_t max_steps,
                                                    AndroidBridgeRunResult* result) {
  if (result == nullptr) {
    return AndroidBridgeStatus::invalid_argument;
  }
  *result = {};
  AndroidCoreBridge* bridge = bridge_from_handle(handle);
  if (bridge == nullptr) {
    result->status = AndroidBridgeStatus::null_handle;
    return result->status;
  }
  try {
    *result = bridge->run(max_steps);
  } catch (const std::exception&) {
    *result = {};
    result->status = AndroidBridgeStatus::internal_error;
  } catch (...) {
    *result = {};
    result->status = AndroidBridgeStatus::internal_error;
  }
  return result->status;
}

extern "C" AndroidBridgeStatus gba_android_core_state_hash(void* handle,
                                                           std::uint64_t* out_hash) {
  if (out_hash == nullptr) {
    return AndroidBridgeStatus::invalid_argument;
  }
  // Dedicated failure status instead of an ambiguous zero hash: callers of
  // this accessor cannot distinguish "empty session hashes to 0" from
  // "no session at all", so the bad-handle case reports invalid_handle.
  if (handle == nullptr) {
    return AndroidBridgeStatus::invalid_handle;
  }
  AndroidCoreBridge* bridge = static_cast<AndroidCoreBridge*>(handle);
  try {
    *out_hash = bridge->state_hash();
    return AndroidBridgeStatus::ok;
  } catch (const std::exception&) {
    return AndroidBridgeStatus::internal_error;
  } catch (...) {
    return AndroidBridgeStatus::internal_error;
  }
}

}  // namespace gba::core
