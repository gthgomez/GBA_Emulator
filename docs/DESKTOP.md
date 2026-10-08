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

The executable lands at `build\desktop\apps\desktop\gba-desktop.exe` and needs
`SDL3.dll` beside it (the smoke scripts copy it automatically). To produce a
self-contained portable ZIP — the exe, every non-system DLL it imports, and
license/attribution files — run:

```powershell
.\tools\package-windows-portable.ps1 -Version 0.1.0
# -> dist\gba-desktop-0.1.0-windows-x64.zip
```

Linux: install the SDL3 dev packages (or build pinned SDL3 from source, as CI
does), then `cmake -S . -B build/desktop -DCMAKE_BUILD_TYPE=Release` and
`cmake --build build/desktop`.

## Play mode

```powershell
.\build\desktop\apps\desktop\gba-desktop.exe            # opens a ROM file dialog
.\build\desktop\apps\desktop\gba-desktop.exe D:\ROMs\game.gba
```

Launching with no arguments opens a file picker instead of printing usage.
Paths containing spaces are fine (quote them in the shell as usual). The window
shows the engine framebuffer (nearest-neighbor, letterboxed, resizable, F11
fullscreen). Controls:

| Input | Action |
| --- | --- |
| Arrow keys / gamepad d-pad | D-pad |
| Z / X (gamepad B / A) | B / A |
| Enter / Backspace | Start / Select |
| A / S | R / L |
| Tab (hold) | Fast-forward |
| P or Space | Pause/resume |
| F5 / F8 | Save / load state (slot 1) |
| F9 | Reset (full re-boot; keeps cartridge save) |
| M | Mute |
| O | Open ROM dialog |
| Drag-and-drop | Open ROM |
| Esc | Quit |

Keyboard and gamepad input are aggregated from independent sources: an idle or
disconnected controller can never cancel a held key, presses from multiple
controllers are OR-combined, and losing window focus releases the keyboard mask
so no button stays stuck.

### Saves and data safety

- Cartridge saves persist to `<rom>.sav` next to the ROM (or in
  `--save-directory`), imported at boot, flushed on exit and every ~5 s of dirty
  frames. Games with no cartridge-backed save never create a `.sav`.
- Save states are `<rom>.state1` etc. (core `SaveStateCodec` v3 format). Because
  the codec snapshot is self-contained, the host additionally refuses a state
  that was captured from a different ROM.
- All save writes are replace-in-place safe: a sibling temp file is written and
  renamed over the destination, so a failed or interrupted write cannot truncate
  a good save. (This is crash-safe, not a power-loss durability guarantee.)
  A save found only as the `<rom>.sav.old` crash orphan is restored and
  re-imported on the next launch. The `.tmp`/`.old`/`.rejected` sibling names
  are owned by the host — do not use them for your own backups.
- Switching ROMs (dialog or drag-and-drop) flushes the outgoing cartridge save
  first and *refuses the switch* if that write fails, so unsaved progress can
  never be discarded; a rejected replacement ROM rolls the running session back
  rather than discarding it.
- Reset re-imports the current cartridge save, so in-game progress survives a
  console reset (save states are separate files).
- A corrupt/incompatible `.sav` is copied to `<rom>.sav.rejected` before the
  host starts fresh, so the original bytes are never destroyed. If even the
  backup cannot be created, the host goes fail-closed: cartridge save writes
  are disabled for the session (the window title shows
  `[save writes disabled]`) and the original file is left untouched.
- An empty machine snapshot is never written over a sized save file.
- Corrupt or wrong-ROM save states fail visibly on stderr and leave the running
  game untouched.

### Automation hooks (CI / scripting)

| Flag | Effect |
| --- | --- |
| `--quit-after N` | quit after N presented frames |
| `--reset-after N` | perform a full reset after N presented frames |
| `--switch-after N` + `--switch-to PATH` | switch to another ROM after N presented frames |
| `--window-screenshot PATH` | capture the presented window to a PNG |

## Headless lab mode (primary verification surface)

