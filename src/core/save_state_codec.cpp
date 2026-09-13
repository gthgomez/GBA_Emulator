#include "gba/core/save_state_codec.hpp"

#include "gba/core/arm7tdmi.hpp"
#include "gba/core/apu.hpp"
#include "gba/core/core_session.hpp"
#include "gba/core/dma_controller.hpp"
#include "gba/core/io_registers.hpp"
#include "gba/core/ppu_timing.hpp"
#include "gba/core/state_hash.hpp"
#include "gba/core/timers.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace gba::core {
namespace {

// v3 wire layout: little-endian, fixed width, no padding, sections in this
// exact order (encode and decode must stay in lockstep):
//   header        magic u32 | version u32 | state_hash u64
//   cpu           elapsed u64 | 16x u32 regs | 7 flag bytes | mode byte |
//                 5+5 u32 R8-R12 banks | 12 u32 banked SP/LR | 5 u32 SPSRs
//   memory        ewram/iwram/palette/vram/oam raw dumps | rom blob |
//                 save blob | save-type byte | flash-FSM bytes |
//                 open-bus latch u32+valid | mGBA string raw dump |
//                 debug-output blob
//   interrupts    IE u16 | IF u16 | IME byte
//   timers        cycle_counter u64 | x4 {counter,reload,control u16,
//                 overflow u64, enable-delay u32, phase u16, enabled byte}
//                 (v2's prescaler_remainder is dropped)
//   dma           x4 {src,dst u32, count,control u16, cur-src,cur-dst,count,
//                 latch u32} | immediate-pending byte
//   ppu           line,line-cycle,dispstat u16 | 0x2B x u16 lcd regs
//   apu           4x u16 SOUNDCNT | wave banks [2][8] u16 | bank byte |
//                 2 FIFOs (32 samples + head/size u32) | latched samples |
//                 square/wave/noise channel fields | audio buffer
//                 (2048 x L/R i16) + head/size u32 | counters/reminders |
//                 frame-step byte | last mixed sample
//   keypad        pressed u16 | keycnt u16
//   waitcnt       control u16
//   bios          mode byte
//   io            serial 7x u16 | sio active byte | sio remaining u32
//   scheduler     all 28 CoreSchedulerState fields (optionals as a presence
//                 byte followed by the payload when present)

void write_u8(std::vector<std::uint8_t>& out, std::uint8_t value) {
  out.push_back(value);
}

void write_bool(std::vector<std::uint8_t>& out, bool value) {
  write_u8(out, value ? 1U : 0U);
}

void write_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
  write_u8(out, static_cast<std::uint8_t>(value & 0xFFU));
  write_u8(out, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void write_i16(std::vector<std::uint8_t>& out, std::int16_t value) {
  write_u16(out, static_cast<std::uint16_t>(value));
}

void write_i8(std::vector<std::uint8_t>& out, std::int8_t value) {
  write_u8(out, static_cast<std::uint8_t>(value));
}

void write_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (std::uint8_t byte = 0; byte < 4; ++byte) {
    write_u8(out, static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xFFU));
  }
}

void write_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (std::uint8_t byte = 0; byte < 8; ++byte) {
    write_u8(out, static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xFFU));
  }
}

void write_fixed(std::vector<std::uint8_t>& out, const std::uint8_t* data,
                 std::size_t size) {
  if (size != 0U) {
    out.insert(out.end(), data, data + size);
  }
}

void write_blob(std::vector<std::uint8_t>& out,
                const std::vector<std::uint8_t>& bytes) {
  write_u32(out, static_cast<std::uint32_t>(bytes.size()));
  write_fixed(out, bytes.data(), bytes.size());
}

void write_optional_u8(std::vector<std::uint8_t>& out,
                       const std::optional<std::uint8_t>& value) {
  write_bool(out, value.has_value());
  if (value.has_value()) {
    write_u8(out, *value);
  }
}

void write_optional_u16(std::vector<std::uint8_t>& out,
                        const std::optional<std::uint16_t>& value) {
  write_bool(out, value.has_value());
  if (value.has_value()) {
    write_u16(out, *value);
  }
}

void write_optional_u32(std::vector<std::uint8_t>& out,
                        const std::optional<std::uint32_t>& value) {
  write_bool(out, value.has_value());
  if (value.has_value()) {
    write_u32(out, *value);
  }
}

