#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace gba::desktop {

// Fixed-memory histogram of intervals between completed presentations.
// Ordinary buckets report an upper bound rounded up to whole milliseconds.
class FrameIntervalStats {
 public:
  void add(double milliseconds) {
    if (!std::isfinite(milliseconds) || milliseconds < 0) return;
    ++count_;
    if (milliseconds > 25.0) ++over_25ms_;
    maximum_ms_ = std::max(maximum_ms_, milliseconds);
    const auto bucket = milliseconds >= kOverflowMs
                            ? kOverflowMs
                            : static_cast<std::size_t>(std::ceil(milliseconds));
    ++buckets_[bucket];
  }

  [[nodiscard]] std::uint64_t count() const { return count_; }
  [[nodiscard]] std::uint64_t over_25ms() const { return over_25ms_; }
  [[nodiscard]] double maximum_ms() const { return maximum_ms_; }
  [[nodiscard]] double percentile_upper_ms(unsigned percentile) const {
    if (count_ == 0 || percentile == 0 || percentile > 100) return 0;
    const auto rank = (count_ / 100) * percentile +
                      ((count_ % 100) * percentile + 99) / 100;
    std::uint64_t cumulative = 0;
    for (std::size_t i = 0; i < buckets_.size(); ++i) {
      cumulative += buckets_[i];
      if (cumulative >= rank) return i == kOverflowMs ? maximum_ms_ : static_cast<double>(i);
    }
    return maximum_ms_;
  }

 private:
  static constexpr std::size_t kOverflowMs = 1000;
  std::array<std::uint64_t, kOverflowMs + 1> buckets_{};
  std::uint64_t count_ = 0;
  std::uint64_t over_25ms_ = 0;
  double maximum_ms_ = 0;
};

}  // namespace gba::desktop