```powershell
.\build\desktop\apps\desktop\gba-desktop.exe --headless `
  --rom D:\ROMs\game.gba --frames 600 `
  --input-script .\local\scripts\menu.json `
  --screenshot-frame 60 --screenshot-output build\lab\shots `
  --frame-hash --audio-hash --state-hash `
  --artifact build\lab\run.json
```

Headless runs are never wall-clock throttled. Key flags: `--frames`,
`--max-steps-per-frame`, `--input-script` (JSON events `{frame, button, state}`),
`--screenshot-frame` (repeatable) + `--screenshot-output`, `--frame-hash` (CRC32s
sampled every 30 frames + final), `--audio-hash`, `--state-hash`,
`--save-state`/`--load-state` (round-trip verified), `--artifact` (versioned JSON,
schema_version 1).

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

## CI and local verification

`.github/workflows/ci.yml` runs a `desktop-host` job on Linux + Windows: pinned
SDL3, CMake Release build, then:

- `tools/run-desktop-smoke.{ps1,sh}` — synthetic legal ROM (never committed),
  two headless runs, asserts completion + bit-identical determinism.
- `tools/run-desktop-save-smoke.{ps1,sh}` — cartridge-save restart persistence,
  reset preservation, no spurious empty `.sav`, rejected-save preservation,
  fail-closed behaviour when a rejected save cannot be backed up, ROM-switch
  refusal while the save flush fails, defective-ROM switch surfacing exit 3
  with the outgoing save flushed, and crash-orphan (`.sav.old`) recovery.
- `tools/run-desktop-interactive-smoke.{ps1,sh}` — drives the *interactive* SDL
  host under the dummy video/audio drivers and asserts the captured window
  contains real (nonuniform) graphics, then checks that an undefined-instruction
  ROM exits non-zero.

The synthetic fixtures are generated by `tools/make-synthetic-rom.py`; PNG
content is checked by `tools/check-png-nonuniform.py`. No proprietary ROMs,
BIOS files, or private saves are used or committed.

## Known state

- The Pokemon Emerald uniform-black regression (issue #15) introduced by
  `6785fcc` was diagnosed in PR #14 and **fixed in PR #17** (PPU windowing
  bypass when no window is enabled). It is historical, not current.
- A separate engine defect was found while building the interactive fixture: a
  bare branch-to-self (`B #-8`) runs away instead of looping, while a
  branch-to-previous-instruction loop works. It is recorded in
  `docs/open-issues-status.md`; the synthetic fixtures use the working idiom.
- mGBA `misc-edge` and `timing` conformance gaps remain (see
  `docs/open-issues-status.md`); they do not block desktop play.

## Remaining limitations

- CI still verifies the interactive host under dummy video/audio drivers. A
  real Windows display/audio qualification was performed manually on the
  development machine (2026-10-08: windowed rendering at ~60 fps from the
  extracted portable ZIP, save/restart/switch/reset/orphan-recovery lifecycle,
  20-minute/72,000-frame sustained run with zero audio underruns); physical
  gamepad testing was not performed (no controller connected), and audible
  audio / hands-on keyboard qualification remains with the user.
- Save-state and cartridge-save behaviour is verified through synthetic fixtures
  and unit tests; no retail ROM compatibility is claimed.
- Reset-preserves-cartridge-save is implemented in the desktop host, because the
  core `CoreSession::reset()` clears the game pak. Any other host (e.g. Android)
  must apply the same policy to be equivalent.
- `--save-directory` keys saves on the ROM *file name*, so two different ROMs with
  the same file name in a shared save directory would share (and overwrite) one
  `.sav`. Use a distinct directory per title, or the default (save next to ROM).
- ROMs that contain more than one save-type marker (`detect_game_pak_save_type()`
  returns `nullopt`) are treated as having no cartridge save: the host neither
  loads nor writes a `.sav` for them.
- A persistently failing save write is retried at most once per flush interval,
  not continuously; if the destination stays unwritable the save is not persisted
  and the failure is reported on stderr.
