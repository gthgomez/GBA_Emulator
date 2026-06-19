#include "gba/core/core_session.hpp"

#include "gba/core/state_hash.hpp"

namespace gba::core {
namespace {

std::optional<std::uint16_t> io_read16(void* context, std::uint32_t address) {
  return static_cast<IoRegisters*>(context)->read16(address);
}

std::optional<std::uint32_t> io_read32(void* context, std::uint32_t address) {
  return static_cast<IoRegisters*>(context)->read32(address);
}

bool io_write16(void* context, std::uint32_t address, std::uint16_t value) {
  return static_cast<IoRegisters*>(context)->write16(address, value);
}

bool io_write32(void* context, std::uint32_t address, std::uint32_t value) {
  return static_cast<IoRegisters*>(context)->write32(address, value);
}

MemoryBusIoCallbacks io_callbacks(IoRegisters& io) {
  return {&io, io_read16, io_read32, io_write16, io_write32};
}

}  // namespace

CoreSession::CoreSession()
    : cpu_(),
      memory_(),
      interrupts_(),
      timers_(),
      dma_(),
      ppu_(),
      apu_(),
      bios_(),
      keypad_(),
      waitcnt_(),
      io_(interrupts_, timers_, dma_, ppu_, apu_, waitcnt_, keypad_),
      scheduler_(cpu_, memory_, interrupts_, timers_, dma_, ppu_, apu_, waitcnt_, bios_) {
  memory_.set_io_callbacks(io_callbacks(io_));
  scheduler_.set_io_registers(io_);
}

void CoreSession::configure_for_game_boot() {
  bios_.set_mode(BiosExecutionMode::hle);
  (void)cpu_.set_cpsr(0x0000001FU);
  cpu_.set_register(Arm7tdmi::kPc, 0x08000000U);
  cpu_.set_register(13, 0x03007F00U);
}

void CoreSession::reset() {
  cpu_.reset();
  memory_.reset();
  memory_.clear_game_pak_rom();
  memory_.clear_game_pak_save();
  memory_.set_io_callbacks(io_callbacks(io_));
  interrupts_.reset();
  timers_.reset();
  dma_.reset();
  ppu_.reset();
  apu_.reset();
  bios_.reset();
  keypad_.reset();
  waitcnt_.reset();
  io_.reset();
  scheduler_.reset_scheduler_cycles();
  scheduler_.set_io_registers(io_);
}

Arm7tdmi& CoreSession::cpu() {
  return cpu_;
}

const Arm7tdmi& CoreSession::cpu() const {
  return cpu_;
}

MemoryBus& CoreSession::memory() {
  return memory_;
}

const MemoryBus& CoreSession::memory() const {
  return memory_;
}

InterruptController& CoreSession::interrupts() {
  return interrupts_;
}

const InterruptController& CoreSession::interrupts() const {
  return interrupts_;
}

Timers& CoreSession::timers() {
  return timers_;
}

const Timers& CoreSession::timers() const {
  return timers_;
}

DmaController& CoreSession::dma() {
  return dma_;
}

PpuTiming& CoreSession::ppu() {
  return ppu_;
}

Apu& CoreSession::apu() {
  return apu_;
}

BiosController& CoreSession::bios() {
  return bios_;
}

const BiosController& CoreSession::bios() const {
  return bios_;
}

Keypad& CoreSession::keypad() {
  return keypad_;
}

const Keypad& CoreSession::keypad() const {
  return keypad_;
}

WaitStateControl& CoreSession::waitcnt() {
  return waitcnt_;
}

const WaitStateControl& CoreSession::waitcnt() const {
  return waitcnt_;
}

IoRegisters& CoreSession::io() {
  return io_;
}

const IoRegisters& CoreSession::io() const {
  return io_;
}

CoreScheduler& CoreSession::scheduler() {
  return scheduler_;
}

const CoreScheduler& CoreSession::scheduler() const {
  return scheduler_;
}

CoreSchedulerFetchStepResult CoreSession::step() {
  return scheduler_.step_from_pc();
}

CoreSchedulerRunResult CoreSession::run(std::uint32_t max_steps) {
  return scheduler_.run_from_pc(max_steps);
}

CoreSessionState CoreSession::save_state() const {
  MemoryBus memory_state = memory_;
  memory_state.clear_io_callbacks();
  return {cpu_, memory_state, interrupts_, timers_, dma_, ppu_, apu_, bios_, keypad_, waitcnt_,
          io_.save_state(), scheduler_.save_state()};
}

void CoreSession::load_state(const CoreSessionState& state) {
  cpu_ = state.cpu;
  memory_ = state.memory;
  memory_.set_io_callbacks(io_callbacks(io_));
  interrupts_ = state.interrupts;
  timers_ = state.timers;
  dma_ = state.dma;
  ppu_ = state.ppu;
  apu_ = state.apu;
  bios_ = state.bios;
  keypad_ = state.keypad;
  waitcnt_ = state.waitcnt;
  io_.load_state(state.io);
  scheduler_.load_state(state.scheduler);
  scheduler_.set_io_registers(io_);
}

std::uint64_t CoreSession::state_hash() const {
  StateHasher hasher;
  hasher.add_u64(cpu_.state_hash());
  hasher.add_u64(memory_.state_hash());
  hasher.add_u64(interrupts_.state_hash());
  hasher.add_u64(timers_.state_hash());
  hasher.add_u64(dma_.state_hash());
  hasher.add_u64(ppu_.state_hash());
  hasher.add_u64(apu_.state_hash());
  hasher.add_u8(static_cast<std::uint8_t>(bios_.mode()));
  hasher.add_u64(keypad_.state_hash());
  hasher.add_u64(waitcnt_.state_hash());
  hasher.add_u64(io_.state_hash());
  hasher.add_u64(scheduler_.state_hash());
  return hasher.value();
}

}  // namespace gba::core
