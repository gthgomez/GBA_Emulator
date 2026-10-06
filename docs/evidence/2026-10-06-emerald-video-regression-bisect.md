# Emerald real-game video regression — bisected to `6785fcc`

Date: 2026-10-06
Detector: `gba-desktop --headless` lab (PR #12/#13); confirmed independently with the
repo's own `rom_video_smoke.exe`.

## Symptom

`Pokemon - Emerald Version (USA, Europe)` (user-owned local dump, local-only) renders
uniform black from ~frame 30 onward on current `main` (`78a3951`). The game keeps
running: no unsupported instructions, no fetch failures, all frames complete,
DISPCNT follows the historical timeline exactly (`0x140` @ frame 60, `0x1F40` @
frame 215), audio samples are produced. Only the framebuffer is empty
(`non_zero_pixel_count == 0`, palette never populated — the BIOS logo decompress
output is not landing where the renderer sees it).

All 30 core verifiers and the 11 green mGBA suites still pass, so the regression
is only observable on real-game content — exactly the gap the desktop lab was
built to close.

## Bisect

`git bisect start main a36ebb19` (last render-verified revision per the
2026-08-14 evidence), marker: framebuffer CRC at frame 300 != uniform black
`901377804`, via `gba-desktop --headless --frames 301 --frame-hash`.

**First bad commit: `6785fcc`** — "hardware-timing Phase 1 — DMA master time,
PPU event-aware advancement, save-state determinism".

## Superseding evidence

The 2026-08-14 evidence (`2026-08-14-rom-video-pokemon-emerald-1500f.md`) was
recorded on a pre-`6785fcc` core. Its checkpoints (logo `non_zero_pixel_count`
1334 @ frame 60, rich content @ 215–1200) do NOT reproduce on current main;
`rom_video_smoke.exe --check-frame 8,60,120,150,180,215,300` on main reproduces
the lab numbers exactly. The old evidence is preserved but is now historical to
the pre-Phase-1 core.

## Experiments tried (all on `main` + targeted edits, all still black)

1. Suppress PPU advancement through DMA bus time (drain loop) — black.
2. Suppress timers/IO/APU advancement through DMA bus time — black.
3. Reintroduce the removed `immediate_dma_bus_visible_to_timers` fetch-region
   gate at both per-step DMA advance sites — black.
4. Substitution of pre-`6785fcc` `core_scheduler.cpp` + `arm7tdmi.cpp` does not
   compile against current headers (state-struct changes), so file-level
   substitution needs header reconciliation — not attempted.

The divergence is therefore somewhere in the Phase-1 timing/step accounting
(DMA bus-time device advancement and/or Thumb misfetch-recovery/prefetch-bubble
changes), manifesting as game-flow divergence before frame 30 while DISPCNT and
audio still track the expected timeline.

## Recommended fix path

This is the flagship case for the planned §14 PPU/render-latch architecture
work AND a timing-flow investigation. Bounded first step: diff per-frame
`state_hash` + PC traces between an `a36ebb19` build and current `main` on the
first 30 frames to find the first divergent frame and the exact IO/DMA event
sequence, then decide whether Phase-1 needs a corrected invariant or whether
per-scanline rendering should land first.

## Repro

```powershell
.\build\desktop\apps\desktop\gba-desktop.exe --headless `
  --rom "..\local\test-roms\Pokemon - Emerald Version (USA, Europe).gba" `
  --frames 301 --max-steps-per-frame 2000000 --frame-hash `
  --artifact build\lab\emerald-repro.json
# frame_hashes: frame 0 == 1788817766 (matches historical), then 901377804 (uniform black) through 1500
```

And with the repo's own harness:

```powershell
.\build\rom_video_smoke.exe --rom "..\local\test-roms\Pokemon - Emerald Version (USA, Europe).gba" `
  --frames 300 --max-steps-per-frame 2000000 --check-frame 8,60,120,150,180,215,300 --json
```
