#include "gba/core/apu.hpp"

#include "gba/core/state_hash.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace gba::core {
namespace {

constexpr std::uint16_t kSoundCntLMask = 0xFF77;
constexpr std::uint16_t kSoundCntHMask = 0xFF0F;
constexpr std::uint16_t kFifoAReset = 0x0800;
constexpr std::uint16_t kFifoBReset = 0x8000;
constexpr std::uint16_t kSoundCntHStoredMask =
    static_cast<std::uint16_t>(kSoundCntHMask & ~kFifoAReset & ~kFifoBReset);
constexpr std::uint16_t kMasterEnable = 0x0080;
// GBATEK SOUNDBIAS: bias level bits 0-9 plus PWM resolution bits 14-15 are
// writable (0x43FF), not the previously stored 0xC3FF.
constexpr std::uint16_t kSoundBiasMask = 0x43FF;
// Provenance (UNVERIFIED-vs-hardware): the 128 amplitude ratio between PSG
// full-scale and Direct Sound full-scale keeps legacy output levels stable;
// exact hardware PWM scaling was never measured against a real unit.
constexpr std::int32_t kDirectSoundHalfScale = 128;
constexpr std::array<std::uint8_t, 4> kSquareDutyHighSamples{{1, 2, 4, 6}};
constexpr std::int32_t kPsgScale = 128;

[[nodiscard]] constexpr std::size_t fifo_index(DirectSoundChannel channel) {
  return static_cast<std::size_t>(channel);
}

[[nodiscard]] constexpr bool frame_length_clock(std::uint8_t step) {
  return step == 0 || step == 2 || step == 4 || step == 6;
}

[[nodiscard]] constexpr bool frame_sweep_clock(std::uint8_t step) {
  return step == 2 || step == 6;
}

[[nodiscard]] constexpr bool frame_envelope_clock(std::uint8_t step) {
  return step == 7;
}

[[nodiscard]] constexpr std::int16_t clamp_mixed_sample(std::int32_t value) {
  return static_cast<std::int16_t>(
      std::clamp(value, static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()),
                 static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max())));
}

}  // namespace

Apu::Apu() {
  reset();
}

void Apu::reset() {
  soundcnt_l_ = 0;
  soundcnt_h_ = 0;
  soundcnt_x_status_ = 0;
  soundbias_ = 0x0200;
  for (std::array<std::uint16_t, kWaveRamHalfwords>& bank : wave_ram_banks_) {
    bank.fill(0);
  }
  wave_bank_select_ = false;
  for (Fifo& state : fifos_) {
    state.samples.fill(0);
    state.head = 0;
    state.size = 0;
  }
  direct_sound_latched_samples_.fill(0);
  for (SquareChannel& square : square_channels_) {
    square = {false, 0, 0, 1, 0};
  }
  wave_channel_ = {false, 0, 1, 0};
  noise_channel_ = {false, 0, 1, 0, 0x7FFF, false};
  audio_buffer_.samples.fill({0, 0});
  audio_buffer_.head = 0;
  audio_buffer_.size = 0;
  frame_step_count_ = 0;
  audio_sample_count_ = 0;
  frame_cycle_remainder_ = 0;
  audio_cycle_remainder_ = 0;
  frame_step_ = 0;
  last_mixed_sample_ = {0, 0};
}

void Apu::write_soundcnt_l(std::uint16_t value) {
  if (!master_enabled()) {
    return;
  }
  soundcnt_l_ = static_cast<std::uint16_t>(value & kSoundCntLMask);
}

void Apu::write_soundcnt_h(std::uint16_t value) {
  if (!master_enabled()) {
    return;
  }
  if ((value & kFifoAReset) != 0) {
    clear_fifo(DirectSoundChannel::a);
  }
  if ((value & kFifoBReset) != 0) {
    clear_fifo(DirectSoundChannel::b);
  }
  soundcnt_h_ = static_cast<std::uint16_t>(value & kSoundCntHStoredMask);
}

void Apu::write_soundcnt_x(std::uint16_t value) {
  const bool enable = (value & kMasterEnable) != 0;
  if (!enable) {
    clear_sound_circuit();
    return;
  }
  soundcnt_x_status_ = static_cast<std::uint16_t>(soundcnt_x_status_ | kMasterEnable);
}

