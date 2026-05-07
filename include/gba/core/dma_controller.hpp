#pragma once

#include "gba/core/interrupt_controller.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gba::core {

class MemoryBus;

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

struct DmaRunResult {
  std::uint8_t channels_executed;
  std::uint32_t units_transferred;
  bool unsupported_request;
};

class DmaController {
 public:
  static constexpr std::size_t kChannelCount = 4;

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
  [[nodiscard]] DmaAddressControl destination_control(std::size_t channel) const;
  [[nodiscard]] DmaAddressControl source_control(std::size_t channel) const;
  [[nodiscard]] DmaStartTiming start_timing(std::size_t channel) const;
  [[nodiscard]] std::uint64_t state_hash() const;

  [[nodiscard]] DmaRunResult run_immediate(MemoryBus& memory,
                                           InterruptController& interrupts);

 private:
  struct Channel {
    std::uint32_t source;
    std::uint32_t destination;
    std::uint16_t word_count;
    std::uint16_t control;
    std::uint32_t current_source;
    std::uint32_t current_destination;
    std::uint32_t current_count;
  };

  std::array<Channel, kChannelCount> channels_;

  [[nodiscard]] Channel& checked_channel(std::size_t channel);
  [[nodiscard]] const Channel& checked_channel(std::size_t channel) const;
  [[nodiscard]] std::uint32_t normalized_word_count(std::size_t channel) const;
  [[nodiscard]] bool execute_channel(std::size_t channel, MemoryBus& memory,
                                     InterruptController& interrupts,
                                     std::uint32_t& units_transferred);
};

}  // namespace gba::core
