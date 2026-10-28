#include "gba/core/core_session.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

#include "test_helpers.hpp"

int main() {
  using gba::core::Arm7tdmi;
  using gba::core::CoreSession;
  using gba::core::ExecuteStatus;
  using gba::core::GamePakSaveType;

  constexpr std::uint32_t kProgramBase = 0x08000000U;
  constexpr std::uint32_t kMovR0EwramFlag = 0xE3A00001U;
  constexpr std::uint32_t kArmSwiRegisterRamReset = 0xEF010000U;
  constexpr std::uint32_t kMovR0Zero = 0xE3A00000U;

  CoreSession session;
  std::vector<std::uint8_t> rom(16);
  write_word(rom, 0, kMovR0EwramFlag);
  write_word(rom, 4, kArmSwiRegisterRamReset);
  write_word(rom, 8, kMovR0Zero);
  write_word(rom, 12, kMovR0Zero);
  expect(session.memory().load_game_pak_rom(rom), "SWI boot test loads ROM");
  expect(session.memory().configure_game_pak_save(GamePakSaveType::none),
         "SWI boot test configures save");
  session.configure_for_game_boot();
  expect(session.memory().write8(0x02000000, 0xAB), "seed EWRAM before RegisterRamReset");
  expect(session.memory().read8(0x02000000).value_or(0) == 0xAB,
         "seeded EWRAM is readable");

  session.cpu().set_register(Arm7tdmi::kPc, kProgramBase);
  const gba::core::CoreSchedulerFetchStepResult mov = session.step();
  expect(!mov.fetch_failed && mov.step.has_value(), "mov r0 executes");
  expect(session.cpu().register_value(0) == 1, "R0 holds EWRAM reset flag");

  const gba::core::CoreSchedulerFetchStepResult swi = session.step();
  expect(!swi.fetch_failed && swi.step.has_value(), "RegisterRamReset SWI executes");
  expect(swi.step->cpu_step.status == ExecuteStatus::executed,
         "RegisterRamReset SWI is handled");
  expect(session.memory().read8(0x02000000).value_or(0xFF) == 0,
         "RegisterRamReset clears EWRAM");
  expect(session.memory().read8(0x08000000).has_value(), "ROM still mapped after reset");

  // Some games call the BIOS on a zero-filled header. A zero output size
  // completes without touching the destination, even if the type byte is zero.
  for (const std::uint32_t swi_number : {0x11U, 0x12U}) {
    CoreSession lz;
    std::vector<std::uint8_t> lz_rom(16);
    write_word(lz_rom, 0, 0xEF000000U | (swi_number << 16));
    expect(lz.memory().load_game_pak_rom(lz_rom), "zero-length LZ77 ROM loads");
    lz.configure_for_game_boot();
    constexpr std::uint32_t source = 0x02000000U;
    const std::uint32_t destination = swi_number == 0x11U ? 0x02000100U : 0x06000000U;
    expect(lz.memory().write32(source, 0), "zero-length LZ77 header");
    expect(lz.memory().write16(destination, 0xA55A), "seed LZ77 destination");
    lz.cpu().set_register(Arm7tdmi::kPc, kProgramBase);
    lz.cpu().set_register(0, source);
    lz.cpu().set_register(1, destination);
    const auto result = lz.step();
    expect(result.step.has_value() && result.step->cpu_step.status == ExecuteStatus::executed,
           "zero-length LZ77 completes without a signature");
    expect(lz.memory().read16(destination).value_or(0) == 0xA55A,
           "zero-length LZ77 leaves destination unchanged");
    expect(lz.cpu().register_value(Arm7tdmi::kPc) == kProgramBase + 4,
           "zero-length LZ77 advances past SWI");
    for (unsigned i = 0; i < 8 &&
         !lz.scheduler().save_state().hle_work_frames.empty(); ++i) {
      expect(lz.step().step.has_value(), "zero-length LZ77 call work advances");
    }
    expect(lz.scheduler().save_state().hle_work_frames.empty(),
           "zero-length LZ77 work finishes before next independent call");
    expect(lz.memory().write32(source, 0x00000100U), "invalid nonempty LZ77 header");
    lz.cpu().set_register(Arm7tdmi::kPc, kProgramBase);
    lz.cpu().set_register(0, source);
    lz.cpu().set_register(1, destination);
    const auto invalid = lz.step();
    expect(invalid.step.has_value() && invalid.step->cpu_step.status == ExecuteStatus::unsupported,
           "nonempty LZ77 still requires its signature");
    expect(lz.memory().read16(destination).value_or(0) == 0xA55A,
           "rejected LZ77 leaves destination unchanged");
  }

  {
    CoreSession affine;
    std::vector<std::uint8_t> affine_rom(16);
    write_word(affine_rom, 0, 0xEF0F0000U);
    expect(affine.memory().load_game_pak_rom(affine_rom), "ObjAffineSet ROM loads");
    affine.configure_for_game_boot();
    constexpr std::uint32_t source = 0x02000000U;
    constexpr std::uint32_t destination = 0x02000100U;
    expect(affine.memory().write16(source, 0x0100), "first affine X scale");
    expect(affine.memory().write16(source + 2, 0x0080), "first affine Y scale");
    expect(affine.memory().write16(source + 4, 0), "first affine angle");
    expect(affine.memory().write16(source + 6, 0x7777), "source alignment padding");
    expect(affine.memory().write16(source + 8, 0x0200), "second affine X scale");
    expect(affine.memory().write16(source + 10, 0x0100), "second affine Y scale");
    expect(affine.memory().write16(source + 12, 0), "second affine angle");
    for (std::uint32_t offset = 0; offset < 64; offset += 2) {
      expect(affine.memory().write16(destination + offset, 0x5555), "seed affine destination");
    }
    affine.cpu().set_register(Arm7tdmi::kPc, kProgramBase);
    affine.cpu().set_register(0, source);
    affine.cpu().set_register(1, destination);
    affine.cpu().set_register(2, 2);
    affine.cpu().set_register(3, 8);
    const auto result = affine.step();
    expect(result.step.has_value() && result.step->cpu_step.status == ExecuteStatus::executed,
           "ObjAffineSet handles two entries");
    constexpr std::uint16_t expected[] = {0x0100, 0, 0, 0x0080, 0x0200, 0, 0, 0x0100};
    for (std::uint32_t index = 0; index < 8; ++index) {
      expect(affine.memory().read16(destination + index * 8).value_or(0xFFFF) == expected[index],
             "ObjAffineSet writes each component at its configured stride");
      expect(affine.memory().read16(destination + index * 8 + 2).value_or(0) == 0x5555,
             "ObjAffineSet preserves gaps between components");
    }
    expect(affine.memory().write16(source, static_cast<std::uint16_t>(-256)) &&
               affine.memory().write16(source + 2, static_cast<std::uint16_t>(-128)) &&
               affine.memory().write16(source + 4, 0x2000),
           "ObjAffineSet seeds negative fractional scales");
    affine.cpu().set_register(Arm7tdmi::kPc, kProgramBase);
    affine.cpu().set_register(0, source);
    affine.cpu().set_register(1, destination);
    affine.cpu().set_register(2, 1);
    affine.cpu().set_register(3, 8);
    const auto fixed_result = affine.step();
    expect(fixed_result.step.has_value() &&
               fixed_result.step->cpu_step.status == ExecuteStatus::executed,
           "ObjAffineSet executes fractional negative scales");
    constexpr std::uint16_t fixed_expected[] = {0xFF4A, 182, 0xFFA5, 0xFFA5};
    for (std::uint32_t index = 0; index < 4; ++index) {
      expect(affine.memory().read16(destination + index * 8).value_or(0) == fixed_expected[index],
             "ObjAffineSet stores quantized signed coefficients at the requested stride");
      expect(affine.memory().read16(destination + index * 8 + 2).value_or(0) == 0x5555,
             "fractional ObjAffineSet retains stride gaps");
    }
  }

  {
    CoreSession affine;
    std::vector<std::uint8_t> affine_rom(16);
    write_word(affine_rom, 0, 0xEF0E0000U);
    expect(affine.memory().load_game_pak_rom(affine_rom), "BgAffineSet ROM loads");
    affine.configure_for_game_boot();
    constexpr std::uint32_t source = 0x02000000U;
    constexpr std::uint32_t destination = 0x02000100U;
    expect(affine.memory().write32(source, 100U << 8), "background origin X");
    expect(affine.memory().write32(source + 4, 200U << 8), "background origin Y");
    expect(affine.memory().write16(source + 8, 10), "background center X");
    expect(affine.memory().write16(source + 10, 20), "background center Y");
    expect(affine.memory().write16(source + 12, 0x0100), "background X scale");
    expect(affine.memory().write16(source + 14, 0x0100), "background Y scale");
    expect(affine.memory().write16(source + 16, 0), "background angle");
    expect(affine.memory().write32(source + 20, 0xFFFFCE00U), "second background origin X");
    expect(affine.memory().write32(source + 24, 0x00003200U), "second background origin Y");
    expect(affine.memory().write16(source + 28, 0), "second background center X");
    expect(affine.memory().write16(source + 30, 0), "second background center Y");
    expect(affine.memory().write16(source + 32, 0x0080), "second background X scale");
    expect(affine.memory().write16(source + 34, 0x0100), "second background Y scale");
    expect(affine.memory().write16(source + 36, 0), "second background angle");
    affine.cpu().set_register(Arm7tdmi::kPc, kProgramBase);
    affine.cpu().set_register(0, source);
    affine.cpu().set_register(1, destination);
    affine.cpu().set_register(2, 2);
    const auto result = affine.step();
    expect(result.step.has_value() && result.step->cpu_step.status == ExecuteStatus::executed,
           "BgAffineSet handles two entries");
    expect(affine.memory().read16(destination).value_or(0) == 0x0100 &&
               affine.memory().read16(destination + 6).value_or(0) == 0x0100,
           "BgAffineSet stores identity matrix");
    expect(affine.memory().read32(destination + 8).value_or(0) == (90U << 8) &&
               affine.memory().read32(destination + 12).value_or(0) == (180U << 8),
           "BgAffineSet calculates translated reference point");
    expect(affine.memory().read16(destination + 16).value_or(0) == 0x0080 &&
               affine.memory().read16(destination + 22).value_or(0) == 0x0100 &&
               affine.memory().read32(destination + 24).value_or(0) == 0xFFFFCE00U &&
               affine.memory().read32(destination + 28).value_or(0) == 0x00003200U,
           "BgAffineSet advances 20-byte source and 16-byte destination entries");
    expect(affine.memory().write16(source + 14, 128) &&
               affine.memory().write16(source + 16, 0x30FF),
           "BgAffineSet seeds fractional rotation and unequal scales");
    affine.cpu().set_register(Arm7tdmi::kPc, kProgramBase);
    affine.cpu().set_register(0, source);
    affine.cpu().set_register(1, destination);
    affine.cpu().set_register(2, 1);
    const auto fixed_result = affine.step();
    expect(fixed_result.step.has_value() &&
               fixed_result.step->cpu_step.status == ExecuteStatus::executed,
           "BgAffineSet executes fractional rotation");
    expect(affine.memory().read16(destination).value_or(0) == 97 &&
               affine.memory().read16(destination + 2).value_or(0) == 0xFF14 &&
               affine.memory().read16(destination + 4).value_or(0) == 118 &&
               affine.memory().read16(destination + 6).value_or(0) == 48,
           "BgAffineSet stores the shared quantized matrix");
    expect(affine.memory().read32(destination + 8).value_or(0) == 29350 &&
               affine.memory().read32(destination + 12).value_or(0) == 49060,
           "BgAffineSet translates using quantized integer coefficients");

    // A coefficient can be +32768 before the halfword store wraps it to
    // -32768. Translation must consume the original full-width value.
    struct OverflowCase {
      std::uint16_t angle;
      std::uint16_t coefficients[4];
      std::uint32_t reference_x;
      std::uint32_t reference_y;
    };
    constexpr OverflowCase overflow_cases[] = {
        {0x8000, {0x8000, 0, 0, 0x8000}, 0xFFFF8000U, 0xFFFF8000U},
        {0x4000, {0, 0x8000, 0x8000, 0}, 0xFFFF8000U, 0x00008000U},
        {0xC000, {0, 0x8000, 0x8000, 0}, 0x00008000U, 0xFFFF8000U},
    };
    expect(affine.memory().write32(source, 0) &&
               affine.memory().write32(source + 4, 0) &&
               affine.memory().write16(source + 8, 1) &&
               affine.memory().write16(source + 10, 1) &&
               affine.memory().write16(source + 12, 0x8000) &&
               affine.memory().write16(source + 14, 0x8000),
           "BgAffineSet seeds signed-scale overflow cases");
    for (const auto& test : overflow_cases) {
      expect(affine.memory().write16(source + 16, test.angle),
             "BgAffineSet seeds cardinal overflow angle");
      affine.cpu().set_register(Arm7tdmi::kPc, kProgramBase);
      affine.cpu().set_register(0, source);
      affine.cpu().set_register(1, destination);
      affine.cpu().set_register(2, 1);
      const auto overflow_result = affine.step();
      expect(overflow_result.step.has_value() &&
                 overflow_result.step->cpu_step.status == ExecuteStatus::executed,
             "BgAffineSet executes overflow case");
      for (std::uint32_t component = 0; component < 4; ++component) {
        expect(affine.memory().read16(destination + component * 2).value_or(1) ==
                   test.coefficients[component],
               "BgAffineSet narrows only stored coefficients");
      }
      expect(affine.memory().read32(destination + 8).value_or(0) == test.reference_x &&
                 affine.memory().read32(destination + 12).value_or(0) == test.reference_y,
             "BgAffineSet translates before halfword narrowing");
    }
  }

  std::cout << "hle_swi_boot_test: PASS\n";
  return 0;
}