void Apu::write_soundbias(std::uint16_t value) {
  if (!master_enabled()) {
    return;
  }
  soundbias_ = static_cast<std::uint16_t>(value & kSoundBiasMask);
}

void Apu::set_wave_bank_select(bool playing_bank) {
  wave_bank_select_ = playing_bank;
}

// Policy: FIFO and wave-RAM writes bypass the NR52 master-enable gate. The
// hardware lock covers PSG channel registers only; DMA FIFO preloads at init
// must not be swallowed while sound is still disabled.
void Apu::write_wave_ram(std::size_t index, std::uint16_t value) {
  if (index >= wave_ram_banks_.at(0).size()) {
    throw std::out_of_range("wave RAM index out of range");
  }
  wave_ram_banks_.at(wave_bank_select_ ? 0U : 1U).at(index) = value;
}

void Apu::write_fifo(DirectSoundChannel channel, std::uint32_t value) {
  for (std::uint8_t byte = 0; byte < 4; ++byte) {
    push_fifo(channel, static_cast<std::int8_t>((value >> (byte * 8U)) & 0xFFU));
  }
}

void Apu::configure_square_channel(std::uint8_t channel, std::uint8_t duty,
                                   std::uint8_t volume,
                                   std::uint16_t period_samples) {
  if (!master_enabled() || channel >= square_channels_.size()) {
    return;
  }
  square_channels_.at(channel) = {true,
                                  static_cast<std::uint8_t>(duty & 0x3U),
                                  static_cast<std::uint8_t>(std::min<std::uint8_t>(volume, 15U)),
                                  static_cast<std::uint16_t>(std::max<std::uint16_t>(1U, period_samples)),
                                  0};
}

void Apu::configure_wave_channel(std::uint8_t volume_shift,
                                 std::uint16_t period_samples) {
  if (!master_enabled()) {
    return;
  }
  wave_channel_ = {true,
                   static_cast<std::uint8_t>(std::min<std::uint8_t>(volume_shift, 4U)),
                   static_cast<std::uint16_t>(std::max<std::uint16_t>(1U, period_samples)),
                   0};
}

void Apu::configure_noise_channel(std::uint8_t volume, std::uint16_t period_samples,
                                  bool narrow_lfsr) {
  if (!master_enabled()) {
    return;
  }
  noise_channel_ = {true,
                    static_cast<std::uint8_t>(std::min<std::uint8_t>(volume, 15U)),
                    static_cast<std::uint16_t>(std::max<std::uint16_t>(1U, period_samples)),
                    0,
                    0x7FFF,
                    narrow_lfsr};
}

void Apu::disable_psg_channel(std::uint8_t channel) {
  if (channel < square_channels_.size()) {
    square_channels_.at(channel).enabled = false;
    return;
  }
  if (channel == 2) {
    wave_channel_.enabled = false;
    return;
  }
  if (channel == 3) {
    noise_channel_.enabled = false;
  }
}

std::optional<ApuFrameStep> Apu::tick(std::uint32_t cpu_cycles) {
  if (!master_enabled()) {
    return std::nullopt;
  }

  frame_cycle_remainder_ += cpu_cycles;
  audio_cycle_remainder_ += cpu_cycles;
  std::optional<ApuFrameStep> last_step;
  while (frame_cycle_remainder_ >= kCpuCyclesPerFrameSequencerStep) {
    frame_cycle_remainder_ -= kCpuCyclesPerFrameSequencerStep;
    frame_step_ = static_cast<std::uint8_t>((frame_step_ + 1U) % kFrameSequencerSteps);
    ++frame_step_count_;
    last_step = ApuFrameStep{frame_step_, frame_length_clock(frame_step_),
                             frame_sweep_clock(frame_step_),
                             frame_envelope_clock(frame_step_)};
  }

  while (audio_cycle_remainder_ >= kCpuCyclesPerAudioSample) {
    audio_cycle_remainder_ -= kCpuCyclesPerAudioSample;
    generate_audio_sample();
  }

  return last_step;
}

