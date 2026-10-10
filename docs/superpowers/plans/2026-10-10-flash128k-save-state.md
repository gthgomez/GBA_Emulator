# Flash128K save-state follow-up implementation plan

> Execute inline with superpowers:executing-plans, then obtain fresh independent review.

**Goal:** Fix the Flash128K save-state limitation disclosed in PR #21 and verify the affected desktop paths.

**Architecture:** Keep the v3 wire layout and transactional decoding unchanged. Bound the cartridge backup blob by maximum supported storage (128 KiB), rather than its 64 KiB bus aperture; retain MemoryBus protocol-specific size validation.

**Tech stack:** C++17, native g++ verifiers, Python synthetic desktop regression, CMake/SDL3.

**Spec:** PR #21 description and `docs/DESKTOP.md` save-type and state safety contracts.

## Constraints and review focus

- No ROM, BIOS or save assets committed; fixtures are generated.
- v3 encoded bytes and hashes remain unchanged; existing valid snapshots remain compatible.
- Restore both flash banks, active bank, ID mode and pending command state.
- Reject oversized, truncated, mismatched-type and hash-corrupted backup blobs without mutating the target.
- Exercise automatic detection and explicit overrides through real desktop save/resume.
- Scope excludes unrelated emulator compatibility issues tracked elsewhere.

## Task 1: Correct and verify backup decoding

**Files:** `src/core/save_state_codec.cpp`, `tests/save_state_codec_test.cpp`, `tests/desktop_save_state_guard_test.cpp`.

**Interface:** `SaveStateCodec::decode_into(CoreSession&, const std::vector<std::uint8_t>&)`; no API or version changes.

- [x] Add roundtrips for every backup type and a Flash128K fixture with distinct bank contents and nontrivial protocol state.
- [x] Compile/run the codec verifier; expect Flash128K roundtrip failure with the original decoder.
- [x] Use `MemoryBus::kFlash128kSize` as the blob capacity bound; keep exact type/size validation in the memory loader.
- [x] Confirm successful roundtrips and transactional rejection of malformed Flash128K payloads.

## Task 2: Verify host behavior and publish follow-up

**Files:** `tools/check-desktop-save-type-override.py`, `docs/DESKTOP.md`, `docs/open-issues-status.md`.

- [x] Extend the real executable regression to save/resume every explicit type and auto-detected Flash128K, comparing resumed/uninterrupted machine hashes.
- [x] Run against the original executable; expect Flash128K state decode failure.
- [x] Build the fixed desktop; run core tests, benchmark checksum suite and desktop smoke scripts.
- [ ] Document resolved limitation and compatibility; inspect diff and obtain independent review bound to the candidate commit.
- [ ] Commit/push task branch, create stacked follow-up PR against #21, inspect and repair CI.

## Evidence ledger

- OBSERVED: PR #21 remains open at `62741ac7c399f310111ce6521ade5dde30b087db`; original codec verifier passes but lacks Flash128K coverage.
- OBSERVED: backup blob decoding uses `kGamePakSaveWindowSize` (64 KiB); Flash128K storage is 128 KiB and the component loader already validates exact protocol sizes.
- Ruling: proceed without another approval request under the owner's explicit investigate/plan/fix/PR task and autonomy contract.
- VERIFIED RED: native codec verifier fails at all-backup roundtrip; play-mode guard verifier fails at matching Flash128K restore; real executable regression exits 3 on Flash128K resume with decode status 4, v3.
- Ruling: add a real-executable Flash128K failed-ROM-switch regression because the same codec is used for rollback; preserve both boundary save bytes and verify continued play.
- VERIFIED RED: original executable rejects a ROM switch and cannot restore its outgoing Flash128K snapshot, ending play after two frames; the new regression catches the missing successful rollback.
- VERIFIED GREEN: `tools/run-core-tests.ps1` passed all 33 verifiers, including the codec and guarded play-mode restore regressions (211.9 seconds).
- VERIFIED GREEN: Release desktop build (two compile workers), `tools/run-core-benchmarks.ps1`, and all three desktop smoke scripts passed. The save smoke includes every explicit backup type, auto-detected Flash128K, deterministic resume, and failed-switch rollback with save preservation.
- VERIFIED: Python compilation, release-readiness gate, and self-review of the source diff passed. Independent review and publication are the remaining gates at this implementation snapshot; their final evidence belongs in the follow-up PR.
