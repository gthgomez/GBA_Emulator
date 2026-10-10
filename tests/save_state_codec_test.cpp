#include "gba/core/core_session.hpp"
#include "gba/core/dma_controller.hpp"
#include "gba/core/io_registers.hpp"
#include "gba/core/keypad.hpp"
#include "gba/core/memory_bus.hpp"
#include "gba/core/save_state_codec.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "test_helpers.hpp"

int main() {
  using gba::core::Arm7tdmi;
  using gba::core::CoreRunStopReason;
  using gba::core::CoreSession;
  using gba::core::DirectSoundChannel;
  using gba::core::GamePakSaveType;
  using gba::core::InterruptSource;
  using gba::core::IoRegisters;
  using gba::core::MemoryBus;
  using gba::core::SaveStateCodec;
  using gba::core::SaveStateDecodeStatus;

  constexpr std::uint32_t kAddR0R0Imm1 = 0xE2800001U;
  constexpr std::uint32_t kBranchBackOneInstruction = 0xEAFFFFFDU;

  // v3 wire-layout anchors for the surgical corruption tests below.
  // Header: magic u32 | version u32 | state_hash u64. CPU section: elapsed
  // u64 | 16 registers | 7 flag bytes | mode | R8-R12 banks | SP/LR | SPSRs.
  constexpr std::size_t kHeaderBytes = 16;
  constexpr std::size_t kCpuSectionBytes =
      8 + Arm7tdmi::kRegisterCount * 4 + 7 + 1 + 10 * 4 + 12 * 4 + 5 * 4;
  const std::size_t kMemoryBase = kHeaderBytes + kCpuSectionBytes;
  const std::size_t kRamBytes =
      MemoryBus::kEwramSize + MemoryBus::kIwramSize + MemoryBus::kPaletteSize +
      MemoryBus::kVramSize + MemoryBus::kOamSize;
  const std::size_t kRomLenOffset = kMemoryBase + kRamBytes;
  const std::size_t kCpuFlagBase =
      kHeaderBytes + 8 + Arm7tdmi::kRegisterCount * 4;

  auto source = std::make_unique<CoreSession>();
  std::vector<std::uint8_t> rom(8);
  write_word(rom, 0, kAddR0R0Imm1);
  write_word(rom, 4, kBranchBackOneInstruction);
  expect(source->memory().load_game_pak_rom(rom), "codec test loads ROM");
  expect(source->memory().configure_game_pak_save(GamePakSaveType::sram32k),
         "codec test configures save");
  expect(source->memory().write8(0x0E000010, 0x5A), "codec test seeds save");
  expect(source->memory().write32(0x02000000, 0x11223344U), "seed EWRAM");
  expect(source->memory().write8(0x03007FF0, 0x99), "seed IWRAM");
  expect(source->memory().write16(0x05000000, 0x1234), "seed palette");
  expect(source->memory().write16(0x06000000, 0x7FFF), "seed VRAM");
  expect(source->memory().write32(0x07000000, 0xDEADBEEFU), "seed OAM");
  expect(source->keypad().set_pressed_mask(0x0001), "codec test seeds keypad");
  source->keypad().write_keycnt(0x4001);
  source->cpu().set_register(Arm7tdmi::kPc, 0x08000000U);

  // Run into mid-execution so scheduler prefetch/HLE state, PPU scanline,
  // APU counters, and timer phases are nontrivial before the snapshot.
  expect(source->run(12).stop_reason == CoreRunStopReason::max_steps,
         "pre-snapshot run completes");

  source->timers().tick(179, source->interrupts());
  source->timers().write_reload(0, 0xFFEE);
  source->timers().write_control(0, 0x00C3);
  source->timers().defer_newly_enabled_ticks();

  source->dma().write_source(0, 0x02000100U);
  source->dma().write_destination(0, 0x02000200U);
  source->dma().write_word_count(0, 0x0004);
  source->dma().write_control(0, 0x8000);  // Enable + immediate start timing.
  expect(source->dma().immediate_pending(), "immediate DMA armed pre-snapshot");

  source->ppu().tick(1234, source->interrupts());

  source->apu().write_soundcnt_x(0x80);
  source->apu().write_fifo(DirectSoundChannel::a, 0x11223344U);
  source->apu().write_wave_ram(3, 0x7FFF);
  source->apu().write_soundbias(0x0300);

  source->interrupts().write_interrupt_enable(0x0008);
  source->interrupts().request(gba::core::InterruptSource::timer0);
  source->interrupts().write_ime(1);

  (void)source->io().write16(IoRegisters::kDispcnt, 0x0040);
  (void)source->io().write16(IoRegisters::kDispstat, 0x0020);
  (void)source->io().write16(IoRegisters::kVcount, 0x0002);
  (void)source->io().write16(0x04000128U, 0x0001);  // SIOCNT, no start bit.
  source->memory().drive_open_bus(0xA5A5F00DU);

  const std::vector<std::uint8_t> encoded = SaveStateCodec::encode(*source);

  // Encoded-size growth sanity: the fixed RAM regions alone dominate the
  // payload, proving complete-machine capture (vs. the v2 partial dump).
  expect(encoded.size() >= kMemoryBase + kRamBytes,
         "encoded blob carries every RAM region");

  const std::uint64_t snapshot_hash = source->state_hash();
  const bool src_dma_pending = source->dma().immediate_pending();
  const std::size_t src_fifo_a_samples =
      source->apu().fifo_size(DirectSoundChannel::a);
  const std::uint16_t src_ppu_line_cycle = source->ppu().line_cycle();
  const std::uint8_t src_vcount_setting = source->ppu().vcount_setting();
  const std::uint64_t src_scheduler_cycles = source->scheduler().scheduler_cycles();

  // Continued-execution reference trace on the source machine.
  (void)source->run(24);
  const std::uint64_t trace_hash_step_24 = source->state_hash();
  (void)source->run(24);
  const std::uint64_t trace_hash_step_48 = source->state_hash();

  auto restored = std::make_unique<CoreSession>();
  const gba::core::SaveStateDecodeResult decoded =
      SaveStateCodec::decode_into(*restored, encoded);
  expect(decoded.status == SaveStateDecodeStatus::ok, "save-state decodes");
  expect(decoded.version == SaveStateCodec::kVersion, "save-state version reports");
  expect(decoded.encoded_state_hash == snapshot_hash, "encoded hash echoes snapshot");
  expect(restored->state_hash() == snapshot_hash, "restored hash equals snapshot");
  expect(restored->memory().game_pak_rom_size() == rom.size(), "ROM bytes restore");
  expect(restored->memory().game_pak_save_type() == GamePakSaveType::sram32k,
         "save type restores");
  expect(restored->keypad().pressed_mask() == 0x0001, "keypad mask restores");
  expect(restored->keypad().keycnt() == 0x4001, "KEYCNT restores");
  expect(restored->interrupts().irq_line(),
         "interrupt save-state restores IE IF and IME");
  expect(restored->dma().immediate_pending() == src_dma_pending,
         "DMA latches restore");
  expect(restored->apu().fifo_size(DirectSoundChannel::a) == src_fifo_a_samples,
         "APU FIFO restores");
  expect(restored->ppu().line_cycle() == src_ppu_line_cycle &&
             restored->ppu().vcount_setting() == src_vcount_setting,
         "PPU timing restores");
  expect(restored->scheduler().scheduler_cycles() == src_scheduler_cycles,
         "scheduler cycles restore");
  expect(restored->memory().open_bus_latch().value_or(0) == 0xA5A5F00DU,
         "open-bus latch restores");

  // Continued-execution traces must match the source path exactly.
  (void)restored->run(24);
  expect(restored->state_hash() == trace_hash_step_24,
         "restored run matches source trace after 24 steps");
  (void)restored->run(24);
  expect(restored->state_hash() == trace_hash_step_48,
         "restored run matches source trace after 48 steps");

  // Timer prescaler phase survives the roundtrip.
  auto timer_session = std::make_unique<CoreSession>();
  timer_session->timers().tick(179, timer_session->interrupts());
  timer_session->timers().write_reload(0, 0xFFEE);
  timer_session->timers().write_control(0, 0x00C3);
  timer_session->timers().defer_newly_enabled_ticks();
  const std::vector<std::uint8_t> timer_encoded =
      SaveStateCodec::encode(*timer_session);
  auto timer_restored = std::make_unique<CoreSession>();
  expect(SaveStateCodec::decode_into(*timer_restored, timer_encoded).status ==
             SaveStateDecodeStatus::ok,
         "timer save-state decodes");
  timer_restored->timers().tick(1, timer_restored->interrupts());
  expect(timer_restored->timers().counter(0) == 0xFFEE,
         "timer save-state preserves deferred enable delay");
  timer_restored->timers().tick(843, timer_restored->interrupts());
  expect(timer_restored->timers().counter(0) == 0xFFEE,
         "timer save-state preserves prescaler phase before edge");
  timer_restored->timers().tick(1, timer_restored->interrupts());
  expect(timer_restored->timers().counter(0) == 0xFFEF,
         "timer save-state resumes from restored prescaler phase");

  // Rejection paths below prove the target session stays byte-identical.
  auto victim = std::make_unique<CoreSession>();
  expect(victim->keypad().set_pressed_mask(0x0002), "victim seeds keypad");

  std::vector<std::uint8_t> legacy = encoded;
  legacy.at(4) = 2;
  legacy.at(5) = 0;
  legacy.at(6) = 0;
  legacy.at(7) = 0;
  const std::uint64_t victim_hash = victim->state_hash();
  const gba::core::SaveStateDecodeResult legacy_result =
      SaveStateCodec::decode_into(*victim, legacy);
  expect(legacy_result.status == SaveStateDecodeStatus::unsupported_version,
         "crafted v2 blob rejected as unsupported_version");
  expect(legacy_result.version == 2, "rejected v2 blob reports its version");
  expect(victim->state_hash() == victim_hash && victim->keypad().pressed_mask() == 0x0002,
         "unsupported_version leaves session untouched");

  std::vector<std::uint8_t> ram_corrupted = encoded;
  ram_corrupted.at(kMemoryBase + 1000) ^= 0xFFU;
  const gba::core::SaveStateDecodeResult ram_result =
      SaveStateCodec::decode_into(*victim, ram_corrupted);
  expect(ram_result.status == SaveStateDecodeStatus::state_hash_mismatch,
         "interior RAM corruption detected via hash mismatch");
  expect(victim->state_hash() == victim_hash,
         "hash mismatch leaves session untouched");

  std::vector<std::uint8_t> flag_corrupted = encoded;
  flag_corrupted.at(kCpuFlagBase) = 2;  // Flag bytes are strictly boolean.
  const gba::core::SaveStateDecodeResult flag_result =
      SaveStateCodec::decode_into(*victim, flag_corrupted);
  expect(flag_result.status == SaveStateDecodeStatus::corrupt_payload,
         "non-boolean flag byte rejected structurally");
  expect(victim->state_hash() == victim_hash,
         "structural rejection leaves session untouched");

  std::vector<std::uint8_t> enum_corrupted = encoded;
  const std::size_t kSaveTypeOffset =
      kRomLenOffset + 4 + rom.size() + 4 + MemoryBus::kSram32kSize;
  expect(enum_corrupted.at(kSaveTypeOffset) ==
                 static_cast<std::uint8_t>(GamePakSaveType::sram32k),
         "save-type byte located where layout predicts");
  enum_corrupted.at(kSaveTypeOffset) = 6;
  const gba::core::SaveStateDecodeResult enum_result =
      SaveStateCodec::decode_into(*victim, enum_corrupted);
  expect(enum_result.status == SaveStateDecodeStatus::corrupt_payload,
         "out-of-range save-type byte rejected in validation phase");
  expect(victim->state_hash() == victim_hash,
         "enum validation rejection leaves session untouched");

  std::vector<std::uint8_t> truncated = encoded;
  truncated.pop_back();
  expect(SaveStateCodec::decode_into(*victim, truncated).status ==
             SaveStateDecodeStatus::corrupt_payload,
         "truncated tail rejects cleanly");
  std::vector<std::uint8_t> magic_corrupted = encoded;
  magic_corrupted.at(0) = 0;
  expect(SaveStateCodec::decode_into(*victim, magic_corrupted).status ==
             SaveStateDecodeStatus::bad_magic,
         "bad magic rejects");
  expect(SaveStateCodec::decode_into(*victim, {}).status ==
             SaveStateDecodeStatus::too_small,
         "empty blob rejects as too small");
  expect(victim->state_hash() == victim_hash,
         "all rejections leave the session untouched");

  // Minimal-session roundtrips: interrupt trio and BIOS mode.
  auto irq_session = std::make_unique<CoreSession>();
  irq_session->interrupts().write_interrupt_enable(0x0008);
  irq_session->interrupts().request(gba::core::InterruptSource::timer0);
  irq_session->interrupts().write_ime(1);
  const std::vector<std::uint8_t> irq_encoded =
      SaveStateCodec::encode(*irq_session);
  auto irq_restored = std::make_unique<CoreSession>();
  expect(SaveStateCodec::decode_into(*irq_restored, irq_encoded).status ==
             SaveStateDecodeStatus::ok,
         "interrupt save-state decodes");
  expect(irq_restored->state_hash() == irq_session->state_hash(),
         "interrupt save-state restores the encoded hash");
  expect(irq_restored->interrupts().irq_line(),
         "interrupt trio restores IE IF and IME");

  auto bios_session = std::make_unique<CoreSession>();
  bios_session->bios().set_mode(gba::core::BiosExecutionMode::hle);
  const std::vector<std::uint8_t> bios_encoded =
      SaveStateCodec::encode(*bios_session);
  auto bios_restored = std::make_unique<CoreSession>();
  expect(SaveStateCodec::decode_into(*bios_restored, bios_encoded).status ==
             SaveStateDecodeStatus::ok,
         "BIOS-mode save-state decodes");
  expect(bios_restored->bios().mode() == gba::core::BiosExecutionMode::hle,
         "BIOS-mode save-state restores HLE mode");
  expect(bios_restored->state_hash() == bios_session->state_hash(),
         "BIOS-mode save-state restores the encoded hash");

  // Every supported backup capacity must roundtrip, including banked storage
  // larger than the 64 KiB cartridge-save bus aperture.
  struct BackupCase {
    GamePakSaveType type;
    std::size_t size;
  };
  for (const auto& backup : {BackupCase{GamePakSaveType::none, 0},
                            BackupCase{GamePakSaveType::sram32k, 32768},
                            BackupCase{GamePakSaveType::flash64k, 65536},
                            BackupCase{GamePakSaveType::flash128k, 131072},
                            BackupCase{GamePakSaveType::eeprom512, 512},
                            BackupCase{GamePakSaveType::eeprom8k, 8192}}) {
    auto backup_source = std::make_unique<CoreSession>();
    expect(backup_source->memory().load_game_pak_rom(rom), "backup fixture loads ROM");
    expect(backup_source->memory().configure_game_pak_save(backup.type),
           "backup fixture configures protocol");
    std::vector<std::uint8_t> data(backup.size, 0xFF);
    if (!data.empty()) {
      data.front() = 0x12;
      data.back() = 0x34;
      expect(backup_source->memory().import_game_pak_save(backup.type, data),
             "backup fixture seeds boundary bytes");
    }
    auto backup_restored = std::make_unique<CoreSession>();
    expect(SaveStateCodec::decode_into(*backup_restored,
                                      SaveStateCodec::encode(*backup_source)).status ==
               SaveStateDecodeStatus::ok,
           "all backup capacities decode, including Flash128K");
    expect(backup_restored->memory().game_pak_save_type() == backup.type &&
               backup_restored->memory().export_game_pak_save() == data,
           "backup roundtrip preserves protocol and every byte");
    expect(backup_restored->state_hash() == backup_source->state_hash(),
           "backup roundtrip preserves machine hash");
  }

  auto flash_source = std::make_unique<CoreSession>();
  expect(flash_source->memory().load_game_pak_rom(rom), "Flash128K fixture loads ROM");
  std::vector<std::uint8_t> flash_data(131072, 0xFF);
  flash_data.at(0x10) = 0x9C;
  flash_data.at(65536 + 0x10) = 0x34;
  expect(flash_source->memory().import_game_pak_save(GamePakSaveType::flash128k, flash_data),
         "Flash128K fixture seeds distinct banks");
  const auto flash_command = [](MemoryBus& memory, std::uint8_t command) {
    expect(memory.write8(0x0E005555, 0xAA), "flash unlock byte 1");
    expect(memory.write8(0x0E002AAA, 0x55), "flash unlock byte 2");
    expect(memory.write8(0x0E005555, command), "flash command byte");
  };
  flash_command(flash_source->memory(), 0xB0);
  expect(flash_source->memory().write8(0x0E000000, 1), "select flash bank 1");
  flash_command(flash_source->memory(), 0x90);
  const auto flash_encoded = SaveStateCodec::encode(*flash_source);
  auto flash_restored = std::make_unique<CoreSession>();
  expect(SaveStateCodec::decode_into(*flash_restored, flash_encoded).status ==
             SaveStateDecodeStatus::ok,
         "Flash128K bank-1 ID-mode snapshot decodes");
  expect(flash_restored->state_hash() == flash_source->state_hash() &&
             flash_restored->memory().flash_protocol_status().bank == 1 &&
             flash_restored->memory().flash_protocol_status().id_mode,
         "Flash128K restores selected bank and ID mode");
  expect(flash_restored->memory().write8(0x0E000100, 0xF0), "restored flash exits ID mode");
  expect(flash_restored->memory().read8(0x0E000010).value_or(0) == 0x34,
         "restored bank 1 exposes its data");
  flash_command(flash_restored->memory(), 0xB0);
  expect(flash_restored->memory().write8(0x0E000000, 0), "restored flash selects bank 0");
  expect(flash_restored->memory().read8(0x0E000010).value_or(0) == 0x9C,
         "restored bank 0 exposes distinct data");

  expect(flash_source->memory().write8(0x0E000100, 0xF0), "source exits ID mode");
  flash_command(flash_source->memory(), 0xA0);
  expect(SaveStateCodec::decode_into(*flash_restored,
                                    SaveStateCodec::encode(*flash_source)).status ==
             SaveStateDecodeStatus::ok,
         "pending flash program command decodes");
  expect(flash_restored->memory().write8(0x0E000020, 0x56),
         "restored flash completes pending program command");
  expect(flash_source->memory().write8(0x0E000020, 0x56),
         "source completes pending program command");
  expect(flash_restored->state_hash() == flash_source->state_hash() &&
             flash_restored->memory().export_game_pak_save().at(65536 + 0x20) == 0x56,
         "pending program resumes deterministically in bank 1");

  const std::size_t flash_len_offset = kRomLenOffset + 4 + rom.size();
  const std::size_t flash_data_offset = flash_len_offset + 4;
  const auto reject_flash = [&](const std::vector<std::uint8_t>& blob,
                                SaveStateDecodeStatus status) {
    expect(SaveStateCodec::decode_into(*victim, blob).status == status,
           "malformed Flash128K snapshot is rejected");
    expect(victim->state_hash() == victim_hash,
           "Flash128K rejection leaves target untouched");
  };
  auto malformed_flash = flash_encoded;
  write_word(malformed_flash, flash_len_offset, 131073);
  reject_flash(malformed_flash, SaveStateDecodeStatus::corrupt_payload);
  malformed_flash = flash_encoded;
  malformed_flash.resize(flash_data_offset + 65536);
  reject_flash(malformed_flash, SaveStateDecodeStatus::corrupt_payload);
  malformed_flash = flash_encoded;
  // A complete 64 KiB blob falsely declaring Flash128K must reach and fail
  // protocol-size validation, rather than consuming subsequent state fields.
  malformed_flash.erase(malformed_flash.begin() + flash_data_offset + 65536,
                        malformed_flash.begin() + flash_data_offset + 131072);
  write_word(malformed_flash, flash_len_offset, 65536);
  reject_flash(malformed_flash, SaveStateDecodeStatus::restore_rejected);
  malformed_flash = flash_encoded;
  malformed_flash.at(flash_data_offset + 131071) ^= 1U;
  reject_flash(malformed_flash, SaveStateDecodeStatus::state_hash_mismatch);

  std::cout << "save_state_codec_test: PASS\n";
  return 0;
}