DirectSoundTimerResult Apu::timer_overflow(std::uint8_t timer_index) {
  if (!master_enabled()) {
    return {};
  }

  DirectSoundTimerResult result{};
  if (direct_sound_uses_timer(DirectSoundChannel::a, timer_index)) {
    result.fifo_a = pop_fifo(DirectSoundChannel::a);
    direct_sound_latched_samples_.at(fifo_index(DirectSoundChannel::a)) =
        result.fifo_a.produced ? result.fifo_a.sample : 0;
  }
  if (direct_sound_uses_timer(DirectSoundChannel::b, timer_index)) {
    result.fifo_b = pop_fifo(DirectSoundChannel::b);
    direct_sound_latched_samples_.at(fifo_index(DirectSoundChannel::b)) =
        result.fifo_b.produced ? result.fifo_b.sample : 0;
  }
  return result;
}

std::optional<ApuMixedSample> Apu::pop_audio_sample() {
  if (audio_buffer_.size == 0) {
    return std::nullopt;
  }

  const ApuMixedSample sample = audio_buffer_.samples.at(audio_buffer_.head);
  audio_buffer_.head = (audio_buffer_.head + 1U) % kAudioBufferCapacity;
  --audio_buffer_.size;
  return sample;
}

void Apu::clear_audio_buffer() {
  audio_buffer_.samples.fill({0, 0});
  audio_buffer_.head = 0;
  audio_buffer_.size = 0;
}

Apu::State Apu::save_state() const {
  State state;
  state.soundcnt_l = soundcnt_l_;
  state.soundcnt_h = soundcnt_h_;
  state.soundcnt_x_status = soundcnt_x_status_;
  state.soundbias = soundbias_;
  state.wave_ram_banks = wave_ram_banks_;
  state.wave_bank_select = wave_bank_select_;
  for (std::size_t index = 0; index < fifos_.size(); ++index) {
    const Fifo& fifo_state = fifos_.at(index);
    FifoState& saved = state.fifos.at(index);
    saved.samples = fifo_state.samples;
    saved.head = fifo_state.head;
    saved.size = fifo_state.size;
  }
  state.direct_sound_latched_samples = direct_sound_latched_samples_;
  for (std::size_t index = 0; index < square_channels_.size(); ++index) {
    const SquareChannel& square = square_channels_.at(index);
    SquareChannelState& saved = state.square_channels.at(index);
    saved.enabled = square.enabled;
    saved.duty = square.duty;
    saved.volume = square.volume;
    saved.period_samples = square.period_samples;
    saved.phase = square.phase;
  }
  state.wave_channel.enabled = wave_channel_.enabled;
  state.wave_channel.volume_shift = wave_channel_.volume_shift;
  state.wave_channel.period_samples = wave_channel_.period_samples;
  state.wave_channel.phase = wave_channel_.phase;
  state.noise_channel.enabled = noise_channel_.enabled;
  state.noise_channel.volume = noise_channel_.volume;
  state.noise_channel.period_samples = noise_channel_.period_samples;
  state.noise_channel.phase = noise_channel_.phase;
  state.noise_channel.lfsr = noise_channel_.lfsr;
  state.noise_channel.narrow_lfsr = noise_channel_.narrow_lfsr;
  state.audio_buffer_samples = audio_buffer_.samples;
  state.audio_buffer_head = audio_buffer_.head;
  state.audio_buffer_size = audio_buffer_.size;
  state.frame_step_count = frame_step_count_;
  state.audio_sample_count = audio_sample_count_;
  state.frame_cycle_remainder = frame_cycle_remainder_;
  state.audio_cycle_remainder = audio_cycle_remainder_;
  state.frame_step = frame_step_;
  state.last_mixed_sample = last_mixed_sample_;
  return state;
}

