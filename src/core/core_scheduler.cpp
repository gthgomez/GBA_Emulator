#include "gba/core/core_scheduler.hpp"

#include "gba/core/memory_bus.hpp"
#include "gba/core/ppu_timing.hpp"
#include "gba/core/state_hash.hpp"
#include "gba/core/timers.hpp"
#include "gba/core/wait_state_control.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace gba::core {

namespace {

constexpr std::uint32_t kGamePakSequentialBoundary = 128U * 1024U;
constexpr std::uint32_t kGamePakInlineLiteralWindowBytes = 256U;
constexpr std::uint8_t kPrefetchBufferCapacityHalfwords = 8;
constexpr std::uint32_t kBiosIrqVector = 0x00000018U;
constexpr std::uint32_t kUserIrqHandlerPointer = 0x03007FFCU;
constexpr std::uint32_t kBiosHleIrqReturnSentinel = 0x0FFFFF00U;
constexpr std::uint32_t kMaxBiosWaitCycles = 280896U * 2U;
constexpr std::uint32_t kBiosWaitBatchCycles = 1U;
constexpr std::uint32_t kTimerIoBase = 0x04000100U;
constexpr std::uint32_t kTimerIoEnd = 0x04000110U;
constexpr std::uint32_t kInterruptFlagIo = 0x04000202U;
constexpr std::uint8_t kAutoIrqLatencyCycles = 5;
constexpr std::uint8_t kAutoIrqSlowTimerLatencyCycles = 3;
constexpr std::uint8_t kAutoIrqPostHleReturnLatencyCycles = 2;
constexpr std::uint8_t kAutoIrqLongTimerChainedPostHleReturnLatencyCycles = 0;
constexpr std::uint8_t kAutoIrqSlowTimerChainedPostHleReturnLatencyCycles = 0;
constexpr std::uint8_t kAutoIrqPhase184SecondSpacedDataLatencyCycles = 4;
constexpr std::uint8_t kAutoIrqChainedPostHleReturnLatencyCycles = 1;
constexpr std::uint32_t kBiosHleIrqDispatchCycles = 21;
constexpr std::uint32_t kBiosHlePostReturnIrqDispatchCycles = 24;
constexpr std::uint32_t kBiosHleChainedPostReturnIrqDispatchCycles = 26;
constexpr std::uint32_t kBiosHleLongTimerNonDataIrqDispatchExtraCycles = 2;
constexpr std::uint32_t kBiosHleVeryLongTimerNonDataIrqDispatchAdvanceCycles = 3;
constexpr std::uint32_t kBiosHleSpacedTimerDataIrqDispatchAdvanceCycles = 1;
constexpr std::uint32_t kBiosHleLongTimerChainedPostReturnExtraCycles = 5;
constexpr std::uint32_t kBiosHleSlowTimer0ActiveReturnExtraCycles = 1;
constexpr std::uint32_t kBiosHleIrqReentryDispatchCycles = 29;
constexpr std::uint32_t kBiosHleIrqReturnCycles = 3;
constexpr std::uint32_t kBiosHleIntrWaitReturnCycles = 57;
constexpr std::uint32_t kTimerIoDataPhaseCycles = 1;
constexpr std::uint32_t kTimerIoLoadDataPhaseCycles = 3;
constexpr std::uint32_t kInterruptFlagStoreDataPhaseCycles = 2;
constexpr std::uint32_t kSpacedTimerIoDispatchGapCycles = 16;
constexpr std::uint32_t kLooseTimerIoIrqDispatchGapCycles = 12;
constexpr std::uint32_t kTightChainedSlowTimerIoLoadExtraCycles = 0;
constexpr std::uint32_t kSpacedSlowTimerIoLoadExtraCycles = 0;
constexpr std::uint32_t kSpacedSlowTimerNearReloadIoLoadExtraCycles = 16;
constexpr std::uint32_t kSpacedSlowTimerPostReloadIoLoadExtraCycles = 8;
constexpr std::uint32_t kSpacedSlowTimerAlignedReloadIoLoadExtraCycles = 8;

enum class HleSwiProfileKind : std::uint8_t {
  simple,
  literal,
  cpu_set,
};

[[nodiscard]] std::int32_t as_i32(std::uint32_t value) {
  return static_cast<std::int32_t>(value);
}

[[nodiscard]] std::uint32_t as_u32(std::int64_t value) {
  return static_cast<std::uint32_t>(static_cast<std::int32_t>(value));
}

[[nodiscard]] std::int32_t wrap_i32(std::uint32_t value) {
  return static_cast<std::int32_t>(value);
}

[[nodiscard]] std::uint32_t wrap_u32(std::int32_t value) {
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::int32_t wrap_mul_i32(std::int32_t left, std::int32_t right) {
  return wrap_i32(wrap_u32(left) * wrap_u32(right));
}

[[nodiscard]] std::int32_t wrap_shl_i32(std::int32_t value, std::uint8_t shift) {
  return wrap_i32(wrap_u32(value) << shift);
}

[[nodiscard]] std::int32_t hle_arc_tan(std::int32_t value,
                                       std::int32_t* scratch_r1,
                                       std::int32_t* scratch_r3) {
  const std::int32_t square = wrap_mul_i32(value, value);
  std::int32_t polynomial = -(square >> 14);
  std::int32_t factor = ((wrap_mul_i32(0xA9, polynomial)) >> 14) + 0x390;
  factor = ((wrap_mul_i32(factor, polynomial)) >> 14) + 0x91C;
  factor = ((wrap_mul_i32(factor, polynomial)) >> 14) + 0xFB6;
  factor = ((wrap_mul_i32(factor, polynomial)) >> 14) + 0x16AA;
  factor = ((wrap_mul_i32(factor, polynomial)) >> 14) + 0x2081;
  factor = ((wrap_mul_i32(factor, polynomial)) >> 14) + 0x3651;
  factor = ((wrap_mul_i32(factor, polynomial)) >> 14) + 0xA2F9;
  if (scratch_r1 != nullptr) {
    *scratch_r1 = polynomial;
  }
  if (scratch_r3 != nullptr) {
    *scratch_r3 = factor;
  }
  return static_cast<std::int16_t>(wrap_mul_i32(value, factor) >> 16);
}

[[nodiscard]] std::int32_t hle_arc_tan2(std::int32_t x, std::int32_t y,
                                        std::int32_t* scratch_r1) {
  const std::int64_t wide_x = x;
  const std::int64_t wide_y = y;
  if (y == 0) {
    return x >= 0 ? 0 : 0x8000;
  }
  if (x == 0) {
    return y >= 0 ? 0x4000 : 0xC000;
  }
  if (y >= 0) {
    if (x >= 0) {
      if (x >= y) {
        return hle_arc_tan(wrap_shl_i32(y, 14) / x, scratch_r1, nullptr);
      }
    } else if (-wide_x >= wide_y) {
      return hle_arc_tan(wrap_shl_i32(y, 14) / x, scratch_r1, nullptr) + 0x8000;
    }
    return 0x4000 - hle_arc_tan(wrap_shl_i32(x, 14) / y, scratch_r1, nullptr);
  }

  if (x <= 0) {
    if (-wide_x > -wide_y) {
      return hle_arc_tan(wrap_shl_i32(y, 14) / x, scratch_r1, nullptr) + 0x8000;
    }
  } else if (wide_x >= -wide_y) {
    return hle_arc_tan(wrap_shl_i32(y, 14) / x, scratch_r1, nullptr) + 0x10000;
  }
  return 0xC000 - hle_arc_tan(wrap_shl_i32(x, 14) / y, scratch_r1, nullptr);
}

[[nodiscard]] std::uint8_t hle_rom_wait_profile(const WaitStateControl* waitcnt) {
  if (waitcnt == nullptr) {
    return 0;
  }
  const std::uint16_t control = waitcnt->read_control();
  const bool prefetch = (control & 0x4000U) != 0;
  const bool nonsequential = (control & 0x0004U) != 0;
  const bool fast_sequential = (control & 0x0010U) != 0;
  return static_cast<std::uint8_t>((prefetch ? 1U : 0U) |
                                   (nonsequential ? 2U : 0U) |
                                   (fast_sequential ? 4U : 0U));
}

[[nodiscard]] std::int32_t hle_swi_profile_adjustment(
    BiosSwiCall call, std::uint32_t fetch_address, const WaitStateControl* waitcnt,
    HleSwiProfileKind profile) {
  constexpr std::array<std::int32_t, 8> kArmSimpleRom{
      16, 16, 14, 14, 13, 13, 11, 11};
  constexpr std::array<std::int32_t, 8> kThumbSimpleRom{
      10, 10, 8, 8, 9, 9, 7, 7};
  constexpr std::array<std::int32_t, 8> kArmLiteralRom{
      16, 19, 14, 16, 13, 17, 11, 14};
  constexpr std::array<std::int32_t, 8> kThumbLiteralRom{
      10, 13, 8, 10, 9, 13, 7, 10};
  constexpr std::array<std::int32_t, 8> kThumbCpuSetRom{
      10, 19, 8, 14, 9, 21, 7, 16};

  const bool thumb = call.source == BiosSwiSource::thumb;
  const Region region = MemoryBus::describe(fetch_address).region;
  if (region == Region::iwram) {
    return 0;
  }

  if (region == Region::ewram) {
    switch (profile) {
      case HleSwiProfileKind::literal:
        return thumb ? 1 : 7;
      case HleSwiProfileKind::cpu_set:
        return thumb ? -9 : 12;
      case HleSwiProfileKind::simple:
        return thumb ? 6 : 12;
    }
  }

  if (region != Region::game_pak_rom) {
    return 0;
  }

  const std::uint8_t index = hle_rom_wait_profile(waitcnt);
  switch (profile) {
    case HleSwiProfileKind::literal:
      return thumb ? kThumbLiteralRom.at(index) : kArmLiteralRom.at(index);
    case HleSwiProfileKind::cpu_set:
      return thumb ? kThumbCpuSetRom.at(index) : kArmSimpleRom.at(index);
    case HleSwiProfileKind::simple:
      return thumb ? kThumbSimpleRom.at(index) : kArmSimpleRom.at(index);
  }
  return 0;
}

[[nodiscard]] std::uint32_t add_hle_profile_adjustment(
    std::uint32_t base_cycles, std::int32_t adjustment) {
  if (adjustment >= 0) {
    return base_cycles + static_cast<std::uint32_t>(adjustment);
  }
  const std::uint32_t magnitude = static_cast<std::uint32_t>(-adjustment);
  return base_cycles > magnitude ? base_cycles - magnitude : 0;
}

[[nodiscard]] std::uint32_t hle_div_base_cycles(std::int32_t numerator,
                                                std::int32_t denominator) {
  if (denominator == 0 ||
      (denominator == -1 &&
       numerator == std::numeric_limits<std::int32_t>::min())) {
    return 330;
  }
  const auto abs64 = [](std::int32_t value) {
    const std::int64_t wide = value;
    return wide < 0 ? -wide : wide;
  };
  return abs64(numerator) < abs64(denominator) ? 70U : 330U;
}

[[nodiscard]] std::uint32_t hle_sqrt_base_cycles(std::uint32_t value) {
  if (value == 0) {
    return 99;
  }
  return value <= 0xFFU ? 214U : 1130U;
}

[[nodiscard]] std::uint32_t hle_cpu_set_base_cycles(std::uint32_t control,
                                                    bool fast) {
  std::uint32_t count = control & 0x001FFFFFU;
  if (fast) {
    count = (count + 7U) & ~7U;
    return 318U + count * 12U;
  }
  return 64U + count * ((control & (1U << 26U)) != 0 ? 8U : 4U);
}

[[nodiscard]] bool immediate_dma_bus_visible_to_timers(std::uint32_t fetch_address,
                                                       bool thumb,
                                                       const WaitStateControl* waitcnt) {
  const Region fetch_region = MemoryBus::describe(fetch_address).region;
  if (fetch_region == Region::iwram) {
    return false;
  }
  if (thumb && fetch_region == Region::game_pak_rom && waitcnt != nullptr &&
      waitcnt->prefetch_enabled()) {
    const MemoryAccessTiming timing =
        MemoryBus::timing(fetch_address, AccessWidth::halfword, *waitcnt);
    return timing.sequential > 1U;
  }
  return true;
}

[[nodiscard]] constexpr std::uint32_t align_word(std::uint32_t value) {
  return value & ~0x3U;
}

[[nodiscard]] constexpr std::uint32_t offset_transfer_address(std::uint32_t base,
                                                              std::uint32_t offset,
                                                              bool up) {
  return up ? base + offset : base - offset;
}

[[nodiscard]] constexpr bool supported_transfer_addressing(bool pre_index,
                                                           bool write_back) {
  return pre_index || !write_back;
}

[[nodiscard]] constexpr bool transfer_needs_write_back(bool pre_index,
                                                       bool write_back) {
  return !pre_index || write_back;
}

[[nodiscard]] std::uint8_t count_registers(std::uint16_t register_list) {
  std::uint8_t count = 0;
  while (register_list != 0) {
    count = static_cast<std::uint8_t>(count + (register_list & 0x1U));
    register_list = static_cast<std::uint16_t>(register_list >> 1);
  }
  return count;
}

[[nodiscard]] bool register_list_contains(std::uint16_t register_list,
                                          std::uint8_t index) {
  return (register_list & (static_cast<std::uint16_t>(1U) << index)) != 0;
}

[[nodiscard]] std::uint32_t block_transfer_first_address(std::uint32_t base,
                                                         std::uint8_t register_count,
                                                         bool pre_index, bool up) {
  const std::uint32_t byte_count = static_cast<std::uint32_t>(register_count) * 4U;
  if (up) {
    return pre_index ? base + 4U : base;
  }
  return pre_index ? base - byte_count : base - byte_count + 4U;
}

[[nodiscard]] bool condition_passed_for_cpu(const Arm7tdmi& cpu,
                                            ArmCondition condition) {
  switch (condition) {
    case ArmCondition::eq:
      return cpu.zero();
    case ArmCondition::ne:
      return !cpu.zero();
    case ArmCondition::cs:
      return cpu.carry();
    case ArmCondition::cc:
      return !cpu.carry();
    case ArmCondition::mi:
      return cpu.negative();
    case ArmCondition::pl:
      return !cpu.negative();
    case ArmCondition::vs:
      return cpu.overflow();
    case ArmCondition::vc:
      return !cpu.overflow();
    case ArmCondition::hi:
      return cpu.carry() && !cpu.zero();
    case ArmCondition::ls:
      return !cpu.carry() || cpu.zero();
    case ArmCondition::ge:
      return cpu.negative() == cpu.overflow();
    case ArmCondition::lt:
      return cpu.negative() != cpu.overflow();
    case ArmCondition::gt:
      return !cpu.zero() && (cpu.negative() == cpu.overflow());
    case ArmCondition::le:
      return cpu.zero() || (cpu.negative() != cpu.overflow());
    case ArmCondition::al:
      return true;
  }
  return false;
}

struct DataAccessTimingProbe {
  std::uint32_t address = 0;
  AccessWidth width = AccessWidth::word;
  bool load = false;
};

[[nodiscard]] bool timer_io_address(std::uint32_t address) {
  return address >= kTimerIoBase && address < kTimerIoEnd;
}

[[nodiscard]] bool arm_test_or_compare_instruction(std::uint32_t instruction) {
  if ((instruction & 0x0C000000U) != 0) {
    return false;
  }
  const std::uint32_t opcode = (instruction >> 21U) & 0xFU;
  return opcode >= 0x8U && opcode <= 0xBU;
}

[[nodiscard]] bool access_covers_address(std::uint32_t address,
                                         AccessWidth width,
                                         std::uint32_t target) {
  std::uint32_t bytes = 4;
  switch (width) {
    case AccessWidth::byte:
      bytes = 1;
      break;
    case AccessWidth::halfword:
      bytes = 2;
      break;
    case AccessWidth::word:
      bytes = 4;
      break;
  }
  return target >= address && target < address + bytes;
}

[[nodiscard]] bool interrupt_flag_io_address(std::uint32_t address,
                                             AccessWidth width) {
  return access_covers_address(address, width, kInterruptFlagIo);
}

[[nodiscard]] std::uint32_t io_store_pre_access_cycles(
    const DataAccessTimingProbe& data_access) {
  if (data_access.load) {
    return timer_io_address(data_access.address) &&
                   data_access.width == AccessWidth::word
               ? kTimerIoLoadDataPhaseCycles
               : 0;
  }

  if (timer_io_address(data_access.address)) {
    return kTimerIoDataPhaseCycles;
  }
  if (interrupt_flag_io_address(data_access.address, data_access.width)) {
    return kInterruptFlagStoreDataPhaseCycles;
  }
  return 0;
}

[[nodiscard]] std::uint32_t adjust_slow_timer_load_pre_access_cycles(
    std::uint32_t base_cycles, const std::optional<DataAccessTimingProbe>& data_access,
    bool timer_io_access, bool spaced_timer_io_access,
    std::uint32_t timer_io_gap_cycles, bool return_latency_pending,
    bool post_return_chain_active, bool timer0_irq_requested, const Timers& timers) {
  if (base_cycles == 0 || !data_access.has_value() || !data_access->load ||
      !timer_io_access || !return_latency_pending || timer0_irq_requested ||
      !timers.enabled(0) || timers.count_up(0) || !timers.irq_enabled(0)) {
    return base_cycles;
  }

  const bool tight_timer_io_access =
      timer_io_gap_cycles <= kLooseTimerIoIrqDispatchGapCycles;
  if (!spaced_timer_io_access && !tight_timer_io_access) {
    return base_cycles;
  }

  const std::uint32_t divisor = timers.prescaler_divisor(0);
  if (divisor <= 1U) {
    return base_cycles;
  }

  const std::uint32_t cycles_until_tick =
      timers.cycles_until_next_prescaler_tick(0);
  if (cycles_until_tick == 0) {
    return base_cycles;
  }

  const std::uint32_t ticks_to_overflow =
      0x10000U - static_cast<std::uint32_t>(timers.counter(0));
  const std::uint64_t cycles_until_overflow =
      static_cast<std::uint64_t>(cycles_until_tick) +
      static_cast<std::uint64_t>(ticks_to_overflow - 1U) * divisor;
  const std::uint32_t post_event_latency =
      post_return_chain_active
          ? kAutoIrqSlowTimerChainedPostHleReturnLatencyCycles
          : kAutoIrqPostHleReturnLatencyCycles;
  const std::uint64_t mature_cycles =
      cycles_until_overflow + post_event_latency;
  if (cycles_until_overflow <= base_cycles) {
    return static_cast<std::uint32_t>(
        std::max<std::uint64_t>(base_cycles, mature_cycles));
  }

  if (tight_timer_io_access && !post_return_chain_active &&
      mature_cycles <= base_cycles + timer_io_gap_cycles) {
    return static_cast<std::uint32_t>(
        std::max<std::uint64_t>(base_cycles, mature_cycles));
  }

  if (tight_timer_io_access && post_return_chain_active) {
    const std::uint32_t extra_window =
        base_cycles + kTightChainedSlowTimerIoLoadExtraCycles;
    if (cycles_until_overflow > base_cycles &&
        cycles_until_overflow <= extra_window) {
      return static_cast<std::uint32_t>(cycles_until_overflow);
    }
  }

  if (!spaced_timer_io_access || !post_return_chain_active) {
    return base_cycles;
  }

  const std::uint32_t extra_window =
      base_cycles + kSpacedSlowTimerIoLoadExtraCycles;
  if (cycles_until_overflow > base_cycles &&
      cycles_until_overflow <= extra_window) {
    return static_cast<std::uint32_t>(cycles_until_overflow);
  }

  const std::uint16_t reload = timers.reload(0);
  if (reload == 0xFFEEU) {
    const std::uint32_t near_reload_extra_window =
        base_cycles + kSpacedSlowTimerNearReloadIoLoadExtraCycles;
    if (cycles_until_overflow > base_cycles &&
        cycles_until_overflow < near_reload_extra_window) {
      return static_cast<std::uint32_t>(cycles_until_overflow);
    }
  }
  if (reload == 0xFFEDU) {
    const std::uint32_t post_reload_extra_window =
        base_cycles + kSpacedSlowTimerPostReloadIoLoadExtraCycles;
    if (cycles_until_overflow > base_cycles &&
        cycles_until_overflow < post_reload_extra_window) {
      return static_cast<std::uint32_t>(cycles_until_overflow);
    }
  }
  if (reload == 0xFFF0U && divisor >= 256U) {
    const std::uint32_t aligned_reload_extra_window =
        base_cycles + kSpacedSlowTimerAlignedReloadIoLoadExtraCycles;
    if (cycles_until_overflow > base_cycles &&
        cycles_until_overflow < aligned_reload_extra_window) {
      return static_cast<std::uint32_t>(cycles_until_overflow);
    }
  }

  return base_cycles;
}

[[nodiscard]] bool should_replay_slow_timer_load_after_irq(
    const std::optional<DataAccessTimingProbe>& data_access,
    bool timer_io_access, bool spaced_timer_io_access, bool post_return_armed,
    bool timer0_irq_requested, const Timers& timers) {
  return data_access.has_value() && data_access->load && timer_io_access &&
         spaced_timer_io_access && post_return_armed && timer0_irq_requested &&
         timers.enabled(0) && !timers.count_up(0) && timers.irq_enabled(0) &&
         timers.prescaler_divisor(0) > 1U && timers.reload(0) == 0xFFEDU;
}

[[nodiscard]] bool should_defer_slow_timer_load_irq_until_after_access(
    const std::optional<DataAccessTimingProbe>& data_access,
    bool timer_io_access, bool spaced_timer_io_access,
    std::uint32_t pre_access_cycles, bool return_latency_pending,
    bool post_return_chain_active, bool timer0_irq_requested,
    const Timers& timers) {
  const std::uint32_t divisor = timers.prescaler_divisor(0);
  const std::uint32_t enable_phase = timers.last_enable_phase(0) & 0x7U;
  if (!data_access.has_value() || !data_access->load || !timer_io_access ||
      !spaced_timer_io_access || !return_latency_pending ||
      !post_return_chain_active || !timer0_irq_requested ||
      !timers.enabled(0) || timers.count_up(0) || !timers.irq_enabled(0) ||
      enable_phase < 2U || enable_phase > 4U) {
    return false;
  }

  const std::uint16_t reload = timers.reload(0);
  if (reload == 0xFFEEU) {
    // Slow chained loads must observe timer0 before the IRQ handler stops it.
    return divisor == 256U || (divisor == 1024U && pre_access_cycles >= 5U);
  }
  return reload == 0xFFEDU && divisor == 256U && pre_access_cycles >= 7U;
}

[[nodiscard]] constexpr std::uint8_t access_width_bytes(AccessWidth width) {
  switch (width) {
    case AccessWidth::byte:
      return 1;
    case AccessWidth::halfword:
      return 2;
    case AccessWidth::word:
      return 4;
  }
  return 0;
}

[[nodiscard]] bool non_word_game_pak_data_access(std::uint32_t address,
                                                 AccessWidth width) {
  const Region region = MemoryBus::describe(address).region;
  return (region == Region::game_pak_rom || region == Region::game_pak_save) &&
         width != AccessWidth::word;
}

[[nodiscard]] bool game_pak_data_access(std::uint32_t address) {
  const Region region = MemoryBus::describe(address).region;
  return region == Region::game_pak_rom || region == Region::game_pak_save;
}

[[nodiscard]] bool game_pak_data_access_disturbs_prefetch(std::uint32_t data_address,
                                                          std::uint32_t fetch_address) {
  if (!game_pak_data_access(data_address)) {
    return false;
  }
  if (data_address < fetch_address) {
    return true;
  }
  return data_address - fetch_address > kGamePakInlineLiteralWindowBytes;
}

[[nodiscard]] constexpr AccessWidth thumb_probe_access_width(
    ThumbMemoryTransferKind kind) {
  switch (kind) {
    case ThumbMemoryTransferKind::word:
      return AccessWidth::word;
    case ThumbMemoryTransferKind::halfword:
    case ThumbMemoryTransferKind::signed_halfword:
      return AccessWidth::halfword;
    case ThumbMemoryTransferKind::byte:
    case ThumbMemoryTransferKind::signed_byte:
      return AccessWidth::byte;
  }
  return AccessWidth::word;
}

[[nodiscard]] std::uint32_t arm_visible_register_value(const Arm7tdmi& cpu,
                                                       std::uint8_t index) {
  return index == Arm7tdmi::kPc ? cpu.register_value(index) + 8U
                                : cpu.register_value(index);
}

[[nodiscard]] std::optional<DataAccessTimingProbe> first_arm_data_access_for_timing(
    const Arm7tdmi& cpu, std::uint32_t instruction) {
  if (Arm7tdmi::can_decode_block_data_transfer(instruction)) {
    const DecodedBlockDataTransferInstruction decoded =
        Arm7tdmi::decode_block_data_transfer(instruction);
    if (!condition_passed_for_cpu(cpu, decoded.condition)) {
      return std::nullopt;
    }
    if (decoded.load && decoded.write_back &&
        register_list_contains(decoded.register_list, decoded.rn)) {
      return std::nullopt;
    }
    const std::uint8_t register_count = count_registers(decoded.register_list);
    return DataAccessTimingProbe{
        block_transfer_first_address(cpu.register_value(decoded.rn), register_count,
                                     decoded.pre_index, decoded.up),
        AccessWidth::word,
        decoded.load};
  }

  if (Arm7tdmi::can_decode_halfword_data_transfer_immediate(instruction) ||
      Arm7tdmi::can_decode_halfword_data_transfer_register(instruction)) {
    const DecodedHalfwordDataTransferInstruction decoded =
        Arm7tdmi::can_decode_halfword_data_transfer_register(instruction)
            ? Arm7tdmi::decode_halfword_data_transfer_register(
                  instruction,
                  arm_visible_register_value(cpu,
                                             static_cast<std::uint8_t>(instruction & 0xFU)))
            : Arm7tdmi::decode_halfword_data_transfer_immediate(instruction);
    if (!condition_passed_for_cpu(cpu, decoded.condition)) {
      return std::nullopt;
    }
    if (!supported_transfer_addressing(decoded.pre_index, decoded.write_back)) {
      return std::nullopt;
    }
    const bool needs_write_back =
        transfer_needs_write_back(decoded.pre_index, decoded.write_back);
    if ((needs_write_back && decoded.load && decoded.rn == decoded.rd) ||
        (!decoded.load && (decoded.signed_transfer || !decoded.halfword))) {
      return std::nullopt;
    }
    const std::uint32_t base = arm_visible_register_value(cpu, decoded.rn);
    const std::uint32_t offset_address =
        offset_transfer_address(base, decoded.offset, decoded.up);
    return DataAccessTimingProbe{decoded.pre_index ? offset_address : base,
                                 decoded.halfword ? AccessWidth::halfword
                                                  : AccessWidth::byte,
                                 decoded.load};
  }

  if (Arm7tdmi::can_decode_single_data_transfer_immediate(instruction) ||
      Arm7tdmi::can_decode_single_data_transfer_register(instruction)) {
    const DecodedSingleDataTransferInstruction decoded =
        Arm7tdmi::can_decode_single_data_transfer_register(instruction)
            ? Arm7tdmi::decode_single_data_transfer_register(
                  instruction,
                  arm_visible_register_value(cpu,
                                             static_cast<std::uint8_t>(instruction & 0xFU)),
                  cpu.carry())
            : Arm7tdmi::decode_single_data_transfer_immediate(instruction);
    if (!condition_passed_for_cpu(cpu, decoded.condition)) {
      return std::nullopt;
    }
    if (!supported_transfer_addressing(decoded.pre_index, decoded.write_back)) {
      return std::nullopt;
    }
    const bool needs_write_back =
        transfer_needs_write_back(decoded.pre_index, decoded.write_back);
    if (needs_write_back && decoded.load && decoded.rn == decoded.rd) {
      return std::nullopt;
    }
    const std::uint32_t base = arm_visible_register_value(cpu, decoded.rn);
    const std::uint32_t offset_address =
        offset_transfer_address(base, decoded.offset, decoded.up);
    return DataAccessTimingProbe{decoded.pre_index ? offset_address : base,
                                 decoded.byte ? AccessWidth::byte : AccessWidth::word,
                                 decoded.load};
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<DataAccessTimingProbe> first_thumb_data_access_for_timing(
    const Arm7tdmi& cpu, std::uint16_t instruction) {
  if (Arm7tdmi::can_decode_thumb_block_transfer(instruction)) {
    const DecodedThumbBlockTransferInstruction decoded =
        Arm7tdmi::decode_thumb_block_transfer(instruction);
    return DataAccessTimingProbe{cpu.register_value(decoded.rb), AccessWidth::word,
                                 decoded.load};
  }

  if (!Arm7tdmi::can_decode_thumb_memory_transfer(instruction)) {
    return std::nullopt;
  }

  const DecodedThumbMemoryTransferInstruction decoded =
      Arm7tdmi::decode_thumb_memory_transfer(instruction);
  if (!decoded.load && (decoded.kind == ThumbMemoryTransferKind::signed_byte ||
                        decoded.kind == ThumbMemoryTransferKind::signed_halfword)) {
    return std::nullopt;
  }
  const std::uint32_t base =
      decoded.rb == Arm7tdmi::kPc
          ? align_word(cpu.register_value(Arm7tdmi::kPc) + 4U)
          : cpu.register_value(decoded.rb);
  const std::uint32_t offset =
      decoded.offset_is_register ? cpu.register_value(static_cast<std::uint8_t>(decoded.offset))
                                 : decoded.offset;
  return DataAccessTimingProbe{base + offset, thumb_probe_access_width(decoded.kind),
                               decoded.load};
}

void merge_device_ticks(CoreDeviceTickResult& target,
                        const CoreDeviceTickResult& source) {
  target.cycles += source.cycles;
  if (source.apu_frame_step.has_value()) {
    target.apu_frame_step = source.apu_frame_step;
  }
  target.apu_timer_events += source.apu_timer_events;
  target.last_direct_sound = source.last_direct_sound;
  target.triggered_dma.channels_executed = static_cast<std::uint8_t>(
      std::min<std::uint32_t>(255U, target.triggered_dma.channels_executed +
                                        source.triggered_dma.channels_executed));
  target.triggered_dma.units_transferred += source.triggered_dma.units_transferred;
  target.triggered_dma.bus_cycles += source.triggered_dma.bus_cycles;
  target.triggered_dma.unsupported_request =
      target.triggered_dma.unsupported_request || source.triggered_dma.unsupported_request;
}

bool should_charge_prefetch_execute_bubble(
    std::uint32_t fetch_address, const WaitStateControl* waitcnt,
    const CoreSchedulerStepResult& step, bool pc_advanced, bool prefetch_hit,
    std::uint8_t fetch_width_bytes, std::uint8_t remaining_prefetch_halfwords) {
  if (waitcnt == nullptr || !waitcnt->prefetch_enabled() || !pc_advanced ||
      step.cpu_step.status != ExecuteStatus::executed ||
      step.data_access.has_value()) {
    return false;
  }
  if (fetch_width_bytes == 2 &&
      (!prefetch_hit || remaining_prefetch_halfwords == 0)) {
    return false;
  }
  return MemoryBus::describe(fetch_address).region == Region::game_pak_rom;
}

bool should_overlap_thumb_internal_data_load(
    std::uint32_t fetch_address, const WaitStateControl* waitcnt,
    const Arm7tdmi& cpu, std::uint16_t instruction, std::uint32_t fetch_cycles) {
  if (waitcnt == nullptr || !waitcnt->prefetch_enabled() || fetch_cycles == 0 ||
      MemoryBus::describe(fetch_address).region != Region::game_pak_rom) {
    return false;
  }

  const std::optional<DataAccessTimingProbe> data_access =
      first_thumb_data_access_for_timing(cpu, instruction);
  if (!data_access.has_value() || !data_access->load) {
    return false;
  }

  const Region data_region = MemoryBus::describe(data_access->address).region;
  return data_region == Region::ewram || data_region == Region::iwram;
}

bool should_suppress_next_thumb_prefetch_execute_bubble(
    std::uint32_t fetch_address, const WaitStateControl* waitcnt,
    const CoreSchedulerStepResult& step, bool pc_advanced,
    bool prefetch_enabled, std::uint32_t fetch_cycles) {
  if (waitcnt == nullptr || !waitcnt->prefetch_enabled() || !prefetch_enabled ||
      fetch_cycles != 0 || !pc_advanced ||
      step.cpu_step.status != ExecuteStatus::executed ||
      !step.data_access.has_value() || !step.data_access->load ||
      MemoryBus::describe(fetch_address).region != Region::game_pak_rom) {
    return false;
  }

  const Region data_region = MemoryBus::describe(step.data_access->address).region;
  return data_region == Region::ewram || data_region == Region::iwram;
}

[[nodiscard]] std::uint32_t scheduler_signed_multiply_iterations(
    std::uint32_t value) {
  if ((value & 0xFFFFFF00U) == 0 || (value & 0xFFFFFF00U) == 0xFFFFFF00U) {
    return 1;
  }
  if ((value & 0xFFFF0000U) == 0 || (value & 0xFFFF0000U) == 0xFFFF0000U) {
    return 2;
  }
  if ((value & 0xFF000000U) == 0 || (value & 0xFF000000U) == 0xFF000000U) {
    return 3;
  }
  return 4;
}

[[nodiscard]] std::uint32_t scheduler_unsigned_multiply_iterations(
    std::uint32_t value) {
  if ((value & 0xFFFFFF00U) == 0) {
    return 1;
  }
  if ((value & 0xFFFF0000U) == 0) {
    return 2;
  }
  if ((value & 0xFF000000U) == 0) {
    return 3;
  }
  return 4;
}

[[nodiscard]] std::uint32_t nonprefetch_multiply_rom_recovery_cycles(
    const MemoryAccessTiming& timing) {
  const std::uint32_t first_access_recovery =
      timing.nonsequential > 2U ? timing.nonsequential - 2U : 0U;
  return first_access_recovery + (timing.sequential == 1U ? 1U : 0U);
}

[[nodiscard]] std::uint32_t prefetched_arm_multiply_cycles(
    std::uint32_t iterations, bool long_multiply, bool accumulate,
    const MemoryAccessTiming& timing) {
  if (timing.sequential != 1U) {
    if (long_multiply && accumulate && iterations == 4U) {
      return 2U;
    }
    return 1U;
  }

  if (!long_multiply) {
    return accumulate ? 1U : (iterations == 4U ? 2U : 1U);
  }
  if (accumulate) {
    return iterations;
  }
  return iterations > 1U ? iterations - 1U : 1U;
}

[[nodiscard]] std::optional<ArmElapsedCycleEstimate> arm_rom_multiply_elapsed_override(
    const Arm7tdmi& cpu, std::uint32_t instruction, std::uint32_t fetch_address,
    const WaitStateControl* waitcnt) {
  if (waitcnt == nullptr ||
      MemoryBus::describe(fetch_address).region != Region::game_pak_rom) {
    return std::nullopt;
  }

  const MemoryAccessTiming timing =
      MemoryBus::timing(fetch_address, AccessWidth::word, *waitcnt);
  if (Arm7tdmi::can_decode_multiply_long(instruction)) {
    const DecodedMultiplyLongInstruction decoded =
        Arm7tdmi::decode_multiply_long(instruction);
    const std::uint32_t multiplier = cpu.register_value(decoded.rs);
    const std::uint32_t iterations =
        decoded.signed_multiply ? scheduler_signed_multiply_iterations(multiplier)
                                : scheduler_unsigned_multiply_iterations(multiplier);
    const std::uint32_t base_cycles =
        iterations + (decoded.accumulate ? 3U : 2U);
    const std::uint32_t cycles =
        waitcnt->prefetch_enabled()
            ? prefetched_arm_multiply_cycles(iterations, true, decoded.accumulate,
                                             timing)
            : base_cycles + nonprefetch_multiply_rom_recovery_cycles(timing);
    return ArmElapsedCycleEstimate{cycles, true, false};
  }

  if (!Arm7tdmi::can_decode_multiply(instruction)) {
    return std::nullopt;
  }

  const DecodedMultiplyInstruction decoded =
      Arm7tdmi::decode_multiply(instruction);
  const std::uint32_t iterations =
      scheduler_signed_multiply_iterations(cpu.register_value(decoded.rs));
  const std::uint32_t base_cycles =
      iterations + (decoded.accumulate ? 2U : 1U);
  const std::uint32_t cycles =
      waitcnt->prefetch_enabled()
          ? prefetched_arm_multiply_cycles(iterations, false, decoded.accumulate,
                                           timing)
          : base_cycles + nonprefetch_multiply_rom_recovery_cycles(timing);
  return ArmElapsedCycleEstimate{cycles, true, false};
}

[[nodiscard]] std::optional<ArmElapsedCycleEstimate> thumb_rom_multiply_elapsed_override(
    const Arm7tdmi& cpu, std::uint16_t instruction, std::uint32_t fetch_address,
    const WaitStateControl* waitcnt) {
  if (waitcnt == nullptr ||
      MemoryBus::describe(fetch_address).region != Region::game_pak_rom ||
      !Arm7tdmi::can_decode_thumb_alu(instruction)) {
    return std::nullopt;
  }

  const DecodedThumbAluInstruction decoded =
      Arm7tdmi::decode_thumb_alu(instruction);
  if (decoded.opcode != ThumbAluOpcode::mul) {
    return std::nullopt;
  }

  const MemoryAccessTiming timing =
      MemoryBus::timing(fetch_address, AccessWidth::halfword, *waitcnt);
  const std::uint32_t iterations =
      scheduler_signed_multiply_iterations(cpu.register_value(decoded.rd));
  const std::uint32_t base_cycles = iterations + 1U;
  const std::uint32_t cycles =
      waitcnt->prefetch_enabled()
          ? (timing.sequential == 1U
                 ? iterations
                 : (iterations > 1U ? iterations - 1U : 1U))
          : base_cycles + nonprefetch_multiply_rom_recovery_cycles(timing);
  return ArmElapsedCycleEstimate{cycles, true, false};
}

[[nodiscard]] bool arm_instruction_refills_pipeline(std::uint32_t instruction) {
  return Arm7tdmi::can_decode_branch(instruction) ||
         Arm7tdmi::can_decode_branch_exchange(instruction);
}

[[nodiscard]] bool thumb_instruction_refills_pipeline(std::uint16_t instruction) {
  if (Arm7tdmi::can_decode_thumb_unconditional_branch(instruction) ||
      Arm7tdmi::can_decode_thumb_conditional_branch(instruction) ||
      Arm7tdmi::can_decode_thumb_long_branch_link(instruction)) {
    return true;
  }

  if (!Arm7tdmi::can_decode_thumb_high_register(instruction)) {
    return false;
  }
  return Arm7tdmi::decode_thumb_high_register(instruction).opcode ==
         ThumbHighRegisterOpcode::bx;
}

[[nodiscard]] std::uint32_t arm_branch_refill_cycles(
    std::uint32_t target_address, const WaitStateControl* waitcnt) {
  const Region target_region = MemoryBus::describe(target_address).region;
  if (target_region == Region::iwram) {
    return 0;
  }
  if (target_region == Region::ewram) {
    return 10;
  }
  if (target_region != Region::game_pak_rom || waitcnt == nullptr) {
    return 0;
  }

  const MemoryAccessTiming timing =
      MemoryBus::timing(target_address, AccessWidth::word, *waitcnt);
  const bool fast_sequential = timing.sequential <= 1U;
  if (fast_sequential) {
    return waitcnt->prefetch_enabled() ? 7U : 6U;
  }
  return waitcnt->prefetch_enabled() ? 11U : 10U;
}

[[nodiscard]] std::uint32_t thumb_branch_refill_cycles(
    std::uint32_t target_address, const WaitStateControl* waitcnt) {
  const Region target_region = MemoryBus::describe(target_address).region;
  if (target_region == Region::iwram) {
    return 2;
  }
  if (target_region == Region::ewram) {
    return 6;
  }
  if (target_region != Region::game_pak_rom || waitcnt == nullptr) {
    return 0;
  }

  const MemoryAccessTiming timing =
      MemoryBus::timing(target_address, AccessWidth::halfword, *waitcnt);
  return timing.sequential <= 1U ? 4U : 6U;
}

[[nodiscard]] std::uint32_t arm_branch_exchange_refill_cycles(
    std::uint32_t fetch_address, std::uint32_t target_address,
    const WaitStateControl* waitcnt) {
  const Region source_region = MemoryBus::describe(fetch_address).region;
  const Region target_region = MemoryBus::describe(target_address).region;
  if (source_region == Region::iwram &&
      target_region == Region::game_pak_rom && waitcnt != nullptr &&
      !waitcnt->prefetch_enabled()) {
    const MemoryAccessTiming timing =
        MemoryBus::timing(target_address, AccessWidth::word, *waitcnt);
    return timing.nonsequential;
  }

  std::uint32_t cycles = arm_branch_refill_cycles(target_address, waitcnt);
  if (source_region == Region::game_pak_rom &&
      target_region == Region::game_pak_rom && waitcnt != nullptr &&
      waitcnt->prefetch_enabled() && target_address < fetch_address) {
    const MemoryAccessTiming timing =
        MemoryBus::timing(target_address, AccessWidth::word, *waitcnt);
    if (timing.sequential <= 1U) {
      cycles += timing.nonsequential >= 4U ? 4U : 3U;
    } else {
      cycles += timing.nonsequential >= 4U ? 3U : 2U;
    }
  }
  return cycles;
}

[[nodiscard]] std::uint32_t thumb_branch_exchange_refill_cycles(
    std::uint32_t fetch_address, std::uint32_t target_address,
    const WaitStateControl* waitcnt) {
  const Region source_region = MemoryBus::describe(fetch_address).region;
  const Region target_region = MemoryBus::describe(target_address).region;
  if (source_region == Region::game_pak_rom && target_region == Region::ewram &&
      (waitcnt == nullptr || !waitcnt->prefetch_enabled())) {
    return 1;
  }

  std::uint32_t cycles = thumb_branch_refill_cycles(target_address, waitcnt);
  if (source_region == Region::game_pak_rom &&
      target_region == Region::game_pak_rom && waitcnt != nullptr &&
      waitcnt->prefetch_enabled() && target_address < fetch_address) {
    const MemoryAccessTiming timing =
        MemoryBus::timing(target_address, AccessWidth::halfword, *waitcnt);
    if (timing.sequential <= 1U) {
      cycles += timing.nonsequential >= 4U ? 4U : 3U;
    } else {
      cycles += timing.nonsequential >= 4U ? 3U : 2U;
    }
  }
  return cycles;
}

[[nodiscard]] bool branch_exchange_seeds_fast_prefetch(
    std::uint32_t fetch_address, std::uint32_t target_address,
    const WaitStateControl* waitcnt) {
  if (waitcnt == nullptr || !waitcnt->prefetch_enabled() ||
      MemoryBus::describe(fetch_address).region != Region::game_pak_rom ||
      MemoryBus::describe(target_address).region != Region::game_pak_rom ||
      target_address >= fetch_address) {
    return false;
  }

  const MemoryAccessTiming timing =
      MemoryBus::timing(target_address, AccessWidth::halfword, *waitcnt);
  return timing.sequential <= 1U;
}

void charge_pipeline_refill(CoreSchedulerStepResult& step,
                            CoreDeviceTickResult refill_devices,
                            std::uint32_t refill_cycles) {
  if (refill_cycles == 0) {
    return;
  }
  merge_device_ticks(step.devices, refill_devices);
  step.cpu_step.elapsed_cycles += refill_cycles;
  step.cpu_step.total_elapsed_cycles += refill_cycles;
  step.scheduler_cycles += refill_cycles + refill_devices.triggered_dma.bus_cycles;
}

}  // namespace

CoreScheduler::CoreScheduler(Arm7tdmi& cpu, MemoryBus& memory,
                             InterruptController& interrupts, Timers& timers,
                             DmaController& dma, PpuTiming& ppu, Apu& apu)
    : cpu_(cpu),
      memory_(memory),
      interrupts_(interrupts),
      timers_(timers),
      dma_(dma),
      ppu_(ppu),
      apu_(apu),
      bios_(nullptr),
      scheduler_cycles_(0),
      halted_(false),
      waitcnt_(nullptr),
      last_waitcnt_control_(std::nullopt),
      prefetch_buffer_halfwords_(0),
      suppress_next_game_pak_prefetch_(false),
      recover_next_game_pak_data_fetch_(false),
      suppress_next_thumb_prefetch_execute_bubble_(false),
      previous_thumb_internal_load_(false),
      hle_irq_return_lr_(std::nullopt),
      hle_irq_saved_registers_(std::nullopt),
      hle_irq_reentry_dispatch_pending_(false),
      hle_irq_return_latency_pending_(false),
      hle_irq_post_return_latency_armed_(false),
      hle_irq_post_return_dispatch_pending_(false),
      hle_irq_chained_post_return_dispatch_pending_(false),
      hle_irq_chained_post_return_data_dispatch_pending_(false),
      hle_irq_chained_post_return_spaced_data_dispatch_pending_(false),
      hle_irq_long_timer_chained_return_pending_(false),
      hle_irq_slow_timer0_return_pending_(false),
      hle_irq_post_return_chain_active_(false),
      hle_irq_chained_spaced_data_service_count_(0),
      auto_irq_line_high_(false),
      auto_irq_latency_cycles_(0),
      timer_io_access_gap_cycles_(0) {}

CoreScheduler::CoreScheduler(Arm7tdmi& cpu, MemoryBus& memory,
                             InterruptController& interrupts, Timers& timers,
                             DmaController& dma, PpuTiming& ppu, Apu& apu,
                             const WaitStateControl& waitcnt)
    : cpu_(cpu),
      memory_(memory),
      interrupts_(interrupts),
      timers_(timers),
      dma_(dma),
      ppu_(ppu),
      apu_(apu),
      bios_(nullptr),
      scheduler_cycles_(0),
      halted_(false),
      waitcnt_(&waitcnt),
      last_waitcnt_control_(std::nullopt),
      prefetch_buffer_halfwords_(0),
      suppress_next_game_pak_prefetch_(false),
      recover_next_game_pak_data_fetch_(false),
      suppress_next_thumb_prefetch_execute_bubble_(false),
      previous_thumb_internal_load_(false),
      hle_irq_return_lr_(std::nullopt),
      hle_irq_saved_registers_(std::nullopt),
      hle_irq_reentry_dispatch_pending_(false),
      hle_irq_return_latency_pending_(false),
      hle_irq_post_return_latency_armed_(false),
      hle_irq_post_return_dispatch_pending_(false),
      hle_irq_chained_post_return_dispatch_pending_(false),
      hle_irq_chained_post_return_data_dispatch_pending_(false),
      hle_irq_chained_post_return_spaced_data_dispatch_pending_(false),
      hle_irq_long_timer_chained_return_pending_(false),
      hle_irq_slow_timer0_return_pending_(false),
      hle_irq_post_return_chain_active_(false),
      hle_irq_chained_spaced_data_service_count_(0),
      auto_irq_line_high_(false),
      auto_irq_latency_cycles_(0),
      timer_io_access_gap_cycles_(0) {}

CoreScheduler::CoreScheduler(Arm7tdmi& cpu, MemoryBus& memory,
                             InterruptController& interrupts, Timers& timers,
                             DmaController& dma, PpuTiming& ppu, Apu& apu,
                             const WaitStateControl& waitcnt, BiosController& bios)
    : cpu_(cpu),
      memory_(memory),
      interrupts_(interrupts),
      timers_(timers),
      dma_(dma),
      ppu_(ppu),
      apu_(apu),
      bios_(&bios),
      scheduler_cycles_(0),
      halted_(false),
      waitcnt_(&waitcnt),
      last_waitcnt_control_(std::nullopt),
      prefetch_buffer_halfwords_(0),
      suppress_next_game_pak_prefetch_(false),
      recover_next_game_pak_data_fetch_(false),
      suppress_next_thumb_prefetch_execute_bubble_(false),
      previous_thumb_internal_load_(false),
      hle_irq_return_lr_(std::nullopt),
      hle_irq_saved_registers_(std::nullopt),
      hle_irq_reentry_dispatch_pending_(false),
      hle_irq_return_latency_pending_(false),
      hle_irq_post_return_latency_armed_(false),
      hle_irq_post_return_dispatch_pending_(false),
      hle_irq_chained_post_return_dispatch_pending_(false),
      hle_irq_chained_post_return_data_dispatch_pending_(false),
      hle_irq_chained_post_return_spaced_data_dispatch_pending_(false),
      hle_irq_long_timer_chained_return_pending_(false),
      hle_irq_slow_timer0_return_pending_(false),
      hle_irq_post_return_chain_active_(false),
      hle_irq_chained_spaced_data_service_count_(0),
      auto_irq_line_high_(false),
      auto_irq_latency_cycles_(0),
      timer_io_access_gap_cycles_(0) {}

std::uint64_t CoreScheduler::scheduler_cycles() const {
  return scheduler_cycles_;
}

void CoreScheduler::reset_scheduler_cycles() {
  scheduler_cycles_ = 0;
  halted_ = false;
  hle_irq_return_lr_.reset();
  hle_irq_saved_registers_.reset();
  hle_irq_reentry_dispatch_pending_ = false;
  hle_irq_return_latency_pending_ = false;
  hle_irq_post_return_latency_armed_ = false;
  hle_irq_post_return_dispatch_pending_ = false;
  hle_irq_chained_post_return_dispatch_pending_ = false;
  hle_irq_chained_post_return_data_dispatch_pending_ = false;
  hle_irq_chained_post_return_spaced_data_dispatch_pending_ = false;
  hle_irq_long_timer_chained_return_pending_ = false;
  hle_irq_slow_timer0_return_pending_ = false;
  hle_irq_post_return_chain_active_ = false;
  hle_irq_chained_spaced_data_service_count_ = 0;
  reset_auto_irq_latency();
  reset_fetch_timing_sequence();
  last_waitcnt_control_.reset();
  suppress_next_game_pak_prefetch_ = false;
  recover_next_game_pak_data_fetch_ = false;
  suppress_next_thumb_prefetch_execute_bubble_ = false;
  previous_thumb_internal_load_ = false;
  timer_io_access_gap_cycles_ = 0;
}

CoreSchedulerState CoreScheduler::save_state() const {
  return {scheduler_cycles_, halted_, last_fetch_address_, last_fetch_width_bytes_,
          last_fetch_window_, prefetch_buffer_halfwords_,
          suppress_next_game_pak_prefetch_, recover_next_game_pak_data_fetch_,
          previous_thumb_internal_load_, hle_irq_return_lr_,
          hle_irq_saved_registers_, hle_irq_reentry_dispatch_pending_,
          hle_irq_return_latency_pending_, hle_irq_post_return_latency_armed_,
          hle_irq_post_return_dispatch_pending_,
          hle_irq_chained_post_return_dispatch_pending_,
          hle_irq_chained_post_return_data_dispatch_pending_,
          hle_irq_chained_post_return_spaced_data_dispatch_pending_,
          hle_irq_long_timer_chained_return_pending_,
          hle_irq_slow_timer0_return_pending_,
          hle_irq_post_return_chain_active_,
          hle_irq_chained_spaced_data_service_count_, auto_irq_line_high_,
          auto_irq_latency_cycles_, timer_io_access_gap_cycles_};
}

void CoreScheduler::load_state(const CoreSchedulerState& state) {
  scheduler_cycles_ = state.scheduler_cycles;
  halted_ = state.halted;
  last_fetch_address_ = state.last_fetch_address;
  last_fetch_width_bytes_ = state.last_fetch_width_bytes;
  last_fetch_window_ = state.last_fetch_window;
  prefetch_buffer_halfwords_ = state.prefetch_buffer_halfwords;
  suppress_next_game_pak_prefetch_ = state.suppress_next_game_pak_prefetch;
  recover_next_game_pak_data_fetch_ = state.recover_next_game_pak_data_fetch;
  previous_thumb_internal_load_ = state.previous_thumb_internal_load;
  hle_irq_return_lr_ = state.hle_irq_return_lr;
  hle_irq_saved_registers_ = state.hle_irq_saved_registers;
  hle_irq_reentry_dispatch_pending_ = state.hle_irq_reentry_dispatch_pending;
  hle_irq_return_latency_pending_ = state.hle_irq_return_latency_pending;
  hle_irq_post_return_latency_armed_ = state.hle_irq_post_return_latency_armed;
  hle_irq_post_return_dispatch_pending_ =
      state.hle_irq_post_return_dispatch_pending;
  hle_irq_chained_post_return_dispatch_pending_ =
      state.hle_irq_chained_post_return_dispatch_pending;
  hle_irq_chained_post_return_data_dispatch_pending_ =
      state.hle_irq_chained_post_return_data_dispatch_pending;
  hle_irq_chained_post_return_spaced_data_dispatch_pending_ =
      state.hle_irq_chained_post_return_spaced_data_dispatch_pending;
  hle_irq_long_timer_chained_return_pending_ =
      state.hle_irq_long_timer_chained_return_pending;
  hle_irq_slow_timer0_return_pending_ =
      state.hle_irq_slow_timer0_return_pending;
  hle_irq_post_return_chain_active_ = state.hle_irq_post_return_chain_active;
  hle_irq_chained_spaced_data_service_count_ =
      state.hle_irq_chained_spaced_data_service_count;
  auto_irq_line_high_ = state.auto_irq_line_high;
  auto_irq_latency_cycles_ = state.auto_irq_latency_cycles;
  timer_io_access_gap_cycles_ = state.timer_io_access_gap_cycles;
}

std::uint64_t CoreScheduler::state_hash() const {
  StateHasher hasher;
  hasher.add_u64(scheduler_cycles_);
  hasher.add_bool(halted_);
  hasher.add_bool(last_fetch_address_.has_value());
  if (last_fetch_address_.has_value()) {
    hasher.add_u32(last_fetch_address_.value());
  }
  hasher.add_bool(last_fetch_width_bytes_.has_value());
  if (last_fetch_width_bytes_.has_value()) {
    hasher.add_u8(last_fetch_width_bytes_.value());
  }
  hasher.add_bool(last_fetch_window_.has_value());
  if (last_fetch_window_.has_value()) {
    hasher.add_u8(last_fetch_window_.value());
  }
  hasher.add_u8(prefetch_buffer_halfwords_);
  hasher.add_bool(suppress_next_game_pak_prefetch_);
  hasher.add_bool(recover_next_game_pak_data_fetch_);
  hasher.add_bool(previous_thumb_internal_load_);
  hasher.add_bool(hle_irq_return_lr_.has_value());
  if (hle_irq_return_lr_.has_value()) {
    hasher.add_u32(hle_irq_return_lr_.value());
  }
  hasher.add_bool(hle_irq_saved_registers_.has_value());
  if (hle_irq_saved_registers_.has_value()) {
    hasher.add_bytes(hle_irq_saved_registers_.value());
  }
  hasher.add_bool(hle_irq_reentry_dispatch_pending_);
  hasher.add_bool(hle_irq_return_latency_pending_);
  hasher.add_bool(hle_irq_post_return_latency_armed_);
  hasher.add_bool(hle_irq_post_return_dispatch_pending_);
  hasher.add_bool(hle_irq_chained_post_return_dispatch_pending_);
  hasher.add_bool(hle_irq_chained_post_return_data_dispatch_pending_);
  hasher.add_bool(hle_irq_chained_post_return_spaced_data_dispatch_pending_);
  hasher.add_bool(hle_irq_long_timer_chained_return_pending_);
  hasher.add_bool(hle_irq_slow_timer0_return_pending_);
  hasher.add_bool(hle_irq_post_return_chain_active_);
  hasher.add_u8(hle_irq_chained_spaced_data_service_count_);
  hasher.add_bool(auto_irq_line_high_);
  hasher.add_u8(auto_irq_latency_cycles_);
  hasher.add_u32(timer_io_access_gap_cycles_);
  return hasher.value();
}

bool CoreScheduler::halted() const {
  return halted_;
}

void CoreScheduler::halt_until_interrupt() {
  halted_ = true;
}

bool CoreScheduler::wake_from_halt_if_irq_pending() {
  if (!halted_ || !interrupts_.irq_line()) {
    return false;
  }
  halted_ = false;
  return true;
}

CoreDeviceTickResult CoreScheduler::advance_devices(std::uint32_t cycles) {
  if (cycles == 0) {
    return {0, std::nullopt, 0, {}, {}};
  }

  const std::uint64_t timer0_overflows_before = timers_.overflow_count(0);
  const std::uint64_t timer1_overflows_before = timers_.overflow_count(1);
  const bool irq_line_active_before =
      interrupts_.irq_line() && !cpu_.irq_disabled();
  const Timers::TickResult timer_tick = timers_.tick(cycles, interrupts_);
  const PpuTickEvents ppu_events = ppu_.tick(cycles, interrupts_);
  std::uint32_t apu_timer_events = 0;
  DirectSoundTimerResult last_direct_sound{};
  DmaRunResult triggered_dma{0, 0, false, 0};
  const auto accumulate_dma = [&](const DmaRunResult& event_result) {
    triggered_dma.channels_executed = static_cast<std::uint8_t>(
        std::min<std::uint32_t>(255U, triggered_dma.channels_executed +
                                          event_result.channels_executed));
    triggered_dma.units_transferred += event_result.units_transferred;
    triggered_dma.bus_cycles += event_result.bus_cycles;
    triggered_dma.unsupported_request =
        triggered_dma.unsupported_request || event_result.unsupported_request;
  };
  for (std::uint16_t event = 0; event < ppu_events.hblank_entries; ++event) {
    accumulate_dma(dma_.run_trigger(DmaTrigger::hblank, memory_, interrupts_,
                                    waitcnt_));
  }
  for (std::uint16_t event = 0; event < ppu_events.vblank_entries; ++event) {
    accumulate_dma(dma_.run_trigger(DmaTrigger::vblank, memory_, interrupts_,
                                    waitcnt_));
  }
  const auto consume_direct_sound_timer = [&](std::uint8_t timer_index,
                                              std::uint64_t overflow_delta) {
    for (std::uint64_t event = 0; event < overflow_delta; ++event) {
      last_direct_sound = apu_.timer_overflow(timer_index);
      if (last_direct_sound.fifo_a.refill_request) {
        accumulate_dma(dma_.run_sound_fifo(DmaTrigger::fifo_a, memory_, apu_,
                                           interrupts_, waitcnt_));
      }
      if (last_direct_sound.fifo_b.refill_request) {
        accumulate_dma(dma_.run_sound_fifo(DmaTrigger::fifo_b, memory_, apu_,
                                           interrupts_, waitcnt_));
      }
      if (apu_timer_events != std::numeric_limits<std::uint32_t>::max()) {
        ++apu_timer_events;
      }
    }
  };
  consume_direct_sound_timer(
      0, timers_.overflow_count(0) - timer0_overflows_before);
  consume_direct_sound_timer(
      1, timers_.overflow_count(1) - timer1_overflows_before);
  std::optional<ApuFrameStep> apu_frame_step = apu_.tick(cycles);
  scheduler_cycles_ += cycles + triggered_dma.bus_cycles;
  const std::uint32_t total_visible_cycles = cycles + triggered_dma.bus_cycles;
  if (total_visible_cycles != 0) {
    timer_io_access_gap_cycles_ =
        std::numeric_limits<std::uint32_t>::max() - timer_io_access_gap_cycles_ <
                total_visible_cycles
            ? std::numeric_limits<std::uint32_t>::max()
            : timer_io_access_gap_cycles_ + total_visible_cycles;
  }
  const bool irq_line_active_after =
      interrupts_.irq_line() && !cpu_.irq_disabled();
  if (irq_line_active_before || !timer_tick.first_irq_cycle.has_value() ||
      !irq_line_active_after) {
    update_auto_irq_latency(total_visible_cycles);
  } else {
    const bool post_hle_return_irq = hle_irq_return_latency_pending_;
    const bool chained_post_hle_return_irq =
        post_hle_return_irq && hle_irq_post_return_chain_active_;
    const bool long_timer_chained_post_hle_return_irq =
        chained_post_hle_return_irq &&
        interrupts_.requested(InterruptSource::timer0) &&
        timers_.reload(0) <= 0xFF80U;
    const bool slow_timer0_irq =
        interrupts_.requested(InterruptSource::timer0) &&
        timers_.prescaler_divisor(0) > 1U;
    const bool slow_timer_chained_post_hle_return_irq =
        chained_post_hle_return_irq && slow_timer0_irq;
    const bool slow_timer_normal_irq = !post_hle_return_irq && slow_timer0_irq;
    const bool phase184_second_spaced_data_irq =
        slow_timer_chained_post_hle_return_irq && timers_.reload(0) == 0xFFEEU &&
        timers_.prescaler_divisor(0) == 1024U &&
        timers_.last_enable_phase(0) == 184U &&
        hle_irq_chained_spaced_data_service_count_ == 1U;
    auto_irq_line_high_ = true;
    auto_irq_latency_cycles_ =
        long_timer_chained_post_hle_return_irq
            ? kAutoIrqLongTimerChainedPostHleReturnLatencyCycles
        : phase184_second_spaced_data_irq
            ? kAutoIrqPhase184SecondSpacedDataLatencyCycles
        : slow_timer_chained_post_hle_return_irq
            ? kAutoIrqSlowTimerChainedPostHleReturnLatencyCycles
        : chained_post_hle_return_irq
            ? kAutoIrqChainedPostHleReturnLatencyCycles
        : post_hle_return_irq ? kAutoIrqPostHleReturnLatencyCycles
        : slow_timer_normal_irq ? kAutoIrqSlowTimerLatencyCycles
                                : kAutoIrqLatencyCycles;
    hle_irq_post_return_latency_armed_ = post_hle_return_irq;
    hle_irq_return_latency_pending_ = false;
    const std::uint32_t event_cycle =
        std::min<std::uint32_t>(timer_tick.first_irq_cycle.value(), cycles);
    const std::uint32_t cycles_after_event = cycles - event_cycle;
    if (cycles_after_event != 0 || triggered_dma.bus_cycles != 0) {
      update_auto_irq_latency(cycles_after_event + triggered_dma.bus_cycles);
    }
  }
  return {cycles, apu_frame_step, apu_timer_events, last_direct_sound, triggered_dma};
}

DmaRunResult CoreScheduler::run_immediate_dma() {
  return dma_.run_immediate(memory_, interrupts_, waitcnt_);
}

bool CoreScheduler::service_pending_irq(bool from_data_access,
                                        bool from_spaced_timer_io_access) {
  const bool serviced = interrupts_.service_pending_irq(cpu_);
  if (serviced && hle_irq_post_return_latency_armed_) {
    const bool chained_spaced_data_service =
        hle_irq_post_return_chain_active_ && from_data_access &&
        from_spaced_timer_io_access;
    hle_irq_post_return_dispatch_pending_ = true;
    hle_irq_chained_post_return_dispatch_pending_ =
        hle_irq_post_return_chain_active_;
    hle_irq_chained_post_return_data_dispatch_pending_ =
        hle_irq_post_return_chain_active_ && from_data_access;
    hle_irq_chained_post_return_spaced_data_dispatch_pending_ =
        hle_irq_post_return_chain_active_ && from_data_access &&
        from_spaced_timer_io_access;
    hle_irq_post_return_chain_active_ = true;
    hle_irq_chained_spaced_data_service_count_ =
        chained_spaced_data_service
            ? static_cast<std::uint8_t>(
                  std::min<std::uint16_t>(
                      static_cast<std::uint16_t>(
                          hle_irq_chained_spaced_data_service_count_) +
                          1U,
                      std::numeric_limits<std::uint8_t>::max()))
            : 0U;
  } else if (serviced) {
    hle_irq_chained_post_return_dispatch_pending_ = false;
    hle_irq_chained_post_return_data_dispatch_pending_ = false;
    hle_irq_chained_post_return_spaced_data_dispatch_pending_ = false;
    hle_irq_post_return_chain_active_ = false;
    hle_irq_chained_spaced_data_service_count_ = 0;
  }
  if (serviced) {
    hle_irq_post_return_latency_armed_ = false;
  }
  return serviced;
}

CoreSchedulerStepResult CoreScheduler::step_arm(
    std::uint32_t instruction,
    std::optional<ArmElapsedCycleEstimate> elapsed_override) {
  const std::uint32_t pre_step_pc = cpu_.register_value(Arm7tdmi::kPc);
  const std::optional<DataAccessTimingProbe> data_access =
      first_arm_data_access_for_timing(cpu_, instruction);
  const bool timer_io_access =
      data_access.has_value() && timer_io_address(data_access->address);
  const std::uint32_t timer_io_gap_cycles =
      timer_io_access ? timer_io_access_gap_cycles_ : 0;
  const bool spaced_timer_io_access =
      timer_io_access && timer_io_gap_cycles >= kSpacedTimerIoDispatchGapCycles;
  const bool return_latency_pending_before_pre_access =
      hle_irq_return_latency_pending_;
  std::uint32_t pre_access_cycles =
      data_access.has_value() ? io_store_pre_access_cycles(data_access.value())
                              : 0;
  pre_access_cycles = adjust_slow_timer_load_pre_access_cycles(
      pre_access_cycles, data_access, timer_io_access, spaced_timer_io_access,
      timer_io_gap_cycles, return_latency_pending_before_pre_access,
      hle_irq_post_return_chain_active_,
      interrupts_.requested(InterruptSource::timer0), timers_);
  const std::optional<CoreDataAccessTrace> data_access_trace =
      data_access.has_value()
          ? std::optional<CoreDataAccessTrace>{CoreDataAccessTrace{
                data_access->address, access_width_bytes(data_access->width),
                data_access->load, timer_io_access, pre_access_cycles,
                timer_io_gap_cycles}}
          : std::nullopt;
  CoreDeviceTickResult devices{0, std::nullopt};
  bool deferred_slow_timer_load_irq_after_access = false;
  if (pre_access_cycles != 0) {
    devices = advance_devices(pre_access_cycles);
    const bool chained_post_return_timer_io_irq =
        hle_irq_post_return_latency_armed_ && hle_irq_post_return_chain_active_;
    const bool armed_slow_timer0_post_return_timer_io_irq =
        hle_irq_post_return_latency_armed_ &&
        interrupts_.requested(InterruptSource::timer0) &&
        timers_.prescaler_divisor(0) > 1U;
    const bool slow_timer0_post_return_timer_io_irq =
        return_latency_pending_before_pre_access &&
        interrupts_.requested(InterruptSource::timer0) &&
        timers_.prescaler_divisor(0) > 1U;
    const bool defer_slow_timer_load_irq =
        should_defer_slow_timer_load_irq_until_after_access(
            data_access, timer_io_access, spaced_timer_io_access,
            pre_access_cycles, return_latency_pending_before_pre_access,
            hle_irq_post_return_chain_active_,
            interrupts_.requested(InterruptSource::timer0), timers_);
    deferred_slow_timer_load_irq_after_access = defer_slow_timer_load_irq;
    if (data_access.has_value() && data_access->load && timer_io_access &&
        (chained_post_return_timer_io_irq ||
         armed_slow_timer0_post_return_timer_io_irq ||
         slow_timer0_post_return_timer_io_irq) &&
        !defer_slow_timer_load_irq &&
        auto_irq_ready()) {
      const bool replay_slow_timer_load = should_replay_slow_timer_load_after_irq(
          data_access, timer_io_access, spaced_timer_io_access,
          hle_irq_post_return_latency_armed_ ||
              slow_timer0_post_return_timer_io_irq,
          interrupts_.requested(InterruptSource::timer0), timers_);
      const std::uint32_t irq_pc_before = cpu_.register_value(Arm7tdmi::kPc);
      if (replay_slow_timer_load) {
        cpu_.set_register(Arm7tdmi::kPc, pre_step_pc);
      }
      const bool serviced = service_pending_irq(true, spaced_timer_io_access);
      if (!serviced && replay_slow_timer_load) {
        cpu_.set_register(Arm7tdmi::kPc, irq_pc_before);
      }
      if (serviced) {
        timer_io_access_gap_cycles_ = 0;
        hle_irq_return_latency_pending_ = false;
        reset_auto_irq_latency();
        const ArmStepResult irq_step{ExecuteStatus::executed, pre_access_cycles,
                                     cpu_.elapsed_cycles(), false, false};
        return {irq_step, devices, {}, true, scheduler_cycles_, data_access_trace};
      }
    }
  }
  const std::optional<ArmStepResult> hle_step =
      execute_hle_arm_swi(instruction, pre_step_pc);
  const ArmStepResult cpu_step =
      hle_step.has_value()
          ? hle_step.value()
          : (waitcnt_ != nullptr ? cpu_.step_arm(instruction, memory_, *waitcnt_,
                                                 elapsed_override)
                                 : cpu_.step_arm(instruction, memory_));
  DmaRunResult dma_result{0, 0, false, 0};
  bool irq_serviced = false;

  if (cpu_step.status == ExecuteStatus::skipped_condition &&
      cpu_step.elapsed_cycles > 0) {
    const CoreDeviceTickResult skipped_devices =
        advance_devices(cpu_step.elapsed_cycles);
    merge_device_ticks(devices, skipped_devices);
  }

  if (cpu_step.status == ExecuteStatus::executed && cpu_step.elapsed_cycles > 0) {
    if (pre_access_cycles != 0 && data_access.has_value() && !data_access->load &&
        timer_io_access) {
      hle_irq_return_latency_pending_ = false;
      hle_irq_post_return_latency_armed_ = false;
      hle_irq_post_return_chain_active_ = false;
      timers_.defer_newly_enabled_ticks();
    }
    const std::uint32_t remaining_cycles =
        cpu_step.elapsed_cycles > pre_access_cycles
            ? cpu_step.elapsed_cycles - pre_access_cycles
            : 0;
    const bool irq_ready_before_remaining_cycles = auto_irq_ready();
    if (remaining_cycles != 0) {
      const CoreDeviceTickResult remaining_devices = advance_devices(remaining_cycles);
      merge_device_ticks(devices, remaining_devices);
    }
    dma_result = run_immediate_dma();
    if (dma_result.bus_cycles != 0 &&
        immediate_dma_bus_visible_to_timers(pre_step_pc, false, waitcnt_)) {
      const CoreDeviceTickResult dma_devices = advance_devices(dma_result.bus_cycles);
      merge_device_ticks(devices, dma_devices);
    }
    const bool sequential_pc = cpu_.register_value(Arm7tdmi::kPc) == pre_step_pc;
    const bool defer_chained_post_return_irq =
        !irq_ready_before_remaining_cycles && !data_access.has_value() &&
        cpu_step.elapsed_cycles == 1 &&
        !arm_test_or_compare_instruction(instruction) &&
        hle_irq_post_return_latency_armed_ &&
        hle_irq_post_return_chain_active_ && auto_irq_ready();
    if (sequential_pc && auto_irq_ready() && !defer_chained_post_return_irq) {
      cpu_.set_register(Arm7tdmi::kPc, pre_step_pc + 4U);
    }
    irq_serviced = sequential_pc && auto_irq_ready() &&
                   !defer_chained_post_return_irq &&
                   service_pending_irq(deferred_slow_timer_load_irq_after_access,
                                       deferred_slow_timer_load_irq_after_access &&
                                           spaced_timer_io_access);
    if (irq_serviced) {
      reset_auto_irq_latency();
    }
    if (!irq_serviced && sequential_pc &&
        cpu_.register_value(Arm7tdmi::kPc) == pre_step_pc + 4U) {
      cpu_.set_register(Arm7tdmi::kPc, pre_step_pc);
    }
    if (data_access.has_value() &&
        (waitcnt_ == nullptr || !waitcnt_->prefetch_enabled())) {
      const bool suppress_game_pak_prefetch =
          game_pak_data_access_disturbs_prefetch(data_access->address, pre_step_pc);
      const bool recover_game_pak_data_fetch = non_word_game_pak_data_access(
          data_access->address, data_access->width) &&
          suppress_game_pak_prefetch;
      reset_fetch_timing_sequence();
      suppress_next_game_pak_prefetch_ = suppress_game_pak_prefetch;
      recover_next_game_pak_data_fetch_ = recover_game_pak_data_fetch;
    }
  }

  if (timer_io_access) {
    timer_io_access_gap_cycles_ = 0;
  }
  return {cpu_step, devices, dma_result, irq_serviced, scheduler_cycles_,
          data_access_trace};
}

CoreSchedulerStepResult CoreScheduler::step_thumb(
    std::uint16_t instruction, bool prefetch_internal_load_overlap,
    std::optional<ArmElapsedCycleEstimate> elapsed_override) {
  const std::uint32_t pre_step_pc = cpu_.register_value(Arm7tdmi::kPc);
  const std::optional<DataAccessTimingProbe> data_access =
      first_thumb_data_access_for_timing(cpu_, instruction);
  const bool timer_io_access =
      data_access.has_value() && timer_io_address(data_access->address);
  const std::uint32_t timer_io_gap_cycles =
      timer_io_access ? timer_io_access_gap_cycles_ : 0;
  const bool spaced_timer_io_access =
      timer_io_access && timer_io_gap_cycles >= kSpacedTimerIoDispatchGapCycles;
  const bool return_latency_pending_before_pre_access =
      hle_irq_return_latency_pending_;
  std::uint32_t pre_access_cycles =
      data_access.has_value() ? io_store_pre_access_cycles(data_access.value())
                              : 0;
  pre_access_cycles = adjust_slow_timer_load_pre_access_cycles(
      pre_access_cycles, data_access, timer_io_access, spaced_timer_io_access,
      timer_io_gap_cycles, return_latency_pending_before_pre_access,
      hle_irq_post_return_chain_active_,
      interrupts_.requested(InterruptSource::timer0), timers_);
  const std::optional<CoreDataAccessTrace> data_access_trace =
      data_access.has_value()
          ? std::optional<CoreDataAccessTrace>{CoreDataAccessTrace{
                data_access->address, access_width_bytes(data_access->width),
                data_access->load, timer_io_access, pre_access_cycles,
                timer_io_gap_cycles}}
          : std::nullopt;
  CoreDeviceTickResult devices{0, std::nullopt};
  bool deferred_slow_timer_load_irq_after_access = false;
  if (pre_access_cycles != 0) {
    devices = advance_devices(pre_access_cycles);
    const bool chained_post_return_timer_io_irq =
        hle_irq_post_return_latency_armed_ && hle_irq_post_return_chain_active_;
    const bool armed_slow_timer0_post_return_timer_io_irq =
        hle_irq_post_return_latency_armed_ &&
        interrupts_.requested(InterruptSource::timer0) &&
        timers_.prescaler_divisor(0) > 1U;
    const bool slow_timer0_post_return_timer_io_irq =
        return_latency_pending_before_pre_access &&
        interrupts_.requested(InterruptSource::timer0) &&
        timers_.prescaler_divisor(0) > 1U;
    const bool defer_slow_timer_load_irq =
        should_defer_slow_timer_load_irq_until_after_access(
            data_access, timer_io_access, spaced_timer_io_access,
            pre_access_cycles, return_latency_pending_before_pre_access,
            hle_irq_post_return_chain_active_,
            interrupts_.requested(InterruptSource::timer0), timers_);
    deferred_slow_timer_load_irq_after_access = defer_slow_timer_load_irq;
    if (data_access.has_value() && data_access->load && timer_io_access &&
        (chained_post_return_timer_io_irq ||
         armed_slow_timer0_post_return_timer_io_irq ||
         slow_timer0_post_return_timer_io_irq) &&
        !defer_slow_timer_load_irq && auto_irq_ready() &&
        service_pending_irq(true, spaced_timer_io_access)) {
      timer_io_access_gap_cycles_ = 0;
      hle_irq_return_latency_pending_ = false;
      reset_auto_irq_latency();
      const ArmStepResult irq_step{ExecuteStatus::executed, pre_access_cycles,
                                   cpu_.elapsed_cycles(), false, false};
      return {irq_step, devices, {}, true, scheduler_cycles_, data_access_trace};
    }
  }
  const std::optional<ArmStepResult> hle_step =
      execute_hle_thumb_swi(instruction, pre_step_pc);
  const ArmStepResult cpu_step =
      hle_step.has_value()
          ? hle_step.value()
          : (waitcnt_ != nullptr ? cpu_.step_thumb(instruction, memory_, *waitcnt_,
                                                   prefetch_internal_load_overlap,
                                                   elapsed_override)
                                 : cpu_.step_thumb(instruction, memory_));
  DmaRunResult dma_result{0, 0, false, 0};
  bool irq_serviced = false;

  if (cpu_step.status == ExecuteStatus::skipped_condition &&
      cpu_step.elapsed_cycles > 0) {
    const CoreDeviceTickResult skipped_devices =
        advance_devices(cpu_step.elapsed_cycles);
    merge_device_ticks(devices, skipped_devices);
  }

  if (cpu_step.status == ExecuteStatus::executed && cpu_step.elapsed_cycles > 0) {
    if (pre_access_cycles != 0 && data_access.has_value() && !data_access->load &&
        timer_io_access) {
      hle_irq_return_latency_pending_ = false;
      hle_irq_post_return_latency_armed_ = false;
      hle_irq_post_return_chain_active_ = false;
      timers_.defer_newly_enabled_ticks();
    }
    const std::uint32_t remaining_cycles =
        cpu_step.elapsed_cycles > pre_access_cycles
            ? cpu_step.elapsed_cycles - pre_access_cycles
            : 0;
    if (remaining_cycles != 0) {
      const CoreDeviceTickResult remaining_devices = advance_devices(remaining_cycles);
      merge_device_ticks(devices, remaining_devices);
    }
    dma_result = run_immediate_dma();
    if (dma_result.bus_cycles != 0 &&
        immediate_dma_bus_visible_to_timers(pre_step_pc, true, waitcnt_)) {
      const CoreDeviceTickResult dma_devices = advance_devices(dma_result.bus_cycles);
      merge_device_ticks(devices, dma_devices);
    }
    const bool sequential_pc = cpu_.register_value(Arm7tdmi::kPc) == pre_step_pc;
    if (sequential_pc && auto_irq_ready()) {
      cpu_.set_register(Arm7tdmi::kPc, pre_step_pc + 2U);
    }
    irq_serviced = sequential_pc && auto_irq_ready() &&
                   service_pending_irq(deferred_slow_timer_load_irq_after_access,
                                       deferred_slow_timer_load_irq_after_access &&
                                           spaced_timer_io_access);
    if (irq_serviced) {
      reset_auto_irq_latency();
    }
    if (!irq_serviced && sequential_pc &&
        cpu_.register_value(Arm7tdmi::kPc) == pre_step_pc + 2U) {
      cpu_.set_register(Arm7tdmi::kPc, pre_step_pc);
    }
    if (data_access.has_value() &&
        (waitcnt_ == nullptr || !waitcnt_->prefetch_enabled())) {
      const bool suppress_game_pak_prefetch =
          game_pak_data_access_disturbs_prefetch(data_access->address, pre_step_pc);
      const bool recover_game_pak_data_fetch = non_word_game_pak_data_access(
          data_access->address, data_access->width) &&
          suppress_game_pak_prefetch;
      reset_fetch_timing_sequence();
      suppress_next_game_pak_prefetch_ = suppress_game_pak_prefetch;
      recover_next_game_pak_data_fetch_ = recover_game_pak_data_fetch;
    }
  }

  if (timer_io_access) {
    timer_io_access_gap_cycles_ = 0;
  }
  return {cpu_step, devices, dma_result, irq_serviced, scheduler_cycles_,
          data_access_trace};
}

std::uint32_t CoreScheduler::apply_fetch_timing(std::uint32_t fetch_address,
                                                std::uint8_t width_bytes,
                                                bool& timing_applied,
                                                bool& sequential,
                                                bool& prefetch_enabled,
                                                bool& prefetch_hit,
                                                bool& boundary_forced_nonsequential) {
  timing_applied = false;
  sequential = false;
  prefetch_enabled = false;
  prefetch_hit = false;
  boundary_forced_nonsequential = false;

  if (waitcnt_ == nullptr) {
    reset_fetch_timing_sequence();
    last_waitcnt_control_.reset();
    return 0;
  }

  const std::uint16_t waitcnt_control = waitcnt_->read_control();
  if (!last_waitcnt_control_.has_value() ||
      last_waitcnt_control_.value() != waitcnt_control) {
    reset_fetch_timing_sequence();
    last_waitcnt_control_ = waitcnt_control;
  }

  const AddressInfo address = MemoryBus::describe(fetch_address);
  const CartridgeAddressInfo cartridge = MemoryBus::describe_cartridge(fetch_address);
  if (address.region == Region::ewram) {
    reset_fetch_timing_sequence();
    const MemoryAccessTiming timing =
        MemoryBus::timing(fetch_address,
                          width_bytes == 4 ? AccessWidth::word : AccessWidth::halfword,
                          *waitcnt_);
    const std::uint32_t cycles =
        timing.nonsequential > 0 ? timing.nonsequential - 1U : 0U;
    timing_applied = cycles != 0;
    if (timing_applied) {
      [[maybe_unused]] const CoreDeviceTickResult devices = advance_devices(cycles);
    }
    return cycles;
  }

  if (address.region != Region::game_pak_rom) {
    reset_fetch_timing_sequence();
    return 0;
  }

  const std::uint8_t window = static_cast<std::uint8_t>(cartridge.window);
  if (last_fetch_address_.has_value() && last_fetch_width_bytes_.has_value() &&
      last_fetch_window_.has_value() &&
      fetch_address == last_fetch_address_.value() + last_fetch_width_bytes_.value() &&
      window == last_fetch_window_.value()) {
    sequential = true;
  }
  if (sequential && (fetch_address % kGamePakSequentialBoundary) == 0) {
    sequential = false;
    boundary_forced_nonsequential = true;
  }

  const MemoryAccessTiming timing =
      MemoryBus::timing(fetch_address,
                        width_bytes == 4 ? AccessWidth::word : AccessWidth::halfword,
                        *waitcnt_);
  prefetch_enabled = waitcnt_->prefetch_enabled();
  if (suppress_next_game_pak_prefetch_) {
    prefetch_enabled = false;
    prefetch_buffer_halfwords_ = 0;
    suppress_next_game_pak_prefetch_ = false;
  }
  const std::uint8_t required_halfwords = static_cast<std::uint8_t>(width_bytes / 2U);
  std::uint32_t cycles = sequential ? timing.sequential : timing.nonsequential;
  if (width_bytes == 4) {
    cycles += timing.sequential;
  }
  if (!prefetch_enabled && width_bytes == 4) {
    ++cycles;
  }
  if (recover_next_game_pak_data_fetch_ && timing.sequential == 1) {
    ++cycles;
  }
  recover_next_game_pak_data_fetch_ = false;
  if (prefetch_enabled && sequential &&
      prefetch_buffer_halfwords_ >= required_halfwords) {
    cycles = width_bytes == 4
                 ? static_cast<std::uint32_t>(timing.sequential) *
                       required_halfwords
                 : (timing.sequential > 0 ? timing.sequential - 1U : 0U);
    prefetch_hit = true;
    prefetch_buffer_halfwords_ =
        static_cast<std::uint8_t>(prefetch_buffer_halfwords_ - required_halfwords);
    if (width_bytes == 2 && cycles != 0) {
      const std::uint32_t capped =
          std::min<std::uint32_t>(kPrefetchBufferCapacityHalfwords,
                                  prefetch_buffer_halfwords_ + cycles);
      prefetch_buffer_halfwords_ = static_cast<std::uint8_t>(capped);
    }
  }
  timing_applied = cycles != 0;
  if (timing_applied) {
    [[maybe_unused]] const CoreDeviceTickResult devices = advance_devices(cycles);
  }

  last_fetch_address_ = fetch_address;
  last_fetch_width_bytes_ = width_bytes;
  last_fetch_window_ = window;
  return cycles;
}

void CoreScheduler::refill_prefetch_after_step(std::uint32_t fetch_address,
                                               std::uint8_t width_bytes,
                                               const CoreSchedulerStepResult& step,
                                               std::uint32_t extra_refill_cycles) {
  if (waitcnt_ == nullptr) {
    return;
  }

  const std::uint16_t waitcnt_control = waitcnt_->read_control();
  if (!last_waitcnt_control_.has_value() ||
      last_waitcnt_control_.value() != waitcnt_control) {
    prefetch_buffer_halfwords_ = 0;
    last_waitcnt_control_ = waitcnt_control;
  }

  if (!waitcnt_->prefetch_enabled() ||
      step.cpu_step.status != ExecuteStatus::executed ||
      step.cpu_step.elapsed_cycles <= 1) {
    return;
  }

  const AddressInfo address = MemoryBus::describe(fetch_address);
  if (address.region != Region::game_pak_rom) {
    prefetch_buffer_halfwords_ = 0;
    return;
  }
  if (step.data_access.has_value()) {
    const AccessWidth width =
        step.data_access->width_bytes == 1
            ? AccessWidth::byte
            : (step.data_access->width_bytes == 2 ? AccessWidth::halfword
                                                   : AccessWidth::word);
    if (game_pak_data_access_disturbs_prefetch(step.data_access->address,
                                               fetch_address)) {
      const bool recover_game_pak_data_fetch =
          non_word_game_pak_data_access(step.data_access->address, width);
      reset_fetch_timing_sequence();
      suppress_next_game_pak_prefetch_ = true;
      recover_next_game_pak_data_fetch_ = recover_game_pak_data_fetch;
      return;
    }
  }

  const std::uint32_t next_fetch_address = fetch_address + width_bytes;
  if ((next_fetch_address % kGamePakSequentialBoundary) == 0) {
    prefetch_buffer_halfwords_ = 0;
    return;
  }

  const MemoryAccessTiming timing =
      MemoryBus::timing(next_fetch_address, AccessWidth::halfword, *waitcnt_);
  if (timing.sequential == 0) {
    return;
  }

  const std::uint32_t spare_cycles =
      step.cpu_step.elapsed_cycles - 1U + extra_refill_cycles;
  const std::uint32_t refill_halfwords = spare_cycles / timing.sequential;
  const std::uint32_t capped =
      std::min<std::uint32_t>(kPrefetchBufferCapacityHalfwords,
                              prefetch_buffer_halfwords_ + refill_halfwords);
  prefetch_buffer_halfwords_ = static_cast<std::uint8_t>(capped);
}

void CoreScheduler::reset_fetch_timing_sequence() {
  last_fetch_address_.reset();
  last_fetch_width_bytes_.reset();
  last_fetch_window_.reset();
  prefetch_buffer_halfwords_ = 0;
  suppress_next_game_pak_prefetch_ = false;
  recover_next_game_pak_data_fetch_ = false;
}

void CoreScheduler::update_auto_irq_latency(std::uint32_t elapsed_cycles) {
  const bool line_active = interrupts_.irq_line() && !cpu_.irq_disabled();
  if (!line_active) {
    reset_auto_irq_latency();
    return;
  }

  if (!auto_irq_line_high_) {
    auto_irq_line_high_ = true;
    auto_irq_latency_cycles_ = kAutoIrqLatencyCycles;
    return;
  }

  if (auto_irq_latency_cycles_ > elapsed_cycles) {
    auto_irq_latency_cycles_ =
        static_cast<std::uint8_t>(auto_irq_latency_cycles_ - elapsed_cycles);
  } else {
    auto_irq_latency_cycles_ = 0;
  }
}

bool CoreScheduler::auto_irq_ready() const {
  return interrupts_.irq_line() && !cpu_.irq_disabled() &&
         auto_irq_line_high_ && auto_irq_latency_cycles_ == 0;
}

void CoreScheduler::reset_auto_irq_latency() {
  auto_irq_line_high_ = false;
  auto_irq_latency_cycles_ = 0;
  hle_irq_post_return_latency_armed_ = false;
}

bool CoreScheduler::bios_hle_enabled() const {
  return bios_ != nullptr && bios_->mode() == BiosExecutionMode::hle;
}

std::optional<CoreSchedulerStepResult> CoreScheduler::dispatch_hle_irq_vector(
    std::uint32_t fetch_address) {
  if (!bios_hle_enabled() || fetch_address != kBiosIrqVector ||
      cpu_.current_mode() != CpuMode::irq) {
    return std::nullopt;
  }

  const std::optional<std::uint32_t> handler = memory_.read32(kUserIrqHandlerPointer);
  if (!handler.has_value() || handler.value() == 0) {
    return std::nullopt;
  }

  const bool thumb_handler = (handler.value() & 0x1U) != 0;
  const std::uint32_t cpsr = cpu_.cpsr();
  if (!cpu_.set_cpsr(thumb_handler ? (cpsr | 0x20U) : (cpsr & ~0x20U))) {
    return std::nullopt;
  }
  const std::uint32_t timer0_reload = timers_.reload(0);
  const bool long_timer_chained_post_return_dispatch =
      hle_irq_chained_post_return_dispatch_pending_ &&
      interrupts_.requested(InterruptSource::timer0) && timer0_reload <= 0xFF80U;
  const bool spaced_long_timer_data_dispatch =
      long_timer_chained_post_return_dispatch &&
      hle_irq_chained_post_return_spaced_data_dispatch_pending_ &&
      timer0_reload >= 0xF800U && timer0_reload < 0xFF80U;
  std::uint32_t chained_dispatch_cycles =
      kBiosHleChainedPostReturnIrqDispatchCycles;
  if (long_timer_chained_post_return_dispatch &&
      !hle_irq_chained_post_return_data_dispatch_pending_) {
    chained_dispatch_cycles += kBiosHleLongTimerNonDataIrqDispatchExtraCycles;
    if (timer0_reload <= 0x8000U &&
        timer_io_access_gap_cycles_ >= kLooseTimerIoIrqDispatchGapCycles) {
      chained_dispatch_cycles -=
          kBiosHleVeryLongTimerNonDataIrqDispatchAdvanceCycles;
    }
  } else if (spaced_long_timer_data_dispatch) {
    chained_dispatch_cycles -=
        kBiosHleSpacedTimerDataIrqDispatchAdvanceCycles;
  }
  const std::uint32_t dispatch_cycles =
      hle_irq_reentry_dispatch_pending_
          ? kBiosHleIrqReentryDispatchCycles
      : hle_irq_chained_post_return_dispatch_pending_
          ? chained_dispatch_cycles
      : hle_irq_post_return_dispatch_pending_
          ? kBiosHlePostReturnIrqDispatchCycles
          : kBiosHleIrqDispatchCycles;
  hle_irq_reentry_dispatch_pending_ = false;
  hle_irq_post_return_dispatch_pending_ = false;
  hle_irq_chained_post_return_dispatch_pending_ = false;
  hle_irq_chained_post_return_data_dispatch_pending_ = false;
  hle_irq_chained_post_return_spaced_data_dispatch_pending_ = false;
  hle_irq_long_timer_chained_return_pending_ =
      long_timer_chained_post_return_dispatch;
  hle_irq_slow_timer0_return_pending_ =
      interrupts_.requested(InterruptSource::timer0) &&
      timers_.prescaler_divisor(0) > 1U;
  hle_irq_return_lr_ = cpu_.register_value(Arm7tdmi::kLinkRegister);
  std::array<std::uint32_t, 13> saved_registers{};
  for (std::uint8_t reg = 0; reg < saved_registers.size(); ++reg) {
    saved_registers.at(reg) = cpu_.register_value(reg);
  }
  hle_irq_saved_registers_ = saved_registers;
  cpu_.set_register(Arm7tdmi::kLinkRegister, kBiosHleIrqReturnSentinel);
  cpu_.set_register(Arm7tdmi::kPc, handler.value() & ~1U);
  const ArmStepResult cpu_step{ExecuteStatus::executed, dispatch_cycles,
                               cpu_.elapsed_cycles(), false, false};
  const CoreDeviceTickResult devices = advance_devices(cpu_step.elapsed_cycles);
  return CoreSchedulerStepResult{cpu_step, devices, {}, false, scheduler_cycles_,
                                 std::nullopt};
}

std::optional<CoreSchedulerStepResult> CoreScheduler::dispatch_hle_irq_return(
    std::uint32_t fetch_address) {
  if (!bios_hle_enabled() || !hle_irq_return_lr_.has_value() ||
      fetch_address != kBiosHleIrqReturnSentinel || cpu_.current_mode() != CpuMode::irq) {
    return std::nullopt;
  }

  cpu_.set_register(Arm7tdmi::kLinkRegister, hle_irq_return_lr_.value());
  hle_irq_return_lr_.reset();
  if (hle_irq_saved_registers_.has_value()) {
    for (std::uint8_t reg = 0; reg < hle_irq_saved_registers_->size(); ++reg) {
      cpu_.set_register(reg, hle_irq_saved_registers_->at(reg));
    }
    hle_irq_saved_registers_.reset();
  }
  const ExecuteStatus status = cpu_.return_from_exception(4);
  const bool slow_timer0_active_return =
      hle_irq_slow_timer0_return_pending_ && timers_.enabled(0) &&
      timers_.prescaler_divisor(0) > 1U;
  const std::uint32_t return_cycles =
      kBiosHleIrqReturnCycles +
      (hle_irq_long_timer_chained_return_pending_
           ? kBiosHleLongTimerChainedPostReturnExtraCycles
           : 0U) +
      (slow_timer0_active_return ? kBiosHleSlowTimer0ActiveReturnExtraCycles
                                  : 0U);
  hle_irq_long_timer_chained_return_pending_ = false;
  hle_irq_slow_timer0_return_pending_ = false;
  const ArmStepResult cpu_step{status, return_cycles, cpu_.elapsed_cycles(), false,
                               false};
  const CoreDeviceTickResult devices =
      status == ExecuteStatus::executed ? advance_devices(cpu_step.elapsed_cycles)
                                        : CoreDeviceTickResult{};
  bool irq_serviced = false;
  if (status == ExecuteStatus::executed && interrupts_.irq_line() &&
      !cpu_.irq_disabled()) {
    irq_serviced = service_pending_irq();
    if (irq_serviced) {
      hle_irq_reentry_dispatch_pending_ = true;
      hle_irq_return_latency_pending_ = false;
      reset_auto_irq_latency();
    } else {
      auto_irq_line_high_ = true;
      auto_irq_latency_cycles_ = 0;
    }
  }
  if (status == ExecuteStatus::executed && !irq_serviced) {
    hle_irq_return_latency_pending_ = true;
  }
  return CoreSchedulerStepResult{cpu_step, devices, {}, irq_serviced, scheduler_cycles_,
                                 std::nullopt};
}

std::optional<ArmStepResult> CoreScheduler::execute_hle_arm_swi(
    std::uint32_t instruction, std::uint32_t fetch_address) {
  if (!bios_hle_enabled() || !Arm7tdmi::can_decode_software_interrupt(instruction)) {
    return std::nullopt;
  }
  const ArmCondition condition =
      static_cast<ArmCondition>((instruction >> 28U) & 0xFU);
  if (condition != ArmCondition::al) {
    return std::nullopt;
  }
  return execute_hle_swi(BiosController::decode_arm_swi(instruction), fetch_address);
}

std::optional<ArmStepResult> CoreScheduler::execute_hle_thumb_swi(
    std::uint16_t instruction, std::uint32_t fetch_address) {
  if (!bios_hle_enabled() || !Arm7tdmi::can_decode_thumb_software_interrupt(instruction)) {
    return std::nullopt;
  }
  return execute_hle_swi(BiosController::decode_thumb_swi(instruction), fetch_address);
}

ArmStepResult CoreScheduler::execute_hle_swi(BiosSwiCall call,
                                             std::uint32_t fetch_address) {
  const auto unsupported = [&]() {
    return ArmStepResult{ExecuteStatus::unsupported, 1, cpu_.elapsed_cycles(), false, false};
  };
  const auto executed = [&](std::uint32_t base_cycles = 3,
                            HleSwiProfileKind profile =
                                HleSwiProfileKind::simple) {
    const std::uint32_t cycles = add_hle_profile_adjustment(
        base_cycles, hle_swi_profile_adjustment(call, fetch_address, waitcnt_, profile));
    return ArmStepResult{ExecuteStatus::executed, cycles, cpu_.elapsed_cycles(), false,
                         false};
  };

  const BiosSwiResult policy = bios_->handle_swi(call);
  if (policy.status != BiosSwiStatus::handled) {
    return unsupported();
  }

  switch (call.service) {
    case 0x02:
      halt_until_interrupt();
      return executed(1);
    case 0x04: {
      const bool discard = cpu_.register_value(0) != 0;
      const std::uint16_t mask = static_cast<std::uint16_t>(cpu_.register_value(1));
      return wait_for_interrupt_mask(mask, discard)
                 ? executed(kBiosHleIntrWaitReturnCycles)
                 : unsupported();
    }
    case 0x05:
      return wait_for_interrupt_mask(0x0001U, true)
                 ? executed(kBiosHleIntrWaitReturnCycles)
                 : unsupported();
    case 0x06:
    case 0x07: {
      const std::int32_t numerator =
          call.service == 0x06 ? as_i32(cpu_.register_value(0)) : as_i32(cpu_.register_value(1));
      const std::int32_t denominator =
          call.service == 0x06 ? as_i32(cpu_.register_value(1)) : as_i32(cpu_.register_value(0));
      const std::uint32_t cycles = hle_div_base_cycles(numerator, denominator);
      if (denominator == 0) {
        cpu_.set_register(0, numerator < 0 ? 0xFFFFFFFFU : 1U);
        cpu_.set_register(1, wrap_u32(numerator));
        cpu_.set_register(3, 1U);
        return executed(cycles, HleSwiProfileKind::literal);
      }
      if (denominator == -1 && numerator == std::numeric_limits<std::int32_t>::min()) {
        cpu_.set_register(0, wrap_u32(std::numeric_limits<std::int32_t>::min()));
        cpu_.set_register(1, 0);
        cpu_.set_register(3, wrap_u32(std::numeric_limits<std::int32_t>::min()));
        return executed(cycles, HleSwiProfileKind::literal);
      }
      const std::int64_t quotient =
          static_cast<std::int64_t>(numerator) / static_cast<std::int64_t>(denominator);
      const std::int64_t remainder =
          static_cast<std::int64_t>(numerator) % static_cast<std::int64_t>(denominator);
      const std::int64_t absolute = quotient < 0 ? -quotient : quotient;
      cpu_.set_register(0, as_u32(quotient));
      cpu_.set_register(1, as_u32(remainder));
      cpu_.set_register(3, static_cast<std::uint32_t>(absolute));
      return executed(cycles, HleSwiProfileKind::literal);
    }
    case 0x08: {
      const std::uint32_t input = cpu_.register_value(0);
      const double value = static_cast<double>(input);
      cpu_.set_register(0, static_cast<std::uint32_t>(std::sqrt(value)));
      const HleSwiProfileKind profile =
          input > 0xFFU ? HleSwiProfileKind::literal : HleSwiProfileKind::simple;
      return executed(hle_sqrt_base_cycles(input), profile);
    }
    case 0x09: {
      std::int32_t scratch_r1 = 0;
      std::int32_t scratch_r3 = 0;
      const std::int32_t result =
          hle_arc_tan(as_i32(cpu_.register_value(0)), &scratch_r1, &scratch_r3);
      cpu_.set_register(0, wrap_u32(result));
      cpu_.set_register(1, wrap_u32(scratch_r1));
      cpu_.set_register(3, wrap_u32(scratch_r3));
      return executed(99, HleSwiProfileKind::simple);
    }
    case 0x0A: {
      std::int32_t scratch_r1 = as_i32(cpu_.register_value(1));
      const std::int32_t result = hle_arc_tan2(as_i32(cpu_.register_value(0)),
                                              as_i32(cpu_.register_value(1)),
                                              &scratch_r1);
      cpu_.set_register(0, static_cast<std::uint16_t>(result));
      cpu_.set_register(1, wrap_u32(scratch_r1));
      cpu_.set_register(3, 0x170U);
      return executed(99, HleSwiProfileKind::simple);
    }
    case 0x0B:
      return hle_cpu_set(false)
                 ? executed(hle_cpu_set_base_cycles(cpu_.register_value(2), false),
                            HleSwiProfileKind::cpu_set)
                 : unsupported();
    case 0x0C:
      return hle_cpu_set(true)
                 ? executed(hle_cpu_set_base_cycles(cpu_.register_value(2), true),
                            HleSwiProfileKind::cpu_set)
                 : unsupported();
    default:
      return unsupported();
  }
}

bool CoreScheduler::wait_for_interrupt_mask(std::uint16_t mask, bool discard_old_flags) {
  if (mask == 0) {
    return false;
  }
  if (discard_old_flags) {
    interrupts_.write_interrupt_flags(mask);
  }
  for (std::uint32_t cycles = 0; cycles < kMaxBiosWaitCycles;) {
    if ((interrupts_.interrupt_flags() & mask) != 0) {
      if (interrupts_.irq_line() && !cpu_.irq_disabled()) {
        auto_irq_line_high_ = true;
        auto_irq_latency_cycles_ = 0;
      }
      return true;
    }
    const std::uint32_t batch =
        std::min<std::uint32_t>(kBiosWaitBatchCycles, kMaxBiosWaitCycles - cycles);
    [[maybe_unused]] const CoreDeviceTickResult devices = advance_devices(batch);
    cycles += batch;
  }
  const bool matched = (interrupts_.interrupt_flags() & mask) != 0;
  if (matched && interrupts_.irq_line() && !cpu_.irq_disabled()) {
    auto_irq_line_high_ = true;
    auto_irq_latency_cycles_ = 0;
  }
  return matched;
}

bool CoreScheduler::hle_cpu_set(bool fast) {
  const std::uint32_t source = cpu_.register_value(0);
  const std::uint32_t dest = cpu_.register_value(1);
  const std::uint32_t control = cpu_.register_value(2);
  const bool fill = (control & (1U << 24U)) != 0;
  const bool word = fast || (control & (1U << 26U)) != 0;
  std::uint32_t count = control & 0x001FFFFFU;
  if (fast) {
    count = (count + 7U) & ~7U;
  }

  if (word) {
    std::optional<std::uint32_t> fill_value = std::nullopt;
    const auto read_word = [&](std::uint32_t address) {
      if (MemoryBus::describe(address).region == Region::bios) {
        return std::optional<std::uint32_t>{0};
      }
      return memory_.read32(MemoryBus::describe(address).region == Region::game_pak_save
                                ? address
                                : (address & ~0x3U));
    };
    for (std::uint32_t index = 0; index < count; ++index) {
      const std::optional<std::uint32_t> value =
          fill ? (fill_value.has_value() ? fill_value : (fill_value = read_word(source)))
               : read_word(source + index * 4U);
      const std::uint32_t write_address = dest + index * 4U;
      const std::uint32_t effective_write_address =
          MemoryBus::describe(write_address).region == Region::game_pak_save
              ? write_address
              : (write_address & ~0x3U);
      if (!value.has_value() || !memory_.write32(effective_write_address, value.value())) {
        return false;
      }
    }
    return true;
  }

  std::optional<std::uint16_t> fill_value = std::nullopt;
  const auto read_halfword = [&](std::uint32_t address) -> std::optional<std::uint16_t> {
    if (MemoryBus::describe(address).region == Region::bios) {
      return 0;
    }
    if ((address & 0x1U) == 0) {
      return memory_.read16(address);
    }
    const std::optional<std::uint8_t> value = memory_.read8(address);
    if (!value.has_value()) {
      return std::nullopt;
    }
    return static_cast<std::uint16_t>(value.value());
  };
  for (std::uint32_t index = 0; index < count; ++index) {
    const std::optional<std::uint16_t> value =
        fill ? (fill_value.has_value() ? fill_value : (fill_value = read_halfword(source)))
             : read_halfword(source + index * 2U);
    if (!value.has_value() || !memory_.write16(dest + index * 2U, value.value())) {
      return false;
    }
  }
  return true;
}

CoreSchedulerFetchStepResult CoreScheduler::step_arm_from_pc() {
  const std::uint32_t fetch_address = cpu_.register_value(Arm7tdmi::kPc);
  const std::optional<CoreSchedulerStepResult> hle_irq_return =
      dispatch_hle_irq_return(fetch_address);
  if (hle_irq_return.has_value()) {
    reset_fetch_timing_sequence();
    return {CoreInstructionSet::arm, 4, fetch_address, std::nullopt, hle_irq_return, 0,
            false, false, false, false, false, false, 0, false};
  }

  const std::optional<CoreSchedulerStepResult> hle_irq =
      dispatch_hle_irq_vector(fetch_address);
  if (hle_irq.has_value()) {
    reset_fetch_timing_sequence();
    return {CoreInstructionSet::arm, 4, fetch_address, std::nullopt, hle_irq, 0,
            false, false, false, false, false, false, 0, false};
  }

  const std::optional<std::uint32_t> instruction = memory_.read32(fetch_address);
  if (!instruction.has_value()) {
    reset_fetch_timing_sequence();
    return {CoreInstructionSet::arm, 4, fetch_address, std::nullopt, std::nullopt, 0,
            true, false, false, false, false, false, 0, false};
  }

  bool fetch_timing_applied = false;
  bool fetch_sequential = false;
  bool prefetch_enabled = false;
  bool prefetch_hit = false;
  bool boundary_forced_nonsequential = false;
  const std::uint32_t fetch_cycles = apply_fetch_timing(
      fetch_address, 4, fetch_timing_applied, fetch_sequential, prefetch_enabled,
      prefetch_hit, boundary_forced_nonsequential);
  const std::optional<ArmElapsedCycleEstimate> elapsed_override =
      arm_rom_multiply_elapsed_override(cpu_, instruction.value(), fetch_address,
                                        waitcnt_);
  CoreSchedulerStepResult step = step_arm(instruction.value(), elapsed_override);
  const bool should_advance_pc =
      step.cpu_step.status != ExecuteStatus::unsupported &&
      cpu_.register_value(Arm7tdmi::kPc) == fetch_address;
  bool seed_prefetch_after_branch_exchange = false;
  bool pc_changed_with_pipeline_refill = false;
  if (!should_advance_pc && step.cpu_step.status == ExecuteStatus::executed &&
      arm_instruction_refills_pipeline(instruction.value())) {
    pc_changed_with_pipeline_refill = true;
    const std::uint32_t branch_target = cpu_.register_value(Arm7tdmi::kPc);
    const bool branch_exchange =
        Arm7tdmi::can_decode_branch_exchange(instruction.value());
    const std::uint32_t refill_cycles =
        branch_exchange
            ? arm_branch_exchange_refill_cycles(
                  fetch_address, branch_target, waitcnt_)
            : arm_branch_refill_cycles(branch_target, waitcnt_);
    seed_prefetch_after_branch_exchange =
        branch_exchange &&
        branch_exchange_seeds_fast_prefetch(fetch_address, branch_target,
                                            waitcnt_);
    const CoreDeviceTickResult refill_devices = advance_devices(refill_cycles);
    charge_pipeline_refill(step, refill_devices, refill_cycles);
  }
  if (step.cpu_step.status == ExecuteStatus::skipped_condition &&
      Arm7tdmi::can_decode_branch(instruction.value()) && prefetch_enabled &&
      MemoryBus::describe(fetch_address).region == Region::game_pak_rom) {
    const CoreDeviceTickResult skipped_branch = advance_devices(1);
    merge_device_ticks(step.devices, skipped_branch);
    ++step.cpu_step.elapsed_cycles;
    ++step.cpu_step.total_elapsed_cycles;
    step.scheduler_cycles = scheduler_cycles_;
  }
  if (should_charge_prefetch_execute_bubble(fetch_address, waitcnt_, step,
                                            should_advance_pc, prefetch_hit,
                                            4,
                                            prefetch_buffer_halfwords_)) {
    const CoreDeviceTickResult bubble = advance_devices(1);
    merge_device_ticks(step.devices, bubble);
    ++step.cpu_step.elapsed_cycles;
    ++step.cpu_step.total_elapsed_cycles;
    step.scheduler_cycles = scheduler_cycles_;
  }
  const bool pc_change_irq_serviced =
      pc_changed_with_pipeline_refill && auto_irq_ready() && service_pending_irq();
  if (pc_change_irq_serviced) {
    step.irq_serviced = true;
    reset_auto_irq_latency();
  }
  if (should_advance_pc) {
    cpu_.set_register(Arm7tdmi::kPc, fetch_address + 4U);
    refill_prefetch_after_step(fetch_address, 4, step);
  } else {
    reset_fetch_timing_sequence();
    if (!pc_change_irq_serviced && seed_prefetch_after_branch_exchange) {
      prefetch_buffer_halfwords_ = 1;
    }
  }

  return {CoreInstructionSet::arm, 4, fetch_address, instruction, step, fetch_cycles,
          false, should_advance_pc, fetch_timing_applied, fetch_sequential,
          prefetch_enabled, prefetch_hit, prefetch_buffer_halfwords_,
          boundary_forced_nonsequential};
}

CoreSchedulerFetchStepResult CoreScheduler::step_from_pc() {
  if (!cpu_.thumb_state()) {
    suppress_next_thumb_prefetch_execute_bubble_ = false;
    return step_arm_from_pc();
  }

  const std::uint32_t fetch_address = cpu_.register_value(Arm7tdmi::kPc);
  const std::optional<std::uint16_t> instruction = memory_.read16(fetch_address);
  if (!instruction.has_value()) {
    reset_fetch_timing_sequence();
    suppress_next_thumb_prefetch_execute_bubble_ = false;
    previous_thumb_internal_load_ = false;
    return {CoreInstructionSet::thumb, 2, fetch_address, std::nullopt, std::nullopt,
            0, true, false, false, false, false, false, 0, false};
  }

  bool fetch_timing_applied = false;
  bool fetch_sequential = false;
  bool prefetch_enabled = false;
  bool prefetch_hit = false;
  bool boundary_forced_nonsequential = false;
  const std::uint32_t fetch_cycles = apply_fetch_timing(
      fetch_address, 2, fetch_timing_applied, fetch_sequential, prefetch_enabled,
      prefetch_hit, boundary_forced_nonsequential);
  bool prefetch_internal_load_overlap =
      should_overlap_thumb_internal_data_load(fetch_address, waitcnt_, cpu_,
                                              instruction.value(), fetch_cycles);
  if (prefetch_internal_load_overlap && !prefetch_enabled &&
      waitcnt_ != nullptr && waitcnt_->prefetch_enabled()) {
    const MemoryAccessTiming timing =
        MemoryBus::timing(fetch_address, AccessWidth::halfword, *waitcnt_);
    if (timing.sequential <= 1) {
      prefetch_internal_load_overlap = false;
    }
  }
  const bool suppress_current_prefetch_execute_bubble =
      suppress_next_thumb_prefetch_execute_bubble_;
  suppress_next_thumb_prefetch_execute_bubble_ = false;
  const bool previous_thumb_internal_load = previous_thumb_internal_load_;
  const std::optional<ArmElapsedCycleEstimate> elapsed_override =
      thumb_rom_multiply_elapsed_override(cpu_, instruction.value(), fetch_address,
                                          waitcnt_);
  CoreSchedulerStepResult step =
      step_thumb(instruction.value(), prefetch_internal_load_overlap,
                 elapsed_override);
  const bool should_advance_pc =
      step.cpu_step.status != ExecuteStatus::unsupported &&
      cpu_.register_value(Arm7tdmi::kPc) == fetch_address;
  bool seed_prefetch_after_branch_exchange = false;
  bool pc_changed_with_pipeline_refill = false;
  if (!should_advance_pc && step.cpu_step.status == ExecuteStatus::executed &&
      thumb_instruction_refills_pipeline(instruction.value())) {
    pc_changed_with_pipeline_refill = true;
    const bool branch_exchange =
        Arm7tdmi::can_decode_thumb_high_register(instruction.value()) &&
        Arm7tdmi::decode_thumb_high_register(instruction.value()).opcode ==
            ThumbHighRegisterOpcode::bx;
    const std::uint32_t branch_target = cpu_.register_value(Arm7tdmi::kPc);
    const std::uint32_t refill_cycles =
        branch_exchange
            ? thumb_branch_exchange_refill_cycles(
                  fetch_address, branch_target, waitcnt_)
            : thumb_branch_refill_cycles(branch_target, waitcnt_);
    seed_prefetch_after_branch_exchange =
        branch_exchange &&
        branch_exchange_seeds_fast_prefetch(fetch_address, branch_target,
                                            waitcnt_);
    const CoreDeviceTickResult refill_devices = advance_devices(refill_cycles);
    charge_pipeline_refill(step, refill_devices, refill_cycles);
  }
  if (previous_thumb_internal_load && should_advance_pc && prefetch_hit &&
      fetch_cycles == 0 && waitcnt_ != nullptr && waitcnt_->prefetch_enabled() &&
      step.data_access.has_value() && step.data_access->load &&
      step.data_access->width_bytes == 4 &&
      MemoryBus::describe(step.data_access->address).region ==
          Region::game_pak_rom) {
    const MemoryAccessTiming timing =
        MemoryBus::timing(fetch_address, AccessWidth::halfword, *waitcnt_);
    if (timing.sequential <= 1) {
      const CoreDeviceTickResult penalty = advance_devices(1);
      merge_device_ticks(step.devices, penalty);
      ++step.cpu_step.elapsed_cycles;
      ++step.cpu_step.total_elapsed_cycles;
      step.scheduler_cycles = scheduler_cycles_;
    }
  }
  if (!suppress_current_prefetch_execute_bubble &&
      should_charge_prefetch_execute_bubble(fetch_address, waitcnt_, step,
                                            should_advance_pc, prefetch_hit, 2,
                                            prefetch_buffer_halfwords_)) {
    const CoreDeviceTickResult bubble = advance_devices(1);
    merge_device_ticks(step.devices, bubble);
    ++step.cpu_step.elapsed_cycles;
    ++step.cpu_step.total_elapsed_cycles;
    step.scheduler_cycles = scheduler_cycles_;
  }
  suppress_next_thumb_prefetch_execute_bubble_ =
      should_suppress_next_thumb_prefetch_execute_bubble(
          fetch_address, waitcnt_, step, should_advance_pc, prefetch_enabled,
          fetch_cycles);
  const bool pc_change_irq_serviced =
      pc_changed_with_pipeline_refill && auto_irq_ready() && service_pending_irq();
  if (pc_change_irq_serviced) {
    step.irq_serviced = true;
    reset_auto_irq_latency();
  }
  if (should_advance_pc) {
    cpu_.set_register(Arm7tdmi::kPc, fetch_address + 2U);
    std::uint32_t extra_refill_cycles = 0;
    if (prefetch_internal_load_overlap && !prefetch_enabled &&
        waitcnt_ != nullptr && waitcnt_->prefetch_enabled() &&
        step.data_access.has_value() && step.data_access->load) {
      const Region data_region =
          MemoryBus::describe(step.data_access->address).region;
      if (data_region == Region::ewram || data_region == Region::iwram) {
        extra_refill_cycles = 1;
      }
    }
    if (prefetch_enabled && waitcnt_ != nullptr &&
        waitcnt_->prefetch_enabled() && step.data_access.has_value()) {
      const MemoryAccessTiming timing =
          MemoryBus::timing(fetch_address, AccessWidth::halfword, *waitcnt_);
      const Region data_region =
          MemoryBus::describe(step.data_access->address).region;
      if (timing.sequential > 1U &&
          (data_region == Region::ewram || data_region == Region::iwram)) {
        extra_refill_cycles = 1;
      }
    }
    refill_prefetch_after_step(fetch_address, 2, step, extra_refill_cycles);
  } else {
    reset_fetch_timing_sequence();
    if (!pc_change_irq_serviced && seed_prefetch_after_branch_exchange) {
      prefetch_buffer_halfwords_ = 1;
    }
  }
  previous_thumb_internal_load_ = false;
  if (should_advance_pc && step.cpu_step.status == ExecuteStatus::executed &&
      step.data_access.has_value() && step.data_access->load) {
    const Region data_region = MemoryBus::describe(step.data_access->address).region;
    previous_thumb_internal_load_ =
        data_region == Region::ewram || data_region == Region::iwram;
  }

  return {CoreInstructionSet::thumb, 2, fetch_address, instruction.value(), step,
          fetch_cycles, false, should_advance_pc, fetch_timing_applied,
          fetch_sequential, prefetch_enabled, prefetch_hit,
          prefetch_buffer_halfwords_, boundary_forced_nonsequential};
}

CoreSchedulerRunResult CoreScheduler::run_arm_from_pc(std::uint32_t max_steps) {
  CoreSchedulerRunResult result{
      max_steps,
      0,
      0,
      0,
      0,
      0,
      CoreRunStopReason::max_steps,
      cpu_.register_value(Arm7tdmi::kPc),
      scheduler_cycles_,
  };

  for (std::uint32_t step_index = 0; step_index < max_steps; ++step_index) {
    const CoreSchedulerFetchStepResult fetched = step_arm_from_pc();
    if (fetched.fetch_failed) {
      ++result.fetch_failures;
      result.stop_reason = CoreRunStopReason::fetch_failed;
      break;
    }

    ++result.attempted_steps;
    const ExecuteStatus status = fetched.step->cpu_step.status;
    if (status == ExecuteStatus::executed) {
      ++result.executed_steps;
      continue;
    }
    if (status == ExecuteStatus::skipped_condition) {
      ++result.skipped_steps;
      continue;
    }

    ++result.unsupported_steps;
    result.stop_reason = CoreRunStopReason::unsupported_instruction;
    break;
  }

  result.final_pc = cpu_.register_value(Arm7tdmi::kPc);
  result.scheduler_cycles = scheduler_cycles_;
  return result;
}

CoreSchedulerRunResult CoreScheduler::run_from_pc(std::uint32_t max_steps) {
  CoreSchedulerRunResult result{
      max_steps,
      0,
      0,
      0,
      0,
      0,
      CoreRunStopReason::max_steps,
      cpu_.register_value(Arm7tdmi::kPc),
      scheduler_cycles_,
  };

  for (std::uint32_t step_index = 0; step_index < max_steps; ++step_index) {
    const CoreSchedulerFetchStepResult fetched = step_from_pc();
    if (fetched.fetch_failed) {
      ++result.fetch_failures;
      result.stop_reason = CoreRunStopReason::fetch_failed;
      break;
    }

    ++result.attempted_steps;
    const ExecuteStatus status = fetched.step->cpu_step.status;
    if (status == ExecuteStatus::executed) {
      ++result.executed_steps;
      continue;
    }
    if (status == ExecuteStatus::skipped_condition) {
      ++result.skipped_steps;
      continue;
    }

    ++result.unsupported_steps;
    result.stop_reason = CoreRunStopReason::unsupported_instruction;
    break;
  }

  result.final_pc = cpu_.register_value(Arm7tdmi::kPc);
  result.scheduler_cycles = scheduler_cycles_;
  return result;
}

}  // namespace gba::core