void write_cpu_state(std::vector<std::uint8_t>& out, const Arm7tdmi::State& cpu) {
  write_u64(out, cpu.elapsed_cycles);
  for (const std::uint32_t register_value : cpu.registers) {
    write_u32(out, register_value);
  }
  write_bool(out, cpu.negative);
  write_bool(out, cpu.zero);
  write_bool(out, cpu.carry);
  write_bool(out, cpu.overflow);
  write_bool(out, cpu.irq_disabled);
  write_bool(out, cpu.fiq_disabled);
  write_bool(out, cpu.thumb_state);
  write_u8(out, static_cast<std::uint8_t>(cpu.mode));
  for (const std::uint32_t value : cpu.shared_r8_r12) {
    write_u32(out, value);
  }
  for (const std::uint32_t value : cpu.fiq_r8_r12) {
    write_u32(out, value);
  }
  write_u32(out, cpu.user_sp);
  write_u32(out, cpu.user_lr);
  write_u32(out, cpu.fiq_sp);
  write_u32(out, cpu.fiq_lr);
  write_u32(out, cpu.irq_sp);
  write_u32(out, cpu.irq_lr);
  write_u32(out, cpu.supervisor_sp);
  write_u32(out, cpu.supervisor_lr);
  write_u32(out, cpu.abort_sp);
  write_u32(out, cpu.abort_lr);
  write_u32(out, cpu.undefined_sp);
  write_u32(out, cpu.undefined_lr);
  write_u32(out, cpu.fiq_spsr);
  write_u32(out, cpu.supervisor_spsr);
  write_u32(out, cpu.abort_spsr);
  write_u32(out, cpu.irq_spsr);
  write_u32(out, cpu.undefined_spsr);
}

void write_memory_state(std::vector<std::uint8_t>& out,
                        const MemoryBus::State& memory) {
  write_fixed(out, memory.ewram.data(), memory.ewram.size());
  write_fixed(out, memory.iwram.data(), memory.iwram.size());
  write_fixed(out, memory.palette.data(), memory.palette.size());
  write_fixed(out, memory.vram.data(), memory.vram.size());
  write_fixed(out, memory.oam.data(), memory.oam.size());
  write_blob(out, memory.game_pak_rom);
  write_blob(out, memory.game_pak_save);
  write_u8(out, static_cast<std::uint8_t>(memory.game_pak_save_type));
  write_u8(out, static_cast<std::uint8_t>(memory.flash_command_state));
  write_bool(out, memory.flash_id_mode);
  write_u8(out, memory.flash_bank);
  write_u32(out, memory.open_bus_latch);
  write_bool(out, memory.open_bus_latch_valid);
  write_fixed(out, memory.mgba_debug_string.data(),
              memory.mgba_debug_string.size());
  const std::vector<std::uint8_t> debug_output(memory.debug_output.begin(),
                                                memory.debug_output.end());
  write_blob(out, debug_output);
}

void write_interrupt_state(std::vector<std::uint8_t>& out,
                           const InterruptController& interrupts) {
  write_u16(out, interrupts.interrupt_enable());
  write_u16(out, interrupts.interrupt_flags());
  write_bool(out, interrupts.master_enabled());
}

void write_timers_state(std::vector<std::uint8_t>& out,
                        const Timers::State& timers) {
  write_u64(out, timers.cycle_counter);
  for (const Timers::TimerState& timer : timers.timers) {
    write_u16(out, timer.counter);
    write_u16(out, timer.reload);
    write_u16(out, timer.control);
    write_u64(out, timer.overflow_count);
    write_u32(out, timer.enable_delay_cycles);
    write_u16(out, timer.last_enable_phase);
    write_bool(out, timer.just_enabled);
  }
}

void write_dma_state(std::vector<std::uint8_t>& out,
                     const DmaController::State& dma) {
  for (const DmaController::DmaChannelState& channel : dma.channels) {
    write_u32(out, channel.source);
    write_u32(out, channel.destination);
    write_u16(out, channel.word_count);
    write_u16(out, channel.control);
    write_u32(out, channel.current_source);
    write_u32(out, channel.current_destination);
    write_u32(out, channel.current_count);
    write_u32(out, channel.data_latch);
  }
  write_bool(out, dma.immediate_pending);
}

void write_ppu_state(std::vector<std::uint8_t>& out, const PpuTiming::State& ppu) {
  write_u16(out, ppu.line);
  write_u16(out, ppu.line_cycle);
  write_u16(out, ppu.dispstat_control);
  for (const std::uint16_t value : ppu.lcd_control) {
    write_u16(out, value);
  }
}

