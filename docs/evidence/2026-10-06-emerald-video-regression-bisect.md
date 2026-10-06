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


## CORRECTION (2026-10-06, later the same day) — bisect attribution withdrawn

The `git bisect` verdict above is **invalid** and `6785fcc` is **not** the regression commit:

- The bisect script swallowed configure/build failures (`>/dev/null 2>&1`, `|| true`) and the
  lab marker never verified the artifact's engine-commit stamp. Every commit older than the
  CMake build surface (PR #12) has no `apps/desktop`, so the build failed silently and the
  **stale binary from the prior bisect step** produced the verdict. Git consequently converged
  on `6785fcc`, which is merely the topological child of the bisect-good endpoint.

Direct per-commit builds (single `g++` invocation, no CMake, per-commit frame traces):

| Commit | Frame-28 framebuffer CRC (reference-good = `10599149`) |
| --- | --- |
| `19b1558` (bisect good endpoint) | `10599149` — good |
| `6785fcc` (falsely blamed) | `10599149` — **good; scheduler cycles match the reference exactly** |
| `0edf607` | `10599149` — good |
| `9755c99` | `1338142574` — **broken (real regression commit)** |

`9755c99` ("misc-edge HBlank flag start-cycle (investigation + groundwork)", PR #6) carried a
~420-line renderer rewrite plus PPU-timing latch changes; it is the sole source of both the
uniform-black symptom and the residual ±2–4 cycle/frame scheduler jitter. The "Experiments"
section above targeted DMA device advancement and is moot for the same reason.

### Actual root cause (proven by hybrid differential + mGBA source)

`window_layer_mask()` (`src/core/ppu_renderer.cpp`) applies `WINOUT & 0x3F` to the
outside-window region **even when no window is enabled in DISPCNT**, and the PPU latch resets
WININ/WINOUT to `0x0000`. Emerald never writes WINOUT, so every layer was masked and only the
backdrop rendered. mGBA — our reference — bypasses windowing entirely when all three DISPCNT
window-enable bits are clear (`video-software.c`: `windows[0].control.packed = 0xFF`) and does
not seed WININ/WINOUT at reset either (`io.c` `GBAIOInit`). The GBATEK "WININ/WINOUT reset =
0x3F3F" claim could not be confirmed against the primary source and is **not** implemented;
the mGBA-aligned bypass is the fix.

Fix: bypass windowing when all window-enable bits are clear (one guard in
`window_layer_mask()`), plus a regression test. Post-fix: frames 0–31 framebuffer CRCs match
the pre-regression core exactly; frame 20 CRC `335654572` and frame 300 CRC `1075455715`
match the superseded 2026-08-14 evidence values verbatim; 30/30 verifiers PASS; video-oracle
suite and determinism re-verified (see fix PR).