bool Apu::load_state(const State& state) {
  for (const FifoState& fifo_state : state.fifos) {
    if (fifo_state.head >= kFifoCapacity || fifo_state.size > kFifoCapacity ||
        fifo_state.size > fifo_state.samples.size()) {
      return false;
    }
  }
  if (state.audio_buffer_head >= kAudioBufferCapacity ||
      state.audio_buffer_size > kAudioBufferCapacity) {
    return false;
  }

  soundcnt_l_ = state.soundcnt_l;
  soundcnt_h_ = state.soundcnt_h;
  soundcnt_x_status_ = state.soundcnt_x_status;
  soundbias_ = state.soundbias;
  wave_ram_banks_ = state.wave_ram_banks;
  wave_bank_select_ = state.wave_bank_select;
  for (std::size_t index = 0; index < fifos_.size(); ++index) {
    const FifoState& saved = state.fifos.at(index);
    Fifo& fifo_state = fifos_.at(index);
    fifo_state.samples = saved.samples;
    fifo_state.head = saved.head;
    fifo_state.size = saved.size;
  }
  direct_sound_latched_samples_ = state.direct_sound_latched_samples;
  for (std::size_t index = 0; index < square_channels_.size(); ++index) {
    const SquareChannelState& saved = state.square_channels.at(index);
    SquareChannel& square = square_channels_.at(index);
    square.enabled = saved.enabled;
    square.duty = saved.duty;
    square.volume = saved.volume;
    square.period_samples = saved.period_samples;
    square.phase = saved.phase;
  }
  wave_channel_.enabled = state.wave_channel.enabled;
  wave_channel_.volume_shift = state.wave_channel.volume_shift;
  wave_channel_.period_samples = state.wave_channel.period_samples;
  wave_channel_.phase = state.wave_channel.phase;
  noise_channel_.enabled = state.noise_channel.enabled;
  noise_channel_.volume = state.noise_channel.volume;
  noise_channel_.period_samples = state.noise_channel.period_samples;
  noise_channel_.phase = state.noise_channel.phase;
  noise_channel_.lfsr = state.noise_channel.lfsr;
  noise_channel_.narrow_lfsr = state.noise_channel.narrow_lfsr;
  audio_buffer_.samples = state.audio_buffer_samples;
  audio_buffer_.head = state.audio_buffer_head;
  audio_buffer_.size = state.audio_buffer_size;
  frame_step_count_ = state.frame_step_count;
  audio_sample_count_ = state.audio_sample_count;
  frame_cycle_remainder_ = state.frame_cycle_remainder;
  audio_cycle_remainder_ = state.audio_cycle_remainder;
  frame_step_ = state.frame_step;
  last_mixed_sample_ = state.last_mixed_sample;
  return true;
}

std::uint16_t Apu::soundcnt_l() const {
  return soundcnt_l_;
}

std::uint16_t Apu::soundcnt_h() const {
  return soundcnt_h_;
}

std::uint16_t Apu::soundcnt_x() const {
  // NR52 readback exposes per-channel active status in bits 0-3.
  std::uint16_t value = soundcnt_x_status_;
  if (square_channels_.at(0).enabled) {
    value = static_cast<std::uint16_t>(value | 0x0001U);
  }
  if (square_channels_.at(1).enabled) {
    value = static_cast<std::uint16_t>(value | 0x0002U);
  }
  if (wave_channel_.enabled) {
    value = static_cast<std::uint16_t>(value | 0x0004U);
  }
  if (noise_channel_.enabled) {
    value = static_cast<std::uint16_t>(value | 0x0008U);
  }
  return value;
}

std::uint16_t Apu::soundbias() const {
  return soundbias_;
}

std::uint16_t Apu::wave_ram(std::size_t index) const {
  if (index >= wave_ram_banks_.at(0).size()) {
    throw std::out_of_range("wave RAM index out of range");
  }
  // CPU accesses target the non-playing bank of the two-bank wave RAM.
  return wave_ram_banks_.at(wave_bank_select_ ? 0U : 1U).at(index);
}

std::uint16_t Apu::wave_ram_playing(std::size_t index) const {
  if (index >= wave_ram_banks_.at(0).size()) {
    throw std::out_of_range("wave RAM index out of range");
  }
  return wave_ram_banks_.at(wave_bank_select_ ? 1U : 0U).at(index);
}

bool Apu::master_enabled() const {
  return (soundcnt_x_status_ & kMasterEnable) != 0;
}

std::uint8_t Apu::frame_step() const {
  return frame_step_;
}

std::uint64_t Apu::frame_step_count() const {
  return frame_step_count_;
}

std::uint32_t Apu::frame_cycle_remainder() const {
  return frame_cycle_remainder_;
}

std::uint32_t Apu::audio_cycle_remainder() const {
  return audio_cycle_remainder_;
}

std::uint64_t Apu::audio_sample_count() const {
  return audio_sample_count_;
}

std::size_t Apu::available_audio_samples() const {
  return audio_buffer_.size;
}