void write_apu_state(std::vector<std::uint8_t>& out, const Apu::State& apu) {
  write_u16(out, apu.soundcnt_l);
  write_u16(out, apu.soundcnt_h);
  write_u16(out, apu.soundcnt_x_status);
  write_u16(out, apu.soundbias);
  for (const auto& bank : apu.wave_ram_banks) {
    for (const std::uint16_t halfword : bank) {
      write_u16(out, halfword);
    }
  }
  write_bool(out, apu.wave_bank_select);
  for (const Apu::FifoState& fifo : apu.fifos) {
    for (const std::int8_t sample : fifo.samples) {
      write_i8(out, sample);
    }
    write_u32(out, static_cast<std::uint32_t>(fifo.head));
    write_u32(out, static_cast<std::uint32_t>(fifo.size));
  }
  for (const std::int8_t sample : apu.direct_sound_latched_samples) {
    write_i8(out, sample);
  }
  for (const Apu::SquareChannelState& square : apu.square_channels) {
    write_bool(out, square.enabled);
    write_u8(out, square.duty);
    write_u8(out, square.volume);
    write_u16(out, square.period_samples);
    write_u16(out, square.phase);
  }
  write_bool(out, apu.wave_channel.enabled);
  write_u8(out, apu.wave_channel.volume_shift);
  write_u16(out, apu.wave_channel.period_samples);
  write_u16(out, apu.wave_channel.phase);
  write_bool(out, apu.noise_channel.enabled);
  write_u8(out, apu.noise_channel.volume);
  write_u16(out, apu.noise_channel.period_samples);
  write_u16(out, apu.noise_channel.phase);
  write_u16(out, apu.noise_channel.lfsr);
  write_bool(out, apu.noise_channel.narrow_lfsr);
  for (const ApuMixedSample& sample : apu.audio_buffer_samples) {
    write_i16(out, sample.left);
    write_i16(out, sample.right);
  }
  write_u32(out, static_cast<std::uint32_t>(apu.audio_buffer_head));
  write_u32(out, static_cast<std::uint32_t>(apu.audio_buffer_size));
  write_u64(out, apu.frame_step_count);
  write_u64(out, apu.audio_sample_count);
  write_u32(out, apu.frame_cycle_remainder);
  write_u32(out, apu.audio_cycle_remainder);
  write_u8(out, apu.frame_step);
  write_i16(out, apu.last_mixed_sample.left);
  write_i16(out, apu.last_mixed_sample.right);
}

void write_scheduler_state(std::vector<std::uint8_t>& out,
                           const CoreSchedulerState& scheduler) {
  write_u64(out, scheduler.scheduler_cycles);
  write_bool(out, scheduler.halted);
  write_optional_u32(out, scheduler.last_fetch_address);
  write_optional_u8(out, scheduler.last_fetch_width_bytes);
  write_optional_u8(out, scheduler.last_fetch_window);
  write_u8(out, scheduler.prefetch_buffer_halfwords);
  write_bool(out, scheduler.suppress_next_game_pak_prefetch);
  write_bool(out, scheduler.recover_next_game_pak_data_fetch);
  write_bool(out, scheduler.suppress_next_thumb_prefetch_execute_bubble);
  write_bool(out, scheduler.previous_thumb_internal_load);
  write_optional_u16(out, scheduler.last_waitcnt_control);
  write_optional_u32(out, scheduler.hle_irq_return_lr);
  write_bool(out, scheduler.hle_irq_saved_registers.has_value());
  if (scheduler.hle_irq_saved_registers.has_value()) {
    for (const std::uint32_t value : *scheduler.hle_irq_saved_registers) {
      write_u32(out, value);
    }
  }
  write_bool(out, scheduler.hle_irq_reentry_dispatch_pending);
  write_bool(out, scheduler.hle_irq_return_latency_pending);
  write_bool(out, scheduler.hle_irq_post_return_latency_armed);
  write_bool(out, scheduler.hle_irq_post_return_dispatch_pending);
  write_bool(out, scheduler.hle_irq_chained_post_return_dispatch_pending);
  write_bool(out, scheduler.hle_irq_chained_post_return_data_dispatch_pending);
  write_bool(out,
             scheduler.hle_irq_chained_post_return_spaced_data_dispatch_pending);
  write_bool(out, scheduler.hle_irq_long_timer_chained_return_pending);
  write_bool(out, scheduler.hle_irq_slow_timer0_return_pending);
  write_bool(out, scheduler.hle_irq_post_return_chain_active);
  write_u8(out, scheduler.hle_irq_chained_spaced_data_service_count);
  write_bool(out, scheduler.auto_irq_line_high);
  write_u8(out, scheduler.auto_irq_latency_cycles);
  write_u32(out, scheduler.timer_io_access_gap_cycles);
  write_u32(out, scheduler.thumb_misfetch_recovery_count);
}

void write_io_state(std::vector<std::uint8_t>& out, const IoRegistersState& io) {
  for (const std::uint16_t value : io.serial) {
    write_u16(out, value);
  }
  write_bool(out, io.sio_transfer_active);
  write_u32(out, io.sio_transfer_cycles_remaining);
}

class Reader {
 public:
  explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

  [[nodiscard]] bool read_u8(std::uint8_t& value) {
    if (offset_ >= bytes_.size()) {
      return false;
    }
    value = bytes_.at(offset_++);
    return true;
  }

  [[nodiscard]] bool read_bool(bool& value) {
    std::uint8_t raw = 0;
    if (!read_u8(raw) || raw > 1U) {
      return false;
    }
    value = raw != 0;
    return true;
  }

