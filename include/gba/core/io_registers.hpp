#pragma once

#include "gba/core/apu.hpp"
#include "gba/core/dma_controller.hpp"
#include "gba/core/interrupt_controller.hpp"
#include "gba/core/keypad.hpp"
#include "gba/core/ppu_timing.hpp"
#include "gba/core/timers.hpp"
#include "gba/core/wait_state_control.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace gba::core {

struct IoRegistersState {
  std::array<std::uint16_t, 7> serial{};
};

class IoRegisters {
 public:
  static constexpr std::uint32_t kDispcnt = 0x04000000;
  static constexpr std::uint32_t kGreenSwap = 0x04000002;
  static constexpr std::uint32_t kDispstat = 0x04000004;
  static constexpr std::uint32_t kVcount = 0x04000006;
  static constexpr std::uint32_t kBg0Cnt = 0x04000008;
  static constexpr std::uint32_t kBg1Cnt = 0x0400000A;
  static constexpr std::uint32_t kBg2Cnt = 0x0400000C;
  static constexpr std::uint32_t kBg3Cnt = 0x0400000E;
  static constexpr std::uint32_t kSoundcntL = 0x04000080;
  static constexpr std::uint32_t kSoundcntH = 0x04000082;
  static constexpr std::uint32_t kSoundcntX = 0x04000084;
  static constexpr std::uint32_t kSoundbias = 0x04000088;
  static constexpr std::uint32_t kFifoA = 0x040000A0;
  static constexpr std::uint32_t kFifoB = 0x040000A4;
  static constexpr std::uint32_t kDmaBase = 0x040000B0;
  static constexpr std::uint32_t kTimerBase = 0x04000100;
  static constexpr std::uint32_t kKeyinput = 0x04000130;
  static constexpr std::uint32_t kKeycnt = 0x04000132;
  static constexpr std::uint32_t kRcnt = 0x04000134;
  static constexpr std::uint32_t kIe = 0x04000200;
  static constexpr std::uint32_t kIf = 0x04000202;
  static constexpr std::uint32_t kWaitcnt = 0x04000204;
  static constexpr std::uint32_t kIme = 0x04000208;

  IoRegisters(InterruptController& interrupts, Timers& timers, DmaController& dma,
              PpuTiming& ppu, Apu& apu, WaitStateControl& waitcnt, Keypad& keypad);

  void reset();
  [[nodiscard]] IoRegistersState save_state() const;
  void load_state(const IoRegistersState& state);
  [[nodiscard]] std::uint64_t state_hash() const;
  [[nodiscard]] std::optional<std::uint16_t> read16(std::uint32_t address) const;
  [[nodiscard]] std::optional<std::uint32_t> read32(std::uint32_t address) const;
  [[nodiscard]] bool write16(std::uint32_t address, std::uint16_t value);
  [[nodiscard]] bool write32(std::uint32_t address, std::uint32_t value);

 private:
  InterruptController& interrupts_;
  Timers& timers_;
  DmaController& dma_;
  PpuTiming& ppu_;
  Apu& apu_;
  WaitStateControl& waitcnt_;
  Keypad& keypad_;
  std::array<std::uint16_t, 7> serial_;
};

}  // namespace gba::core
