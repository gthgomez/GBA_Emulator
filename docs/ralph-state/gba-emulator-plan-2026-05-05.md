# GBA Emulator Ralph State

Status: Phase 72 complete
Date: 2026-05-07
Scope: `Project_Android/GBA_Emulator`

## Completed Phase Evidence

- Phase 59: bounded save-protocol seed implemented for Flash ID/program/erase/bank
  switching, EEPROM explicit block read/write, and conservative save marker detection.
- Phase 60: bounded scheduled-DMA seed implemented for HBlank/VBlank/special triggers,
  bus-cycle metadata, and Direct Sound FIFO refill routing through the scheduler.
- Phase 61: bounded mode 0-2 renderer seed implemented for multi-BG priority, BG scroll,
  OBJ priority, simple WIN0 masking, and mosaic snap.
- Phase 62: bounded bitmap/blend renderer seed implemented for modes 3/4/5, mode 4
  palette/page behavior, forced blank, and brightness blending.
- Phase 63: bounded PSG/mixer seed implemented for square, wave, and noise generators
  mixed with Direct Sound and hashed in APU/core-session state.
- Phase 64: bounded keypad/input IO seed implemented for active-low KEYINPUT,
  KEYCNT-selected OR/AND keypad IRQ behavior, IO routing, and session hashing.
- Phase 65: bounded legal fixture corpus seed implemented for explicit byte fixtures,
  license/redistributability rejection, harness execution, and combined hashes.
- Phase 66: bounded versioned save-state codec seed implemented for the current public
  restorable core-session subset, with magic/version/corruption rejection.
- Phase 67: bounded predecoded-instruction cache seed implemented for ARM/Thumb
  operation classification and hit/miss accounting.
- Phase 68: bounded Android-facing opaque-handle bridge seed implemented for
  thread-safe create/destroy/reset/load-explicit-ROM/run/state-hash APIs.
- Phase 69: bounded Android-facing runtime seed implemented for explicit ROM loading,
  keypad mapping, framebuffer rendering, APU audio draining, frame stepping, and underrun
  reporting.
- Phase 70: bounded local Android performance/power gate seed implemented for average/
  p95 frame timing, missed-frame count, audio underruns, final hash, and conservative
  thermal observation.
- Phase 71: bounded release governance/legal review gate implemented for BIOS/ROM,
  fixture, trademark, screenshot, compatibility-language, privacy/data-safety, and
  telemetry controls.
- Phase 72: bounded controlled beta readiness gate implemented for RC checklist,
  regression commands, rollback/recovery, known issues, high risks, and roadmap
  requirement audit.

## Verification

- `tools/run-core-tests.ps1` from `C:\Workspace`: PASS on 2026-05-07, including
  keypad, compatibility corpus, save-state codec, instruction cache, and Android bridge
  verifiers, plus Android runtime and Android performance gate verifiers.
- `tools/run-core-benchmarks.ps1` from `C:\Workspace`: PASS on 2026-05-07.
- `tools/check-core-performance-regression.ps1` from `C:\Workspace`: PASS on
  2026-05-07. Note: one parallel benchmark/regression attempt failed with a Windows
  output-file lock on `build/core_benchmark.exe`; the serial rerun passed.
- `tools/check-release-readiness.ps1` from `C:\Workspace`: required before any release
  or beta claim; validates governance docs, asset gates, unsupported wording patterns,
  and explicit blocked production/beta audit status.

## Deferred

- Exact Flash/EEPROM timing, EEPROM serial bitstream, filesystem persistence, Android
  SAF, exact DMA arbitration, full PPU affine/window/color-effect behavior, PSG
  register-accurate sweep/length/envelope behavior, full save-state device sections,
  JNI/Gradle/CMake app integration, real OpenGL/Oboe upload/playback, Android lifecycle
  instrumentation, device thermal/power evidence, and any compatibility or
  production-readiness claims remain out of scope.

## Next Unlock

Phase 71: Release governance and legal review. Review BIOS/ROM wording, fixture
licenses, screenshots, compatibility language, privacy/data-safety implications, and any
telemetry/crash-reporting decision before external beta preparation.

Superseded: all roadmap phases now have bounded local artifacts. Next unlock is
out-of-scope device/platform evidence for the blockers in
`docs/roadmap-requirements-audit.md`.
