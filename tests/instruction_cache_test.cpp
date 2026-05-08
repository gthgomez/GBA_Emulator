#include "gba/core/instruction_cache.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  using gba::core::InstructionCache;
  using gba::core::PredecodedInstructionSet;
  using gba::core::PredecodedOperation;

  constexpr std::uint32_t kAddR0R0Imm1 = 0xE2800001U;
  constexpr std::uint16_t kThumbAddR0Imm1 = 0x3001U;

  const gba::core::PredecodedInstruction arm =
      gba::core::predecode_arm(0x08000000, kAddR0R0Imm1);
  expect(arm.supported, "ARM ADD predecodes as supported");
  expect(arm.operation == PredecodedOperation::arm_data_processing_immediate,
         "ARM ADD operation class is stable");
  expect(arm.width_bytes == 4, "ARM width is recorded");

  const gba::core::PredecodedInstruction thumb =
      gba::core::predecode_thumb(0x08000000, kThumbAddR0Imm1);
  expect(thumb.supported, "Thumb ADD predecodes as supported");
  expect(thumb.instruction_set == PredecodedInstructionSet::thumb,
         "Thumb instruction set is recorded");

  InstructionCache cache(4);
  expect(cache.lookup_or_decode_arm(0x08000000, kAddR0R0Imm1).supported,
         "cache decodes ARM miss");
  expect(cache.misses() == 1 && cache.hits() == 0, "first lookup is a miss");
  expect(cache.lookup_or_decode_arm(0x08000000, kAddR0R0Imm1).supported,
         "cache returns ARM hit");
  expect(cache.hits() == 1, "second lookup is a hit");
  expect(cache.lookup_or_decode_thumb(0x08000002, kThumbAddR0Imm1).supported,
         "cache decodes Thumb miss");
  expect(cache.size() <= cache.capacity(), "cache size stays bounded");
  cache.clear();
  expect(cache.size() == 0 && cache.hits() == 0 && cache.misses() == 0,
         "cache clear resets entries and counters");

  std::cout << "instruction_cache_test: PASS\n";
  return 0;
}
