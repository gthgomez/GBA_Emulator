# Roadmap Requirements Audit

Status: Phase 72 audit
Date: 2026-05-07

This audit checks the roadmap requirements against current repository evidence. It is
intentionally stricter than the phase-status labels: bounded local seeds can be complete
while production readiness remains blocked.

## Phase Done-When Audit

| Phase | Done-When Requirement | Current Status |
| --- | --- | --- |
| 52 | Thumb programs load/store/push/pop/branch/SWI; unsupported forms reject. | SATISFIED LOCALLY |
| 53 | CPU tests cover PSR transfer, SWI, RRX, undefined rejection. | SATISFIED LOCALLY |
| 54 | Synthetic timer IRQ can enter IRQ mode, use banked registers, return modeled path. | SATISFIED LOCALLY |
| 55 | SWI deterministic; unsupported BIOS services fail cleanly; no BIOS assets. | SATISFIED LOCALLY |
| 56 | Memory/IO behavior explicit for alignment, mirroring, unmapped, read-only, reset. | SATISFIED LOCALLY |
| 57 | Instruction streams report different elapsed cycles by region/WAITCNT. | SATISFIED LOCALLY |
| 58 | Tiny legal test programs run through deterministic in-memory harness. | SATISFIED LOCALLY |
| 59 | SRAM/Flash/EEPROM behavior testable through bus/core-owned buffers. | SATISFIED LOCALLY |
| 60 | Scheduler traces show DMA occupying bus time and affecting timing. | SATISFIED LOCALLY |
| 61 | Synthetic frames cover BG/OBJ priority, transparency, scroll, window behavior. | SATISFIED LOCALLY |
| 62 | Synthetic renderer tests cover major display modes without Android/OpenGL. | SATISFIED LOCALLY |
| 63 | Synthetic APU tests produce stable hashes for PSG/Direct Sound/mixed output. | SATISFIED LOCALLY |
| 64 | Legal harness/core APIs can drive input without Android dependencies. | SATISFIED LOCALLY |
| 65 | Curated legal fixture set runs with reproducible hashes. | PARTIAL: corpus runner exists; no external checked-in fixture set admitted. |
| 66 | Run/save/restore/run deterministic; malformed blobs reject. | SATISFIED FOR PUBLIC RESTORABLE SUBSET |
| 67 | Benchmarks hold without weakened tests; optimization has measurement artifact. | SATISFIED LOCALLY |
| 68 | Core can be created, load explicit bytes, step, release without races/leaks. | SATISFIED LOCALLY BY BRIDGE SEED |
| 69 | Legal test program can render/input/audio/pause/resume/shutdown on Android. | PARTIAL: local runtime seed exists; no real Android lifecycle/device evidence. |
| 70 | Midrange Android hardware runs fixture set with metrics. | BLOCKED: no target-device evidence. |
| 71 | Release wording and fixture handling reviewed and evidence-backed. | SATISFIED LOCALLY |
| 72 | No critical blockers, high risks documented, beta evidence on one device. | PARTIAL: risks documented; beta device evidence missing. |

## Production Readiness Definition Audit

| Requirement | Status | Evidence / Blocker |
| --- | --- | --- |
| Legal fixture policy enforced and no copyrighted ROM/BIOS bundled. | SATISFIED LOCALLY | Fixture registry, release readiness check, no fixture assets. |
| BIOS/HLE explicit, tested, no silent bundled dependency. | SATISFIED LOCALLY | BIOS tests and roadmap policy. |
| CPU, memory, timing, DMA, PPU, APU, save, save-state have positive/negative tests. | SATISFIED LOCALLY | Core verifier suite. |
| Core keypad/input tested before Android mapping. | SATISFIED LOCALLY | `keypad_test`, IO routing tests. |
| Performance regression gates exist for local core paths and Android device paths. | PARTIAL | Local synthetic and local Android-facing gate exist; real Android device path missing. |
| Save persistence and save states have corruption, migration, rollback handling. | PARTIAL | Save-state corruption rejection and rollback docs exist; persistence/migration UX missing. |
| Android lifecycle, input, rendering, audio have device evidence. | BLOCKED | No Android app shell/device run exists. |
| Public docs avoid unsupported compatibility, legality, superiority claims. | SATISFIED LOCALLY | README/roadmap/release governance wording. |

## Final Readiness Judgment

All roadmap phases now have bounded local implementation artifacts and checks.

Production readiness: **NO**.

Controlled external beta readiness: **BLOCKED** until at least one target Android device
run exists with lifecycle, frame pacing, audio, input, shutdown, thermal, and recovery
evidence.