ApuMixedSample Apu::last_mixed_sample() const {
  return last_mixed_sample_;
}

std::size_t Apu::fifo_size(DirectSoundChannel channel) const {
  return fifo(channel).size;
}

bool Apu::fifo_refill_needed(DirectSoundChannel channel) const {
  return fifo_size(channel) <= kFifoRefillThreshold;
}

bool Apu::direct_sound_uses_timer(DirectSoundChannel channel, std::uint8_t timer_index) const {
  if (!direct_sound_enabled(channel)) {
    return false;
  }
  const std::uint8_t selected_timer =
      channel == DirectSoundChannel::a ? static_cast<std::uint8_t>((soundcnt_h_ >> 10) & 0x1U)
                                       : static_cast<std::uint8_t>((soundcnt_h_ >> 14) & 0x1U);
  return timer_index <= 1 && selected_timer == timer_index;
}

bool Apu::direct_sound_enabled(DirectSoundChannel channel) const {
  const std::uint16_t enable_mask =
      channel == DirectSoundChannel::a ? 0x0300U : 0x3000U;
  return (soundcnt_h_ & enable_mask) != 0;
}

bool Apu::psg_channel_enabled(std::uint8_t channel) const {
  if (channel < square_channels_.size()) {
    return square_channels_.at(channel).enabled;
  }
  if (channel == 2) {
    return wave_channel_.enabled;
  }
  if (channel == 3) {
    return noise_channel_.enabled;
  }
  return false;
}

std::uint64_t Apu::state_hash() const {
  StateHasher hasher;
  hasher.add_u16(soundcnt_l_);
  hasher.add_u16(soundcnt_h_);
  hasher.add_u16(soundcnt_x_status_);
  hasher.add_u16(soundbias_);
  for (const std::array<std::uint16_t, kWaveRamHalfwords>& bank : wave_ram_banks_) {
    hasher.add_bytes(bank);
  }
  hasher.add_bool(wave_bank_select_);
  for (const Fifo& fifo_state : fifos_) {
    hasher.add_bytes(fifo_state.samples);
    hasher.add_u64(static_cast<std::uint64_t>(fifo_state.head));
    hasher.add_u64(static_cast<std::uint64_t>(fifo_state.size));
  }
  for (const std::int8_t sample : direct_sound_latched_samples_) {
    hasher.add_u8(static_cast<std::uint8_t>(sample));
  }
  for (const SquareChannel& square : square_channels_) {
    hasher.add_bool(square.enabled);
    hasher.add_u8(square.duty);
    hasher.add_u8(square.volume);
    hasher.add_u16(square.period_samples);
    hasher.add_u16(square.phase);
  }
  hasher.add_bool(wave_channel_.enabled);
  hasher.add_u8(wave_channel_.volume_shift);
  hasher.add_u16(wave_channel_.period_samples);
  hasher.add_u16(wave_channel_.phase);
  hasher.add_bool(noise_channel_.enabled);
  hasher.add_u8(noise_channel_.volume);
  hasher.add_u16(noise_channel_.period_samples);
  hasher.add_u16(noise_channel_.phase);
  hasher.add_u16(noise_channel_.lfsr);
  hasher.add_bool(noise_channel_.narrow_lfsr);
  for (const ApuMixedSample& sample : audio_buffer_.samples) {
    hasher.add_u16(static_cast<std::uint16_t>(sample.left));
    hasher.add_u16(static_cast<std::uint16_t>(sample.right));
  }
  hasher.add_u64(static_cast<std::uint64_t>(audio_buffer_.head));
  hasher.add_u64(static_cast<std::uint64_t>(audio_buffer_.size));
  hasher.add_u64(frame_step_count_);
  hasher.add_u64(audio_sample_count_);
  hasher.add_u32(frame_cycle_remainder_);
  hasher.add_u32(audio_cycle_remainder_);
  hasher.add_u8(frame_step_);
  hasher.add_u16(static_cast<std::uint16_t>(last_mixed_sample_.left));
  hasher.add_u16(static_cast<std::uint16_t>(last_mixed_sample_.right));
  return hasher.value();
}

Apu::Fifo& Apu::fifo(DirectSoundChannel channel) {
  return fifos_.at(fifo_index(channel));
}

