#include "gba/core/core_session.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

#include "test_helpers.hpp"

int main() {
  using gba::core::Arm7tdmi;
  using gba::core::CoreSession;

  // Thumb encodings:
  // MOV Rd, #imm8: 001 00 Rd imm8
  // LSL Rd, Rs, #imm5: 000 00 imm5 Rs Rd
  // LDR Rd, [Rn, #0]: 0101 100 0 000 Rn Rd  (LDR Rd, [Rn, #0])
  // NOP: 0100 0111 0 000 0000 = 0x46C0

  // ==================================================================
  // Test 1: Thumb LDR from open bus — asymmetric halfword proof
  //
  // ROM layout:
  //   0: MOV R0, #0x0F          => R0 = 0x0F
  //   2: LSL R0, R0, #24        => R0 = 0x0F000000
  //   4: LDR R1, [R0, #0]       => open bus from 0x0F000000
  //   6: NOP
  //   8: 0x1234                  => pipeline fetch halfword
  //  10: 0xABCD                  => other half
  //
  // Thumb open bus: read16(PC+4) duplicated => 0x12341234
  // Wrong (read32): read32(PC+4) => 0xABCD1234
  // ==================================================================
  {
    CoreSession session;
    session.reset();
    session.bios().set_mode(gba::core::BiosExecutionMode::hle);

    std::vector<std::uint8_t> rom(256, 0);
    put_rom_halfword(rom, 0, 0x200FU);   // MOV R0, #0x0F
    put_rom_halfword(rom, 2, 0x0600U);   // LSL R0, R0, #24
    // LDR R1, [R0, #0]: 0101 100 0 000 000 001 = 0x6808
    put_rom_halfword(rom, 4, 0x6801U);   // LDR R1, [R0, #0]
    put_rom_halfword(rom, 6, 0x46C0U);   // NOP
    put_rom_halfword(rom, 8, 0x1234U);   // pipeline halfword
    put_rom_halfword(rom, 10, 0xABCDU);  // second half

    expect(session.memory().load_game_pak_rom(rom), "open bus ROM loads");
    session.cpu().set_register(Arm7tdmi::kPc, 0x08000000U);
    expect(session.cpu().set_cpsr(0x00000030U), "Thumb user mode");

    const auto run = session.run(4);
    std::cout << "  Thumb: executed=" << run.executed_steps
              << " R0=0x" << std::hex << session.cpu().register_value(0)
              << " R1=0x" << session.cpu().register_value(1) << std::dec << '\n';

    expect(run.executed_steps >= 3, "Thumb: executed >= 3");
    expect(session.cpu().register_value(0) == 0x0F000000U,
           "Thumb: R0 == 0x0F000000 (MOV+LSL worked)");

    const std::uint32_t r1 = session.cpu().register_value(1);
    std::cout << "  Thumb R1=0x" << std::hex << r1 << std::dec
              << " (expect 0x12341234 for halfword duplication)\n";
    // Thumb pipeline open bus: read16(PC+4) where PC=0x08000004+4=0x08000008
    // read16(0x08000008) = 0x1234, duplicated = 0x12341234
    expect(r1 == 0x12341234U,
           "Thumb open bus: halfword at PC+4 duplicated (NOT read32)");
  }

  // ARM open-bus behavior is verified by the existing arm7tdmi_test suite.
  // The critical proof here is the Thumb asymmetric halfword duplication.

  // ==================================================================
  // Test 2: No regression — Thumb LDR from valid memory
  // ==================================================================
  {
    CoreSession session;
    session.reset();
    session.bios().set_mode(gba::core::BiosExecutionMode::hle);

    std::vector<std::uint8_t> rom(256, 0);
    // MOV R0, #0x10 => R0 = 0x10
    put_rom_halfword(rom, 0, 0x2010U);
    // LDR R1, [R0, #0] where R0 = 0x10 (EWRAM, valid)
    put_rom_halfword(rom, 2, 0x6801U);  // LDR R1, [R0, #0]
    put_rom_halfword(rom, 4, 0x46C0U);  // NOP

    expect(session.memory().load_game_pak_rom(rom), "valid ROM loads");
    session.cpu().set_register(Arm7tdmi::kPc, 0x08000000U);
    expect(session.cpu().set_cpsr(0x00000030U), "Thumb user mode");

    const auto run = session.run(2);
    expect(run.executed_steps >= 2, "valid: executed >= 2");
    expect(session.cpu().register_value(0) == 0x10U, "valid: R0 == 0x10");
  }

  std::cout << "thumb_open_bus_asymmetric_test: PASS\n";
  return 0;
}
