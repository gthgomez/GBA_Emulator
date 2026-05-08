# External Test Suite Baseline

Status: source downloaded and reproducibly built with official devkitPro Docker image
Date: 2026-05-08

This file records legally reviewable, reproducible external test material for measuring
where the engine stands against public GBA emulator evidence. It does not admit any
generated ROM binary as a fixture.

## Selected Suite

| Field | Value |
| --- | --- |
| Name | mGBA Game Boy Advance Test Suite |
| Upstream | `https://github.com/mgba-emu/suite` |
| Local path | `external/test-suites/mgba-suite` |
| License | MIT |
| Downloaded commit | `aac98dca785eaec3932af217aa658275737a8ed8` |
| Upstream documentation | mGBA forum thread "Game Boy Advance Test Suite" |

## Local Inventory

- `git ls-files` reports 55 tracked upstream files.
- The downloaded tree contains C, header, ARM assembly, `.grit`, shell, and BMP source
  asset files.
- A local scan found no `.gba`, `.gb`, `.gbc`, `.bin`, `.bios`, `.sav`, `.srm`,
  `.eep`, or `.fla` files.
- The suite root has `LICENSE` and `Makefile`; it has no README in this commit.

## Build Status

The Windows devkitPro graphical installer started and installed the MSYS2 base, but
stalled before `gba-dev` completed. The recovered build path uses the official
`devkitpro/devkitarm` Docker image instead of the half-installed Windows package
database.

| Field | Value |
| --- | --- |
| Docker image | `devkitpro/devkitarm:latest` |
| Image digest | `devkitpro/devkitarm@sha256:4debd5b33cf4361a557b6bf3be5ff823804868125ce1429912f1a4e773e7ac5d` |
| Build script | `tools/build-mgba-suite.ps1` |
| Build output | `build/test-suite-build/mgba-suite/suite.gba` |
| Generated size | 524,288 bytes |
| Generated SHA-256 | `073AC37DB89B791A589EC93853074043B31D0C931F43F4A69AFA7319248EC8BB` |
| Compiler | `arm-none-eabi-gcc (devkitARM) 15.2.0` |
| ROM fixer | `GBA ROM fixer v1.05` |
| Graphics tool | `grit v0.9.2` |
| Make | `GNU Make 4.3` |

Result: `suite.gba` is available as a generated local artifact under `build/`. It is
not a checked-in fixture.

## What This Measures Once Built

The mGBA public documentation thread describes this suite as covering memory, I/O read,
timing, DMA, BIOS math, video, shifter, carry, multiply, SIO, timers, and related
hardware behaviors. Those areas map directly to the roadmap gaps where our current
engine still has bounded local seeds rather than broad public-suite evidence.

## First Local Run

The first current-core run is recorded in `docs/mgba-suite-test-results.md`. The ROM
loads and begins executing, then stops after four attempted instructions at the first
startup IME write (`str r0, [r0, #520]` to `0x04000208`). This is useful baseline data,
not a compatibility pass.

## Admission Rules For Generated Output

Before `suite.gba` can be used as a checked-in fixture or public compatibility
artifact:

- Build it locally from the recorded source commit.
- Record the exact devkitARM/devkitPro versions and build command.
- Record a SHA-256 checksum for the generated binary.
- Confirm the generated binary contains no BIOS dump and no copyrighted game content.
- Keep pass/fail results separate from marketing or compatibility claims.

The current generated ROM may be used for local testing data collection only.
