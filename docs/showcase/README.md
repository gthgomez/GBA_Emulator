# Desktop-host showcase evidence

Captured on 2026-10-30 from
[`f85025e253ae`](https://github.com/gthgomez/GBA_Emulator/commit/f85025e253ae)
with the capture-only wrapper added in this branch. No core/host behavior changed.

## What the image proves

[`build/showcase/desktop-test-pattern.png`](../../build/showcase/desktop-test-pattern.png)
is the **actual 720×480 SDL desktop-host render** of
the existing source-authored `video` fixture in
[`tools/make-synthetic-rom.py`](../../tools/make-synthetic-rom.py). Its ARM code
writes alternating red/blue pixels to GBA mode-3 VRAM, then idles. The host ran
30 presented frames using SDL's dummy video/audio drivers and its software
renderer, and captured the presented window through `--window-screenshot`.

The tracked PNG lives in the release gate’s existing `build/` generated-output
allowance. The capture wrapper writes there; no guard, extension rule, or
commercial-media restriction is changed. Only this exact generated PNG is
tracked, not build outputs, fixture binaries, or saves.

The PNG is checked for the exact synthetic bars, dimensions, and nonuniform
pixels. It demonstrates the core → framebuffer → SDL texture/present/capture
path. It is **not gameplay**, a physical desktop-display check, a licensed
mGBA conformance-suite result, an audio demonstration, or evidence of retail-ROM
compatibility. No video was added because this fixture's final image is static.

The 512-byte synthetic ROM is generated temporarily outside the repository and
removed after capture. It has no commercial game content or BIOS. No ROM, BIOS,
save file, or external homebrew binary is committed. The generator is already
part of the repository's desktop smoke tests; this pass adds no new ROM source
or legal-fixture admission.

## Reproduce

Build the host using [DESKTOP.md](../DESKTOP.md) with CMake and SDL3 3.4.18, then
run from the repository root:

```sh
./tools/capture-showcase.sh
```

For a differently located executable, set `GBA_DESKTOP_BIN=/path/to/gba-desktop`.
The wrapper runs the source-authored fixture and verifies the PNG. Regenerate
`provenance.json` after replacing media; old hashes certify only the old image.

Capture environment: Linux x86_64, GCC 14.2.0, CMake 4.4.4, Release build, SDL3
3.4.18 from its official release source. SDL was compiled with
`SDL_UNIX_CONSOLE_BUILD=ON`: the cloud environment lacks X11/Wayland development
headers, so this local SDL build supports the tested dummy/offscreen paths.
A normal graphical desktop build needs those platform dependencies.

## Checks executed for this source

- `./tools/run-core-tests.sh`: **36 tests PASS**, exit 0.
- `./tools/run-desktop-smoke.sh`: **PASS**, 120-frame deterministic headless runs plus an incomplete-run negative check.
- `./tools/run-desktop-save-smoke.sh`: **PASS**, including persistence/reset, save preservation, switch failure and save-type override checks.
- `./tools/run-desktop-interactive-smoke.sh`: **PASS**, real SDL dummy-window rendering, exact nonuniform test bars, and undefined-instruction negative check (host exit 3).
- `./tools/capture-showcase.sh`: **PASS**, 720×480 image with equal-share red/blue bars.

These checks do not supersede the README's conformance, timing, Android device,
controlled-beta, or physical-playtest limitations. No physical device, commercial
ROM, new mGBA run, or controlled-beta certification is claimed here.