  [[nodiscard]] bool read_u16(std::uint16_t& value) {
    std::uint8_t low = 0;
    std::uint8_t high = 0;
    if (!read_u8(low) || !read_u8(high)) {
      return false;
    }
    value = static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(high) << 8U));
    return true;
  }

  [[nodiscard]] bool read_i16(std::int16_t& value) {
    std::uint16_t raw = 0;
    if (!read_u16(raw)) {
      return false;
    }
    value = static_cast<std::int16_t>(raw);
    return true;
  }

  [[nodiscard]] bool read_i8(std::int8_t& value) {
    std::uint8_t raw = 0;
    if (!read_u8(raw)) {
      return false;
    }
    value = static_cast<std::int8_t>(raw);
    return true;
  }

  [[nodiscard]] bool read_u32(std::uint32_t& value) {
    value = 0;
    for (std::uint8_t byte = 0; byte < 4; ++byte) {
      std::uint8_t next = 0;
      if (!read_u8(next)) {
        return false;
      }
      value |= static_cast<std::uint32_t>(next) << (byte * 8U);
    }
    return true;
  }

  [[nodiscard]] bool read_u64(std::uint64_t& value) {
    value = 0;
    for (std::uint8_t byte = 0; byte < 8; ++byte) {
      std::uint8_t next = 0;
      if (!read_u8(next)) {
        return false;
      }
      value |= static_cast<std::uint64_t>(next) << (byte * 8U);
    }
    return true;
  }

  [[nodiscard]] bool read_fixed(std::uint8_t* dest, std::size_t size) {
    if (bytes_.size() - offset_ < size) {
      return false;
    }
    for (std::size_t index = 0; index < size; ++index) {
      dest[index] = bytes_.at(offset_++);
    }
    return true;
  }

  // Length-prefixed blob with an explicit capacity bound.
  [[nodiscard]] bool read_blob(std::vector<std::uint8_t>& out,
                               std::uint32_t max_size) {
    std::uint32_t size = 0;
    if (!read_u32(size) || size > max_size ||
        bytes_.size() - offset_ < size) {
      return false;
    }
    out.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
               bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + size));
    offset_ += size;
    return true;
  }

  [[nodiscard]] bool read_optional_u8(std::optional<std::uint8_t>& value) {
    bool present = false;
    std::uint8_t payload = 0;
    if (!read_bool(present)) {
      return false;
    }
    if (present && !read_u8(payload)) {
      return false;
    }
    value = present ? std::optional<std::uint8_t>(payload) : std::nullopt;
    return true;
  }

  [[nodiscard]] bool read_optional_u16(std::optional<std::uint16_t>& value) {
    bool present = false;
    std::uint16_t payload = 0;
    if (!read_bool(present)) {
      return false;
    }
    if (present && !read_u16(payload)) {
      return false;
    }
    value = present ? std::optional<std::uint16_t>(payload) : std::nullopt;
    return true;
  }

  [[nodiscard]] bool read_optional_u32(std::optional<std::uint32_t>& value) {
    bool present = false;
    std::uint32_t payload = 0;
    if (!read_bool(present)) {
      return false;
    }
    if (present && !read_u32(payload)) {
      return false;
    }
    value = present ? std::optional<std::uint32_t>(payload) : std::nullopt;
    return true;
  }

  [[nodiscard]] bool at_end() const {
    return offset_ == bytes_.size();
  }

 private:
  const std::vector<std::uint8_t>& bytes_;
  std::size_t offset_ = 0;
};

bool parse_cpu_state(Reader& reader, Arm7tdmi::State& cpu) {
  if (!reader.read_u64(cpu.elapsed_cycles)) {
    return false;
  }
  for (std::uint32_t& register_value : cpu.registers) {
    if (!reader.read_u32(register_value)) {
      return false;
    }
  }
  if (!reader.read_bool(cpu.negative) || !reader.read_bool(cpu.zero) ||
      !reader.read_bool(cpu.carry) || !reader.read_bool(cpu.overflow) ||
      !reader.read_bool(cpu.irq_disabled) ||
      !reader.read_bool(cpu.fiq_disabled) ||
      !reader.read_bool(cpu.thumb_state)) {
    return false;
  }
  std::uint8_t mode = 0;
  if (!reader.read_u8(mode)) {
    return false;
  }
  // CpuMode validity is enforced by Arm7tdmi::load_state's own policy.
  cpu.mode = static_cast<CpuMode>(mode);
  for (std::uint32_t& value : cpu.shared_r8_r12) {
    if (!reader.read_u32(value)) {
      return false;
    }
  }
  for (std::uint32_t& value : cpu.fiq_r8_r12) {
    if (!reader.read_u32(value)) {
      return false;
    }
  }
  if (!reader.read_u32(cpu.user_sp) || !reader.read_u32(cpu.user_lr) ||
      !reader.read_u32(cpu.fiq_sp) || !reader.read_u32(cpu.fiq_lr) ||
      !reader.read_u32(cpu.irq_sp) || !reader.read_u32(cpu.irq_lr) ||
      !reader.read_u32(cpu.supervisor_sp) ||
      !reader.read_u32(cpu.supervisor_lr) || !reader.read_u32(cpu.abort_sp) ||
      !reader.read_u32(cpu.abort_lr) || !reader.read_u32(cpu.undefined_sp) ||
      !reader.read_u32(cpu.undefined_lr) || !reader.read_u32(cpu.fiq_spsr) ||
      !reader.read_u32(cpu.supervisor_spsr) ||
      !reader.read_u32(cpu.abort_spsr) || !reader.read_u32(cpu.irq_spsr) ||
      !reader.read_u32(cpu.undefined_spsr)) {
    return false;
  }
  return true;
}