const Apu::Fifo& Apu::fifo(DirectSoundChannel channel) const {
  return fifos_.at(fifo_index(channel));
}

void Apu::clear_fifo(DirectSoundChannel channel) {
  Fifo& state = fifo(channel);
  state.head = 0;
  state.size = 0;
  state.samples.fill(0);
}

void Apu::push_fifo(DirectSoundChannel channel, std::int8_t sample) {
  Fifo& state = fifo(channel);
  if (state.size == kFifoCapacity) {
    state.head = (state.head + 1U) % kFifoCapacity;
    --state.size;
  }
  const std::size_t tail = (state.head + state.size) % kFifoCapacity;
  state.samples.at(tail) = sample;
  ++state.size;
}

DirectSoundSample Apu::pop_fifo(DirectSoundChannel channel) {
  Fifo& state = fifo(channel);
  if (state.size == 0) {
    return {false, 0, true};
  }
  const std::int8_t sample = state.samples.at(state.head);
  state.head = (state.head + 1U) % kFifoCapacity;
  --state.size;
  return {true, sample, fifo_refill_needed(channel)};
}

ApuMixedSample Apu::mix_sample() const {
  std::int32_t left = 0;
  std::int32_t right = 0;

  const std::array<std::int32_t, 4> psg_outputs = psg_channel_outputs();
  // SOUNDCNT_L is a 16-bit register: the low byte is NR50 (master volume,
  // bits 0-2 right / 4-6 left) and the high byte is NR51 (channel routing,
  // bits 0-3 right ch1-4 / 4-7 left ch1-4).
  const std::uint16_t nr50 = static_cast<std::uint16_t>(soundcnt_l_ & 0x00FFU);
  const std::uint16_t nr51 = static_cast<std::uint16_t>((soundcnt_l_ >> 8) & 0x00FFU);
  const std::int32_t left_volume =
      static_cast<std::int32_t>(((nr50 >> 4) & 0x7U) + 1U);
  const std::int32_t right_volume =
      static_cast<std::int32_t>((nr50 & 0x7U) + 1U);
  for (std::size_t index = 0; index < psg_outputs.size(); ++index) {
    if ((nr51 & static_cast<std::uint16_t>(1U << index)) != 0) {
      right += psg_outputs.at(index) * right_volume;
    }
    if ((nr51 & static_cast<std::uint16_t>(1U << (index + 4U))) != 0) {
      left += psg_outputs.at(index) * left_volume;
    }
  }

  const auto mix_direct_channel = [&](DirectSoundChannel channel) {
    if (!direct_sound_enabled(channel)) {
      return;
    }

    const std::size_t index = fifo_index(channel);
    const std::int32_t sample = direct_sound_latched_samples_.at(index);
    const bool is_a = channel == DirectSoundChannel::a;
    const bool full_volume =
        (soundcnt_h_ & (is_a ? 0x0004U : 0x0008U)) != 0;
    const std::int32_t scaled =
        sample * kDirectSoundHalfScale * (full_volume ? 2 : 1);
    const bool route_right = (soundcnt_h_ & (is_a ? 0x0100U : 0x1000U)) != 0;
    const bool route_left = (soundcnt_h_ & (is_a ? 0x0200U : 0x2000U)) != 0;
    if (route_right) {
      right += scaled;
    }
    if (route_left) {
      left += scaled;
    }
  };

  mix_direct_channel(DirectSoundChannel::a);
  mix_direct_channel(DirectSoundChannel::b);

  // Deviation marker: SOUNDBIAS is masked and stored but intentionally not
  // applied to mixed samples; faithful PWM bias semantics would shift every
  // legacy output-level expectation (UNVERIFIED-vs-hardware).
  return {clamp_mixed_sample(left), clamp_mixed_sample(right)};
}

