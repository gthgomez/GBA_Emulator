#pragma once

#include "gba/core/apu.hpp"
#include "gba/core/interrupt_controller.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gba::core {

class MemoryBus;
class WaitStateControl;

enum class DmaAddressControl : std::uint8_t {
  increment = 0,
  decrement = 1,
  fixed = 2,
  increment_reload = 3,
};

enum class DmaStartTiming : std::uint8_t {
  immediate = 0,
  vblank = 1,
  hblank = 2,
  special = 3,
};

enum class DmaTrigger : std::uint8_t {
  immediate,
  vblank,
  hblank,
  special,
  fifo_a,
  fifo_b,
};

struct DmaRunResult {
  std::uint8_t channels_executed;
  std::uint32_t units_transferred;
  bool unsupported_request;
  std::uint32_t bus_cycles;
};

class DmaController {
 public:
  static constexpr std::size_t kChannelCount = 4;

  struct DmaChannelState {
    std::uint32_t source = 0;
    std::uint32_t destination = 0;
    std::uint16_t word_count = 0;
    std::uint16_t control = 0;
    std::uint32_t current_source = 0;
    std::uint32_t current_destination = 0;
    std::uint32_t current_count = 0;
    std::uint32_t data_latch = 0;
  };

  struct State {
    std::array<DmaChannelState, kChannelCount> channels{};
    bool immediate_pending = false;
  };

  DmaController();

  void reset();

  void write_source(std::size_t channel, std::uint32_t value);
  void write_destination(std::size_t channel, std::uint32_t value);
  void write_word_count(std::size_t channel, std::uint16_t value);
  void write_control(std::size_t channel, std::uint16_t value);

  [[nodiscard]] std::uint32_t source(std::size_t channel) const;
  [[nodiscard]] std::uint32_t destination(std::size_t channel) const;
  [[nodiscard]] std::uint16_t word_count(std::size_t channel) const;
  [[nodiscard]] std::uint16_t control(std::size_t channel) const;
  [[nodiscard]] std::uint32_t active_count(std::size_t channel) const;
  [[nodiscard]] bool enabled(std::size_t channel) const;
  [[nodiscard]] bool repeat(std::size_t channel) const;
  [[nodiscard]] bool transfer_32bit(std::size_t channel) const;
  [[nodiscard]] bool irq_on_completion(std::size_t channel) const;
  [[nodiscard]] bool immediate_pending() const;
  [[nodiscard]] DmaAddressControl destination_control(std::size_t channel) const;
  [[nodiscard]] DmaAddressControl source_control(std::size_t channel) const;
  [[nodiscard]] DmaStartTiming start_timing(std::size_t channel) const;
  [[nodiscard]] std::uint64_t state_hash() const;

  [[nodiscard]] DmaRunResult run_immediate(MemoryBus& memory,
                                           InterruptController& interrupts,
                                           const WaitStateControl* waitcnt = nullptr);
  [[nodiscard]] DmaRunResult run_trigger(DmaTrigger trigger, MemoryBus& memory,
                                         InterruptController& interrupts,
                                         const WaitStateControl* waitcnt = nullptr);
  [[nodiscard]] DmaRunResult run_sound_fifo(DmaTrigger trigger, MemoryBus& memory,
                                             Apu& apu,
                                             InterruptController& interrupts,
                                             const WaitStateControl* waitcnt = nullptr);

  [[nodiscard]] State save_state() const;
  [[nodiscard]] bool load_state(const State& state);

 private:
  using Channel = DmaChannelState;

  std::array<Channel, kChannelCount> channels_;
  bool immediate_pending_;

  [[nodiscard]] Channel& checked_channel(std::size_t channel);
  [[nodiscard]] const Channel& checked_channel(std::size_t channel) const;
  void refresh_immediate_pending();
  [[nodiscard]] std::uint32_t normalized_word_count(std::size_t channel) const;
  [[nodiscard]] static std::uint32_t effective_source_address(std::size_t channel,
                                                              std::uint32_t address);
  [[nodiscard]] static std::uint32_t effective_destination_address(std::size_t channel,
                                                                   std::uint32_t address);
  [[nodiscard]] bool execute_channel(std::size_t channel, MemoryBus& memory,
                                     InterruptController& interrupts,
                                     const WaitStateControl* waitcnt,
                                     std::uint32_t& units_transferred,
                                     std::uint32_t& bus_cycles,
                                     bool drive_open_bus);
  [[nodiscard]] bool execute_sound_fifo_channel(std::size_t channel,
                                                DirectSoundChannel fifo,
                                                MemoryBus& memory, Apu& apu,
                                                InterruptController& interrupts,
                                                const WaitStateControl* waitcnt,
                                                std::uint32_t& units_transferred,
                                                std::uint32_t& bus_cycles);
};

}  // namespace gba::core
