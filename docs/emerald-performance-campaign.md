# Emerald performance campaign

Started 2026-10-26 against `main` at `6eb935a25869`. The owner's current task
explicitly authorizes local testing with their Emerald ROM. Game images,
screenshots, saves and save states remain local and must never be committed.
These results describe the measured desktop workload, not general compatibility
or Android performance.

## Acceptance and iteration

Run scripted input through the shared `EmulatorRuntime`, inspect screenshots to
establish the actual game phase, and keep identical input across comparisons.
Check complete frames, execution failures, audio counts, framebuffer hashes and
final state hashes before interpreting speed. Repeat measurements serially and
record host pressure. Fix the first demonstrated correctness or performance
problem, then repeat the same workload and extend the playthrough.

The requested target is sustained approximately 60 FPS with correct progression,
smooth presentation and audio. Headless throughput alone cannot establish the
interactive goal. Record median, p95, p99, maximum frame step duration and count
over the 16.67 ms budget; validate the SDL host separately. Existing core timing
conformance gaps remain relevant even when a game appears to work.

## Initial observations

OBSERVED: local unmodified Emerald is 16,777,216 bytes, SHA-256
`a9dec84dfe7f62ab2220bafaef7479da0929d066ece16a6885f6226db19085af`.
The Downloads copy matches the relocated fixture. The `crestv2` ROM is a separate
input and must have its own results.

OBSERVED: native Windows, MinGW GCC 12.2, C++17 `-O3 -DNDEBUG`, SDL3 from the
existing workspace dependency. About 2.8 GiB physical RAM available; builds and
experiments serialized. Baseline and initial candidate used identical flags.

| Workload | Baseline | Remove unused per-frame state hashing |
| --- | ---: | ---: |
| Emerald boot/intro, 600 frames | 26,286.75 ms | 16,406.13 ms |
| Completed FPS | 22.83 | 36.57 |
| Unsupported / fetch failures | 0 / 0 | 0 / 0 |
| Audio samples / engine underruns | 329,149 / 0 | 329,149 / 0 |
| Final framebuffer CRC32 | `a2bf7412` | `a2bf7412` |
| Final state hash | `306f23feae603d7f` | `306f23feae603d7f` |

VERIFIED: all sampled framebuffer hashes, audio hash, counters, scheduler cycles
and final state hash match across these two runs. Initial speed ratio 1.60x is
one pair, not a repeated performance conclusion. Screenshots show boot and intro
content; this is not yet an overworld playthrough.

OBSERVED: an additional run with temporary coarse runtime instrumentation spent
13,434.51 ms in stepping, 2,973.33 ms rendering and 1.59 ms draining audio across
600 frames. p50/p95/p99/max frame step times were
30.08/33.31/35.04/36.82 ms; 559/600 steps exceeded 16.67 ms. GNU gprof on this
Windows toolchain produced no usable samples; its empty report is not profiling
evidence. Temporary instrumentation stays in the local build tree.

## Demonstrated defects

1. The headless host enabled full machine hashing every frame but discarded
   every intermediate hash. `MemoryBus::state_hash` scans the whole ROM.
   Disable that unused work; retain the final deterministic state hash.
2. A run with `--frames 3 --max-steps-per-frame 1` returned exit 0 and
   `stop_reason=completed` despite zero completed frames. Stop on the first
   incomplete frame, report `frame_budget_exhausted`, and exit 1.
3. Successful lab artifacts reported final PC 0 because they read an
   unsupported-instruction dump filled only on failures. Read the CPU's final PC.
4. Aggregate elapsed time could not distinguish smooth frames from stalls.
   Add completed FPS and frame step distributions, excluding screenshot/hash
   work from step timing while retaining it in total elapsed time.