std::array<std::int32_t, 4> Apu::psg_channel_outputs() const {
  std::array<std::int32_t, 4> outputs{0, 0, 0, 0};
  for (std::size_t index = 0; index < square_channels_.size(); ++index) {
    const SquareChannel& square = square_channels_.at(index);
    if (!square.enabled || square.volume == 0) {
      continue;
    }
    const std::uint8_t duty_step =
        static_cast<std::uint8_t>((square.phase * 8U) / square.period_samples);
    const bool high = duty_step < kSquareDutyHighSamples.at(square.duty);
    outputs.at(index) =
        (high ? 1 : -1) * static_cast<std::int32_t>(square.volume) * kPsgScale;
  }

  if (wave_channel_.enabled && wave_channel_.volume_shift != 0) {
    const std::uint8_t sample_index =
        static_cast<std::uint8_t>((wave_channel_.phase * 32U) / wave_channel_.period_samples);
    const std::array<std::uint16_t, kWaveRamHalfwords>& playing_bank =
        wave_ram_banks_.at(wave_bank_select_ ? 1U : 0U);
    const std::uint16_t packed = playing_bank.at(sample_index / 4U);
    const std::uint8_t nibble_shift =
        static_cast<std::uint8_t>((3U - (sample_index % 4U)) * 4U);
    const std::int32_t sample =
        static_cast<std::int32_t>((packed >> nibble_shift) & 0x0FU) - 8;
    outputs.at(2) = (sample * kPsgScale) >> (wave_channel_.volume_shift - 1U);
  }

  if (noise_channel_.enabled && noise_channel_.volume != 0) {
    const bool high = (noise_channel_.lfsr & 0x1U) == 0;
    outputs.at(3) =
        (high ? 1 : -1) * static_cast<std::int32_t>(noise_channel_.volume) * kPsgScale;
  }
  return outputs;
}

void Apu::push_audio_sample(ApuMixedSample sample) {
  if (audio_buffer_.size == kAudioBufferCapacity) {
    audio_buffer_.head = (audio_buffer_.head + 1U) % kAudioBufferCapacity;
    --audio_buffer_.size;
  }

  const std::size_t tail = (audio_buffer_.head + audio_buffer_.size) %
                           kAudioBufferCapacity;
  audio_buffer_.samples.at(tail) = sample;
  ++audio_buffer_.size;
}

void Apu::generate_audio_sample() {
  last_mixed_sample_ = mix_sample();
  push_audio_sample(last_mixed_sample_);
  ++audio_sample_count_;
  advance_psg_generators();
}

void Apu::advance_psg_generators() {
  for (SquareChannel& square : square_channels_) {
    if (!square.enabled) {
      continue;
    }
    square.phase = static_cast<std::uint16_t>((square.phase + 1U) % square.period_samples);
  }
  if (wave_channel_.enabled) {
    wave_channel_.phase =
        static_cast<std::uint16_t>((wave_channel_.phase + 1U) % wave_channel_.period_samples);
  }
  if (noise_channel_.enabled) {
    noise_channel_.phase =
        static_cast<std::uint16_t>((noise_channel_.phase + 1U) % noise_channel_.period_samples);
    if (noise_channel_.phase == 0) {
      const std::uint16_t bit =
          static_cast<std::uint16_t>((noise_channel_.lfsr ^ (noise_channel_.lfsr >> 1U)) & 0x1U);
      noise_channel_.lfsr = static_cast<std::uint16_t>((noise_channel_.lfsr >> 1U) | (bit << 14U));
      if (noise_channel_.narrow_lfsr) {
        noise_channel_.lfsr =
            static_cast<std::uint16_t>((noise_channel_.lfsr & ~(1U << 6U)) | (bit << 6U));
      }
    }
  }
}

// Hardware preserves wave RAM across NR52 master-disable; only registers,
// FIFOs, counters, and channel state reset here.
void Apu::clear_sound_circuit() {
  soundcnt_l_ = 0;
  soundcnt_h_ = 0;
  soundcnt_x_status_ = 0;
  frame_step_count_ = 0;
  audio_sample_count_ = 0;
  frame_cycle_remainder_ = 0;
  audio_cycle_remainder_ = 0;
  direct_sound_latched_samples_.fill(0);
  last_mixed_sample_ = {0, 0};
  frame_step_ = 0;
  wave_bank_select_ = false;
  for (SquareChannel& square : square_channels_) {
    square = {false, 0, 0, 1, 0};
  }
  wave_channel_ = {false, 0, 1, 0};
  noise_channel_ = {false, 0, 1, 0, 0x7FFF, false};
  clear_fifo(DirectSoundChannel::a);
  clear_fifo(DirectSoundChannel::b);
  clear_audio_buffer();
}

}  // namespace gba::core