bool parse_memory_state(Reader& reader, MemoryBus::State& memory) {
  if (!reader.read_fixed(memory.ewram.data(), memory.ewram.size()) ||
      !reader.read_fixed(memory.iwram.data(), memory.iwram.size()) ||
      !reader.read_fixed(memory.palette.data(), memory.palette.size()) ||
      !reader.read_fixed(memory.vram.data(), memory.vram.size()) ||
      !reader.read_fixed(memory.oam.data(), memory.oam.size())) {
    return false;
  }
  if (!reader.read_blob(memory.game_pak_rom, MemoryBus::kGamePakRomWindowSize) ||
      !reader.read_blob(memory.game_pak_save,
                        MemoryBus::kGamePakSaveWindowSize)) {
    return false;
  }
  std::uint8_t save_type = 0;
  std::uint8_t flash_command_state = 0;
  std::uint8_t flash_bank = 0;
  if (!reader.read_u8(save_type) || !reader.read_u8(flash_command_state) ||
      !reader.read_bool(memory.flash_id_mode) || !reader.read_u8(flash_bank) ||
      !reader.read_u32(memory.open_bus_latch) ||
      !reader.read_bool(memory.open_bus_latch_valid) ||
      !reader.read_fixed(memory.mgba_debug_string.data(),
                         memory.mgba_debug_string.size())) {
    return false;
  }
  if (save_type > static_cast<std::uint8_t>(GamePakSaveType::eeprom8k) ||
      flash_command_state >
          static_cast<std::uint8_t>(FlashCommandState::bank_select) ||
      flash_bank > 1U) {
    return false;
  }
  memory.game_pak_save_type = static_cast<GamePakSaveType>(save_type);
  memory.flash_command_state = static_cast<FlashCommandState>(flash_command_state);
  memory.flash_bank = flash_bank;
  std::vector<std::uint8_t> debug_output;
  if (!reader.read_blob(debug_output, 0xFFFFFFFFU)) {
    return false;
  }
  memory.debug_output.assign(debug_output.begin(), debug_output.end());
  return true;
}

bool parse_interrupt_state(Reader& reader, std::uint16_t& interrupt_enable,
                           std::uint16_t& interrupt_flags, bool& master_enabled) {
  if (!reader.read_u16(interrupt_enable) ||
      !reader.read_u16(interrupt_flags) || !reader.read_bool(master_enabled)) {
    return false;
  }
  return (interrupt_enable & ~InterruptController::kSupportedMask) == 0 &&
         (interrupt_flags & ~InterruptController::kSupportedMask) == 0;
}

bool parse_timers_state(Reader& reader, Timers::State& timers) {
  if (!reader.read_u64(timers.cycle_counter)) {
    return false;
  }
  for (Timers::TimerState& timer : timers.timers) {
    if (!reader.read_u16(timer.counter) || !reader.read_u16(timer.reload) ||
        !reader.read_u16(timer.control) ||
        !reader.read_u64(timer.overflow_count) ||
        !reader.read_u32(timer.enable_delay_cycles) ||
        !reader.read_u16(timer.last_enable_phase) ||
        !reader.read_bool(timer.just_enabled)) {
      return false;
    }
  }
  return true;
}

bool parse_dma_state(Reader& reader, DmaController::State& dma) {
  for (DmaController::DmaChannelState& channel : dma.channels) {
    if (!reader.read_u32(channel.source) ||
        !reader.read_u32(channel.destination) ||
        !reader.read_u16(channel.word_count) ||
        !reader.read_u16(channel.control) ||
        !reader.read_u32(channel.current_source) ||
        !reader.read_u32(channel.current_destination) ||
        !reader.read_u32(channel.current_count) ||
        !reader.read_u32(channel.data_latch)) {
      return false;
    }
  }
  return reader.read_bool(dma.immediate_pending);
}

bool parse_ppu_state(Reader& reader, PpuTiming::State& ppu) {
  if (!reader.read_u16(ppu.line) || !reader.read_u16(ppu.line_cycle) ||
      !reader.read_u16(ppu.dispstat_control)) {
    return false;
  }
  for (std::uint16_t& value : ppu.lcd_control) {
    if (!reader.read_u16(value)) {
      return false;
    }
  }
  // Scanline position invariants maintained by tick(); garbage here would
  // wedge IRQ/DMA scheduling.
  return ppu.line < PpuTiming::kTotalLines &&
         ppu.line_cycle < PpuTiming::kCyclesPerLine;
}

