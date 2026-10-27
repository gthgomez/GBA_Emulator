#include "../apps/desktop/frame_interval_stats.hpp"
#include <cstdlib>
#include <iostream>

void expect(bool ok, const char* message) {
  if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main() {
  gba::desktop::FrameIntervalStats stats;
  expect(stats.count() == 0 && stats.percentile_upper_ms(95) == 0, "empty sample");
  for (int i = 0; i < 95; ++i) stats.add(16.5);
  for (int i = 0; i < 4; ++i) stats.add(26.1);
  stats.add(1200.0);
  expect(stats.count() == 100, "sample count");
  expect(stats.over_25ms() == 5, "slow interval count");
  expect(stats.percentile_upper_ms(95) == 17.0, "p95 upper bound");
  expect(stats.percentile_upper_ms(99) == 27.0, "p99 upper bound");
  expect(stats.percentile_upper_ms(100) == 1200.0, "overflow upper bound");
  expect(stats.maximum_ms() == 1200.0, "maximum interval");
  std::cout << "desktop_frame_interval_test: PASS\n";
}
