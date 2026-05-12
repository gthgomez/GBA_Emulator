#include "gba/core/core_session.hpp"
#include "gba/core/save_state_codec.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

namespace {

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void write_word(std::vector<std::uint8_t>& bytes, std::size_t offset,
                std::uint32_t value) {
  bytes.at(offset + 0U) = static_cast<std::uint8_t>(value & 0xFFU);
  bytes.at(offset + 1U) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  bytes.at(offset + 2U) = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  bytes.at(offset + 3U) = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

}  // namespace

int main() {
  using gba::core::CoreRunStopReason;
  using gba::core::CoreSession;
  using gba::core::GamePakSaveType;
  using gba::core::SaveStateCodec;
  using gba::core::SaveStateDecodeStatus;

  constexpr std::uint32_t kAddR0R0Imm1 = 0xE2800001U;
  constexpr std::uint32_t kBranchBackOneInstruction = 0xEAFFFFFDU;

  auto session = std::make_unique<CoreSession>();
  std::vector<std::uint8_t> rom(8);
  write_word(rom, 0, kAddR0R0Imm1);
  write_word(rom, 4, kBranchBackOneInstruction);
  expect(session->memory().load_game_pak_rom(rom), "codec test loads ROM");
  expect(session->memory().configure_game_pak_save(GamePakSaveType::sram32k),
         "codec test configures save");
  expect(session->memory().write8(0x0E000010, 0x5A), "codec test seeds save");
  expect(session->keypad().set_pressed_mask(0x0001), "codec test seeds keypad");
  session->keypad().write_keycnt(0x4001);
  session->cpu().set_register(gba::core::Arm7tdmi::kPc, 0x08000000U);

  const std::vector<std::uint8_t> encoded = SaveStateCodec::encode(*session);
  expect(encoded.size() > 32, "encoded save-state has header and payload");
  const gba::core::CoreSchedulerRunResult run_a = session->run(8);
  expect(run_a.stop_reason == CoreRunStopReason::max_steps, "source run completes");
  const std::uint64_t final_hash_a = session->state_hash();

  auto restored = std::make_unique<CoreSession>();
  const gba::core::SaveStateDecodeResult decoded =
      SaveStateCodec::decode_into(*restored, encoded);
  expect(decoded.status == SaveStateDecodeStatus::ok, "save-state decodes");
  expect(decoded.version == SaveStateCodec::kVersion, "save-state version reports");
  expect(restored->memory().game_pak_rom_size() == rom.size(), "ROM bytes restore");
  expect(restored->memory().game_pak_save_type() == GamePakSaveType::sram32k,
         "save type restores");
  expect(restored->keypad().pressed_mask() == 0x0001, "keypad state restores");
  expect(restored->state_hash() == decoded.encoded_state_hash,
         "decoded save-state restores the encoded hash");
  const gba::core::CoreSchedulerRunResult run_b = restored->run(8);
  expect(run_b.stop_reason == CoreRunStopReason::max_steps, "restored run completes");
  expect(restored->state_hash() == final_hash_a, "restored run reaches same hash");

  auto irq_session = std::make_unique<CoreSession>();
  irq_session->interrupts().write_interrupt_enable(0x0008);
  irq_session->interrupts().request(gba::core::InterruptSource::timer0);
  irq_session->interrupts().write_ime(1);
  const std::vector<std::uint8_t> irq_encoded =
      SaveStateCodec::encode(*irq_session);
  auto irq_restored = std::make_unique<CoreSession>();
  const gba::core::SaveStateDecodeResult irq_decoded =
      SaveStateCodec::decode_into(*irq_restored, irq_encoded);
  expect(irq_decoded.status == SaveStateDecodeStatus::ok,
         "interrupt save-state decodes");
  expect(irq_restored->state_hash() == irq_decoded.encoded_state_hash,
         "interrupt save-state restores the encoded hash");
  expect(irq_restored->interrupts().irq_line(),
         "interrupt save-state restores IE IF and IME");

  auto bios_session = std::make_unique<CoreSession>();
  bios_session->bios().set_mode(gba::core::BiosExecutionMode::hle);
  const std::vector<std::uint8_t> bios_encoded =
      SaveStateCodec::encode(*bios_session);
  auto bios_restored = std::make_unique<CoreSession>();
  const gba::core::SaveStateDecodeResult bios_decoded =
      SaveStateCodec::decode_into(*bios_restored, bios_encoded);
  expect(bios_decoded.status == SaveStateDecodeStatus::ok,
         "BIOS-mode save-state decodes");
  expect(bios_restored->bios().mode() == gba::core::BiosExecutionMode::hle,
         "BIOS-mode save-state restores HLE mode");
  expect(bios_restored->state_hash() == bios_decoded.encoded_state_hash,
         "BIOS-mode save-state restores the encoded hash");

  auto timer_session = std::make_unique<CoreSession>();
  timer_session->timers().tick(179, timer_session->interrupts());
  timer_session->timers().write_reload(0, 0xFFEE);
  timer_session->timers().write_control(0, 0x00C3);
  timer_session->timers().defer_newly_enabled_ticks();
  const std::vector<std::uint8_t> timer_encoded =
      SaveStateCodec::encode(*timer_session);
  auto timer_restored = std::make_unique<CoreSession>();
  const gba::core::SaveStateDecodeResult timer_decoded =
      SaveStateCodec::decode_into(*timer_restored, timer_encoded);
  expect(timer_decoded.status == SaveStateDecodeStatus::ok,
         "timer save-state decodes");
  expect(timer_restored->state_hash() == timer_decoded.encoded_state_hash,
         "timer save-state restores the encoded hash");
  timer_restored->timers().tick(1, timer_restored->interrupts());
  expect(timer_restored->timers().counter(0) == 0xFFEE,
         "timer save-state preserves deferred enable delay");
  timer_restored->timers().tick(843, timer_restored->interrupts());
  expect(timer_restored->timers().counter(0) == 0xFFEE,
         "timer save-state preserves prescaler phase before edge");
  timer_restored->timers().tick(1, timer_restored->interrupts());
  expect(timer_restored->timers().counter(0) == 0xFFEF,
         "timer save-state resumes from restored prescaler phase");

  std::vector<std::uint8_t> corrupted = encoded;
  corrupted.at(0) = 0;
  expect(SaveStateCodec::decode_into(*restored, corrupted).status ==
             SaveStateDecodeStatus::bad_magic,
         "bad magic rejects");
  corrupted = encoded;
  corrupted.pop_back();
  expect(SaveStateCodec::decode_into(*restored, corrupted).status ==
             SaveStateDecodeStatus::corrupt_payload,
         "truncated payload rejects");

  std::cout << "save_state_codec_test: PASS\n";
  return 0;
}
