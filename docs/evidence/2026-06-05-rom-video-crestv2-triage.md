# crestv2 ROM triage (2026-06-05)

Workstation: `run-rom-video-smoke.ps1` with `Pokemon - Emerald Version (USA, Europe) [crestv2].gba`

## Header validation

| Check | Standard Emerald | crestv2 |
| --- | --- | --- |
| ROM loads via `load_rom` | yes | yes |
| Header complement (`RomValidator`) | valid | valid |
| Cartridge title/game code | matches retail | matches retail |

The crestv2 dump is **not** rejected by header validation; failure is runtime/video-gate, not corrupt header bytes.

## Observed divergence (frames 60–215)

| Frame | cycles_delta | final_pc | unique_colors | notes |
| --- | --- | --- | --- | --- |
| 8 | ~281k | 0x083794EA | 1 | uniform backdrop |
| 60 | ~478k | **0x18** | 5 | ~1.7× normal frame budget; PC in BIOS vector region |
| 120 | ~478k | **0x18** | 5 | stuck |
| 215 | ~281k | 0x080071A6 | 1 | uniform black despite DISPCNT 0x1F40 |

## Assessment

- **Standard USA/Europe dump:** PASS at frame 215 (`unique_colors >= 8`) after SWI 0x13+ HLE; frame-200 palette dip remains visible in checkpoints but recovers by 215.
- **crestv2 dump:** FAIL — early PC `0x18` stall and black framebuffer at 215 indicate a **variant-specific boot path** (likely patched intro or different code entry), not a generic header/complement problem.
- **Recommendation:** Use standard Emerald for ROM smoke gates and device soak; track crestv2 separately if the dump source is confirmed legal and intentionally different.

## Workstation command

```powershell
cd GBA_Emulator
.\tools\run-rom-video-smoke.ps1 `
  -RomPath "..\local\test-roms\Pokemon - Emerald Version (USA, Europe) [crestv2].gba" `
  -Frames 216 -RequireValidHeader
```
