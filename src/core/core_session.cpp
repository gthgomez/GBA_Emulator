#include "gba/core/core_session.hpp"

#include "gba/core/state_hash.hpp"

namespace gba::core {

CoreSession::CoreSession()
    : cpu_(),
      memory_(),
      interrupts_(),
      timers_(),
      dma_(),
      ppu_(),
      apu_(),
      waitcnt_(),
      scheduler_(cpu_, memory_, interrupts_, timers_, dma_, ppu_, apu_, waitcnt_) {}

void CoreSession::reset() {
  cpu_.reset();
  memory_.reset();
  memory_.clear_game_pak_rom();
  memory_.clear_game_pak_save();
  interrupts_.reset();
  timers_.reset();
  dma_.reset();
  ppu_.reset();
  apu_.reset();
  waitcnt_.reset();
  scheduler_.reset_scheduler_cycles();
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

Timers& CoreSession::timers() {
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

WaitStateControl& CoreSession::waitcnt() {
  return waitcnt_;
}

CoreScheduler& CoreSession::scheduler() {
  return scheduler_;
}

CoreSchedulerFetchStepResult CoreSession::step() {
  return scheduler_.step_from_pc();
}

CoreSchedulerRunResult CoreSession::run(std::uint32_t max_steps) {
  return scheduler_.run_from_pc(max_steps);
}

CoreSessionState CoreSession::save_state() const {
  return {cpu_, memory_, interrupts_, timers_, dma_, ppu_, apu_, waitcnt_,
          scheduler_.save_state()};
}

void CoreSession::load_state(const CoreSessionState& state) {
  cpu_ = state.cpu;
  memory_ = state.memory;
  interrupts_ = state.interrupts;
  timers_ = state.timers;
  dma_ = state.dma;
  ppu_ = state.ppu;
  apu_ = state.apu;
  waitcnt_ = state.waitcnt;
  scheduler_.load_state(state.scheduler);
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
  hasher.add_u64(waitcnt_.state_hash());
  hasher.add_u64(scheduler_.state_hash());
  return hasher.value();
}

}  // namespace gba::core
