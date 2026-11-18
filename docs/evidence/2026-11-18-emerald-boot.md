# Evidence: Local Retail Boot Smoke — Pokemon Emerald

Date: 2026-11-18
Scope: single user-supplied retail ROM boot smoke, local only, non-redistributed.
Owner-authorized narrow screenshot exception for portfolio evidence.

## Source material

| Item | Value |
| --- | --- |
| ROM | `Pokemon - Emerald Version (USA, Europe).gba` (user-supplied local dump) |
| Header | `POKEMON EMER`, game code `BPEE`, valid complement check |
| ROM size | 16,777,216 bytes |
| ROM SHA-256 | `a9dec84dfe7f62ab2220bafaef7479da0929d066ece16a6885f6226db19085af` |
| Save supplied | 131,072 bytes, all `0xFF` (erased 128 KiB Flash image; no save data) |
| Save SHA-256 | `b5a41c3758763bbec72769fab4a2533bf2db0b6312d93d25a695f9e4b9e02260` |

No ROM, BIOS image, save file, or ROM patch is tracked by this repository or
distributed with this evidence. The ROM and save remain outside the repository.

## Build and method

- Engine revision: `f85025e253ae588906823ecbaf39d37fe0f79479` (current `main`).
- Build: Release, MinGW 12.2.0, Ninja, SDL3 3.4.18.
- Host: `gba-desktop` headless mode, fresh copy of the ROM with no `.sav` file.
- Command: `gba-desktop --headless --rom emerald.gba --frames 450 --screenshot-frame 450 --screenshot-output <dir> --frame-hash --artifact <artifact.json>`.
- The 240x160 framebuffer capture was scaled 3x with nearest-neighbor filtering to
  720x480, matching the desktop host's integer presentation scale.
- Screenshot file: `docs/evidence/2026-11-18-emerald-boot/emerald-title.png`
  (720x480 PNG, SHA-256 `4ebe2bf7fc833e03e40d67386a20a2624639dcb0f7bfd54233e9dc2a5db65283`).

## Observed

| Check | Result |
| --- | --- |
| Boot to title screen | PASS — "POKéMON EMERALD VERSION / PRESS START" renders |
| Intro after START | PASS — Professor Birch intro and moving-truck scene render |
| Unsupported instructions in the 450-frame capture run | 0 |
| 1,100-frame stock run (no save) | completed, 0 unsupported |
| 1,100-frame stock run (blank 128 KiB save) | completed, 0 unsupported on `f85025e` |
| 1,100-frame Crest v2 run (fresh and blank save) | completed, 0 unsupported on `f85025e` |

The blank save is treated as a dead battery: the game shows the battery/clock
messages and starts a new game. It does not provide a Continue/saved-game state.

## NOT CLAIMED

- No general commercial-ROM or retail-game compatibility.
- No full-game playthrough, completion, save compatibility, audio quality, or
  performance qualification.
- No claim that other retail titles, regions, revisions, or ROM hacks behave the same.
- No redistribution rights for the ROM, BIOS, save, or game assets.
- No Nintendo, Creatures Inc., or GAME FREAK affiliation or endorsement.

Pokemon and Pokemon Emerald are trademarks of Nintendo, Creatures Inc., and
GAME FREAK inc. The single title-screen capture is used as local portfolio
evidence under the owner's authorization; it is not used in a store listing,
marketing claim, or compatibility matrix.

## Related fix

The blank-save path previously jumped into ROM font data and aborted with an
unsupported instruction. Bisection identified `f9d2db6` ("Preserve nested HLE
interrupt contexts") as the boundary: its parent `e0ef832` fails at frame 835,
and `f9d2db6` passes. See the task evidence report for the full matrix.
