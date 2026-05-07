#pragma once

#include <cstdint>

namespace gba::core {

class Arm7tdmi;

enum class InterruptSource : std::uint8_t {
  vblank = 0,
  hblank = 1,
  vcount = 2,
  timer0 = 3,
  timer1 = 4,
  timer2 = 5,
  timer3 = 6,
  serial = 7,
  dma0 = 8,
  dma1 = 9,
  dma2 = 10,
  dma3 = 11,
  keypad = 12,
  game_pak = 13,
};

class InterruptController {
 public:
  static constexpr std::uint16_t kSupportedMask = 0x3FFF;

  InterruptController();

  void reset();

  [[nodiscard]] std::uint16_t interrupt_enable() const;
  [[nodiscard]] std::uint16_t interrupt_flags() const;
  [[nodiscard]] bool master_enabled() const;
  [[nodiscard]] std::uint16_t ime() const;

  void write_interrupt_enable(std::uint16_t value);
  void write_interrupt_flags(std::uint16_t acknowledge_mask);
  void write_ime(std::uint16_t value);
  void set_master_enabled(bool enabled);

  void request(InterruptSource source);
  [[nodiscard]] bool requested(InterruptSource source) const;
  [[nodiscard]] bool enabled(InterruptSource source) const;
  [[nodiscard]] std::uint16_t pending_mask() const;
  [[nodiscard]] bool irq_line() const;
  [[nodiscard]] bool service_pending_irq(Arm7tdmi& cpu) const;
  [[nodiscard]] std::uint64_t state_hash() const;

 private:
  std::uint16_t interrupt_enable_;
  std::uint16_t interrupt_flags_;
  bool master_enabled_;
};

}  // namespace gba::core
