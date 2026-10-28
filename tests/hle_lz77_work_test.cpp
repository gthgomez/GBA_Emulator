#include "gba/core/emulator_runtime.hpp"
#include "gba/core/save_state_codec.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {
void expect(bool condition, const char* message) {
  if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
using namespace gba::core;

void seed_literals(CoreSession& session, std::uint32_t source, std::uint32_t count) {
  expect(session.memory().write32(source, (count << 8) | 0x10), "literal header");
  auto cursor = source + 4;
  for (std::uint32_t first = 0; first < count; first += 8) {
    expect(session.memory().write8(cursor++, 0), "literal flag");
    for (std::uint32_t i = first; i < first + 8 && i < count; ++i) {
      expect(session.memory().write8(cursor++, i & 255), "literal byte");
    }
  }
}

std::unique_ptr<CoreSession> pending_session() {
  auto session = std::make_unique<CoreSession>();
  session->bios().set_mode(BiosExecutionMode::hle);
  seed_literals(*session, 0x02010000, 64);
  expect(session->memory().write16(0x03000000, 0xdf12), "pending SWI");
  expect(session->memory().write16(0x03000002, 0x46c0), "caller NOP");
  expect(session->cpu().set_cpsr(0x3f), "Thumb system caller");
  session->cpu().set_register(0, 0x02010000);
  session->cpu().set_register(1, 0x06000000);
  session->cpu().set_register(15, 0x03000000);
  expect(session->step().instruction == 0xdf12, "start pending decompression");
  expect(session->scheduler().save_state().hle_work_frames.size() == 1,
         "decompression queues one job");
  return session;
}

std::uint64_t remaining(const CoreSession& session) {
  const auto state = session.scheduler().save_state();
  return state.hle_work_frames.empty() ? 0 : state.hle_work_frames.back().remaining_cycles;
}

void snapshot_and_raw_api_test() {
  auto session = pending_session();
  const auto encoded = SaveStateCodec::encode(*session);
  auto restored = std::make_unique<CoreSession>();
  expect(SaveStateCodec::decode_into(*restored, encoded).status == SaveStateDecodeStatus::ok,
         "restore pending work");
  expect(restored->state_hash() == session->state_hash(), "pending work hash round trip");
  const auto before = remaining(*session);
  // Synthetic IWRAM caller: reference Timer2 result 2559, with 9 cycles
  // outside the pending work (timer sampling, two loads and the SWI call).
  expect(before == 2550, "64-byte EWRAM-to-VRAM reference work cost");
  const auto pc = session->cpu().register_value(15);
  // Raw stepping must wait too, without executing the supplied MOV r0,#1.
  const auto raw = session->scheduler().step_thumb(0x2001);
  const auto fetched = restored->step();
  expect(!fetched.instruction && fetched.step && !fetched.fetch_failed,
         "work step is a successful device advance without instruction fetch");
  expect(raw.cpu_step.elapsed_cycles > 0 && remaining(*session) < before,
         "raw Thumb stepping consumes work");
  expect(session->cpu().register_value(15) == pc &&
             session->cpu().register_value(0) == 0x0201004c,
         "raw stepping preserves caller PC and registers");
  expect(session->state_hash() == restored->state_hash(),
         "raw and fetched paths share pending-work semantics");
  const auto arm = session->scheduler().step_arm(0xe3a00001);
  expect(arm.cpu_step.elapsed_cycles > 0 && session->cpu().register_value(15) == pc,
         "raw ARM stepping consumes work without executing supplied instruction");
  auto corrupt = encoded;
  // Footer: count(4), remaining cycles(8), owner depth(4).
  corrupt[corrupt.size() - 12] ^= 1;
  const auto original = restored->state_hash();
  expect(SaveStateCodec::decode_into(*restored, corrupt).status ==
             SaveStateDecodeStatus::state_hash_mismatch,
         "pending cycle corruption fails integrity");
  expect(restored->state_hash() == original, "integrity failure preserves target");
  for (const bool invalid_owner : {false, true}) {
    corrupt = encoded;
    if (invalid_owner) corrupt[corrupt.size() - 4] = 1;
    else for (std::size_t i = corrupt.size() - 12; i < corrupt.size() - 4; ++i) corrupt[i] = 0;
    const auto result = SaveStateCodec::decode_into(*restored, corrupt);
    expect(result.status != SaveStateDecodeStatus::ok, "invalid work frame rejected");
    expect(restored->state_hash() == original, "structural rejection preserves target");
  }
  restored->reset();
  expect(remaining(*restored) == 0, "reset clears pending work");
}

void irq_and_nested_work_test() {
  auto session = pending_session();
  const auto outer_remaining = remaining(*session);
  expect(session->memory().write32(BiosHleConstants::kUserIrqHandlerPointer,
                                   0x03000201), "Thumb IRQ pointer");
  expect(session->memory().write16(0x03000200, 0xdf11), "handler decompression");
  expect(session->memory().write16(0x03000202, 0x4770), "handler BX LR");
  seed_literals(*session, 0x02020000, 16);
  session->cpu().set_register(0, 0x02020000);
  session->cpu().set_register(1, 0x02030000);
  session->interrupts().write_interrupt_enable(1);
  session->interrupts().write_ime(1);
  session->interrupts().request(InterruptSource::vblank);
  bool serviced = false;
  for (unsigned i = 0; i < 4 && !serviced; ++i) {
    const auto step = session->step();
    serviced = step.step && step.step->irq_serviced;
  }
  expect(serviced, "pending work admits IRQ with bounded latency");
  const auto paused_remaining = remaining(*session);
  expect(paused_remaining <= outer_remaining, "work advances until IRQ admission");
  expect(session->step().step.has_value(), "dispatch interrupting handler");
  expect(remaining(*session) == paused_remaining, "IRQ dispatch pauses parent work");
  expect(session->step().instruction == 0xdf11, "handler can queue nested work");
  auto state = session->scheduler().save_state();
  expect(state.hle_work_frames.size() == 2 &&
             state.hle_work_frames[0].owner_irq_depth == 0 &&
             state.hle_work_frames[1].owner_irq_depth == 1,
         "nested work belongs to interrupting handler");
  const auto encoded = SaveStateCodec::encode(*session);
  auto restored = std::make_unique<CoreSession>();
  expect(SaveStateCodec::decode_into(*restored, encoded).status == SaveStateDecodeStatus::ok,
         "restore nested work and IRQ");
  session->interrupts().write_interrupt_flags(1);
  restored->interrupts().write_interrupt_flags(1);
  for (unsigned i = 0; i < 32; ++i) {
    state = session->scheduler().save_state();
    if (state.hle_work_frames.size() == 1) break;
    expect(session->step().step.has_value() && restored->step().step.has_value(),
           "nested work progresses after restoration");
    expect(session->state_hash() == restored->state_hash(), "nested continuation deterministic");
  }
  expect(session->scheduler().save_state().hle_work_frames.size() == 1 &&
             remaining(*session) == paused_remaining,
         "only handler work runs while parent is interrupted");
  expect(session->step().instruction == 0x4770, "handler returns after its work");
  expect(session->step().step.has_value(), "complete IRQ return");
  expect(session->cpu().register_value(15) == 0x03000002 &&
             remaining(*session) == paused_remaining,
         "IRQ returns to still-pending caller");
  expect(session->step().step.has_value() && remaining(*session) < paused_remaining,
         "parent work resumes after IRQ return");
}

void dma_and_audio_test() {
  auto session = pending_session();
  session->apu().write_soundcnt_x(0x80);
  session->timers().write_reload(2, 0);
  session->timers().write_control(2, 0x80);
  const auto start = session->scheduler().scheduler_cycles();
  const auto work = remaining(*session);
  expect(session->memory().write16(0x02000000, 0xbeef), "DMA source");
  session->dma().write_source(0, 0x02000000);
  session->dma().write_destination(0, 0x03001000);
  session->dma().write_word_count(0, 1);
  session->dma().write_control(0, 0x8000);
  const auto step = session->step();
  expect(step.step && step.step->immediate_dma.channels_executed == 1,
         "immediate DMA executes during work");
  expect(session->memory().read16(0x03001000).value_or(0) == 0xbeef,
         "immediate DMA copied data");
  expect(session->scheduler().scheduler_cycles() - start ==
             work - remaining(*session) + step.step->immediate_dma.bus_cycles,
         "DMA steals time without consuming decompression work");
  session->dma().write_source(0, 0x02000000);
  session->dma().write_destination(0, 0x03001002);
  session->dma().write_word_count(0, 1);
  session->dma().write_control(0, 0xa000);
  for (unsigned i = 0; i < 128 && remaining(*session); ++i) {
    expect(session->step().step.has_value(), "device work tick");
  }
  expect(remaining(*session) == 0, "bounded work completes");
  expect(session->memory().read16(0x03001002).value_or(0) == 0xbeef,
         "HBlank DMA triggers during pending work");
  expect(session->apu().available_audio_samples() > 0, "APU produces samples during work");
  expect(session->timers().counter(2) == session->scheduler().scheduler_cycles() - start,
         "timers include work and stolen DMA cycles");
}

void hblank_dma_edge_test() {
  auto session = pending_session();
  // No HBlank IRQ is enabled. DMA starts Timer0, making a late transfer
  // observable independently of whether the copy eventually completes.
  expect(session->memory().write32(0x02000000, 0x00800000), "timer DMA payload");
  session->dma().write_source(0, 0x02000000);
  session->dma().write_destination(0, 0x04000100);
  session->dma().write_word_count(0, 1);
  session->dma().write_control(0, 0xa400);
  for (unsigned i = 0; i < 32 && !session->timers().enabled(0); ++i) {
    expect(session->step().step.has_value(), "advance to HBlank DMA");
  }
  expect(session->timers().enabled(0), "HBlank DMA starts timer");
  expect(session->timers().counter(0) ==
             session->scheduler().scheduler_cycles() - PpuTiming::kHblankFlagCycles,
         "HBlank DMA starts timer at its event, without IRQ gating or overshoot");
}
} // namespace

