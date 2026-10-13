# Conformance re-measurement — 2026-10-11

Purpose: refresh the mGBA public-suite conformance figures against the current
tree so status documents are tied to a reproducible measurement instead of a
historical claim. No ROM, BIOS, or save binary is committed; the suite ROM is
built out of tree and referenced by hash only.

## Tested revision

| Field | Value |
| --- | --- |
| Tested SHA | `c2e315f1c1734b4a8f128ba9eb9f8a5a8b7becc6` (local HEAD) |
| Equivalent base | `6eb935a25869acc06beb7c31c8ae59ad85f5aa9c` (`origin/main`; `git diff --stat origin/main HEAD` is empty) |
| Core verifier runner | `./tools/run-core-tests.sh` (`g++ 13.3.0`, `-std=c++17 -Wall -Wextra -Werror`) |
| mGBA suite source | `external/test-suites/mgba-suite` @ `aac98dca785eaec3932af217aa658275737a8ed8` |
| mGBA suite ROM | locally built `suite.gba`, sha256 `9693913e8b9a41050122cf768c8157eea092d192a435c12d274173a810a38b1b` |
| Host | Linux x86_64, 8 cores, no Docker, no devkitARM, SDL3 3.4.18 |

Environment note (record as **BLOCKED**, not a pass): the suite ROM could not be
rebuilt because Docker and devkitARM are absent. The pinned source is present,
and a previously built ROM from that source was used; only the diagnostic
formatting of `src/misc-edge.c` differs from the pinned source (identical test
logic and expectations). A rebuild would change the ROM hash.

## Commands

```bash
./tools/run-core-tests.sh
pwsh -File ./tools/run-mgba-suite.ps1 -Suite misc-edge
pwsh -File ./tools/run-mgba-suite.ps1 -Suite timers
pwsh -File ./tools/run-mgba-suite.ps1 -Suite timing
tools/run-desktop-smoke.sh && tools/run-desktop-save-smoke.sh && tools/run-desktop-interactive-smoke.sh
pwsh -File ./tools/run-core-benchmarks.ps1
pwsh -File ./tools/check-core-benchmark.ps1          # after the 2026-10-11 gate fix
pwsh -File ./tools/test-core-benchmark-gate.ps1      # after the 2026-10-11 gate fix
pwsh -File ./tools/check-release-readiness.ps1
```

## Results

| Surface | Result | Notes |
| --- | --- | --- |
| Core verifiers (`run-core-tests.sh`) | **33/33 PASS** | Linux; matches `run-core-tests.ps1` inventory (33 entries each) |
| Desktop headless smoke | **PASS** | deterministic, engine `c2e315f1c173` |
| Desktop save-lifecycle smoke | **PASS** | persistence, reset, no-spurious-save, rejected-save preservation, save-directory, switch refusal, crash-orphan recovery |
| Desktop interactive smoke | **PASS** | dummy video/audio; nonuniform window; undefined-instruction exit 3 |
| Core benchmarks | **PASS** | checksums/operations match `tools/core-benchmark-baseline.json`; wall-clock medians exceed the Windows-calibrated ceilings on this host (informational only) |
| `check-core-benchmark.ps1` | **PASS** | now also verifies baseline operations/checksum |
| `test-core-benchmark-gate.ps1` | **PASS** | drift fixtures rejected independent of wall-clock |
| `check-release-readiness.ps1` | **PASS** | |
| mGBA `misc-edge` | **RED 4/12** | see below |
| mGBA `timers` | **GREEN 936/936** | supersedes the 2026-09-29 "UNKNOWN / open PR #8" note |
| mGBA `timing` | **RED 1956/2020** | 64 failures, all `Trivial DMA` / `Short DMA` timing rows |
| mGBA `timer-irq`, `sio-timing`, other suites | **not re-run** | no current measurement |

### `misc-edge` detail (4/12)

PASS: `H-blank bit start Start`, `H-blank bit start Flip 2`, `DMA count latching
First`, `DMA count latching Reload`.

FAIL: `DMA Prefetch Break` (produced `0x10002A6C`, expected `0x10002A64`),
`DMA Prefetch Read` (produced `0`, expected `0xDEAD0000`), `H-blank bit start
Hblank` (produced `0x4D0`, expected `0x4D1`), `Flip 1` (`0x5A` vs `0x85`),
`Flip 3` (`0xE3` vs `0xE4`), `Flip 4` (`0x3ED` vs `0x3EC`), `Flip 5` (`0xE3` vs
`0xE4`), `Flip 6` (`0x3E9` vs `0x3F5`). The failing lines are all HBlank-flag
phase (off-by-one plus one larger miss) or the categorical `DMA Prefetch` pair.

### `timing` detail

All 64 failures are in the `Trivial DMA (...)` and `Short DMA (...)` rows for the
`ARM/IWRAM`, `Thumb/ROM P.S`, `Thumb/ROM PNS`, and `Thumb/IWRAM` access-class
variants; the rest of the suite passes.

## Classification of the `misc-edge` failures

- **Core timing behavior** (HBlank-flag phase vs the CPU/timer sampler): the
  DISPSTAT sampler compensation rules were explicitly flagged as coupled to
  `kHblankFlagCycles`; PR #6 (merged `9755c99`) changed the constant 1006→1004
  without reworking them, and recorded no post-merge suite sweep.
- **Fixture/calibration drift**: the pinned `aac98dca` expected values differ from
  current upstream `misc-edge.c` (which also changed the `DMA Prefetch` scan), so
  the exact 6/12-vs-4/12 figures are revision- and hardware-calibration-dependent.
- **Environment limitation**: the suite ROM could not be rebuilt here (no
  Docker/devkitARM).

Any correction to the timing/DMA path is a pause-and-confirm change and is not
made by this pass.