bool parse_apu_state(Reader& reader, Apu::State& apu) {
  if (!reader.read_u16(apu.soundcnt_l) || !reader.read_u16(apu.soundcnt_h) ||
      !reader.read_u16(apu.soundcnt_x_status) ||
      !reader.read_u16(apu.soundbias)) {
    return false;
  }
  for (auto& bank : apu.wave_ram_banks) {
    for (std::uint16_t& halfword : bank) {
      if (!reader.read_u16(halfword)) {
        return false;
      }
    }
  }
  if (!reader.read_bool(apu.wave_bank_select)) {
    return false;
  }
  for (Apu::FifoState& fifo : apu.fifos) {
    for (std::int8_t& sample : fifo.samples) {
      if (!reader.read_i8(sample)) {
        return false;
      }
    }
    std::uint32_t head = 0;
    std::uint32_t size = 0;
    if (!reader.read_u32(head) || !reader.read_u32(size)) {
      return false;
    }
    fifo.head = head;
    fifo.size = size;
  }
  for (std::int8_t& sample : apu.direct_sound_latched_samples) {
    if (!reader.read_i8(sample)) {
      return false;
    }
  }
  for (Apu::SquareChannelState& square : apu.square_channels) {
    if (!reader.read_bool(square.enabled) || !reader.read_u8(square.duty) ||
        !reader.read_u8(square.volume) ||
        !reader.read_u16(square.period_samples) ||
        !reader.read_u16(square.phase)) {
      return false;
    }
    // Duty indexes kSquareDutyHighSamples; garbage would throw post-restore.
    if (square.duty > 3U) {
      return false;
    }
  }
  if (!reader.read_bool(apu.wave_channel.enabled) ||
      !reader.read_u8(apu.wave_channel.volume_shift) ||
      !reader.read_u16(apu.wave_channel.period_samples) ||
      !reader.read_u16(apu.wave_channel.phase)) {
    return false;
  }
  // volume_shift feeds a variable shift; > 4 is unreachable via configure.
  if (apu.wave_channel.volume_shift > 4U) {
    return false;
  }
  if (!reader.read_bool(apu.noise_channel.enabled) ||
      !reader.read_u8(apu.noise_channel.volume) ||
      !reader.read_u16(apu.noise_channel.period_samples) ||
      !reader.read_u16(apu.noise_channel.phase) ||
      !reader.read_u16(apu.noise_channel.lfsr) ||
      !reader.read_bool(apu.noise_channel.narrow_lfsr)) {
    return false;
  }
  for (ApuMixedSample& sample : apu.audio_buffer_samples) {
    if (!reader.read_i16(sample.left) || !reader.read_i16(sample.right)) {
      return false;
    }
  }
  std::uint32_t buffer_head = 0;
  std::uint32_t buffer_size = 0;
  if (!reader.read_u32(buffer_head) || !reader.read_u32(buffer_size) ||
      !reader.read_u64(apu.frame_step_count) ||
      !reader.read_u64(apu.audio_sample_count) ||
      !reader.read_u32(apu.frame_cycle_remainder) ||
      !reader.read_u32(apu.audio_cycle_remainder) ||
      !reader.read_u8(apu.frame_step) ||
      !reader.read_i16(apu.last_mixed_sample.left) ||
      !reader.read_i16(apu.last_mixed_sample.right)) {
    return false;
  }
  apu.audio_buffer_head = buffer_head;
  apu.audio_buffer_size = buffer_size;
  return apu.frame_step < Apu::kFrameSequencerSteps;
}