void multi_frame_test() {
  using namespace gba::core;
  auto runtime = std::make_unique<EmulatorRuntime>();
  std::vector<std::uint8_t> rom{0xfe, 0xff, 0xff, 0xea}; // ARM branch to itself.
  expect(runtime->load_rom(rom) == EmulatorRuntimeStatus::ok, "load synthetic ROM");
  auto& session = runtime->session();
  session.bios().set_mode(BiosExecutionMode::hle);
  constexpr std::uint32_t source = 0x02010000;
  constexpr std::uint32_t destination = 0x06000000;
  constexpr std::uint32_t marker = 0x03001000;
  constexpr std::uint32_t count = 32768;
  expect(session.memory().write32(source, (count << 8) | 0x10), "LZ77 header");
  std::uint32_t cursor = source + 4;
  for (std::uint32_t first = 0; first < count; first += 8) {
    expect(session.memory().write8(cursor++, 0), "literal flag group");
    for (std::uint32_t i = 0; i < 8; ++i) {
      expect(session.memory().write8(cursor++, (first + i) & 255), "literal payload");
    }
  }
  expect(session.memory().write16(0x03000000, 0xdf12), "Thumb VRAM decompression SWI");
  expect(session.memory().write16(0x03000002, 0x6037), "Thumb STR r7,[r6]");
  expect(session.memory().write16(0x03000004, 0xe7fe), "Thumb idle branch");
  const auto store = Arm7tdmi::decode_thumb_memory_transfer(0x6037);
  expect(!store.load && store.rb == 6 && store.rd == 7,
         "marker instruction decodes as the intended store");
  expect(session.cpu().set_cpsr(0x3f), "Thumb system mode");
  session.cpu().set_register(0, source);
  session.cpu().set_register(1, destination);
  session.cpu().set_register(6, marker);
  session.cpu().set_register(7, 0x12345678);
  session.cpu().set_register(15, 0x03000000);
  session.timers().write_reload(2, 0);
  session.timers().write_control(2, 0x80);
  for (unsigned frame = 0; frame < 4; ++frame) {
    const auto result = runtime->step_frame(500000);
    expect(result.frame_complete, "pending decompression produces a complete frame");
    expect(session.memory().read32(marker).value_or(0) == 0,
           "large HLE decompression delays the next instruction across frames");
    expect(session.timers().save_state().cycle_counter >=
               std::uint64_t(frame + 1) * PpuTiming::kCyclesPerFrame,
           "timers progress while HLE work is pending");
  }
  expect(runtime->step_frame(500000).frame_complete, "completion frame advances");
  expect(session.memory().read32(marker).value_or(0) == 0x12345678,
         "caller resumes when decompression work completes");
  for (std::uint32_t i = 0; i < count; ++i) {
    expect(session.memory().read8(destination + i).value_or(255) == (i & 255),
           "decompression retains every output byte");
  }
}

int main() {
  snapshot_and_raw_api_test();
  irq_and_nested_work_test();
  dma_and_audio_test();
  hblank_dma_edge_test();
  multi_frame_test();
  std::cout << "hle_lz77_work_test: PASS\n";
}
