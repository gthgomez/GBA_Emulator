// Native headless lock for the mGBA `io-read` suite contract.
//
// The external mGBA suite proves this by executing a Thumb `ldrh` and letting
// the ARM7TDMI open-bus path supply the value for write-only/INVALID IO
// registers. That suite needs a devkitARM-built ROM and is therefore not part
// of the local core verifier set. This test replays the exact same table
// (src/io-read.c, pinned suite aac98dca785eaec3932af217aa658275737a8ed8)
// against the in-memory core so the contract cannot silently regress.
//
// Layout of the synthetic Thumb routine at 0x08000000:
//   0: LDRH R1, [R0, #0]   (0x8801)
//   2: B +2                (0xE001)
//   4: 0xDEAD              (low half of the open-bus word at PC+4)
//   6: 0xDEAD              (high half of the open-bus word at PC+4)
// At the execute cycle of the LDRH the Thumb pipeline open bus reads the word
// at PC+4, so any register that does not drive the data bus returns 0xDEAD.

#include "gba/core/core_session.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

struct IoReadCase {
  const char* name;
  std::uint32_t address;
  std::uint16_t expected;
};

// Mirrors src/io-read.c `ioReadTests` (storeFirst is true for every entry).
constexpr std::array<IoReadCase, 130> kIoReadCases{{
    {"BG0CNT", 0x04000008U, 0xDFFFU},
    {"BG1CNT", 0x0400000AU, 0xDFFFU},
    {"BG2CNT", 0x0400000CU, 0xFFFFU},
    {"BG3CNT", 0x0400000EU, 0xFFFFU},
    {"BG0HOFS", 0x04000010U, 0xDEADU},
    {"BG0VOFS", 0x04000012U, 0xDEADU},
    {"BG1HOFS", 0x04000014U, 0xDEADU},
    {"BG1VOFS", 0x04000016U, 0xDEADU},
    {"BG2HOFS", 0x04000018U, 0xDEADU},
    {"BG2VOFS", 0x0400001AU, 0xDEADU},
    {"BG3HOFS", 0x0400001CU, 0xDEADU},
    {"BG3VOFS", 0x0400001EU, 0xDEADU},
    {"BG2PA", 0x04000020U, 0xDEADU},
    {"BG2PB", 0x04000022U, 0xDEADU},
    {"BG2PC", 0x04000024U, 0xDEADU},
    {"BG2PD", 0x04000026U, 0xDEADU},
    {"BG2X_LO", 0x04000028U, 0xDEADU},
    {"BG2X_HI", 0x0400002AU, 0xDEADU},
    {"BG2Y_LO", 0x0400002CU, 0xDEADU},
    {"BG2Y_HI", 0x0400002EU, 0xDEADU},
    {"BG3PA", 0x04000030U, 0xDEADU},
    {"BG3PB", 0x04000032U, 0xDEADU},
    {"BG3PC", 0x04000034U, 0xDEADU},
    {"BG3PD", 0x04000036U, 0xDEADU},
    {"BG3X_LO", 0x04000038U, 0xDEADU},
    {"BG3X_HI", 0x0400003AU, 0xDEADU},
    {"BG3Y_LO", 0x0400003CU, 0xDEADU},
    {"BG3Y_HI", 0x0400003EU, 0xDEADU},
    {"WIN0H", 0x04000040U, 0xDEADU},
    {"WIN1H", 0x04000042U, 0xDEADU},
    {"WIN0V", 0x04000044U, 0xDEADU},
    {"WIN1V", 0x04000046U, 0xDEADU},
    {"WININ", 0x04000048U, 0x3F3FU},
    {"WINOUT", 0x0400004AU, 0x3F3FU},
    {"MOSAIC", 0x0400004CU, 0xDEADU},
    {"INVALID (4E)", 0x0400004EU, 0xDEADU},
    {"BLDCNT", 0x04000050U, 0x3FFFU},
    {"BLDALPHA", 0x04000052U, 0x1F1FU},
    {"BLDY", 0x04000054U, 0xDEADU},
    {"INVALID (56)", 0x04000056U, 0xDEADU},
    {"INVALID (58)", 0x04000058U, 0xDEADU},
    {"INVALID (5A)", 0x0400005AU, 0xDEADU},
    {"INVALID (5C)", 0x0400005CU, 0xDEADU},
    {"INVALID (5E)", 0x0400005EU, 0xDEADU},
    {"SOUND1CNT_LO", 0x04000060U, 0x007FU},
    {"SOUND1CNT_HI", 0x04000062U, 0xFFC0U},
    {"SOUND1CNT_X", 0x04000064U, 0x4000U},
    {"INVALID (66)", 0x04000066U, 0x0000U},
    {"SOUND2CNT_LO", 0x04000068U, 0xFFC0U},
    {"INVALID (6A)", 0x0400006AU, 0x0000U},
    {"SOUND2CNT_HI", 0x0400006CU, 0x4000U},
    {"INVALID (6E)", 0x0400006EU, 0x0000U},
    {"SOUND3CNT_LO", 0x04000070U, 0x00E0U},
    {"SOUND3CNT_HI", 0x04000072U, 0xE000U},
    {"SOUND3CNT_X", 0x04000074U, 0x4000U},
    {"INVALID (76)", 0x04000076U, 0x0000U},
    {"SOUND4CNT_LO", 0x04000078U, 0xFF00U},
    {"INVALID (7A)", 0x0400007AU, 0x0000U},
    {"SOUND4CNT_HI", 0x0400007CU, 0x40FFU},
    {"INVALID (7E)", 0x0400007EU, 0x0000U},
    {"SOUNDCNT_LO", 0x04000080U, 0xFF77U},
    {"SOUNDCNT_HI", 0x04000082U, 0x770FU},
    {"SOUNDCNT_X", 0x04000084U, 0x0080U},
    {"INVALID (86)", 0x04000086U, 0x0000U},
    {"INVALID (8A)", 0x0400008AU, 0x0000U},
    {"INVALID (8C)", 0x0400008CU, 0xDEADU},
    {"INVALID (8E)", 0x0400008EU, 0xDEADU},
    {"WAVE_RAM_0", 0x04000090U, 0xFFFFU},
    {"WAVE_RAM_1", 0x04000092U, 0xFFFFU},
    {"WAVE_RAM_2", 0x04000094U, 0xFFFFU},
    {"WAVE_RAM_3", 0x04000096U, 0xFFFFU},
    {"WAVE_RAM_4", 0x04000098U, 0xFFFFU},
    {"WAVE_RAM_5", 0x0400009AU, 0xFFFFU},
    {"WAVE_RAM_6", 0x0400009CU, 0xFFFFU},
    {"WAVE_RAM_7", 0x0400009EU, 0xFFFFU},
    {"FIFO_A_LO", 0x040000A0U, 0xDEADU},
    {"FIFO_A_HI", 0x040000A2U, 0xDEADU},
    {"FIFO_B_LO", 0x040000A4U, 0xDEADU},
    {"FIFO_B_HI", 0x040000A6U, 0xDEADU},
    {"INVALID (A8)", 0x040000A8U, 0xDEADU},
    {"INVALID (AA)", 0x040000AAU, 0xDEADU},
    {"INVALID (AC)", 0x040000ACU, 0xDEADU},
    {"INVALID (AE)", 0x040000AEU, 0xDEADU},
    {"DMA0SAD_LO", 0x040000B0U, 0xDEADU},
    {"DMA0SAD_HI", 0x040000B2U, 0xDEADU},
    {"DMA0DAD_LO", 0x040000B4U, 0xDEADU},
    {"DMA0DAD_HI", 0x040000B6U, 0xDEADU},
    {"DMA0CNT_LO", 0x040000B8U, 0x0000U},
    {"DMA0CNT_HI", 0x040000BAU, 0xF7E0U},
    {"DMA1SAD_LO", 0x040000BCU, 0xDEADU},
    {"DMA1SAD_HI", 0x040000BEU, 0xDEADU},
    {"DMA1DAD_LO", 0x040000C0U, 0xDEADU},
    {"DMA1DAD_HI", 0x040000C2U, 0xDEADU},
    {"DMA1CNT_LO", 0x040000C4U, 0x0000U},
    {"DMA1CNT_HI", 0x040000C6U, 0xF7E0U},
    {"DMA2SAD_LO", 0x040000C8U, 0xDEADU},
    {"DMA2SAD_HI", 0x040000CAU, 0xDEADU},
    {"DMA2DAD_LO", 0x040000CCU, 0xDEADU},
    {"DMA2DAD_HI", 0x040000CEU, 0xDEADU},
    {"DMA2CNT_LO", 0x040000D0U, 0x0000U},
    {"DMA2CNT_HI", 0x040000D2U, 0xF7E0U},
    {"DMA3SAD_LO", 0x040000D4U, 0xDEADU},
    {"DMA3SAD_HI", 0x040000D6U, 0xDEADU},
    {"DMA3DAD_LO", 0x040000D8U, 0xDEADU},
    {"DMA3DAD_HI", 0x040000DAU, 0xDEADU},
    {"DMA3CNT_LO", 0x040000DCU, 0x0000U},
    {"DMA3CNT_HI", 0x040000DEU, 0xFFE0U},
    {"INVALID (E0)", 0x040000E0U, 0xDEADU},
    {"INVALID (E2)", 0x040000E2U, 0xDEADU},
    {"INVALID (E4)", 0x040000E4U, 0xDEADU},
    {"INVALID (E6)", 0x040000E6U, 0xDEADU},
    {"INVALID (E8)", 0x040000E8U, 0xDEADU},
    {"INVALID (EA)", 0x040000EAU, 0xDEADU},
    {"INVALID (EC)", 0x040000ECU, 0xDEADU},
    {"INVALID (EE)", 0x040000EEU, 0xDEADU},
    {"INVALID (F0)", 0x040000F0U, 0xDEADU},
    {"INVALID (F2)", 0x040000F2U, 0xDEADU},
    {"INVALID (F4)", 0x040000F4U, 0xDEADU},
    {"INVALID (F6)", 0x040000F6U, 0xDEADU},
    {"INVALID (F8)", 0x040000F8U, 0xDEADU},
    {"INVALID (FA)", 0x040000FAU, 0xDEADU},
    {"INVALID (FC)", 0x040000FCU, 0xDEADU},
    {"INVALID (FE)", 0x040000FEU, 0xDEADU},
    {"INVALID (136)", 0x04000136U, 0x0000U},
    {"INVALID (142)", 0x04000142U, 0x0000U},
    {"INVALID (15A)", 0x0400015AU, 0x0000U},
    {"INVALID (206)", 0x04000206U, 0x0000U},
    {"INVALID (20A)", 0x0400020AU, 0x0000U},
    {"INVALID (302)", 0x04000302U, 0x0000U},
    {"INVALID (100C)", 0x0400100CU, 0xDEADU},
}};

