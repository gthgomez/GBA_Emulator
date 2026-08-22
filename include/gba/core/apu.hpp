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
  static constexpr std::size_t kWaveRamBanks = 2;
  static constexpr std::size_t kFifoCapacity = 32;
  static constexpr std::size_t kFifoRefillThreshold = 16;
  static constexpr std::size_t kAudioBufferCapacity = 2048;

  struct FifoState {
    std::array<std::int8_t, kFifoCapacity> samples{};
    std::size_t head = 0;
    std::size_t size = 0;
  };

  struct SquareChannelState {
    bool enabled = false;
    std::uint8_t duty = 0;
    std::uint8_t volume = 0;
    std::uint16_t period_samples = 1;
    std::uint16_t phase = 0;
  };

  struct WaveChannelState {
    bool enabled = false;
    std::uint8_t volume_shift = 0;
    std::uint16_t period_samples = 1;
    std::uint16_t phase = 0;
  };

  struct NoiseChannelState {
    bool enabled = false;
    std::uint8_t volume = 0;
    std::uint16_t period_samples = 1;
    std::uint16_t phase = 0;
    std::uint16_t lfsr = 0x7FFF;
    bool narrow_lfsr = false;
  };

  struct State {
    std::uint16_t soundcnt_l = 0;
    std::uint16_t soundcnt_h = 0;
    std::uint16_t soundcnt_x_status = 0;
    std::uint16_t soundbias = 0x0200;
    std::array<std::array<std::uint16_t, kWaveRamHalfwords>, kWaveRamBanks>
        wave_ram_banks{};
    bool wave_bank_select = false;
    std::array<FifoState, 2> fifos{};
    std::array<std::int8_t, 2> direct_sound_latched_samples{};
    std::array<SquareChannelState, 2> square_channels{};
    WaveChannelState wave_channel{};
    NoiseChannelState noise_channel{};
    std::array<ApuMixedSample, kAudioBufferCapacity> audio_buffer_samples{};
    std::size_t audio_buffer_head = 0;
    std::size_t audio_buffer_size = 0;
    std::uint64_t frame_step_count = 0;
    std::uint64_t audio_sample_count = 0;
    std::uint32_t frame_cycle_remainder = 0;
    std::uint32_t audio_cycle_remainder = 0;
    std::uint8_t frame_step = 0;
    ApuMixedSample last_mixed_sample{0, 0};
  };

  Apu();

  void reset();

  void write_soundcnt_l(std::uint16_t value);
  void write_soundcnt_h(std::uint16_t value);
  void write_soundcnt_x(std::uint16_t value);
  void write_soundbias(std::uint16_t value);
  void set_wave_bank_select(bool playing_bank);
  void write_wave_ram(std::size_t index, std::uint16_t value);
  void write_fifo(DirectSoundChannel channel, std::uint32_t value);
  void configure_square_channel(std::uint8_t channel, std::uint8_t duty,
                                std::uint8_t volume, std::uint16_t period_samples);
  void configure_wave_channel(std::uint8_t volume_shift, std::uint16_t period_samples);
  void configure_noise_channel(std::uint8_t volume, std::uint16_t period_samples,
                               bool narrow_lfsr);
  void disable_psg_channel(std::uint8_t channel);
  [[nodiscard]] std::optional<ApuFrameStep> tick(std::uint32_t cpu_cycles);
  [[nodiscard]] DirectSoundTimerResult timer_overflow(std::uint8_t timer_index);
  [[nodiscard]] std::optional<ApuMixedSample> pop_audio_sample();
  void clear_audio_buffer();

  [[nodiscard]] State save_state() const;
  [[nodiscard]] bool load_state(const State& state);

  [[nodiscard]] std::uint16_t soundcnt_l() const;
  [[nodiscard]] std::uint16_t soundcnt_h() const;
  [[nodiscard]] std::uint16_t soundcnt_x() const;
  [[nodiscard]] std::uint16_t soundbias() const;
  [[nodiscard]] std::uint16_t wave_ram(std::size_t index) const;
  [[nodiscard]] std::uint16_t wave_ram_playing(std::size_t index) const;
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
  [[nodiscard]] bool psg_channel_enabled(std::uint8_t channel) const;
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

  struct SquareChannel {
    bool enabled;
    std::uint8_t duty;
    std::uint8_t volume;
    std::uint16_t period_samples;
    std::uint16_t phase;
  };

  struct WaveChannel {
    bool enabled;
    std::uint8_t volume_shift;
    std::uint16_t period_samples;
    std::uint16_t phase;
  };

  struct NoiseChannel {
    bool enabled;
    std::uint8_t volume;
    std::uint16_t period_samples;
    std::uint16_t phase;
    std::uint16_t lfsr;
    bool narrow_lfsr;
  };

  std::uint16_t soundcnt_l_;
  std::uint16_t soundcnt_h_;
  std::uint16_t soundcnt_x_status_;
  std::uint16_t soundbias_;
  std::array<std::array<std::uint16_t, kWaveRamHalfwords>, kWaveRamBanks>
      wave_ram_banks_;
  bool wave_bank_select_;
  std::array<Fifo, 2> fifos_;
  std::array<std::int8_t, 2> direct_sound_latched_samples_;
  std::array<SquareChannel, 2> square_channels_;
  WaveChannel wave_channel_;
  NoiseChannel noise_channel_;
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
  [[nodiscard]] std::array<std::int32_t, 4> psg_channel_outputs() const;
  void push_audio_sample(ApuMixedSample sample);
  void generate_audio_sample();
  void advance_psg_generators();
  void clear_sound_circuit();
};

}  // namespace gba::core
