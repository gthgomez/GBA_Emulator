#include "gba/core/apu.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
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
  using gba::core::Apu;
  using gba::core::DirectSoundChannel;

  Apu apu;
  expect(!apu.master_enabled(), "APU master starts disabled");
  expect(apu.soundcnt_l() == 0, "SOUNDCNT_L resets clear");
  expect(apu.soundcnt_h() == 0, "SOUNDCNT_H resets clear");
  expect(apu.soundcnt_x() == 0, "SOUNDCNT_X resets clear");
  expect(apu.soundbias() == 0x0200, "SOUNDBIAS resets to midpoint bias");
  expect(apu.frame_step() == 0, "frame sequencer step resets");
  expect(apu.frame_step_count() == 0, "frame sequencer count resets");
  expect(apu.fifo_size(DirectSoundChannel::a) == 0, "FIFO A resets empty");
  expect(apu.fifo_size(DirectSoundChannel::b) == 0, "FIFO B resets empty");

  apu.write_soundcnt_l(0xFFFF);
  apu.write_soundcnt_h(0xFFFF);
  apu.write_soundbias(0xFFFF);
  apu.write_wave_ram(0, 0xBEEF);
  apu.write_fifo(DirectSoundChannel::a, 0x04030201);
  expect(apu.soundcnt_l() == 0, "disabled APU ignores SOUNDCNT_L writes");
  expect(apu.soundcnt_h() == 0, "disabled APU ignores SOUNDCNT_H writes");
  expect(apu.soundbias() == 0x0200, "disabled APU ignores SOUNDBIAS writes");
  expect(apu.wave_ram(0) == 0, "disabled APU ignores wave RAM writes");
  expect(apu.fifo_size(DirectSoundChannel::a) == 0, "disabled APU ignores FIFO writes");

  apu.write_soundcnt_x(0x0080);
  expect(apu.master_enabled(), "SOUNDCNT_X bit 7 enables master sound");
  expect(apu.soundcnt_x() == 0x0080, "SOUNDCNT_X exposes master enable");

  apu.write_soundcnt_l(0xFFFF);
  expect(apu.soundcnt_l() == 0xFF77, "SOUNDCNT_L masks writable bits");
  apu.write_soundbias(0xFFFF);
  expect(apu.soundbias() == 0xC3FF, "SOUNDBIAS masks writable bits");
  apu.write_wave_ram(2, 0xBEEF);
  expect(apu.wave_ram(2) == 0xBEEF, "enabled APU stores wave RAM");

  apu.write_soundcnt_h(0xFFFF);
  expect(apu.soundcnt_h() == 0x770F, "SOUNDCNT_H masks writable bits and reset pulses");
  expect(apu.direct_sound_enabled(DirectSoundChannel::a), "direct sound A route enables channel");
  expect(apu.direct_sound_enabled(DirectSoundChannel::b), "direct sound B route enables channel");
  expect(apu.direct_sound_uses_timer(DirectSoundChannel::a, 1),
         "direct sound A timer bit selects timer 1");
  expect(apu.direct_sound_uses_timer(DirectSoundChannel::b, 1),
         "direct sound B timer bit selects timer 1");
  expect(!apu.direct_sound_uses_timer(DirectSoundChannel::a, 0),
         "direct sound A ignores unselected timer");
  expect(!apu.direct_sound_uses_timer(DirectSoundChannel::b, 2),
         "direct sound B rejects timers outside 0/1");

  apu.write_fifo(DirectSoundChannel::a, 0x04030201);
  apu.write_fifo(DirectSoundChannel::a, 0x08070605);
  expect(apu.fifo_size(DirectSoundChannel::a) == 8, "FIFO A stores two words");
  expect(apu.fifo_refill_needed(DirectSoundChannel::a), "FIFO A below threshold asks refill");
  apu.write_fifo(DirectSoundChannel::b, 0xFCFDFEFF);
  expect(apu.fifo_size(DirectSoundChannel::b) == 4, "FIFO B stores one word");
  const gba::core::DirectSoundTimerResult timer0 = apu.timer_overflow(0);
  expect(!timer0.fifo_a.produced, "timer0 does not pop FIFO A when timer1 selected");
  expect(!timer0.fifo_b.produced, "timer0 does not pop FIFO B when timer1 selected");
  const gba::core::DirectSoundTimerResult timer1 = apu.timer_overflow(1);
  expect(timer1.fifo_a.produced, "timer1 pops FIFO A");
  expect(timer1.fifo_a.sample == 1, "FIFO A pops little-endian byte 0 first");
  expect(timer1.fifo_a.refill_request, "FIFO A pop reports refill request when low");
  expect(timer1.fifo_b.produced, "timer1 pops FIFO B");
  expect(timer1.fifo_b.sample == -1, "FIFO B stores samples as signed 8-bit values");
  expect(apu.fifo_size(DirectSoundChannel::a) == 7, "FIFO A size decreases after pop");
  expect(apu.fifo_size(DirectSoundChannel::b) == 3, "FIFO B size decreases after pop");

  apu.write_soundcnt_h(0x0800);
  expect(apu.fifo_size(DirectSoundChannel::a) == 0, "FIFO A reset pulse clears FIFO");
  expect(apu.soundcnt_h() == 0, "FIFO A reset pulse is not stored");

  for (std::uint8_t word = 0; word < 9; ++word) {
    apu.write_fifo(DirectSoundChannel::a,
                   static_cast<std::uint32_t>(0x03020100U + word * 0x04040404U));
  }
  expect(apu.fifo_size(DirectSoundChannel::a) == Apu::kFifoCapacity,
         "FIFO A saturates at 32 samples");
  expect(!apu.fifo_refill_needed(DirectSoundChannel::a), "full FIFO A does not ask refill");

  expect(!apu.tick(Apu::kCpuCyclesPerFrameSequencerStep - 1).has_value(),
         "frame sequencer waits for full 32768 cycles");
  expect(apu.frame_cycle_remainder() == Apu::kCpuCyclesPerFrameSequencerStep - 1,
         "frame sequencer keeps cycle remainder");
  const std::optional<gba::core::ApuFrameStep> step1 = apu.tick(1);
  expect(step1.has_value(), "frame sequencer emits event at 32768 cycles");
  expect(step1->step == 1, "frame sequencer advances to step 1");
  expect(!step1->length_clock, "frame sequencer step 1 does not clock length");
  expect(!step1->sweep_clock, "frame sequencer step 1 does not clock sweep");
  expect(!step1->envelope_clock, "frame sequencer step 1 does not clock envelope");
  expect(apu.frame_step_count() == 1, "frame sequencer count increments");
  const std::optional<gba::core::ApuFrameStep> step2 =
      apu.tick(Apu::kCpuCyclesPerFrameSequencerStep);
  expect(step2.has_value(), "frame sequencer reaches step 2");
  expect(step2->step == 2, "frame sequencer reports step 2");
  expect(step2->length_clock, "frame sequencer step 2 clocks length");
  expect(step2->sweep_clock, "frame sequencer step 2 clocks sweep");
  const std::optional<gba::core::ApuFrameStep> step3 =
      apu.tick(Apu::kCpuCyclesPerFrameSequencerStep);
  expect(step3.has_value(), "frame sequencer reaches step 3");
  expect(step3->step == 3, "frame sequencer reports step 3");
  for (std::uint8_t i = 0; i < 4; ++i) {
    const std::optional<gba::core::ApuFrameStep> ignored_step =
        apu.tick(Apu::kCpuCyclesPerFrameSequencerStep);
    expect(ignored_step.has_value(), "frame sequencer advances through later steps");
  }
  const std::optional<gba::core::ApuFrameStep> step0 =
      apu.tick(Apu::kCpuCyclesPerFrameSequencerStep);
  expect(step0.has_value(), "frame sequencer wraps after step 7");
  expect(step0->step == 0, "frame sequencer wraps to step 0");
  expect(step0->length_clock, "frame sequencer step 0 clocks length");
  const std::optional<gba::core::ApuFrameStep> step7 =
      apu.tick(Apu::kCpuCyclesPerFrameSequencerStep * 7U);
  expect(step7.has_value(), "frame sequencer can emit later event");
  expect(step7->step == 7, "large tick advances through multiple frame steps");
  expect(step7->envelope_clock, "frame sequencer step 7 clocks envelope");
  expect(apu.frame_step_count() == 15, "large tick increments total frame-step count");

  Apu audio;
  expect(!audio.tick(Apu::kCpuCyclesPerAudioSample).has_value(),
         "disabled APU does not generate audio samples");
  expect(audio.available_audio_samples() == 0,
         "disabled APU leaves audio buffer empty");

  audio.write_soundcnt_x(0x0080);
  audio.write_soundcnt_h(0x0304);
  audio.write_fifo(DirectSoundChannel::a, 0x00000040);
  expect(!audio.tick(Apu::kCpuCyclesPerAudioSample - 1U).has_value(),
         "audio resampler waits for full sample interval");
  expect(audio.available_audio_samples() == 0,
         "partial audio interval does not publish sample");
  const gba::core::DirectSoundTimerResult audio_timer = audio.timer_overflow(0);
  expect(audio_timer.fifo_a.produced, "timer0 pops Direct Sound A for audio mix");
  expect(audio_timer.fifo_a.sample == 0x40,
         "Direct Sound A latches signed FIFO sample for mixer");
  expect(!audio.tick(1).has_value(),
         "audio sample tick need not coincide with frame sequencer event");
  expect(audio.audio_cycle_remainder() == 0, "audio sample interval consumes remainder");
  expect(audio.audio_sample_count() == 1, "audio sample count increments");
  expect(audio.available_audio_samples() == 1, "mixed sample enters audio buffer");
  expect(audio.last_mixed_sample().left == 16384,
         "Direct Sound A full-volume left mix is deterministic");
  expect(audio.last_mixed_sample().right == 16384,
         "Direct Sound A full-volume right mix is deterministic");
  const std::optional<gba::core::ApuMixedSample> mixed = audio.pop_audio_sample();
  expect(mixed.has_value(), "mixed sample can be popped from audio buffer");
  expect(mixed->left == 16384 && mixed->right == 16384,
         "popped sample matches last mixed sample");
  expect(audio.available_audio_samples() == 0, "popping audio drains buffer");

  audio.reset();
  audio.write_soundcnt_x(0x0080);
  audio.write_soundcnt_h(0x0104);
  audio.write_fifo(DirectSoundChannel::a, 0x0000007F);
  [[maybe_unused]] const gba::core::DirectSoundTimerResult routed_timer =
      audio.timer_overflow(0);
  [[maybe_unused]] const std::optional<gba::core::ApuFrameStep> routed_step =
      audio.tick(Apu::kCpuCyclesPerAudioSample);
  expect(audio.last_mixed_sample().left == 0,
         "Direct Sound A left route stays silent when disabled");
  expect(audio.last_mixed_sample().right == 32512,
         "Direct Sound A right route emits scaled sample");

  audio.clear_audio_buffer();
  for (std::size_t i = 0; i < Apu::kAudioBufferCapacity + 2U; ++i) {
    [[maybe_unused]] const std::optional<gba::core::ApuFrameStep> fill_step =
        audio.tick(Apu::kCpuCyclesPerAudioSample);
  }
  expect(audio.available_audio_samples() == Apu::kAudioBufferCapacity,
         "audio buffer stays fixed capacity under sustained generation");

  apu.write_soundcnt_x(0);
  expect(!apu.master_enabled(), "clearing SOUNDCNT_X bit 7 disables master sound");
  expect(apu.soundcnt_l() == 0, "master disable clears SOUNDCNT_L");
  expect(apu.soundcnt_h() == 0, "master disable clears SOUNDCNT_H");
  expect(apu.fifo_size(DirectSoundChannel::a) == 0, "master disable clears FIFO A");
  expect(apu.wave_ram(2) == 0, "master disable clears wave RAM in this seed");
  expect(!apu.tick(Apu::kCpuCyclesPerFrameSequencerStep).has_value(),
         "disabled APU does not tick frame sequencer");

  std::cout << "apu_test: PASS\n";
  return 0;
}
