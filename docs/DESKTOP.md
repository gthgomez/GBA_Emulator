# Desktop host (`gba-desktop`)

One desktop executable, two modes, one engine. Both modes go through the same
platform-neutral `gba::core::EmulatorRuntime` facade (`include/gba/core/emulator_runtime.hpp`);
no emulation logic lives in the host.

## Build (Windows, MinGW-w64 g++)

```powershell
.\tools\setup-desktop-deps.ps1        # pinned SDL3 3.4.18 devel -> external\SDL3 (gitignored)
cmake -S . -B build/desktop -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="$PWD\external\SDL3\x86_64-w64-mingw32"
cmake --build build/desktop --config Release
```

Linux: install the SDL3 dev packages (or let `tools/run-desktop-smoke.sh`'s CI
recipe build pinned SDL3 from source), then `cmake -S . -B build/desktop` and
`cmake --build build/desktop`. Copy `SDL3.dll` beside the exe on Windows if it
is not already there (the smoke script does this automatically).

## Play mode

```powershell
.\build\desktop\apps\desktop\gba-desktop.exe D:\ROMs\game.gba
```

Window shows the engine framebuffer (nearest-neighbor, letterboxed, resizable,
F11 fullscreen). Controls:

| Input | Action |
| --- | --- |
| Arrow keys / gamepad d-pad | D-pad |
| Z / X (gamepad B / A) | B / A |
| Enter / Backspace | Start / Select |
| A / S | R / L |
| Tab (hold) | Fast-forward |
| P or Space | Pause/resume |
| F5 / F8 | Save / load state (slot 1) |
| F9 | Reset (full re-boot) |
| M | Mute |
| O | Open ROM dialog |
| Drag-and-drop | Open ROM |
| Esc | Quit |

Cartridge saves persist to `<rom>.sav` next to the ROM (imported at boot,
flushed on exit and every ~5 s of dirty frames). Save states are
`<rom>.sav.state1` (core `SaveStateCodec` v3 format). Incompatible or corrupt
save states fail visibly on stderr, never silently.

## Headless lab mode (primary verification surface)

```powershell
.\build\desktop\apps\desktop\gba-desktop.exe --headless `
  --rom D:\ROMs\game.gba --frames 600 --unthrottled `
  --input-script .\local\scripts\menu.json `
  --screenshot-frame 60 --screenshot-output build\lab\shots `
  --frame-hash --audio-hash --state-hash `
  --artifact build\lab\run.json
```

Key flags: `--frames`, `--max-steps-per-frame`, `--input-script` (JSON events
`{frame, button, state}`), `--screenshot-frame` (repeatable) +
`--screenshot-output`, `--frame-hash` (CRC32s sampled every 30 frames + final),
`--audio-hash`, `--state-hash`, `--save-state`/`--load-state` (round-trip
verified), `--artifact` (versioned JSON, schema_version 1).

The artifact records engine commit + build type, ROM sha256/size, stop reason,
unsupported-instruction/fetch-failure counters, framebuffer CRC32, audio
sample count/hash/underruns, final state hash, and wall-clock timing
(excluded from all hashes). Exit code is non-zero on unsupported/fetch/step
failures, so scripts and CI can gate on it.

Input script format:

```json
{
  "events": [
    { "frame": 60, "button": "START", "state": "down" },
    { "frame": 62, "button": "START", "state": "up" }
  ]
}
```

## CI

`.github/workflows/ci.yml` runs a `desktop-host` job on Linux + Windows:
pinned SDL3, CMake Release build, then `tools/run-desktop-smoke.ps1` /
`.sh`, which generate a synthetic legal ROM in `build/` (never committed),
run headless twice, and assert completion + bit-identical determinism.
No proprietary ROMs in CI.

## Known state

Real-game video rendering is currently broken on `main` for Pokemon Emerald
(uniform black since core commit `6785fcc`; timing suites unaffected). See
[issue #15](https://github.com/gthgomez/GBA_Emulator/issues/15) and
`docs/evidence/2026-10-06-emerald-video-regression-bisect.md`. The synthetic
ROM path used by CI is unaffected.
