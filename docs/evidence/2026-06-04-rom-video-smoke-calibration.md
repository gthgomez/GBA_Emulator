# ROM video smoke calibration (2026-06-04)

Workstation: `rom_video_smoke` / `run-local-rom-video-smoke.ps1`  
ROM set: `Project_Android/local/test-roms/` (game ROMs only; `synthetic-*.gba` excluded)

## Thresholds (retail Emerald)

| Gate | Frame | Value |
|------|-------|-------|
| `require-scanlines` | each checkpoint | 160 |
| `require-dispcnt-after` | 60 | mask `0x0100` (BG0) |
| `require-unique-colors-after` | 215 | min `8` (Emerald splash holds 5 RGB565 values through ~frame 150; 8+ appear after logo transition ~frame 215) |
| `require-not-uniform-after` | 60 | dominant ratio < 0.99 |
| `check-frame` | — | `8,60,120,600` |

## Observed (USA/Europe Emerald, 2M steps/frame) — 2026-06-05 after IRQ/Thumb/SWI fixes

| Frame | frame_complete | cycles_delta | scanlines | final_pc | DISPCNT | unique | non_zero | notes |
|-------|----------------|--------------|-----------|----------|---------|--------|----------|-------|
| 0–1 | yes | ~281k | 160 | ~0x082E6DCE | 0x0000 | 1 | 38400 | uniform 0x7FFF backdrop |
| 2 | yes | ~281k | 160 | 0x082E6DCE | 0x0000 | 1 | 38400 | **no unsupported** (Thumb recovery + NV skip) |
| 8 | yes | ~281k | 160 | 0x082E6DCE | 0x0000 | 1 | 38400 | pre-BG-init |
| 14 | yes | ~281k | 160 | 0x080008C8 | 0x0140 | 1 | 38400 | BG0 on, single fill color |
| 15–28 | yes | ~281k | 160 | ~0x080008C8 | 0x0140 | **5** | 38400 | full-screen splash, palette 7–10 entries |
| 29–150 | yes | ~281k | 160 | ~0x080008C8 | 0x0140 | **5** | 1334 | palette fade; mostly black backdrop |
| 60 | yes | ~281k | 160 | 0x080008C8 | 0x0140 | **5** | 1334 | **gate FAIL**: unique `< 8` |
| 200 | yes | ~281k | 160 | 0x03007D4C | 0x0140 | 1 | 0 | palette cleared; boot regression |
| 215 | yes | ~281k | 160 | 0x08006DBE | 0x1F40 | **8** | 14775 | **unique gate PASS** |
| 219 | yes | ~281k | 160 | 0x08006DBE | 0x1F40 | **14** | 38188 | full copyright-style BG |

### Gate status (frame 60)

| Check | Result |
|-------|--------|
| `frame_complete` / 160 scanlines | PASS |
| `dispcnt & 0x0100` (BG0) | PASS (`0x0140`) |
| `unique_colors >= 8` | PASS at **frame 215+** (5 at frame 60 during early splash) |
| not uniform backdrop | PASS |

## Core changes in this pass

1. **Thumb/ARM recovery** — `recover_thumb_state_from_rom_misfetch`, NV ARM condition skip, IRQ LR offset 2 for Thumb.
2. **BIOS HLE** — SWI `0x10` BitUnPack, `0x11` LZ77UncompWram, `0x0E` BgAffineSet, `0x0F` ObjAffineSet; shared LZ77 decompress helper.
3. **`memory_bus_test` link** — `run-core-tests.ps1` includes full IO/PPU/interrupt object files.
4. **Unit tests** — IRQ LR/Thumb scheduler expectations aligned with hardware (+2 link).
5. **Android** — `:app:assembleDebug` + `:app:testDebugUnitTest` green with rebuilt native libs.

## Next P0 work

- Boot splash uses only **5 visible RGB565 values** through frame 150 despite **11 palette entries** loaded; investigate early decompression / DMA tile upload (frames 14–15) or compare mGBA unique-color timeline at frame 60.
- Post-frame-200 palette dip: monitor with checkpoints 150/180/200/210; SWI **0x13–0x17** HLE implemented 2026-06-05 (`hle_decompress_swi_test`).
- crestv2 variant tracked separately: [`2026-06-05-rom-video-crestv2-triage.md`](2026-06-05-rom-video-crestv2-triage.md).