bool parse_scheduler_state(Reader& reader, CoreSchedulerState& scheduler) {
  if (!reader.read_u64(scheduler.scheduler_cycles) ||
      !reader.read_bool(scheduler.halted) ||
      !reader.read_optional_u32(scheduler.last_fetch_address) ||
      !reader.read_optional_u8(scheduler.last_fetch_width_bytes) ||
      !reader.read_optional_u8(scheduler.last_fetch_window) ||
      !reader.read_u8(scheduler.prefetch_buffer_halfwords) ||
      !reader.read_bool(scheduler.suppress_next_game_pak_prefetch) ||
      !reader.read_bool(scheduler.recover_next_game_pak_data_fetch) ||
      !reader.read_bool(scheduler.suppress_next_thumb_prefetch_execute_bubble) ||
      !reader.read_bool(scheduler.previous_thumb_internal_load) ||
      !reader.read_optional_u16(scheduler.last_waitcnt_control) ||
      !reader.read_optional_u32(scheduler.hle_irq_return_lr)) {
    return false;
  }
  bool saved_registers_present = false;
  if (!reader.read_bool(saved_registers_present)) {
    return false;
  }
  if (saved_registers_present) {
    std::array<std::uint32_t, 13> saved{};
    for (std::uint32_t& value : saved) {
      if (!reader.read_u32(value)) {
        return false;
      }
    }
    scheduler.hle_irq_saved_registers = saved;
  } else {
    scheduler.hle_irq_saved_registers = std::nullopt;
  }
  if (!reader.read_bool(scheduler.hle_irq_reentry_dispatch_pending) ||
      !reader.read_bool(scheduler.hle_irq_return_latency_pending) ||
      !reader.read_bool(scheduler.hle_irq_post_return_latency_armed) ||
      !reader.read_bool(scheduler.hle_irq_post_return_dispatch_pending) ||
      !reader.read_bool(scheduler.hle_irq_chained_post_return_dispatch_pending) ||
      !reader.read_bool(
          scheduler.hle_irq_chained_post_return_data_dispatch_pending) ||
      !reader.read_bool(
          scheduler.hle_irq_chained_post_return_spaced_data_dispatch_pending) ||
      !reader.read_bool(scheduler.hle_irq_long_timer_chained_return_pending) ||
      !reader.read_bool(scheduler.hle_irq_slow_timer0_return_pending) ||
      !reader.read_bool(scheduler.hle_irq_post_return_chain_active) ||
      !reader.read_u8(scheduler.hle_irq_chained_spaced_data_service_count) ||
      !reader.read_bool(scheduler.auto_irq_line_high) ||
      !reader.read_u8(scheduler.auto_irq_latency_cycles) ||
      !reader.read_u32(scheduler.timer_io_access_gap_cycles) ||
      !reader.read_u32(scheduler.thumb_misfetch_recovery_count)) {
    return false;
  }
  return true;
}

bool parse_io_state(Reader& reader, IoRegistersState& io) {
  for (std::uint16_t& value : io.serial) {
    if (!reader.read_u16(value)) {
      return false;
    }
  }
  return reader.read_bool(io.sio_transfer_active) &&
         reader.read_u32(io.sio_transfer_cycles_remaining);
}

// Mirrors CoreSession::state_hash() exactly, computed over a scratch
// snapshot so a mismatch can reject BEFORE the target session is mutated.
[[nodiscard]] std::uint64_t snapshot_state_hash(CoreSessionState& snapshot) {
  IoRegisters io(snapshot.interrupts, snapshot.timers, snapshot.dma,
                 snapshot.ppu, snapshot.apu, snapshot.waitcnt, snapshot.keypad);
  io.load_state(snapshot.io);
  // CoreSchedulerState is plain data; the scheduler hash lives on the
  // CoreScheduler facade.
  CoreScheduler scheduler(snapshot.cpu, snapshot.memory, snapshot.interrupts,
                          snapshot.timers, snapshot.dma, snapshot.ppu,
                          snapshot.apu);
  scheduler.load_state(snapshot.scheduler);

  StateHasher hasher;
  hasher.add_u64(snapshot.cpu.state_hash());
  hasher.add_u64(snapshot.memory.state_hash());
  hasher.add_u64(snapshot.interrupts.state_hash());
  hasher.add_u64(snapshot.timers.state_hash());
  hasher.add_u64(snapshot.dma.state_hash());
  hasher.add_u64(snapshot.ppu.state_hash());
  hasher.add_u64(snapshot.apu.state_hash());
  hasher.add_u8(static_cast<std::uint8_t>(snapshot.bios.mode()));
  hasher.add_u64(snapshot.keypad.state_hash());
  hasher.add_u64(snapshot.waitcnt.state_hash());
  hasher.add_u64(io.state_hash());
  hasher.add_u64(scheduler.state_hash());
  return hasher.value();
}

}  // namespace

std::vector<std::uint8_t> SaveStateCodec::encode(const CoreSession& session) {
  // Const whole-machine view; the dma/ppu/apu accessors lack const overloads.
  const CoreSessionState machine = session.save_state();
  const Arm7tdmi::State cpu = machine.cpu.save_state();
  const MemoryBus::State memory = machine.memory.save_state();
  const Timers::State timers = machine.timers.save_state();
  const DmaController::State dma = machine.dma.save_state();
  const PpuTiming::State ppu = machine.ppu.save_state();
  const Apu::State apu = machine.apu.save_state();
  const IoRegistersState io = machine.io;

  std::vector<std::uint8_t> out;
  out.reserve(sizeof(std::uint32_t) * 3 + memory.ewram.size() +
              memory.iwram.size() + memory.palette.size() + memory.vram.size() +
              memory.oam.size() + memory.mgba_debug_string.size() +
              memory.game_pak_rom.size() + memory.game_pak_save.size());

  write_u32(out, kMagic);
  write_u32(out, kVersion);
  write_u64(out, session.state_hash());

  write_cpu_state(out, cpu);
  write_memory_state(out, memory);
  write_interrupt_state(out, machine.interrupts);
  write_timers_state(out, timers);
  write_dma_state(out, dma);
  write_ppu_state(out, ppu);
  write_apu_state(out, apu);
  write_u16(out, machine.keypad.pressed_mask());
  write_u16(out, machine.keypad.keycnt());
  write_u16(out, machine.waitcnt.read_control());
  write_u8(out, static_cast<std::uint8_t>(machine.bios.mode()));
  write_io_state(out, io);
  write_scheduler_state(out, machine.scheduler);
  return out;
}

