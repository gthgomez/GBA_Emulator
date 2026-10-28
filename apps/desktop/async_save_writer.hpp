#pragma once

#include "desktop_persistence.hpp"

#include <chrono>
#include <exception>
#include <future>
#include <optional>
#include <string>
#include <utility>

namespace gba::desktop {

// One owned encoding/write job. The caller supplies an encoder that owns all
// its inputs; it must never reference the live emulator. Keep the completed
// result until collected so a second request cannot hide an earlier failure.
class AsyncSaveWriter {
 public:
  struct Result {
    std::string path;
    bool written;
    std::string error;
  };

  [[nodiscard]] bool busy() const { return pending_.valid(); }

  template <class Encoder>
  bool start(std::string path, Encoder encoder) {
    if (busy()) return false;
    pending_ = std::async(std::launch::async,
                         [path = std::move(path), encoder = std::move(encoder)]() mutable {
      try {
        const auto blob = encoder();
        const bool written = persistence::write_file_atomic(path, blob);
        return Result{path, written, written ? "" : "atomic write failed"};
      } catch (const std::exception& error) {
        return Result{path, false, error.what()};
      } catch (...) {
        return Result{path, false, "unknown save worker error"};
      }
    });
    return true;
  }

  std::optional<Result> poll() {
    if (!busy() || pending_.wait_for(std::chrono::seconds(0)) !=
                       std::future_status::ready) return std::nullopt;
    return pending_.get();
  }

  // Drain before loading a state or exiting. The future also joins on
  // destruction, but owners must collect the result to report write errors.
  std::optional<Result> finish() {
    if (!busy()) return std::nullopt;
    return pending_.get();
  }

 private:
  std::future<Result> pending_;
};

}  // namespace gba::desktop
