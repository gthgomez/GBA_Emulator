#include "gba/core/core_session.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

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
  using gba::core::Arm7tdmi;
  using gba::core::BiosExecutionMode;
  using gba::core::CoreRunStopReason;
  using gba::core::CoreSession;
  using gba::core::ExecuteStatus;
  using gba::core::GamePakSaveType;
  using gba::core::InterruptSource;
  using gba::core::IoRegisters;
  using gba::core::PpuTiming;

  constexpr std::uint32_t kProgramBase = 0x08000000U;
  constexpr std::uint32_t kAddR0R0Imm1 = 0xE2800001U;
  constexpr std::uint32_t kBranchBackOneInstruction = 0xEAFFFFFDU;
  constexpr std::uint32_t kMovR0IoBase = 0xE3A00301U;
  constexpr std::uint32_t kStrR0Ime = 0xE5800208U;
  constexpr std::uint32_t kLdrR2R1Imm0 = 0xE5912000U;
  constexpr std::uint32_t kStrR5R4Imm0 = 0xE5845000U;
  constexpr std::uint32_t kArmSwiDiv = 0xEF060000U;
  constexpr std::uint32_t kArmSwiArcTan = 0xEF090000U;
  constexpr std::uint32_t kArmSwiArcTan2 = 0xEF0A0000U;
  constexpr std::uint16_t kThumbSwiHalt = 0xDF02U;
  constexpr std::uint16_t kHblankIrqBit =
      static_cast<std::uint16_t>(1U << static_cast<std::uint8_t>(InterruptSource::hblank));

  CoreSession session;
  expect(session.state_hash() == session.state_hash(), "state hash is stable");
  expect(session.memory().configure_game_pak_save(GamePakSaveType::sram32k),
         "session exposes save backing API");
  expect(session.memory().write8(0x0E000000, 0x42),
         "session save backing writes through memory bus");
  const std::uint64_t with_save_hash = session.state_hash();
  expect(session.memory().write8(0x0E000000, 0x43),
         "changing save backing mutates session state");
  expect(session.state_hash() != with_save_hash, "state hash includes save backing");

  session.reset();
  std::vector<std::uint8_t> rom(256);
  write_word(rom, 0, kAddR0R0Imm1);
  write_word(rom, 4, kBranchBackOneInstruction);
  expect(session.memory().load_game_pak_rom(rom), "session loads explicit ROM bytes");
  session.waitcnt().write_control(gba::core::WaitStateControl::kStandardGamePakSetting);
  session.cpu().set_register(Arm7tdmi::kPc, kProgramBase);

  const gba::core::CoreSchedulerFetchStepResult first = session.step();
  expect(!first.fetch_failed, "session step fetches through owned scheduler");
  expect(first.step->cpu_step.status == ExecuteStatus::executed,
         "session step executes instruction");
  expect(session.cpu().register_value(0) == 1, "session step mutates owned CPU");
  const gba::core::CoreSessionState saved = session.save_state();
  const std::uint64_t saved_hash = session.state_hash();

  const gba::core::CoreSchedulerRunResult run_a = session.run(8);
  expect(run_a.stop_reason == CoreRunStopReason::max_steps,
         "session run reaches max-steps stop");
  const std::uint64_t after_a = session.state_hash();

  session.load_state(saved);
  expect(session.state_hash() == saved_hash, "state restore returns exact hash");
  const gba::core::CoreSchedulerRunResult run_b = session.run(8);
  expect(run_b.stop_reason == CoreRunStopReason::max_steps,
         "restored session run reaches max-steps stop");
  expect(session.state_hash() == after_a,
         "save/restore/run determinism produces identical final hash");
  expect(run_a.final_pc == run_b.final_pc, "restored run preserves final PC");
  expect(run_a.scheduler_cycles == run_b.scheduler_cycles,
         "restored run preserves scheduler-cycle result");

  CoreSession peer;
  expect(peer.memory().load_game_pak_rom(rom), "peer loads same explicit ROM bytes");
  peer.waitcnt().write_control(gba::core::WaitStateControl::kStandardGamePakSetting);
  peer.cpu().set_register(Arm7tdmi::kPc, kProgramBase);
  [[maybe_unused]] const gba::core::CoreSchedulerRunResult peer_run = peer.run(9);
  expect(peer.state_hash() == after_a,
         "fresh peer with same inputs reaches same deterministic hash");

  CoreSession io_session;
  std::vector<std::uint8_t> io_rom(256);
  write_word(io_rom, 0, kMovR0IoBase);
  write_word(io_rom, 4, kStrR0Ime);
  expect(io_session.memory().load_game_pak_rom(io_rom),
         "IO-routing session loads explicit ROM bytes");
  expect(io_session.io().write16(IoRegisters::kIme, 1),
         "IO-routing test seeds IME before CPU store");
  io_session.cpu().set_register(Arm7tdmi::kPc, kProgramBase);
  const gba::core::CoreSchedulerRunResult io_run = io_session.run(2);
  expect(io_run.stop_reason == CoreRunStopReason::max_steps,
         "CoreSession CPU stores can route through IO registers");
  expect(io_run.executed_steps == 2, "IO-routing program executes both instructions");
  const std::optional<std::uint16_t> ime_after = io_session.io().read16(IoRegisters::kIme);
  expect(ime_after.has_value() && ime_after.value() == 0,
         "CoreSession IO callback updates IME through MemoryBus");

  auto timer_phase_session = std::make_unique<CoreSession>();
  timer_phase_session->timers().write_reload(0, 0);
  timer_phase_session->timers().write_control(0, 0x0080);
  timer_phase_session->cpu().set_register(1, IoRegisters::kTimerBase);
  const gba::core::CoreSchedulerStepResult timer_load =
      timer_phase_session->scheduler().step_arm(kLdrR2R1Imm0);
  expect(timer_load.cpu_step.status == ExecuteStatus::executed,
         "timer IO phase fixture executes ARM LDR");
  expect(timer_load.devices.cycles == timer_load.cpu_step.elapsed_cycles,
         "timer IO phase fixture preserves total device cycle accounting");
  expect((timer_phase_session->cpu().register_value(2) & 0xFFFFU) == 3,
         "timer IO reads sample at the load data phase");

  auto timer_store_session = std::make_unique<CoreSession>();
  timer_store_session->cpu().set_register(4, IoRegisters::kTimerBase);
  timer_store_session->cpu().set_register(5, 0x00800000U);
  const gba::core::CoreSchedulerStepResult timer_store =
      timer_store_session->scheduler().step_arm(kStrR5R4Imm0);
  expect(timer_store.cpu_step.status == ExecuteStatus::executed,
         "timer IO store fixture executes ARM STR");
  expect(timer_store_session->timers().enabled(0),
         "timer IO store enables timer0 through CoreSession callbacks");
  expect(timer_store_session->timers().counter(0) == 0,
         "timer IO store loads timer0 reload value without ticking immediately");
  const gba::core::CoreSchedulerStepResult timer_store_tick_1 =
      timer_store_session->scheduler().step_arm(kAddR0R0Imm1);
  expect(timer_store_tick_1.cpu_step.status == ExecuteStatus::executed,
         "timer IO store delayed-start tick fixture executes first ARM ADD");
  expect(timer_store_session->timers().counter(0) == 1,
         "timer IO store starts timer0 after the one-cycle enable delay");
  const gba::core::CoreSchedulerStepResult timer_store_tick_2 =
      timer_store_session->scheduler().step_arm(kAddR0R0Imm1);
  expect(timer_store_tick_2.cpu_step.status == ExecuteStatus::executed,
         "timer IO store delayed-start tick fixture executes second ARM ADD");
  expect(timer_store_session->timers().counter(0) == 2,
         "timer IO store continues ticking after the delayed start");

  auto timer_word_store_session = std::make_unique<CoreSession>();
  timer_word_store_session->cpu().set_register(4, IoRegisters::kTimerBase);
  timer_word_store_session->cpu().set_register(5, 0x00C3FFEEU);
  const gba::core::CoreSchedulerStepResult timer_word_store =
      timer_word_store_session->scheduler().step_arm(kStrR5R4Imm0);
  expect(timer_word_store.cpu_step.status == ExecuteStatus::executed,
         "timer IO word-store fixture executes ARM STR");
  expect(timer_word_store_session->timers().reload(0) == 0xFFEE,
         "timer IO word store writes timer0 reload before control");
  expect(timer_word_store_session->timers().counter(0) == 0xFFEE,
         "timer IO word store enables timer0 with the new reload value");
  expect(timer_word_store_session->timers().control(0) == 0x00C3,
         "timer IO word store writes timer0 control after reload");

  auto irq_hle = std::make_unique<CoreSession>();
  irq_hle->bios().set_mode(BiosExecutionMode::hle);
  std::vector<std::uint8_t> irq_rom(256);
  write_word(irq_rom, 0x40, kAddR0R0Imm1);
  expect(irq_hle->memory().load_game_pak_rom(irq_rom), "IRQ-HLE ROM loads");
  expect(irq_hle->memory().write32(0x03007FFC, 0x08000040),
         "IRQ-HLE fixture seeds libgba-style handler pointer");
  expect(irq_hle->cpu().set_cpsr(0x00000092), "IRQ-HLE fixture enters IRQ mode");
  irq_hle->cpu().set_register(Arm7tdmi::kPc, 0x00000018);
  const gba::core::CoreSchedulerFetchStepResult irq_dispatch = irq_hle->step();
  expect(!irq_dispatch.fetch_failed, "IRQ-HLE dispatch does not fetch BIOS bytes");
  expect(irq_dispatch.step->cpu_step.status == ExecuteStatus::executed,
         "IRQ-HLE dispatch reports executed");
  expect(irq_dispatch.step->cpu_step.elapsed_cycles == 21,
         "IRQ-HLE dispatch accounts for BIOS vector dispatch latency");
  expect(irq_hle->cpu().register_value(Arm7tdmi::kPc) == 0x08000040,
         "IRQ-HLE dispatch jumps to user IRQ handler pointer");

  auto hblank_halt_before = std::make_unique<CoreSession>();
  hblank_halt_before->bios().set_mode(BiosExecutionMode::hle);
  std::vector<std::uint8_t> halt_rom(256);
  halt_rom.at(0) = static_cast<std::uint8_t>(kThumbSwiHalt & 0xFFU);
  halt_rom.at(1) = static_cast<std::uint8_t>((kThumbSwiHalt >> 8U) & 0xFFU);
  expect(hblank_halt_before->memory().load_game_pak_rom(halt_rom),
         "HBlank Halt-HLE ROM loads");
  expect(hblank_halt_before->cpu().set_cpsr(0x00000030),
         "HBlank Halt-HLE fixture enters Thumb system mode");
  hblank_halt_before->cpu().set_register(Arm7tdmi::kPc, kProgramBase);
  hblank_halt_before->interrupts().write_interrupt_enable(kHblankIrqBit);
  hblank_halt_before->interrupts().write_ime(1);
  hblank_halt_before->ppu().tick(static_cast<std::uint32_t>(PpuTiming::kCyclesPerLine) *
                                     PpuTiming::kVisibleLines +
                                 PpuTiming::kVisibleCycles - 1U,
                                 hblank_halt_before->interrupts());
  hblank_halt_before->ppu().write_dispstat(0x0010);
  const gba::core::CoreSchedulerFetchStepResult hblank_halt_before_step =
      hblank_halt_before->step();
  expect(hblank_halt_before_step.step->cpu_step.status == ExecuteStatus::executed,
         "HBlank Halt-HLE before event executes");
  expect(hblank_halt_before_step.step->cpu_step.elapsed_cycles == 93,
         "HBlank Halt-HLE before raw event includes fetch and HLE return cycles");
   expect(hblank_halt_before->ppu().line_cycle() ==
              PpuTiming::kVisibleCycles + 96U,
          "HBlank Halt-HLE entered before raw HBlank overlaps the event cycle");

  auto hblank_halt_pending = std::make_unique<CoreSession>();
  hblank_halt_pending->bios().set_mode(BiosExecutionMode::hle);
  expect(hblank_halt_pending->memory().load_game_pak_rom(halt_rom),
         "pending HBlank Halt-HLE ROM loads");
  expect(hblank_halt_pending->cpu().set_cpsr(0x00000030),
         "pending HBlank Halt-HLE fixture enters Thumb system mode");
  hblank_halt_pending->cpu().set_register(Arm7tdmi::kPc, kProgramBase);
  hblank_halt_pending->interrupts().write_interrupt_enable(kHblankIrqBit);
  hblank_halt_pending->interrupts().write_ime(1);
  hblank_halt_pending->ppu().tick(static_cast<std::uint32_t>(PpuTiming::kCyclesPerLine) *
                                      PpuTiming::kVisibleLines +
                                  PpuTiming::kVisibleCycles,
                                  hblank_halt_pending->interrupts());
  hblank_halt_pending->interrupts().request(InterruptSource::hblank);
  const gba::core::CoreSchedulerFetchStepResult hblank_halt_pending_step =
      hblank_halt_pending->step();
  expect(hblank_halt_pending_step.step->cpu_step.status == ExecuteStatus::executed,
         "pending HBlank Halt-HLE executes");
  expect(hblank_halt_pending_step.step->cpu_step.elapsed_cycles == 93,
         "pending HBlank Halt-HLE includes fetch and HLE return cycles");
   expect(hblank_halt_pending->ppu().line_cycle() ==
              PpuTiming::kVisibleCycles + 97U,
          "pending HBlank Halt-HLE keeps the steady return phase");

  auto div_hle = std::make_unique<CoreSession>();
  div_hle->bios().set_mode(BiosExecutionMode::hle);
  std::vector<std::uint8_t> swi_rom(256);
  swi_rom.at(0) = 0x06;
  swi_rom.at(1) = 0xDF;
  expect(div_hle->memory().load_game_pak_rom(swi_rom), "SWI-HLE ROM loads");
  expect(div_hle->cpu().set_cpsr(0x00000030), "SWI-HLE fixture enters Thumb user mode");
  div_hle->cpu().set_register(Arm7tdmi::kPc, kProgramBase);
  div_hle->cpu().set_register(0, 7);
  div_hle->cpu().set_register(1, 3);
  const gba::core::CoreSchedulerFetchStepResult div_step = div_hle->step();
  expect(div_step.step->cpu_step.status == ExecuteStatus::executed,
         "SWI-HLE Div executes without trapping to BIOS vector");
  expect(div_hle->cpu().register_value(0) == 2, "SWI-HLE Div returns quotient");
  expect(div_hle->cpu().register_value(1) == 1, "SWI-HLE Div returns remainder");
  expect(div_hle->cpu().register_value(Arm7tdmi::kPc) == kProgramBase + 2,
         "SWI-HLE Div advances past Thumb SWI");

  auto arm_arctan_hle = std::make_unique<CoreSession>();
  arm_arctan_hle->bios().set_mode(BiosExecutionMode::hle);
  std::vector<std::uint8_t> arm_arctan_rom(256);
  write_word(arm_arctan_rom, 0, kArmSwiArcTan);
  expect(arm_arctan_hle->memory().load_game_pak_rom(arm_arctan_rom),
         "SWI-HLE ArcTan ARM ROM loads");
  expect(arm_arctan_hle->cpu().set_cpsr(0x0000001F),
         "SWI-HLE ArcTan fixture enters ARM system mode");
  arm_arctan_hle->cpu().set_register(Arm7tdmi::kPc, kProgramBase);
  arm_arctan_hle->cpu().set_register(0, 0);
  arm_arctan_hle->cpu().set_register(2, kArmSwiArcTan);
  const gba::core::CoreSchedulerFetchStepResult arctan_step = arm_arctan_hle->step();
  expect(arctan_step.step->cpu_step.status == ExecuteStatus::executed,
         "SWI-HLE ArcTan ARM executes without trapping to BIOS vector");
  expect(arm_arctan_hle->cpu().register_value(0) == 0,
         "SWI-HLE ArcTan zero returns zero");
  expect(arm_arctan_hle->cpu().register_value(1) == 0,
         "SWI-HLE ArcTan zero leaves polynomial scratch at zero");
  expect(arm_arctan_hle->cpu().register_value(2) == kArmSwiArcTan,
         "SWI-HLE ArcTan preserves r2 scratch instruction");
  expect(arm_arctan_hle->cpu().register_value(3) == 0xA2F9,
         "SWI-HLE ArcTan writes BIOS polynomial factor scratch");
  expect(arm_arctan_hle->cpu().register_value(Arm7tdmi::kPc) == kProgramBase + 4,
         "SWI-HLE ArcTan advances past ARM SWI");

  auto arm_arctan2_hle = std::make_unique<CoreSession>();
  arm_arctan2_hle->bios().set_mode(BiosExecutionMode::hle);
  std::vector<std::uint8_t> arm_arctan2_rom(256);
  write_word(arm_arctan2_rom, 0, kArmSwiArcTan2);
  expect(arm_arctan2_hle->memory().load_game_pak_rom(arm_arctan2_rom),
         "SWI-HLE ArcTan2 ARM ROM loads");
  expect(arm_arctan2_hle->cpu().set_cpsr(0x0000001F),
         "SWI-HLE ArcTan2 fixture enters ARM system mode");
  arm_arctan2_hle->cpu().set_register(Arm7tdmi::kPc, kProgramBase);
  arm_arctan2_hle->cpu().set_register(0, 1);
  arm_arctan2_hle->cpu().set_register(1, 1);
  arm_arctan2_hle->cpu().set_register(2, kArmSwiArcTan2);
  const gba::core::CoreSchedulerFetchStepResult arctan2_step = arm_arctan2_hle->step();
  expect(arctan2_step.step->cpu_step.status == ExecuteStatus::executed,
         "SWI-HLE ArcTan2 ARM executes without trapping to BIOS vector");
  expect(arm_arctan2_hle->cpu().register_value(0) == 0x2000,
         "SWI-HLE ArcTan2 1,1 returns eighth-turn angle");
  expect(arm_arctan2_hle->cpu().register_value(1) == 0xFFFFC000,
         "SWI-HLE ArcTan2 preserves BIOS ArcTan scratch");
  expect(arm_arctan2_hle->cpu().register_value(2) == kArmSwiArcTan2,
         "SWI-HLE ArcTan2 preserves r2 scratch instruction");
  expect(arm_arctan2_hle->cpu().register_value(3) == 0x170,
         "SWI-HLE ArcTan2 writes BIOS scratch r3");
  expect(arm_arctan2_hle->cpu().register_value(Arm7tdmi::kPc) == kProgramBase + 4,
         "SWI-HLE ArcTan2 advances past ARM SWI");

  auto arm_div_zero_hle = std::make_unique<CoreSession>();
  arm_div_zero_hle->bios().set_mode(BiosExecutionMode::hle);
  std::vector<std::uint8_t> arm_div_rom(256);
  write_word(arm_div_rom, 0, kArmSwiDiv);
  expect(arm_div_zero_hle->memory().load_game_pak_rom(arm_div_rom),
         "SWI-HLE Div ARM ROM loads");
  expect(arm_div_zero_hle->cpu().set_cpsr(0x0000001F),
         "SWI-HLE Div fixture enters ARM system mode");
  arm_div_zero_hle->cpu().set_register(Arm7tdmi::kPc, kProgramBase);
  arm_div_zero_hle->cpu().set_register(0, 0xFFFFFFFFU);
  arm_div_zero_hle->cpu().set_register(1, 0);
  const gba::core::CoreSchedulerFetchStepResult div_zero_step = arm_div_zero_hle->step();
  expect(div_zero_step.step->cpu_step.status == ExecuteStatus::executed,
         "SWI-HLE Div ARM denominator-zero executes");
  expect(arm_div_zero_hle->cpu().register_value(0) == 0xFFFFFFFFU,
         "SWI-HLE Div denominator-zero returns signed saturation for negative numerator");
  expect(arm_div_zero_hle->cpu().register_value(1) == 0xFFFFFFFFU,
         "SWI-HLE Div denominator-zero preserves numerator as remainder");
  expect(arm_div_zero_hle->cpu().register_value(3) == 1,
         "SWI-HLE Div denominator-zero writes BIOS abs scratch");

  auto arm_div_overflow_hle = std::make_unique<CoreSession>();
  arm_div_overflow_hle->bios().set_mode(BiosExecutionMode::hle);
  expect(arm_div_overflow_hle->memory().load_game_pak_rom(arm_div_rom),
         "SWI-HLE Div overflow ARM ROM loads");
  expect(arm_div_overflow_hle->cpu().set_cpsr(0x0000001F),
         "SWI-HLE Div overflow fixture enters ARM system mode");
  arm_div_overflow_hle->cpu().set_register(Arm7tdmi::kPc, kProgramBase);
  arm_div_overflow_hle->cpu().set_register(0, 0x80000000U);
  arm_div_overflow_hle->cpu().set_register(1, 0xFFFFFFFFU);
  const gba::core::CoreSchedulerFetchStepResult div_overflow_step = arm_div_overflow_hle->step();
  expect(div_overflow_step.step->cpu_step.status == ExecuteStatus::executed,
         "SWI-HLE Div ARM INT_MIN/-1 executes");
  expect(arm_div_overflow_hle->cpu().register_value(0) == 0x80000000U,
         "SWI-HLE Div INT_MIN/-1 returns wrapped quotient");
  expect(arm_div_overflow_hle->cpu().register_value(1) == 0,
         "SWI-HLE Div INT_MIN/-1 returns zero remainder");
  expect(arm_div_overflow_hle->cpu().register_value(3) == 0x80000000U,
         "SWI-HLE Div INT_MIN/-1 writes wrapped abs scratch");

  auto cpuset_hle = std::make_unique<CoreSession>();
  cpuset_hle->bios().set_mode(BiosExecutionMode::hle);
  std::vector<std::uint8_t> cpuset_rom(256);
  cpuset_rom.at(0) = 0x0B;
  cpuset_rom.at(1) = 0xDF;
  cpuset_rom.at(0x40) = 0xAA;
  cpuset_rom.at(0x41) = 0xBB;
  cpuset_rom.at(0x42) = 0xCC;
  cpuset_rom.at(0x43) = 0xDD;
  expect(cpuset_hle->memory().load_game_pak_rom(cpuset_rom), "CpuSet-HLE ROM loads");
  expect(cpuset_hle->cpu().set_cpsr(0x00000030), "CpuSet-HLE fixture enters Thumb user mode");
  cpuset_hle->cpu().set_register(Arm7tdmi::kPc, kProgramBase);
  cpuset_hle->cpu().set_register(0, 0x08000041U);
  cpuset_hle->cpu().set_register(1, 0x02000000U);
  cpuset_hle->cpu().set_register(2, 2);
  const gba::core::CoreSchedulerFetchStepResult cpuset_step = cpuset_hle->step();
  expect(cpuset_step.step->cpu_step.status == ExecuteStatus::executed,
         "SWI-HLE CpuSet handles odd halfword source");
  expect(cpuset_hle->memory().read32(0x02000000).value_or(0) == 0x00DD00BB,
         "SWI-HLE CpuSet odd halfword source copies addressed bytes");

  auto cpuset_word_hle = std::make_unique<CoreSession>();
  cpuset_word_hle->bios().set_mode(BiosExecutionMode::hle);
  std::vector<std::uint8_t> cpuset_word_rom(256);
  cpuset_word_rom.at(0) = 0x0B;
  cpuset_word_rom.at(1) = 0xDF;
  cpuset_word_rom.at(0x40) = 0xEF;
  cpuset_word_rom.at(0x41) = 0xBE;
  cpuset_word_rom.at(0x42) = 0xAD;
  cpuset_word_rom.at(0x43) = 0xDE;
  expect(cpuset_word_hle->memory().load_game_pak_rom(cpuset_word_rom),
         "CpuSet-HLE word ROM loads");
  expect(cpuset_word_hle->cpu().set_cpsr(0x00000030),
         "CpuSet-HLE word fixture enters Thumb user mode");
  cpuset_word_hle->cpu().set_register(Arm7tdmi::kPc, kProgramBase);
  cpuset_word_hle->cpu().set_register(0, 0x08000041U);
  cpuset_word_hle->cpu().set_register(1, 0x02000003U);
  cpuset_word_hle->cpu().set_register(2, (1U << 26U) | 1U);
  const gba::core::CoreSchedulerFetchStepResult cpuset_word_step = cpuset_word_hle->step();
  expect(cpuset_word_step.step->cpu_step.status == ExecuteStatus::executed,
         "SWI-HLE CpuSet handles unaligned word source and destination");
  expect(cpuset_word_hle->memory().read32(0x02000000).value_or(0) == 0xDEADBEEF,
         "SWI-HLE CpuSet word path aligns source and destination");

  auto cpuset_bios_hle = std::make_unique<CoreSession>();
  cpuset_bios_hle->bios().set_mode(BiosExecutionMode::hle);
  std::vector<std::uint8_t> cpuset_bios_rom(256);
  cpuset_bios_rom.at(0) = 0x0B;
  cpuset_bios_rom.at(1) = 0xDF;
  expect(cpuset_bios_hle->memory().load_game_pak_rom(cpuset_bios_rom),
         "CpuSet-HLE BIOS-source ROM loads");
  expect(cpuset_bios_hle->cpu().set_cpsr(0x00000030),
         "CpuSet-HLE BIOS-source fixture enters Thumb user mode");
  cpuset_bios_hle->cpu().set_register(Arm7tdmi::kPc, kProgramBase);
  cpuset_bios_hle->cpu().set_register(0, 0x00010000U);
  cpuset_bios_hle->cpu().set_register(1, 0x02000000U);
  cpuset_bios_hle->cpu().set_register(2, (1U << 26U) | 1U);
  const gba::core::CoreSchedulerFetchStepResult cpuset_bios_step = cpuset_bios_hle->step();
  expect(cpuset_bios_step.step->cpu_step.status == ExecuteStatus::executed,
         "SWI-HLE CpuSet handles protected BIOS source");
  expect(cpuset_bios_hle->memory().read32(0x02000000).value_or(0xFFFFFFFFU) == 0,
         "SWI-HLE CpuSet protected BIOS source copies zero");

  auto lz77_literal_hle = std::make_unique<CoreSession>();
  lz77_literal_hle->bios().set_mode(BiosExecutionMode::hle);
  std::vector<std::uint8_t> lz77_literal_rom(256);
  lz77_literal_rom.at(0) = 0x12;
  lz77_literal_rom.at(1) = 0xDF;
  lz77_literal_rom.at(0x40) = 0x10;
  lz77_literal_rom.at(0x41) = 0x05;
  lz77_literal_rom.at(0x42) = 0x00;
  lz77_literal_rom.at(0x43) = 0x00;
  lz77_literal_rom.at(0x44) = 0x00;
  lz77_literal_rom.at(0x45) = 0x11;
  lz77_literal_rom.at(0x46) = 0x22;
  lz77_literal_rom.at(0x47) = 0x33;
  lz77_literal_rom.at(0x48) = 0x44;
  lz77_literal_rom.at(0x49) = 0x55;
  expect(lz77_literal_hle->memory().load_game_pak_rom(lz77_literal_rom),
         "LZ77UnCompVram literal ROM loads");
  expect(lz77_literal_hle->cpu().set_cpsr(0x00000030),
         "LZ77UnCompVram literal fixture enters Thumb user mode");
  lz77_literal_hle->cpu().set_register(Arm7tdmi::kPc, kProgramBase);
  lz77_literal_hle->cpu().set_register(0, 0x08000040U);
  lz77_literal_hle->cpu().set_register(1, 0x06000000U);
  const gba::core::CoreSchedulerFetchStepResult lz77_literal_step =
      lz77_literal_hle->step();
  expect(lz77_literal_step.step->cpu_step.status == ExecuteStatus::executed,
         "SWI-HLE LZ77UnCompVram handles literal stream");
  expect(lz77_literal_hle->memory().read16(0x06000000).value_or(0) == 0x2211,
         "SWI-HLE LZ77UnCompVram writes first VRAM halfword");
  expect(lz77_literal_hle->memory().read16(0x06000002).value_or(0) == 0x4433,
         "SWI-HLE LZ77UnCompVram writes second VRAM halfword");
  expect(lz77_literal_hle->memory().read16(0x06000004).value_or(0xFFFFU) == 0x0055,
         "SWI-HLE LZ77UnCompVram pads odd final byte as halfword");
  expect(lz77_literal_hle->cpu().register_value(Arm7tdmi::kPc) == kProgramBase + 2,
         "SWI-HLE LZ77UnCompVram advances past Thumb SWI");

  auto lz77_backref_hle = std::make_unique<CoreSession>();
  lz77_backref_hle->bios().set_mode(BiosExecutionMode::hle);
  std::vector<std::uint8_t> lz77_backref_rom(256);
  lz77_backref_rom.at(0) = 0x12;
  lz77_backref_rom.at(1) = 0xDF;
  lz77_backref_rom.at(0x40) = 0x10;
  lz77_backref_rom.at(0x41) = 0x06;
  lz77_backref_rom.at(0x42) = 0x00;
  lz77_backref_rom.at(0x43) = 0x00;
  lz77_backref_rom.at(0x44) = 0x20;
  lz77_backref_rom.at(0x45) = 0xAA;
  lz77_backref_rom.at(0x46) = 0xBB;
  lz77_backref_rom.at(0x47) = 0x10;
  lz77_backref_rom.at(0x48) = 0x01;
  expect(lz77_backref_hle->memory().load_game_pak_rom(lz77_backref_rom),
         "LZ77UnCompVram back-reference ROM loads");
  expect(lz77_backref_hle->cpu().set_cpsr(0x00000030),
         "LZ77UnCompVram back-reference fixture enters Thumb user mode");
  lz77_backref_hle->cpu().set_register(Arm7tdmi::kPc, kProgramBase);
  lz77_backref_hle->cpu().set_register(0, 0x08000040U);
  lz77_backref_hle->cpu().set_register(1, 0x06000020U);
  const gba::core::CoreSchedulerFetchStepResult lz77_backref_step =
      lz77_backref_hle->step();
  expect(lz77_backref_step.step->cpu_step.status == ExecuteStatus::executed,
         "SWI-HLE LZ77UnCompVram handles back-reference stream");
  expect(lz77_backref_hle->memory().read32(0x06000020).value_or(0) == 0xBBAABBAA,
         "SWI-HLE LZ77UnCompVram expands repeated VRAM bytes");
  expect(lz77_backref_hle->memory().read16(0x06000024).value_or(0) == 0xBBAA,
         "SWI-HLE LZ77UnCompVram completes back-reference expansion");

  std::cout << "core_session_test: PASS\n";
  return 0;
}
