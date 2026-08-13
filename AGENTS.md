# AGENTS.md — GBA_Emulator (Gemini 3 Flash Override)

> Inherits from [root AGENTS.md](file:///C:/Workspace/Project_Android/AGENTS.md). See [CLAUDE.md](./CLAUDE.md).

## Gemini-Specific Risks
- Hallucinated ARM7TDMI opcode implementations (encoding errors, wrong cycle counts)
- Incorrect DMA timing behavior (immediate/VBlank/HBlank/FIFO mode confusion)
- Confusing C++17 with older standards or hallucinating memory map addresses
- Treating ROM/BIOS files as committable assets — must never be committed

**Verification gate:** Run `tools/run-core-tests.ps1` and `tools/run-core-benchmarks.ps1`