5. The SDL texture interpreted native GBA 15-bit pixels as RGB565, and PNG
   conversion swapped red and blue. A synthetic fixture writing `0x001F`,
   then `0x7C00`, rendered the first pixel blue in the baseline. Use SDL's
   `XBGR1555` and extract PNG red from bits 0–4. VERIFIED: both paths now
   pass ordered red/blue pixel assertions; native frame hashes stay unchanged.
   Format reference: [mGBA color macros](https://github.com/mgba-emu/mgba/blob/master/include/mgba-util/image.h)
   and [SDL pixel definitions](https://github.com/libsdl-org/SDL/blob/release-3.4.18/include/SDL3/SDL_pixels.h).

## Repeatable local experiments

Build the desktop executable as described in `DESKTOP.md`, then supply an
explicit ROM path (no implicit fixture discovery):

```powershell
.\tools\run-rom-playthrough.ps1 -RomPath 'D:\ROMs\emerald.gba' `
  -Frames 1800 -Runs 3 -InputScript 'D:\Experiments\intro-input.json' `
  -ScreenshotFrames 60,300,900,1799
```

Each run writes its console log, versioned lab artifact and requested screenshots
to a fresh directory under `build/rom-playthrough`. The summary records host
resource samples, checks execution and repeat determinism, and reports
`headless_target_met` only when every run meets average FPS and p95 step budget.
The input script is copied into the evidence directory before running; its hash
and the executable hash are retained. At least two runs are needed to establish
repeat determinism; one run reports null for `deterministic` and cannot meet the
repeated target gate.
It is not an interactive smoothness verdict. Execution failures or nondeterminism
exit nonzero; below-target performance remains measurement data with a false
target field. Choose a new output directory for every experiment.

## Next measurements

- Keep link-time optimization disabled; see the failed experiment below.
- Extend scripted Start/A input to the title menu and new-game introduction;
  inspect actual checkpoint screens before naming a playthrough phase.
- Profile the CPU/scheduler further if throughput remains below target.
- Compare the patched ROM as a separate workload after the base-ROM baseline.
- Verify core tests, synthetic benchmarks and host smokes on the delivery
  candidate. Preserve raw results locally and summarize only observed outcomes.

### First delivery candidate validation

VERIFIED on native Windows: `tools/run-core-tests.ps1` passed 33/33 verifiers;
`tools/run-core-benchmarks.ps1` passed its checksum suite; headless and
interactive desktop smokes passed, including incomplete-frame failure and
ordered-color assertions. Two fresh read-only reviews found no blocking defects
in the host/lab candidate. Linux smoke paths were inspected, not locally run.

VERIFIED: the normal CMake Release candidate completed three identical
600-frame Emerald runs. All execution/video/audio/state identities matched.
FPS was 35.65, 36.92, 37.20 (median 36.92); p95 step time was
35.09, 33.02, 32.20 ms. The repeated 60 FPS gate remains false. These runs used
the new runner with executable SHA-256 and host samples, without screenshots
inside the timed run. Raw evidence is retained in the task worktree's
`build/emerald-candidate-repeat` directory. A separate one-run synthetic probe
verified that determinism remains unestablished (`null`) without repetition.

OBSERVED: the normal candidate writes an Emerald checkpoint, but loading that
same snapshot exits 3 with decode status 4 and version 3. This is the known
128 KiB storage versus 64 KiB aperture decoder limit addressed by pending PR
[#22](https://github.com/gthgomez/GBA_Emulator/pull/22), stacked on
[#21](https://github.com/gthgomez/GBA_Emulator/pull/21). Verify those fixes before
depending on resumed Emerald playthroughs. Intro screenshots from the rejected
LTO experiment establish observed input progression through the title screen,
clock warning and Birch dialogue, not a successfully checkpointed run.

### Rejected experiment: link-time optimization

OBSERVED: an LTO build preserved the 600-frame hashes, but its save-state
encoding reproducibly crashed with Windows `0xC00000FD` (stack overflow), even
after one frame. The same probe succeeds on the normal release baseline and
candidate. A 2,400-frame LTO run produced all requested screenshots, then
crashed at save-state encoding without writing a final artifact. It is not a
completed verification run. LTO is not enabled in the project. Any future LTO
work must first address snapshot temporaries/inlining stack use and validate
save-state lifecycle; raw throughput from this experiment is not shippable.

## Checkpoint iteration

VERIFIED: PR [#23](https://github.com/gthgomez/GBA_Emulator/pull/23) merged as
`de45d47b0b25` after all four Windows/Linux CI jobs passed. The campaign continues;
that merge does not establish the 60 FPS goal.

The next candidate integrates existing save-type selection (#21) and Flash128K
restoration (#22) with the lab/color fixes. No wire-format change is required:
the parser now bounds backup storage by the largest supported capacity rather
than the 64 KiB bus aperture; exact type-size validation and transactional hash
verification remain. VERIFIED locally: the exact Emerald state that previously
failed to load now loads successfully. All 33 core verifiers and the extended
save lifecycle smoke pass on the integrated candidate, including every backup
capacity, bank/ID/pending-command restoration, wrong-size/hash rejection,
rollback and automatic/explicit resume paths.

Independent review found another preexisting provenance error: headless
automatic state restoration could execute the embedded ROM while recording
another caller-supplied ROM's hash/path. VERIFIED: the new synthetic regression
failed before the repair (`exit 0`, expected 3); it passes with unconditional
restored-ROM identity verification, including explicit `--save-type auto`.
No misleading artifact is produced on rejection. Two fresh read-only reviews
covered the integrated restoration behavior and refreshed this repair.

### Further performance probes

OBSERVED: replacing power-of-two timer divisions with shifts/masks preserved
execution/video/audio/state identities but measured 35.31 and 35.60 FPS, within
the existing range/noise. No gain was demonstrated, so the experiment remains
outside the product source.

OBSERVED: temporary sampled instrumentation counted 55,967,071 scheduler fetch
steps and 692,380,821 memory-map descriptions in 600 Emerald boot frames.
Sampling overhead distorts tiny function durations, so these counts identify
work frequency; sampled inclusive timings are not additive CPU percentages.
Investigate cheaper equivalent memory classification before changing scheduling
or instruction timing semantics. The requested sustained smooth 60 FPS remains
unverified and unmet by the recorded release measurements.

### Memory classification candidate

The candidate dispatches by the address's high byte and makes that stateless
calculation available to the CPU, DMA, bus and scheduler compiler. The existing
out-of-line `MemoryBus::describe` symbol delegates to the same implementation,
so callers using the earlier entry point retain it. The more detailed cartridge
metadata path, including EEPROM candidacy, remains separate.

VERIFIED: the candidate's classification matched the compiled pre-change
implementation on 68,110,400 addresses, comparing region, offset, writable and
mirrored fields. Coverage includes every high-byte bank through its first
256 KiB, selected high offsets, and one million deterministic addresses across
the 32-bit space. The existing public symbol was found in the built library.
The 33 core verifiers passed and two independent read-only reviews found no
blocking defect. Existing tests and this comparison establish equivalence to
the prior implementation; they do not establish complete hardware accuracy.

OBSERVED: two 600-frame release candidate runs measured 42.63 and 42.48 FPS,
with p95 step times 28.41 and 28.25 ms. Execution counters, sampled frame
hashes, audio and final state matched the earlier 600-frame workload. Earlier
baseline runs measured 35.65–37.20 FPS with p95 32.20–35.09 ms. The runs were
not interleaved and host load varied, so the practical speedup remains
provisional until an alternating comparison is possible. Both candidate runs
remain below 60 FPS and most frames exceeded the 16.67 ms step budget.

OBSERVED: an alternating baseline/candidate/candidate/baseline comparison of
600 boot frames used Windows CI Release packages built with GNU 15.2.0 and the
same SDL library. Baseline FPS was 28.68 and 32.30 (p95 54.61 and 42.17 ms);
candidate FPS was 31.84 and 36.06 (p95 44.91 and 35.64 ms). Sampled video,
audio and final state hashes matched across all four runs. Host load and memory
pressure varied, so this small comparison suggests an improvement without
establishing a precise speedup. Neither build met the headless target. Raw
artifacts remain in ignored `build/paired-ci-run-*` directories.

### Checkpointed intro

VERIFIED locally at `439ece4fe497`: the normal release candidate completed
2,400 scripted frames through the title, clock warning and Birch introduction,
then wrote a Flash128K checkpoint. It resumed that exact checkpoint for another
1,800 frames through Birch dialogue and wrote a second checkpoint. Each segment
reported zero unsupported instructions, fetch failures and engine audio
underruns. The segments measured 36.38 and 38.29 FPS, respectively; neither is
an interactive audio/presentation test or a complete playthrough. Screenshots,
states, ROMs and JSON artifacts remain local in the task worktree's ignored
`build/` directory.

VERIFIED: the headless input script used to initialize its local button mask to
zero after loading a state. The new regression holds one button across a
checkpoint and presses/releases a second after resumption, comparing the final
state to an uninterrupted script. It failed on the original host and passed
after initializing the mask from the restored keypad. The fix merged in PR
[#26](https://github.com/gthgomez/GBA_Emulator/pull/26), with desktop smoke
verification on Windows and Linux.

### Littleroot continuation

OBSERVED: the Windows CI Release build containing both the input-resume fix and
memory classification change resumed the frame-4200 checkpoint in nine
600-frame segments. The character left the truck, spoke with their mother and
entered the house by frame 9000. Each segment completed and saved the next
checkpoint, reporting zero unsupported instructions, fetch failures and engine
audio underruns. Segment throughput ranged from 33.07 to 40.57 FPS; this is
headless throughput under varying host load, not a presentation or audible
smoothness measurement. The final segment's p95 step time was 38.39 ms and all
600 frames exceeded the 16.67 ms step budget.

OBSERVED, unresolved: the frame-9600 screenshot shows the visible house interior
in the upper-left portion with black surrounding it and dialogue below. Compare
against a trusted reference at the same point before deciding whether this is
expected framing or a rendering defect. Local screenshots, input scripts,
artifacts and the frame-9600 checkpoint are retained under ignored `build/`.
The scheduler candidate continued to frame 10200 at 39.30 FPS with zero reported
unsupported instructions, fetch failures and engine audio underruns. The black
area persisted. Its saved display state has WIN0 enabled with horizontal and
vertical bounds `0x00FF`; the current renderer treats that window as covering
the screen. A [published house screenshot](https://multiplayer.it/soluzioni/la-soluzione-completa-di-pokemon-smeraldo.html)
shows the same room at a different camera position and does not resolve the
comparison; capture the same game state in a reference emulator.

### Instruction sampling and accessor experiments

OBSERVED: process-local Windows instruction sampling on the merged Release build
collected 1,374 samples during boot and 1,494 at the saved house scene. The most
frequently named location was `MemoryBus::read16` (189 and 200 samples), followed
by scheduler stepping and device advancement. Sampling suspends the owned thread
briefly, identifies only its current instruction, and may be biased by sampling
interval and instruction timing. It is a guide to investigate, not a precise
breakdown of inclusive CPU time. Raw samples and sampler are in `build/profile/`.

OBSERVED: an isolated aligned-ROM-read shortcut preserved sampled video, audio
and state identities, but alternating 600-frame runs measured 41.56/41.24 FPS
for the baseline and 41.37/41.32 FPS for the experiment. No useful gain was
established and the shortcut was not added to the core.

OBSERVED: an isolated scheduler accessor experiment measured 44.02/44.11 FPS
against 41.29/41.31 FPS for the baseline in alternating 600-frame runs. Sampled
video, audio and final state identities matched. These are boot measurements on
one host and do not establish sustained 60 FPS or interactive smoothness.

VERIFIED: the full accessor candidate passed all 33 core verifiers and the
core benchmark checksum suite. Its Release build completed alternating 600-frame
boot runs at 43.98/44.01 FPS, versus 41.15/41.09 FPS for the preserved baseline;
sampled video, audio and final state hashes matched. At the frame-9600 house
checkpoint, one 600-frame run per build also matched run, video, audio and state
artifacts: 37.67 FPS baseline and 39.48 FPS candidate. The single house pair and
variable host load limit the precision of any performance comparison.

### Background video-read candidate

OBSERVED: a follow-up instruction sample at the house checkpoint identified
background pixel fetching as the caller for 92 of 130 sampled returns from one
hot `read16` return site. This is a sampled subset, not a count of all reads.
The candidate makes aligned VRAM and palette halfword reads available inline to
the background fetcher and falls back to the original bus operation elsewhere.
The focused memory-bus and background tests passed; the memory-bus test seeds
video data and compares mirrors, alignment and fallback cases.

OBSERVED: alternating 600-frame boot runs of the full Release candidate measured
46.73/46.94 FPS versus 43.36/43.59 FPS for its scheduler-optimized baseline.
Sampled video, audio and final state hashes matched. These short runs on a single
host show a workload-specific improvement, not 60 FPS or interactive smoothness.
VERIFIED: all 33 core verifiers and the core benchmark checksum suite passed.
From the frame-10200 house checkpoint, one 600-frame run per build produced
matching run, video, audio and state artifacts; baseline measured 39.95 FPS and
the candidate 43.52 FPS. A single pair cannot establish a precise speedup.
