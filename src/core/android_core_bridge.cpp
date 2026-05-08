#include "gba/core/android_core_bridge.hpp"

#include "gba/core/arm7tdmi.hpp"
#include "gba/core/core_session.hpp"

#include <mutex>
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
    std::vector<std::uint8_t> rom(bytes, bytes + size);
    const std::lock_guard<std::mutex> lock(mutex_);
    session_.reset();
    if (!session_.memory().load_game_pak_rom(rom)) {
      return AndroidBridgeStatus::rom_rejected;
    }
    session_.cpu().set_register(Arm7tdmi::kPc, 0x08000000U);
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

AndroidCoreBridge* bridge_from_handle(void* handle) {
  return static_cast<AndroidCoreBridge*>(handle);
}

}  // namespace

extern "C" void* gba_android_core_create() {
  return new AndroidCoreBridge();
}

extern "C" void gba_android_core_destroy(void* handle) {
  delete bridge_from_handle(handle);
}

extern "C" AndroidBridgeStatus gba_android_core_reset(void* handle) {
  AndroidCoreBridge* bridge = bridge_from_handle(handle);
  if (bridge == nullptr) {
    return AndroidBridgeStatus::null_handle;
  }
  return bridge->reset();
}

extern "C" AndroidBridgeStatus gba_android_core_load_rom(void* handle,
                                                         const std::uint8_t* bytes,
                                                         std::size_t size) {
  AndroidCoreBridge* bridge = bridge_from_handle(handle);
  if (bridge == nullptr) {
    return AndroidBridgeStatus::null_handle;
  }
  return bridge->load_rom(bytes, size);
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
  *result = bridge->run(max_steps);
  return result->status;
}

extern "C" std::uint64_t gba_android_core_state_hash(void* handle) {
  AndroidCoreBridge* bridge = bridge_from_handle(handle);
  if (bridge == nullptr) {
    return 0;
  }
  return bridge->state_hash();
}

}  // namespace gba::core