void put_rom_halfword(std::vector<std::uint8_t>& rom, std::size_t offset,
                      std::uint16_t value) {
  rom.at(offset) = static_cast<std::uint8_t>(value & 0xFFU);
  rom.at(offset + 1) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

std::uint16_t read_io_register(std::uint32_t address) {
  using gba::core::Arm7tdmi;
  using gba::core::CoreSession;

  CoreSession session;
  session.reset();
  session.bios().set_mode(gba::core::BiosExecutionMode::hle);

  std::vector<std::uint8_t> rom(64, 0);
  put_rom_halfword(rom, 0, 0x8801U);  // LDRH R1, [R0, #0]
  put_rom_halfword(rom, 2, 0xE001U);  // B +2 (skip the planted open-bus word)
  put_rom_halfword(rom, 4, 0xDEADU);
  put_rom_halfword(rom, 6, 0xDEADU);
  expect(session.memory().load_game_pak_rom(rom), "open-bus ROM loads");

  // runIOReadSuite() turns the master sound circuit on before the table so the
  // SOUNDCNT_L/H stores are accepted.
  expect(session.memory().write16(0x04000084U, 0x0080U), "master sound enable");

  // The suite stores 0xFFFF before every read.
  expect(session.memory().write16(address, 0xFFFFU), "IO store-first write routes");

  session.cpu().set_register(0, address);
  session.cpu().set_register(1, 0);
  session.cpu().set_register(Arm7tdmi::kPc, 0x08000000U);
  expect(session.cpu().set_cpsr(0x00000030U), "Thumb user mode");
  static_cast<void>(session.run(1));

  return static_cast<std::uint16_t>(session.cpu().register_value(1) & 0xFFFFU);
}

}  // namespace

int main() {
  std::size_t failures = 0;
  for (const IoReadCase& test : kIoReadCases) {
    const std::uint16_t value = read_io_register(test.address);
    if (value != test.expected) {
      ++failures;
      std::cerr << "FAIL: " << test.name << " got 0x" << std::hex << value
                << " expected 0x" << test.expected << std::dec << '\n';
    }
  }

  if (failures != 0) {
    std::cerr << "io_read_open_bus_test: FAIL (" << failures << " of "
              << kIoReadCases.size() << ")\n";
    return 1;
  }

  std::cout << "io_read_open_bus_test: PASS (" << kIoReadCases.size()
            << " cases)\n";
  return 0;
}
