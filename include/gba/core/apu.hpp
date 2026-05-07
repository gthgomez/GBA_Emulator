#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace gba::core {

enum class DirectSoundChannel : std::uint8_t {
  a = 0,
  b = 1,
};

struct ApuFrameStep {
  std::uint8_t step;
  bool length_clock;
  bool sweep_clock;
  bool envelope_clock;
};

struct DirectSoundSample {
  bool produced;
  std::int8_t sample;
  bool refill_request;
};

struct DirectSoundTimerResult {
  DirectSoundSample fifo_a;
  DirectSoundSample fifo_b;
};

struct ApuMixedSample {
  std::int16_t left;
  std::int16_t right;
};

class Apu {
 public:
  static constexpr std::uint32_t kAudioSampleRate = 32768;
  static constexpr std::uint32_t kCpuCyclesPerAudioSample = 512;
  static constexpr std::uint32_t kCpuCyclesPerFrameSequencerStep = 32768;
  static constexpr std::uint8_t kFrameSequencerSteps = 8;
  static constexpr std::size_t kWaveRamHalfwords = 8;
  static constexpr std::size_t kFifoCapacity = 32;
  static constexpr std::size_t kFifoRefillThreshold = 16;
  static constexpr std::size_t kAudioBufferCapacity = 2048;

  Apu();

  void reset();

  void write_soundcnt_l(std::uint16_t value);
  void write_soundcnt_h(std::uint16_t value);
  void write_soundcnt_x(std::uint16_t value);
  void write_soundbias(std::uint16_t value);
  void write_wave_ram(std::size_t index, std::uint16_t value);
  void write_fifo(DirectSoundChannel channel, std::uint32_t value);
  [[nodiscard]] std::optional<ApuFrameStep> tick(std::uint32_t cpu_cycles);
  [[nodiscard]] DirectSoundTimerResult timer_overflow(std::uint8_t timer_index);
  [[nodiscard]] std::optional<ApuMixedSample> pop_audio_sample();
  void clear_audio_buffer();

  [[nodiscard]] std::uint16_t soundcnt_l() const;
  [[nodiscard]] std::uint16_t soundcnt_h() const;
  [[nodiscard]] std::uint16_t soundcnt_x() const;
  [[nodiscard]] std::uint16_t soundbias() const;
  [[nodiscard]] std::uint16_t wave_ram(std::size_t index) const;
  [[nodiscard]] bool master_enabled() const;
  [[nodiscard]] std::uint8_t frame_step() const;
  [[nodiscard]] std::uint64_t frame_step_count() const;
  [[nodiscard]] std::uint32_t frame_cycle_remainder() const;
  [[nodiscard]] std::uint32_t audio_cycle_remainder() const;
  [[nodiscard]] std::uint64_t audio_sample_count() const;
  [[nodiscard]] std::size_t available_audio_samples() const;
  [[nodiscard]] ApuMixedSample last_mixed_sample() const;
  [[nodiscard]] std::size_t fifo_size(DirectSoundChannel channel) const;
  [[nodiscard]] bool fifo_refill_needed(DirectSoundChannel channel) const;
  [[nodiscard]] bool direct_sound_uses_timer(DirectSoundChannel channel,
                                             std::uint8_t timer_index) const;
  [[nodiscard]] bool direct_sound_enabled(DirectSoundChannel channel) const;
  [[nodiscard]] std::uint64_t state_hash() const;

 private:
  struct Fifo {
    std::array<std::int8_t, kFifoCapacity> samples;
    std::size_t head;
    std::size_t size;
  };

  struct AudioBuffer {
    std::array<ApuMixedSample, kAudioBufferCapacity> samples;
    std::size_t head;
    std::size_t size;
  };

  std::uint16_t soundcnt_l_;
  std::uint16_t soundcnt_h_;
  std::uint16_t soundcnt_x_status_;
  std::uint16_t soundbias_;
  std::array<std::uint16_t, kWaveRamHalfwords> wave_ram_;
  std::array<Fifo, 2> fifos_;
  std::array<std::int8_t, 2> direct_sound_latched_samples_;
  AudioBuffer audio_buffer_;
  std::uint64_t frame_step_count_;
  std::uint64_t audio_sample_count_;
  std::uint32_t frame_cycle_remainder_;
  std::uint32_t audio_cycle_remainder_;
  std::uint8_t frame_step_;
  ApuMixedSample last_mixed_sample_;

  [[nodiscard]] Fifo& fifo(DirectSoundChannel channel);
  [[nodiscard]] const Fifo& fifo(DirectSoundChannel channel) const;
  void clear_fifo(DirectSoundChannel channel);
  void push_fifo(DirectSoundChannel channel, std::int8_t sample);
  [[nodiscard]] DirectSoundSample pop_fifo(DirectSoundChannel channel);
  [[nodiscard]] ApuMixedSample mix_sample() const;
  void push_audio_sample(ApuMixedSample sample);
  void generate_audio_sample();
  void clear_sound_circuit();
};

}  // namespace gba::core