SaveStateDecodeResult SaveStateCodec::decode_into(
    CoreSession& session, const std::vector<std::uint8_t>& bytes) {
  constexpr std::size_t kHeaderSize = 16;  // magic + version + state_hash.
  if (bytes.size() < kHeaderSize) {
    return {SaveStateDecodeStatus::too_small, 0, 0};
  }

  Reader reader(bytes);
  std::uint32_t magic = 0;
  std::uint32_t version = 0;
  std::uint64_t encoded_hash = 0;
  if (!reader.read_u32(magic) || magic != kMagic) {
    return {SaveStateDecodeStatus::bad_magic, version, encoded_hash};
  }
  if (!reader.read_u32(version)) {
    return {SaveStateDecodeStatus::corrupt_payload, version, encoded_hash};
  }
  if (version != kVersion) {
    // Includes every pre-v3 layout (e.g. v2 partial snapshots).
    return {SaveStateDecodeStatus::unsupported_version, version, encoded_hash};
  }
  if (!reader.read_u64(encoded_hash)) {
    return {SaveStateDecodeStatus::corrupt_payload, version, encoded_hash};
  }

  // Phase 1: parse and structurally validate everything into locals. The
  // target session is not touched on any rejection below.
  Arm7tdmi::State cpu{};
  MemoryBus::State memory{};
  std::uint16_t interrupt_enable = 0;
  std::uint16_t interrupt_flags = 0;
  bool master_enabled = false;
  Timers::State timers{};
  DmaController::State dma{};
  PpuTiming::State ppu{};
  Apu::State apu{};
  std::uint16_t pressed_mask = 0;
  std::uint16_t keycnt = 0;
  std::uint16_t waitcnt = 0;
  std::uint8_t bios_mode = 0;
  IoRegistersState io{};
  CoreSchedulerState scheduler{};
  if (!parse_cpu_state(reader, cpu) || !parse_memory_state(reader, memory) ||
      !parse_interrupt_state(reader, interrupt_enable, interrupt_flags,
                             master_enabled) ||
      !parse_timers_state(reader, timers) || !parse_dma_state(reader, dma) ||
      !parse_ppu_state(reader, ppu) || !parse_apu_state(reader, apu) ||
      !reader.read_u16(pressed_mask) || !reader.read_u16(keycnt) ||
      !reader.read_u16(waitcnt) || !reader.read_u8(bios_mode) ||
      bios_mode > static_cast<std::uint8_t>(BiosExecutionMode::hle) ||
      !parse_io_state(reader, io) ||
      !parse_scheduler_state(reader, scheduler) || !reader.at_end()) {
    return {SaveStateDecodeStatus::corrupt_payload, version, encoded_hash};
  }

  // Phase 2: build a scratch machine through each component loader's own
  // validation policy. Any rejection leaves `session` untouched.
  auto next = std::make_unique<CoreSessionState>();
  if (!next->cpu.load_state(cpu) || !next->memory.load_state(memory) ||
      !next->timers.load_state(timers) || !next->dma.load_state(dma) ||
      !next->ppu.load_state(ppu) || !next->apu.load_state(apu) ||
      !next->keypad.set_pressed_mask(pressed_mask)) {
    return {SaveStateDecodeStatus::restore_rejected, version, encoded_hash};
  }
  next->interrupts.write_interrupt_enable(interrupt_enable);
  for (std::uint8_t bit = 0; bit < 14U; ++bit) {
    const std::uint16_t mask = static_cast<std::uint16_t>(1U << bit);
    if ((interrupt_flags & mask) != 0) {
      next->interrupts.request(static_cast<InterruptSource>(bit));
    }
  }
  next->interrupts.set_master_enabled(master_enabled);
  next->keypad.write_keycnt(keycnt);
  next->waitcnt.write_control(waitcnt);
  next->bios.set_mode(static_cast<BiosExecutionMode>(bios_mode));
  next->io = io;
  next->scheduler = scheduler;

  // Phase 2b: integrity gate BEFORE commit so a mismatch leaves the target
  // session provably unmutated.
  if (snapshot_state_hash(*next) != encoded_hash) {
    return {SaveStateDecodeStatus::state_hash_mismatch, version, encoded_hash};
  }

  // Phase 3: single commit. Cannot fail.
  session.load_state(*next);
  return {SaveStateDecodeStatus::ok, version, encoded_hash};
}

}  // namespace gba::core
