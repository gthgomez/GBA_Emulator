# mGBA Emerald trace (frames 0–3) — manual comparison

Use this when `rom_video_smoke` unsupported dumps need a reference PC/DISPCNT timeline.

## Prerequisites

- mGBA with Lua scripting, or mGBA CLI + a small Lua script
- Same ROM: `Pokemon - Emerald Version (USA, Europe).gba`

## Quick Lua script (mGBA)

Save as `tools/mgba-emerald-trace.lua` and run from mGBA **Tools → Scripting**:

```lua
local max_frame = 3
local last_frame = -1
callbacks:add("frame", function()
  local frame = emu:framecount()
  if frame > max_frame then
    emu:stop()
    return
  end
  if frame ~= last_frame then
    last_frame = frame
    local pc = emu:getRegister("r15") - (emu:getRegister("cpsr") & 32 ~= 0 and 4 or 8)
    local dispcnt = emu:read16(0x04000000)
    console:log(string.format("frame=%d pc=0x%08X dispcnt=0x%04X", frame, pc, dispcnt))
  end
end)
```

Reset the game, run until frame 3 logs, then compare PCs against our `unsupported_instruction dump` / checkpoint output.

## Compare against our harness

```powershell
cd C:\Workspace\Project_Android\GBA_Emulator
.\tools\run-rom-video-smoke.ps1 `
  -RomPath "..\local\test-roms\Pokemon - Emerald Version (USA, Europe).gba" `
  -Frames 5 -CheckFrame "0,1,2"
```

Match frame indices (mGBA frame 0 ≈ first visible frame after reset). Divergence before frame 2 usually indicates IRQ/HLE return or boot handoff issues.
