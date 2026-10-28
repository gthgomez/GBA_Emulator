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

## Current checkpoint (2026-10-27)

| Area | Latest bounded evidence |
| --- | --- |
| Integration | PR #52 merged as `4db5171067b1`; all four Linux/Windows CI jobs passed final head `9f9425bd7239` |
| Current candidate | Nominal OBJ scanline work limit; 36 core verifiers, benchmarks and fresh independent review pass; runtime comparison pending |
| Story | Frame 207,240: Aqua grunt defeated; Devon employee dialogue in Petalburg Woods; Torchic level 10, HP 11/29 |
| Repeat throughput | Two exact 10,800-frame normal-host replays: 88.22 / 88.29 FPS; three / two steps over 16.67 ms |
| SDL delivery | Integrated pacing build: 10,800 diagnostic + 10,800 normal frames at 59.7290 / 59.7291 FPS; zero intervals over 25 ms, maxima 17.96 / 20.45 ms |
| Determinism | New 10,800-frame exact-input replay byte-identical to the post-pacing diagnostic checkpoint; six additional story segments repeat identically |
| Audio | No queue-empty proxy or additional-input requests; finite postmix data; no physical output claim |
| Intro | Initial 600-frame lag explained by reference HLE accounting; longer 3,600-frame capture exposes further differences, starting with an extra ripple sprite |
| Status | Active: verify OBJ work-limit candidate, investigate later intro differences, extend story/native soak coverage; original unsupported stop remains unexplained |

The sections below are chronological. Earlier measurements and pending-work
statements describe their original candidate, not the latest integrated code.

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

OBSERVED: the frame-9600 screenshot shows the visible house interior in the
upper-left portion with black surrounding it and dialogue below. Local
screenshots, input scripts, artifacts and the frame-9600 checkpoint are retained
under ignored `build/`.
The scheduler candidate continued to frame 10200 at 39.30 FPS with zero reported
unsupported instructions, fetch failures and engine audio underruns. The black
area persisted. Its saved display state has WIN0 enabled with horizontal and
vertical bounds `0x00FF`; the current renderer treats that window as covering
the screen. A [published house screenshot](https://multiplayer.it/soluzioni/la-soluzione-completa-di-pokemon-smeraldo.html)
shows the same room at a different camera position and does not resolve the
comparison.

VERIFIED against the local mGBA libretro reference (0.11-212-7a12d6d, DLL
SHA-256 `5cac11c643d7f8dfa50c68855f2f2240ff7cc0b4cd0312dd9e17a639c85667e4`):
replaying the recorded inputs produced matching screenshots at frames 6600,
8400, 9600, 10800, 12000, 13800, 15600 and 16800 after quantizing both RGB
images to five bits per channel. All 38,400 pixels matched at each of those
frames, including the black house boundary. Raw 8-bit RGB values differ by up
to five due to the two frontends' pixel-format conversion. The frame-2400 and
frame-4200 captures differed in dialogue progression; these comparisons do
not prove full emulation accuracy. Reference screenshots and replay code are
local in ignored `build/reference-mgba/`.

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

### Later playthrough and rejected scheduler probe

OBSERVED: with the three pending optimizations, the scripted playthrough reached
the upstairs bedroom at frame 13800 and confirmed the clock by frame 15600.
The six 600-frame segments from frame 12000 through 15600 all completed with zero
reported unsupported instructions, fetch failures and engine audio underruns.
The latest segment ran at 44.75 FPS with a 23.79 ms p95 step. Screenshots,
scripts, state checkpoints and artifacts remain under ignored `build/`.
Two further 600-frame segments reached frame 16800 in the bedroom, at 44.84 and
43.63 FPS, again with no reported unsupported instructions or fetch failures.
The character then returned downstairs, left the house and reached the north
edge of Littleroot at frame 23400. Across the 22 candidate segments starting at
frame 10200, all 600-frame segments completed with zero reported unsupported
instructions, fetch failures and engine audio underruns; segment throughput
ranged from 43.27 to 45.44 FPS. The exploratory directory named
`emerald-continued-22800` starts from frame 21000 and is not part of that chain;
`emerald-continued-22800-correct` contains the actual frame-22800 continuation.

OBSERVED: an isolated scheduler experiment skipped a timer pre-access probe when
its input was already zero. Alternating 600-frame boot runs measured 46.67/46.46
FPS versus 48.02/47.29 FPS for the baseline; recorded video, audio and state
hashes matched. The probe showed no performance benefit and was not added to the
core.

OBSERVED: a separate inline byte-read experiment for background video memory
measured 48.43/48.66 FPS against 47.73/48.07 FPS in alternating 600-frame boot
runs, with matching recorded video, audio and state hashes. The small difference
overlaps observed run variation; the extra bus path remains experimental.

OBSERVED: replacing background coordinate modulo with equivalent power-of-two
masks measured 46.82/46.52 FPS versus 45.96/47.67 FPS for the baseline in
alternating 600-frame boot runs, with matching recorded identities. No useful
gain was established, so the experiment remains outside the core.

OBSERVED: skipping the serial-device tick when no transfer is active measured
48.88/48.57 FPS versus 47.84/47.55 FPS in alternating 600-frame boot runs,
with matching recorded identities. Longer 1,200-frame runs measured 45.77/46.45
FPS versus 45.17/45.65 FPS with matching identities. The ranges overlap, so no
clear gain was established and the experiment remains outside the core.

OBSERVED: a sprite video-read experiment measured 48.42/47.76 FPS versus
47.53/47.24 FPS in alternating 600-frame boot runs, with matching recorded
identities. The ranges nearly overlap and host load varied; the experiment
remains outside the core pending stronger evidence.

### Scanline background decoding

OBSERVED: an isolated experiment decoded each background's control once per
scanline instead of once per pixel. Alternating 1,200-frame boot runs measured
46.06/46.08 FPS versus 44.24/44.90 FPS for the baseline, with matching sampled
video, audio and final state hashes. This is a small comparison on one host;
neither side meets the 60 FPS target. A renderer test verifies changed BG
registers take effect on the next render call.
VERIFIED: the complete candidate passed all 33 core verifiers and the benchmark
checksum suite. One 600-frame house checkpoint run per build had matching run,
video, audio and state artifacts; 43.52 FPS baseline versus 43.64 FPS candidate
does not demonstrate a meaningful house-scene throughput gain.

### Affine BIOS and introduction rendering

OBSERVED: an independent replay using the mGBA libretro core (version
`0.11-212-7a12d6d`) matched all 38,400 pixels at each of eight sampled house and
bedroom frames after converting both images to five-bit color channels. The dark
surrounding area in those scenes also appears in the reference. The reference
core and replay artifacts are local to ignored `build/`; game data is not
included in the repository.

OBSERVED: at introduction frame 240, the original Game Freak logo contained
rectangular sprite blocks. The reference and candidate had identical VRAM and
palette contents at that point, but their OAM affine matrix values differed.
The HLE affine calculation divided the already fixed-point scale by 256. After
correcting that calculation, the frame-240 difference against the reference
fell from 1,121 to 297 pixels (five-bit channel comparison). Other animation
frames still differ, so this does not establish complete rendering fidelity.

VERIFIED: focused regression cases failed before their respective corrections
and passed afterward for 8.8 affine scale, object matrix destination stride,
and background affine record layout and translated reference coordinates. The
object routine now advances padded eight-byte inputs and spaces each output
component by the supplied stride; the background routine reads 20-byte inputs
and writes 16-byte results. The existing floating-point trigonometry has not been
established as bit-exact to the BIOS lookup table. The reference implementation
is available at https://github.com/mgba-emu/mgba/blob/master/src/gba/bios.c.
VERIFIED: the affine candidate passed all 33 native core verifiers and the core benchmark checksum suite. The optional ROM video smoke was not included in the core test invocation.

### Startup timing, neighbor's house, and CPU register access

OBSERVED: after the affine corrections, the first same-frame difference in a
frame-by-frame reference comparison occurs at frame 211. The introduction
transition begins at candidate frame 217 and matches the reference at frame 211;
candidate frames 217 through 245 each had a matching reference frame six or
seven frames earlier after five-bit channel quantization. Candidate frames 270,
300, 330, 360 and 390 likewise had exact matches within that offset. At frame
360, the two runs had different blend coefficients and moving-sprite attributes.
These observations explain the sampled visual differences as an animation phase
offset; they do not establish the cause or prove general rendering accuracy.
No scripted input occurs during these first 390 frames. Reference replay and
images remain in ignored `build/reference-mgba/`.

OBSERVED: a later comparison of the existing corrected candidate and reference
captures found 1,121, 1,125, 1,169, 4,903, 346, 0 and 76 differing pixels at
frames 420, 450, 480, 510, 540, 570 and 600 respectively, after converting
both images to five-bit color channels. The reference captures at frames 540
and 570 are identical, so the exact match at frame 570 does not show that the
timing offset has disappeared. These are 30-frame candidate samples, not a
continuous comparison. At frame 540 the
346 differing pixels are confined to x=85–151, y=117–141; at frame 600 the 76
differing pixels span x=45–203, y=61–93. Coordinates are zero-based. The first
scripted button event is at zero-based frame 600, after these captures.

OBSERVED: an every-frame reference capture showed that at candidate frame 420
the central Game Freak symbol was black rather than blue even at reference
frame 414. The two frames had byte-identical VRAM, palette and OAM dumps, and
matching sampled display and blend registers. The renderer applied alpha to a
semi-transparent OBJ even with no selected second target; BLDCNT and BLDALPHA
were zero. GBATEK describes displaying the top pixel normally when there is no
second target: https://mgba-emu.github.io/gbatek/.

VERIFIED: a focused test for an unselected second target failed before the
renderer change and passed afterward; selected backdrop blending remains
covered. A rebuilt ordinary Release candidate matched the reference exactly
after five-bit conversion at candidate frames 420 and 450 against reference
frames 414 and 444 respectively. Candidate frames 480 and 510 still differed
by 66 and 133 pixels from reference frames 474 and 504. This is a sampled
comparison, not a complete intro fidelity result.

OBSERVED: at candidate frame 480 and reference frame 474, VRAM, palette and
OAM dumps were byte-identical, and sampled display and blend registers matched.
The renderer had been considering overlapping OBJ pixels as separate layers,
allowing OBJ-to-OBJ blending. Resolving the visible OBJ before mixing layers
removes that path. A focused overlapping-sprite regression failed before this
change and passed afterward, and a second case checks OAM ordering when OBJ
priorities relative to backgrounds differ. The corrected Release build matched
the reference after five-bit conversion at sampled candidate frames 420, 450,
480, 510, 540, 570 and 600 (reference frames 414, 444, 474, 504, 534, 570 and
594). This does not explain the animation phase offset or prove all frames match.

OBSERVED: the affine candidate continued from the prior frame-25800 checkpoint
into the neighbor's house, completed the downstairs conversation and reached
the upstairs room by frame 28320. Nine resumed segments completed by frame 28620
with zero reported unsupported instructions, fetch failures or engine audio
underruns. The 300- or 420-frame segments measured 42.38 to 45.74 headless FPS;
these are not interactive display or audible playback measurements.

OBSERVED: a process-local PC sampler collected 871 samples from a 600-frame
upstairs checkpoint run. The most sampled functions were Thumb scheduling (148),
device advancement (105), and PC dispatch (74); register access accounted for
19 samples. These samples can be biased and are not exact CPU-time shares.
An isolated inline register-access experiment preserved the existing exported
accessor and bounds behavior. In alternating 600-frame upstairs runs, baseline
throughput was 44.05/44.61 FPS and the experiment 45.78/45.82 FPS. Alternating
boot runs measured 47.82/46.96 FPS baseline and 48.71/49.16 FPS experimental.
Run, video, audio and final-state artifacts matched within each comparison.
Host load varied, samples are short, and the measurements do not establish
60 FPS or interactive smoothness.
The complete Release implementation was then compared in another alternating
upstairs run: baseline 42.15/41.29 FPS and candidate 41.89/42.13 FPS. All four
runs completed with matching run, video, audio and state artifacts and no
reported unsupported instructions, fetch failures or engine audio underruns.
The ranges overlap under increased host load, so a useful gain was not
established; the accessor change was removed from the proposed core. The
candidate passed all 33 local core verifiers and the benchmark checksum suite
before it was removed. The optional ROM video smoke was not included.

### First battle: zero-length LZ77 BIOS call

OBSERVED: the playthrough met May, returned to Route 101, and selected Torchic.
The initial battle attempt stopped after 283 of 800 requested frames with one
unsupported instruction at Thumb PC `0x082E7090` (SWI `0x11`), with no fetch
failure. Its source pointer referenced a four-byte zero header and destination
was in EWRAM. The previous segments from frame 28620 through frame 34800 all
completed with zero reported unsupported instructions, fetch failures and
engine audio underruns; their headless FPS ranged from 38.77 to 44.20.

DOCUMENTED: the [Pokémon Emerald decompilation](https://github.com/pret/pokeemerald/blob/master/src/trainer_pokemon_sprites.c)
describes the game's call on uncompressed trainer back graphics and notes that
its zero header makes the decompression do nothing. The [mGBA HLE implementation](https://github.com/mgba-emu/mgba/blob/master/src/gba/bios.c)
reads the length independently of the signature and performs no decompression
when that length is zero.

VERIFIED: a synthetic ARM SWI regression for both WRAM and VRAM variants failed
on the old implementation and passes with zero-length inputs treated as no-ops.
It checks destination preservation, PC advancement, and continued rejection of
nonempty streams with an invalid signature. Replaying the same starter input
from the frame-34800 checkpoint completed all 800 frames and reached the Torchic
battle screen, with zero reported unsupported instructions, fetch failures and
engine audio underruns. It measured 40.54 headless FPS, with 26.41 ms p95 step
latency. This is not an interactive presentation or audible-playback measurement.

VERIFIED: the fix passed all 33 local core verifiers and the benchmark checksum
suite. The optional ROM video smoke was not included. The playthrough continued
through the battle and returned to the route by frame 38420. Five consecutive
segments from frame 34800 to 38420 completed with zero reported unsupported
instructions, fetch failures and engine audio underruns. Their headless
throughput was 39.00 to 44.03 FPS, including the battle.

### Release link-time optimization probe

OBSERVED: the same source was built with GCC 12.2 Release `-O3 -DNDEBUG` and a
second build added `-flto=1` to compilation and linking. In alternating
600-frame battle-checkpoint runs, the ordinary build measured 40.35/40.48 FPS
and LTO measured 49.49/49.21 FPS. In alternating boot runs, ordinary Release
measured 45.80/46.52 FPS and LTO 54.49/54.10 FPS. The run, video, audio and
final-state identities matched within each comparison, and all runs completed
without reported unsupported instructions, fetch failures or engine audio
underruns. These short, single-host headless measurements do not establish 60 FPS
or smooth interactive presentation. The LTO build is a local experiment, not
the default build configuration.

For a comparable GCC experiment, configure a separate Release build with
`-DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -flto=1"` and
`-DCMAKE_EXE_LINKER_FLAGS="-flto=1"`, alongside the normal generator and SDL3
prefix settings, and build with one worker. On this Windows host, the linker
failed with its inherited `C:\Windows\Temp` path; setting process-local `TMP`
and `TEMP` to an existing writable forward-slash path allowed the link to
succeed. Other compiler, platform and interactive-host configurations require
separate validation.

OBSERVED: adding `-march=native` to LTO measured 49.48/48.75 FPS versus
48.93/49.43 FPS for ordinary LTO in alternating battle-checkpoint runs, with
matching artifacts. The ranges overlap, so no useful gain was established;
the host-specific build remains experimental. A separate experiment disabling
the headless runner's unused fetch trace measured 50.37/50.40 FPS versus
50.74/50.19 FPS with the existing trace, with matching artifacts. Its ranges
also overlap, so that change was not added to the core or host.

### LTO save-state stack overflow

OBSERVED: the original LTO binary crashed with Windows status `0xC00000FD`
while saving a state after loading frame 38420. A one-frame synthetic ROM run
also reproduced the crash; the same one-frame LTO run without saving completed.
A debugger stopped in `___chkstk_ms` during save-state encoding. Reserving a
4 MiB executable stack made the original LTO binary save successfully, providing
evidence of stack exhaustion rather than a game-specific failure.

VERIFIED: moving the headless runtime to the heap allowed the synthetic LTO
save-state case to pass with the original 2 MiB stack reservation, and the
existing desktop save-type and state checks passed against the LTO build. An
800-frame replay from the post-battle checkpoint completed, reached Birch's lab,
and wrote a state byte-identical to the ordinary Release replay. Run, video,
audio and final-state artifacts matched. The final LTO candidate measured
51.51 FPS and 20.16 ms p95 in this single replay; the ordinary build measured
44.84 FPS in its earlier paired replay. Host load varied and these individual
runs do not establish a precise speedup or interactive smoothness. All 33 core
verifiers and the benchmark checksum suite passed; optional ROM video smoke was
not included. LTO remains a locally tested configuration, not a default.

### Prescaler division experiment and later lab checkpoint

OBSERVED: an LTO profile sampled 723 instruction locations over a completed
600-frame lab checkpoint run. Timer ticking accounted for 38 samples; samples
are not exact CPU-time shares. The Release binary performed integer division in
the timer prescaler path. All four divisors are powers of two, allowing the same
unsigned phase calculation to use a mask and shift.

OBSERVED: in alternating 600-frame battle-checkpoint runs, the LTO baseline
measured 50.12/49.79 FPS and the isolated prescaler candidate 52.43/52.58 FPS.
In alternating lab-checkpoint runs, baseline measured 50.88/51.95 FPS and the
candidate 55.20/55.26 FPS. Run, video, audio and final-state artifacts matched
within each comparison; all runs completed with zero reported unsupported
instructions, fetch failures and engine audio underruns. These short single-host
headless measurements do not establish 60 FPS or interactive smoothness.

VERIFIED: a timer regression compares all four prescalers, deferred ticks,
cycle boundaries and scheduler cycle counts crossing 32 bits against the prior
division-based arithmetic. The regression passed against both implementations.
Existing timer and integration tests remain necessary to cover overflow, IRQ and
cascade behavior.

OBSERVED: the LTO playthrough continued from the first battle into Birch's lab,
received Torchic and reached frame 42040. Four further 420- or 800-frame segments
completed with zero reported unsupported instructions, fetch failures and
engine audio underruns, at 47.67 to 52.07 headless FPS. Screenshots, input
scripts and the checkpoint remain under ignored `build/`.

OBSERVED: the complete rebuilt LTO candidate did not reproduce the isolated
lab gain. Alternating 600-frame lab runs measured 55.01/55.20 FPS for the
preserved baseline and 54.94/54.56 FPS for the rebuilt candidate, with matching
run, video, audio and state artifacts and no reported unsupported instructions,
fetch failures or engine audio underruns. Host load and build/link layout varied
between experiments. The full candidate passed all 33 core verifiers and the
benchmark checksum suite, but the speedup was not repeatable; the timer change
and its experimental test were removed from the proposed core.

### Profile-guided build and desktop audio

OBSERVED: an experimental GCC 12.2 build used `-O3 -DNDEBUG -flto=1` with
`-fprofile-generate=<profile-directory>` for training and
`-fprofile-use=<profile-directory> -fprofile-correction` for the rebuild. Boot
and the first battle were used for training; the lab checkpoint was held out.
Alternating 600-frame battle runs measured 52.87/52.89 FPS with ordinary LTO and
81.60/81.81 FPS with profile guidance. Alternating 1,200-frame lab runs measured
55.64/55.46 FPS with LTO and 85.53/85.96 FPS with profile guidance. The guided
lab runs had 12.72/12.64 ms p95 steps and two frames above 16.67 ms in each run.
Run, video, audio and final-state artifacts matched within each comparison; all
runs completed with zero reported unsupported instructions, fetch failures and
engine audio underruns. These measurements establish headless throughput above
60 FPS for these workloads on this host; they do not establish smooth interactive
play across the game or other machines.

The profile-guided build is experimental and is not the default configuration.
Its compiler reported missing profile data for four unexercised translation
units. Profile data and the user's ROM remain outside the repository. Training
and profile use must use the same build directory and profile directory; use a
fresh directory for a new training set. Validate any other compiler, platform,
training data and release configuration independently.

OBSERVED: with the original audio startup, three 1,200-frame real desktop boot
runs ended at 59.23 to 59.80 FPS in the host's last reporting window and recorded
11 to 14 empty-queue events. A separate 2,048-sample silence priming experiment
recorded zero in three runs. In a later alternating two-pair comparison of the
final profile-guided candidate and baseline, the baseline recorded 18/13 events
and the candidate zero/zero; last-window presentation rates were 59.09/59.71 and
59.97/59.87 FPS respectively. The candidate's profile also included a desktop
training run, so these builds differ in both profile data and audio initialization.
The priming adds approximately 62.5 ms of initial audio buffering. The counter
is a queue-empty proxy, not a direct measurement of audible glitches; these
short boot runs do not prove smooth play throughout the game.

DOCUMENTED: SDL reports `SDL_GetAudioStreamAvailable` in converted output bytes,
which can be affected by resampling and internal buffering; see the [SDL API
reference](https://wiki.libsdl.org/SDL3/SDL_GetAudioStreamAvailable). The host's
existing underrun counter is therefore not a precise input-queue or audible
underrun measurement. Reset, mute and long-session behavior need further testing.

VERIFIED: the final audio candidate's profile-guided build completed two
1,200-frame runs from the held-out lab checkpoint at 76.27/77.32 headless FPS,
with 14.13/13.93 ms p95 steps and 6/0 frames over the 16.67 ms budget. Run,
video, audio and final-state artifacts matched, with no reported unsupported
instructions, fetch failures or engine audio underruns. The desktop save-type
and state checks passed, as did the desktop rendering/error smoke using dummy
SDL drivers. All 33 native core verifiers and the benchmark checksum suite
passed; the optional ROM video smoke was not included.

### Interactive checkpoint loading and one-minute desktop run

OBSERVED: interactive mode accepted `--load-state` but ignored it. The desktop
host now uses the existing guarded save-state load path at startup, rejecting a
missing, corrupt, different-ROM or different-save-type state. A synthetic host
check failed before the change and passed afterward for valid, missing, corrupt,
wrong-ROM and wrong-save-type inputs. A ROM is required when this option is
used. Slot-based F8 loading still uses the same guard.

OBSERVED: a mixed build with profile-guided core code and the checkpoint-loading
host resumed the post-lab checkpoint in the real desktop host. A 3,600-frame run
completed in 61.18 seconds of external wall time including startup, reported
59.68 FPS for its last window, and recorded 10 queue-empty proxy events. In a
second instrumented run, four such events each had 28 input bytes still queued,
showing the existing available-output-byte counter is not a literal empty-input
queue or direct audible-underrun count. These runs used an isolated save
directory and no scripted input; they do not prove smooth interactive gameplay
or audible behavior over a full playthrough.

The interactive session summary now also prints average FPS across the host
loop, in addition to its existing last-window rate. This average excludes ROM
loading and window/audio initialization; the external wall-clock measurement
above includes them.

### Reproducing the Windows GCC profile-guided experiment

This procedure is for a local experiment with Windows CMake, Ninja, MinGW GCC,
and the SDL3 development package already installed. It uses the same build and
profile directories for both compilation stages. It does not enable profile
guidance in the default build or establish correctness on another machine.
Choose a fresh profile directory with no spaces in its path. Set the ROM,
checkpoint, SDL3 prefix, Windows CMake, Ninja, MinGW compiler and profile paths
below before running the commands; keep ROMs and states private.

```powershell
# Run from the repository root in PowerShell 7.3 or newer.
$ErrorActionPreference = 'Stop'
$rom = 'D:/path/to/game.gba'
$checkpoint = 'D:/path/to/checkpoint.state'
$sdlPrefix = 'D:/path/to/SDL3/x86_64-w64-mingw32'
$cmake = 'D:/path/to/windows-cmake/bin/cmake.exe'
$ninja = 'D:/path/to/ninja.exe'
$cxx = 'C:/msys64/mingw64/bin/g++.exe'
$windres = 'C:/msys64/mingw64/bin/windres.exe'
$profileDir = 'D:/path-without-spaces/gba-pgo-profile'
$buildDir = Join-Path (Get-Location) 'build/pgo-desktop-example'
if ((Test-Path -LiteralPath $buildDir) -or (Test-Path -LiteralPath $profileDir)) {
    throw 'Choose new build and profile directories to preserve existing data.'
}
foreach ($path in @($rom, $checkpoint, $sdlPrefix, $cmake, $ninja, $cxx, $windres)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing input: $path" }
}
if ($profileDir -match '\s') { throw 'Use a profile directory path without spaces.' }
$sdlDll = Join-Path $sdlPrefix 'bin/SDL3.dll'
if (-not (Test-Path -LiteralPath $sdlDll -PathType Leaf)) { throw 'SDL3.dll is missing.' }
New-Item -ItemType Directory -Path $profileDir | Out-Null
$generateFlags = "-O3 -DNDEBUG -flto=1 -fprofile-generate=$profileDir"
$generateLink = "-flto=1 -fprofile-generate=$profileDir"
& $cmake -S . -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_CXX_COMPILER=$cxx" "-DCMAKE_RC_COMPILER=$windres" `
    "-DCMAKE_PREFIX_PATH=$sdlPrefix" "-DCMAKE_CXX_FLAGS_RELEASE=$generateFlags" `
    "-DCMAKE_EXE_LINKER_FLAGS=$generateLink"
if ($LASTEXITCODE -ne 0) { throw 'Instrumented configure failed.' }
& $cmake --build $buildDir --target gba-desktop --parallel 1
if ($LASTEXITCODE -ne 0) { throw 'Instrumented build failed.' }
$exe = Join-Path $buildDir 'apps/desktop/gba-desktop.exe'
Copy-Item -LiteralPath $sdlDll -Destination (Join-Path (Split-Path $exe) 'SDL3.dll')

# Train boot and a distinct checkpoint; the artifacts stay in the build directory.
& $exe --headless --rom $rom --frames 600 --frame-hash --audio-hash --state-hash `
    --artifact (Join-Path $buildDir 'training-boot.json')
if ($LASTEXITCODE -ne 0) { throw 'Boot training failed.' }
& $exe --headless --rom $rom --load-state $checkpoint --frames 600 `
    --frame-hash --audio-hash --state-hash `
    --artifact (Join-Path $buildDir 'training-checkpoint.json')
if ($LASTEXITCODE -ne 0) { throw 'Checkpoint training failed.' }
foreach ($name in @('training-boot.json', 'training-checkpoint.json')) {
    $artifact = Get-Content -LiteralPath (Join-Path $buildDir $name) -Raw | ConvertFrom-Json
    if ($artifact.run.completed_frames -ne 600 -or $artifact.run.stop_reason -ne 'completed' -or
        $artifact.run.unsupported_instructions -ne 0 -or $artifact.run.fetch_failures -ne 0 -or
        $artifact.audio.underruns -ne 0) {
        throw "Incomplete training workload: $name"
    }
}
if (-not (Get-ChildItem -LiteralPath $profileDir -Recurse -Filter '*.gcda' | Select-Object -First 1)) {
    throw 'No GCC profile data was written.'
}

# Reconfigure the same build directory to consume that profile.
$useFlags = "-O3 -DNDEBUG -flto=1 -fprofile-use=$profileDir -fprofile-correction"
$useLink = "-flto=1 -fprofile-use=$profileDir -fprofile-correction"
& $cmake -S . -B $buildDir "-DCMAKE_CXX_FLAGS_RELEASE=$useFlags" `
    "-DCMAKE_EXE_LINKER_FLAGS=$useLink"
if ($LASTEXITCODE -ne 0) { throw 'Profile-use configure failed.' }
& $cmake --build $buildDir --target gba-desktop --parallel 1
if ($LASTEXITCODE -ne 0) { throw 'Profile-use build failed.' }
Get-FileHash -LiteralPath $exe -Algorithm SHA256
```

The final desktop audio experiment also included a 600-frame interactive boot
training run with `--quit-after 600` and a separate `--save-directory`. The
instrumented binary ran slowly; its timing is not a performance result. Review
compiler warnings: this campaign saw missing-profile warnings for four
unexercised translation units. Do not suppress profile mismatch warnings or
assume untrained paths are optimized correctly.

Measure a separate workload after building, for example with
`tools/run-rom-playthrough.ps1 -RomPath $rom -ExePath $exe -LoadState $checkpoint -SaveState -Frames 1200 -Runs 2`.
The helper copies the initial checkpoint into its new local output directory,
records its SHA-256, and writes a final state and SHA-256 for each repetition.
Those state files can contain ROM and cartridge-save data; keep the output
private. For a comparison between builds, use the same checkpoint and input
script, and compare completion, run, video, audio, final-state and saved-file
identities before comparing FPS and frame times. Headless throughput is not a
substitute for a real desktop audio and presentation soak. On the tested
MinGW toolchain, linking also required process-local `TMP` and `TEMP` pointing
to an existing writable path with forward slashes; restore those variables
after the build if changing them in a persistent shell.

OBSERVED: the standalone recipe completed in fresh build and profile
directories using the corrected renderer candidate `a7fd4019bf89`, with boot
and Oldale checkpoint training. Four translation units reported missing profile
data. Two 1,200-frame no-input holdout runs from frame 47240 completed with
matching identities and measured 86.45 and 86.41 headless FPS; p95 step times
were 12.31 and 12.48 ms, with 10 and zero steps respectively over 16.67 ms.
Neither run reported unsupported instructions, fetch failures or engine audio
underruns. The profile and all ROM-derived data remain local.

OBSERVED: the resulting desktop executable loaded the Oldale checkpoint and
completed a 1,800-frame no-input run in 32.39 seconds including startup. It
reported 59.70 FPS averaged over the host loop, 59.56 FPS in the last window,
and zero audio queue-empty proxy events. This single run does not establish
audible audio quality or sustained smoothness during active gameplay.

OBSERVED: two subsequent 3,600-frame no-input desktop runs from a rival-battle
checkpoint reported 59.25 and 59.72 FPS average, with zero audio queue-empty
proxy events. Their external wall times were 61.53 and 60.89 seconds including
startup. Only the first requested a final window screenshot. The average timer
currently includes the final save flush and optional screenshot after the loop,
so the first result also includes capture overhead; these runs do not isolate
the cause of the difference or measure active-play frame pacing or audibility.
The desktop host now captures the end time immediately after the frame loop,
before that cleanup. In a local 120-frame diagnostic with a temporary one-second
delay, placing the delay inside the measurement yielded 32.24 FPS while placing
it after the loop end yielded 43.42 FPS. The delay was removed after the probe;
the ordinary Release build used for it is slower than the profile-guided build.

OBSERVED: a Windows-only local keyboard probe posted arrow key messages to the
owned desktop window after loading the frame-61880 checkpoint. A first attempt
without the extended-key flag did not visibly move the character and is not an
active-input result. With that flag, the character moved and triggered May's
dialogue. The 1,800-frame session reported 59.67 FPS average, 59.68 FPS in the
last window and zero audio queue-empty proxy events; most of the session was
spent at the dialogue. The executable predates the loop-timer correction and
the reported average includes its final screenshot and save cleanup. The probe
uses wall-clock key durations, so it is not a frame-exact deterministic replay;
it does not establish audible quality or smoothness throughout active play.
An additional 3,600-frame run from the same checkpoint used approximately 20
seconds of timed movement and dialogue inputs, then continued without input.
Its final screenshot showed Littleroot Town; the host reported 59.69 FPS average,
59.56 FPS in the last window and zero audio queue-empty proxy events. This uses
the same pre-timer-fix executable and includes final cleanup in the average. The
probe does not save a machine checkpoint, measure frame-time percentiles, or
verify what the audio sounded like.

OBSERVED: a fresh profile-guided build from commit `9bb6e132e057`, trained on
boot and the frame-68880 lab checkpoint, completed with four missing-profile
warnings. Repeating the same 3,600-frame Windows keyboard probe from frame 61880
with the corrected loop timer reached Littleroot and reported 59.70 FPS average,
59.36 FPS in the last window and zero audio queue-empty proxy events. Inputs
were wall-clock timed and the final position differed from the previous run;
this is not a deterministic replay or a frame-pacing or audibility check.
Two separate 1,200-frame no-input headless holdout runs from frame 47240 measured
87.66 and 86.07 FPS, with p95 step times of 11.98 and 12.21 ms and no steps
over 16.67 ms. They matched each other and the earlier build's saved-state,
video and audio hashes, with zero reported instruction, fetch or engine-audio
failures. The two profile builds used different training checkpoints.

### Repeatable checkpoint experiments

The local experiment helper accepts `-LoadState` to copy a starting checkpoint
into a new output directory and record its SHA-256. `-SaveState` requests a
separate final state and hash for each run; repeated runs compare those hashes
alongside the existing run, video, audio and machine-state identities. A failed
run remains a failed run even if it produced a partial state. These local state
files can contain ROM and cartridge-save data and must not be committed.

VERIFIED: a generated synthetic-ROM check exercised repeated checkpoint replay,
saved-file hash matching against a direct run, the unchanged boot-only summary,
missing and corrupt checkpoints, a different ROM, and preservation of an
existing output directory. The 33 core verifiers and benchmark checksum suite
also passed; the optional ROM video smoke was not included.

OBSERVED: two 120-frame, no-input real-ROM replays from the frame-47240
checkpoint completed through the updated helper with matching run, video,
audio, machine-state and saved-file identities. Both reported zero unsupported
instructions, fetch failures and engine audio underruns. The profile-guided
executable reports commit `18bc8702dcc1`; it is not the current helper commit.
The runs measured 63.62 and 56.89 FPS, with 22 and 71 frames exceeding 16.67 ms
and p95 frame times of 19.46 and 22.45 ms. Host samples during the runs recorded
96% and 97% system commit. This short, pressured headless replay verifies the
checkpoint helper with this ROM but neither advances the story nor establishes
smooth playback or a reliable performance comparison. Artifacts remain local.

OBSERVED: after memory pressure eased, five scripted segments continued from
frame 47240 to frame 48940, reaching the route north of town. Each segment was
run twice from a frozen starting state; repeat identities matched and each run
completed. The profile-guided executable used here predates the second-target
renderer correction described above. Across the ten 300- or 400-frame runs,
headless throughput ranged from 69.31 to 81.99 FPS and 13 of 3,400 frame steps
exceeded 16.67 ms. All reported zero unsupported instructions, fetch failures
and engine audio underruns. The local helper output retains input scripts,
checkpoint hashes, artifacts and screenshots for each segment. These short
headless runs do not verify sustained interactive presentation or audible audio.

OBSERVED: a further 300-frame scripted segment from frame 48940 was replayed
twice with the ordinary Release executable rebuilt with the two renderer fixes.
Both runs completed with matching identities, no reported unsupported
instructions, fetch failures or engine audio underruns, and reached frame 49240
near the route ledge. They measured 41.95 and 43.20 headless FPS, with all 300
steps in each run above 16.67 ms. Screenshots were enabled, so these figures are
not directly comparable to screenshot-free runs. The existing CMake directory
retained its older embedded commit label despite rebuilding the changed source;
the helper records the actual executable hash in the local summary. This run
predates the fresh profile-guided build and desktop measurement described above.
Another repeated 300-frame input segment completed deterministically at frame
49540 with zero reported instruction, fetch or engine-audio failures. It
measured 46.55 and 47.16 headless FPS with all steps over budget; the character
remained near the ledge, so the attempted route did not reach a new scene.
Three additional repeated segments continued to frame 50580 but also remained
near the ledge. These six runs completed with matching identities per segment,
no reported unsupported instructions, fetch failures or engine audio underruns,
and measured 44.75–48.61 headless FPS; all 2,080 frame steps exceeded 16.67 ms.
Screenshots were enabled. The input scripts and checkpoints remain local.
A further repeated 600-frame segment reached frame 51180 but remained near the
ledge; it completed deterministically at 47.22 and 48.33 headless FPS, with all
steps over budget and no reported instruction, fetch or engine-audio failures.
Four more repeated segments traversed the eastern grass and reached Oldale at
frame 53180. All completed deterministically with zero reported instruction,
fetch or engine-audio failures. These screenshot-enabled Release runs measured
47.09–48.25 headless FPS; 3,998 of 4,000 steps exceeded 16.67 ms.

OBSERVED: seven further repeated segments with the freshly rebuilt profile-guided
candidate advanced from frame 53180 to 60080, crossed Route 103, and completed
the rival battle; screenshots show Torchic reaching level 6 and the victory
message. Each pair had matching identities and no reported unsupported
instructions, fetch failures or engine audio underruns. Across 13,800 frame
steps, 19 exceeded 16.67 ms; individual screenshot-enabled runs ranged from
77.75 to 93.24 headless FPS, with p95 step times of 11.51–12.50 ms. The latest
checkpoint, input scripts and screenshots remain local. These measurements do
not establish smooth presentation or audible audio during active play.

OBSERVED: two further repeated segments advanced through the post-battle
dialogue and returned to Oldale at frame 61880. All four runs completed with
matching identities per segment and no reported unsupported instructions,
fetch failures or engine audio underruns. The screenshot-enabled runs measured
83.71–92.25 headless FPS, with seven of 3,600 steps exceeding 16.67 ms.

OBSERVED: four more repeated profile-guided segments returned to Littleroot,
entered Birch's lab, and reached the Poké Ball award at frame 68880. All eight
runs completed with matching identities per segment and no reported unsupported
instructions, fetch failures or engine audio underruns. Screenshot-enabled
headless runs measured 83.66–88.02 FPS, with p95 step times of 12.01–13.32 ms
and 49 of 14,000 steps above 16.67 ms. These runs used the earlier executable
before the desktop FPS timer correction; its core includes the renderer fixes.
The checkpoint and replay inputs remain local.

OBSERVED: seven repeated segments using the profile-guided build at commit
`9bb6e132e057` continued from frame 68880 to frame 76080, left the lab and
reached the running-shoes event. All pairs matched their replay identities and
reported no unsupported instructions, fetch failures or engine audio underruns.
The screenshot-enabled runs measured 82.41–88.83 headless FPS, with p95 step
times of 12.08–13.31 ms; 68 of 14,400 steps exceeded 16.67 ms. Repeated A
presses re-engaged May's dialogue before a single confirmation and movement
exited the lab. The checkpoint and input scripts stay local.

OBSERVED: a separate 600-frame no-input sample at frame 76080 completed on the
same build with zero reported instruction, fetch or engine-audio failures. Of
432 sampled instruction locations, 74 mapped to frame stepping, 73 to device
advancement, 52 to scanline rendering, 49 to Thumb stepping and 27 to 16-bit
memory reads. Sampling interrupts the process and these counts are not exact
CPU-time shares or evidence of a speedup; they identify areas for further
profiling. The local sample and its ROM-derived artifacts are not published.

OBSERVED: after the running-shoes event, a scripted Route 101 segment from frame
77680 stopped after 348 completed frames with a fetch failure at PC `0x4000`.
The ordinary Release and profile-guided executables reproduced the same stop
frame and final machine hash. A separate 348-frame checkpoint was replayed
successfully, and an instruction trace from it showed a second IRQ entering
while the first IRQ's handler was still active. The inner handler returned to
the original interrupted code; that code subsequently popped zero as a return
address and branched into BIOS space. The scheduler currently stores only one
HLE IRQ return address and one saved-register set. INFERRED: a nested IRQ can
overwrite the outer HLE context, causing the premature return. A focused
regression reproduced the premature return and passed after preserving the
outer IRQ register, return-address and return-timing state. The new v4 snapshot
format records nested contexts; the decoder also accepts v3 snapshots, while
older builds cannot read v4. From the original v3 checkpoint, the corrected
ordinary Release candidate completed the previously failing 600-frame segment
twice with matching identities and reached a wild Wurmple encounter. Both runs
reported zero unsupported instructions, fetch failures and engine audio
underruns; they measured below 60 FPS. All 35 core verifiers, the benchmark
checksum suite, the desktop save-type check and the synthetic checkpoint replay
check passed. Optional ROM video smoke was not included in the core suite.
The failing checkpoint, script and trace stay local. Independent review and
broader IRQ timing validation remain necessary.

### Desktop presentation pacing

OBSERVED: with the profile-guided IRQ candidate and `--frame-timing`, a
3,600-frame desktop probe averaged 59.72 FPS but recorded a 28 ms p95 interval
upper bound and 321 of 3,599 intervals over 25 ms. A no-input repeat also had
a 28 ms p95 upper bound and 151 of 1,799 intervals over 25 ms. In a separate
local probe, a requested 5 ms `std::this_thread::sleep_for` had a 15.78 ms
median; `SDL_DelayPrecise` had a 5.00 ms median. SDL documents that the precise
wait can busy-wait and can still return late due to scheduling:
https://wiki.libsdl.org/SDL3/SDL_DelayPrecise.

OBSERVED: in four alternating 600-frame no-input desktop runs from the same
checkpoint, the prior wait produced 55 intervals over 25 ms in each run and a
28 ms p95 upper bound; the precise-wait candidate produced zero such intervals
and an 18 ms p95 upper bound. The builds were retrained separately, so this is
not a controlled CPU-cost comparison. A separate 3,600-frame probe with about
20 seconds of timed movement and dialogue reported 59.73 FPS average, 18 ms p95
and 19 ms p99 upper bounds, a 20.94 ms maximum and no intervals over 25 ms. It
reported zero audio queue-empty proxy events. Inputs were wall-clock timed;
presentation-return intervals do not prove physical scanout or audible audio
quality. The precise wait may increase CPU or power use, especially on other
hosts; further active gameplay and platform measurements remain necessary.
The candidate was rebuilt with a fresh profile because the compiler rejected
the old profile after the source edit; both profiles used the same boot and
checkpoint training recipe and each reported four unexercised translation
units. Two 600-frame headless replays of the previously failing IRQ segment
completed with matching saved-state, video and audio hashes against the prior
build. They measured 89.59 and 90.08 FPS; those headless numbers do not measure
desktop pacing.

### Sound FIFO DMA investigation

OBSERVED: a local PCM capture from a saved route scene contained nonzero
samples only 0.97% of the time; separate lab and battle captures showed about
1% nonzero samples. The route capture's sample count and audio hash matched the
headless runner. A separate no-input boot capture covering frames 600–1199
was 0.99% nonzero, while the mGBA libretro reference was 98.07% nonzero over
the same nominal frame interval. The reference emitted 65,536 samples/second
and this core emits 32,768; frame phase and amplitude also differ, so this is
not a sample-aligned waveform comparison. Captures remain local.

OBSERVED: the saved scene configured both sound DMA channels with control
`0xB600`, which programs an incrementing destination. The core rejected such
FIFO requests because it required fixed destination mode, leaving the FIFO
empty. The [mGBA sound DMA implementation](https://github.com/mgba-emu/mgba/blob/master/src/gba/audio.c)
forces the transfer width and destination stepping for FIFO requests. The core
now accepts the programmed width and destination fields and uses its existing
four-word fixed-FIFO transfer; unsupported source increment/reload remains
unchanged.

VERIFIED: direct DMA regressions for both sound channels, four destination
modes and two programmed widths, plus a scheduler timer/refill regression,
failed against the old implementation and passed with the change. All 35 core
verifiers and the benchmark checksum suite passed; optional ROM video smoke
was not included. A corrected local route capture was 93.63% nonzero and its
hash matched the corrected headless runner. The corrected boot capture was
97.00% nonzero. These measurements show the previous near-silence was reduced;
they do not establish hardware-accurate waveform or audible quality.

OBSERVED: a fresh profile-guided build with this correction reported four
missing-profile translation units. Two 1,800-frame battle replays matched
within the pair, completed at 85.55 and 81.14 headless FPS with p95 steps of
12.24 and 12.32 ms, and returned to the victory screen. Two 3,600-frame
replays from a later checkpoint also matched and completed at 88.36 and 88.82
headless FPS. These are single-host headless measurements.

OBSERVED: the first 3,600-frame desktop attempt stopped early on an unsupported
instruction at `0x03002158`. The wall-clock input sequence was not recorded
frame by frame, and two subsequent desktop runs did not reproduce the stop.
One repeat with eight save-state checkpoints finished at 59.7247 FPS average,
recorded eight presentation intervals over 25 ms (maximum 69.41 ms), and one
audio queue-empty proxy event; saving states perturbs pacing. A second repeat
without checkpoint writes finished at 59.7247 FPS average, with 18 ms p95 and
19 ms p99 interval upper bounds, no interval over 25 ms (maximum 24.25 ms),
and zero queue-empty proxy events. It used directional and Z-key (GBA A)
messages for about 46 seconds, then idle time. The original unsupported stop,
audio fidelity, audible output and longer active play remain unresolved.

OBSERVED: a further desktop retry used a locally instrumented host that would
record registers and a private checkpoint on an unsupported stop. It completed
3,600 frames without reproducing the stop. Because the host was rebuilt with
diagnostic changes and the input is wall-clock timed, this run does not rule
out the original failure or provide a controlled pacing comparison. The
diagnostic executable and any captured state are local only.

OBSERVED: for four one-second windows of the captured boot interval, the mGBA
65,536 Hz PCM was downsampled to 32,768 Hz using polyphase filtering, and each
candidate window was compared over a ±0.5-second lag range. The corrected
capture's highest absolute normalized correlations were 0.867, 0.885, 0.889
and 0.909, each at a reference lag of approximately -113.10 ms. A linear
least-squares level fit gave candidate/reference gains of 1.03–1.12 across
those windows. The pre-fix capture's best absolute correlations were
0.073–0.111. This is one boot scene, one reference implementation and an
estimated time alignment; filtering, phase and gain differences remain, and
these statistics do not establish hardware accuracy or audible quality.
Before-and-after WAV files derived from the local ROM remain under ignored
`build/` for listening; they must not be published.

OBSERVED: an extended run of that diagnostic host completed 10,800 frames
without reproducing the unsupported stop. It reported 59.7265 FPS average,
18 ms p95 and 19 ms p99 presentation-return interval upper bounds, four
intervals over 25 ms (maximum 26.6608 ms), and zero queue-empty proxy events.
Directional and Z-key messages were posted for about 172 seconds. This is a
single run with an instrumented host and wall-clock input; it does not prove
the original stop is fixed, physical display timing, audible audio quality, or
that every posted input produced movement.

OBSERVED: thirty-one further screenshot-enabled segments continued with the
corrected sound DMA build from Route 102 to Petalburg City. The player healed
the party (Torchic 27/27 HP and Wurmple 16/16 HP), entered the gym and followed
Wally's catching tutorial. Screenshots show the capture of Ralts and the
return to Norman's dialogue. All 62 runs completed with matching run, video,
audio, machine-state and saved-state identities within each pair and zero
reported unsupported instructions, fetch failures or engine audio underruns.
Across 49,600 frame steps, individual headless runs measured 81.86–93.93 FPS,
with p95 step times of 11.45–13.19 ms; 139 steps exceeded 16.67 ms. The
post-tutorial checkpoint and captures remain local. These short, single-host
replays do not resolve the earlier desktop stop or verify physical display
timing and audible audio quality.

### Extended desktop replay and buffered timing diagnostics

VERIFIED: a diagnostic desktop run recorded all 360 input-mask changes over
10,800 frames from the post-tutorial checkpoint. Replaying those frame-indexed
inputs headlessly produced a byte-identical final save state. The headless run
completed at 92.02 FPS, with a p95 step time of 11.58 ms and 12 steps above
16.67 ms. This verifies that session's input replay and final-state equality;
one headless run does not establish repeat determinism or smooth presentation.

OBSERVED: the recording desktop run averaged 59.5036 FPS, with six of 10,799
presentation intervals over 25 ms, a maximum of 238.771 ms and 35 audio
queue-empty proxy events. A subsequent synchronously logged timing probe
averaged 59.3814 FPS, with four intervals over 25 ms, a maximum of 655.101 ms
and 36 proxy events. Its 655.101 ms and 287.305 ms gaps included 638.060 ms and
270.958 ms respectively before frame stepping. That measured region included
SDL event handling, input pumping and synchronous diagnostic output; it did
not isolate event processing or establish the cause.

OBSERVED: a repeat buffered diagnostic records in memory and separated event,
input, logging, core, audio and presentation timing. It completed 10,800 frames
at 59.7242 FPS with zero queue-empty proxy events. One interval exceeded 25 ms
(25.159 ms); its core step took 19.949 ms, events 0.0064 ms and presentation
0.3691 ms. The earlier long pre-step pauses did not recur. This sequential,
single-host comparison with wall-clock inputs cannot attribute the difference
to logging alone. The local diagnostics have not changed the production host.
These runs used directional keys and X (GBA B). Production controls map Z to
GBA A and X to GBA B; the desktop controls table has been corrected accordingly.

### Continuous intro comparison

OBSERVED: a fresh no-input boot capture with the corrected FIFO build was
compared against all 600 existing mGBA reference frames. After five-bit channel
conversion, every candidate frame had an exact image match within twelve
reference frames; 240 matched at the same frame number. Thus no unmatched
image was found in this interval under that alignment search. Static images
can match several reference frames, so the result does not determine a unique
time mapping or explain the remaining animation phase offset. The comparison,
reference images and candidate screenshots remain local.

### FIFO count and completion follow-up

OBSERVED: fresh independent read-only review found no introduced defect in the
width/destination correction at commit `46c8a4d699f0`. It identified inherited
COUNT handling: the FIFO path transferred four words per request but deferred
completion and IRQ until the programmed count was exhausted. With COUNT=16,
repeat and IRQ enabled, the first request incorrectly omitted its IRQ.
[GBATEK's FIFO mode and sound buffer notes](https://mgba-emu.github.io/gbatek/)
describe ignoring programmed COUNT and sound DMA interrupts per sixteen bytes.

VERIFIED: the replacement regression fails against the previous implementation
at the first four-word completion IRQ assertion. With the countdown removed,
it passes across both sound DMA channels, counts 0, 1, 3, 4, 16 and 16383,
IRQ on/off and repeat on/off. IRQ flags are acknowledged between requests to
check fresh completion notifications; source continuation, fixed four-word
transfers and non-repeat completion are also covered. The previous regression
had asserted the incorrect programmed-block IRQ behavior and has been replaced.
Full-suite and ROM follow-up observations are recorded below.

OBSERVED: of those 600 intro comparisons, 267 had exactly one reference match
within the search window, and every such match was six frames earlier. The
first same-frame mismatch in this fresh capture is at frame 15: the initial
fade is one frame ahead of the reference on alternating fade steps through
frame 29. This refines the older first-divergence observation from a prior
candidate; the offset is not constant over the entire boot. The repeated
static images and frame-boundary sampling require a timing investigation
before adjusting emulated cycles.

OBSERVED: a second fresh independent read-only review of the count/completion
code and test delta found no introduced actionable defect. It confirmed the
manual's ignored-COUNT/four-word rules and noted that repeat-off FIFO operation
is outside the manual's required setup. The non-repeat regression preserves a
defined emulator behavior for that configuration; hardware fidelity there is
not established. Neither source review ran the local tests or accessed the ROM.

VERIFIED: the count/completion candidate passed all 35 native core verifiers
and the benchmark checksum suite. Optional ROM video smoke was not included.
The focused failure-before and passing-after executables and full logs are
retained locally. This corrects completion behavior; it is not a measured
throughput optimization.

### FIFO stop transition and early fade sampling

OBSERVED: the count/completion-only build changed 918 of 329,149 stereo sample
frames in a 600-frame boot capture, while sampled video hashes and the final
PC/cycle count matched the prior build. Swapping only the prior DMA controller
object into the new core reproduced the prior PCM exactly. A per-instruction
trace located the first non-counter divergence at a boot immediate DMA:
16,380 units / 32,762 bus cycles before the completion change, versus 16,384
units / 32,770 bus cycles afterward. The runtime had retained a large count
from FIFO initialization with programmed COUNT=0.

DOCUMENTED: the [AGB programming manual, section 12.4.2.2](https://pdfcoffee.com/gameboy-advance-programming-manual-pdf-free.html),
prescribes stopping FIFO repeat by programming COUNT=4 and selecting immediate,
32-bit, fixed-destination mode while Enable stays set. COUNT becomes effective
again on leaving FIFO mode. The core latched COUNT only on a new enable edge,
so that transition instead used its old internal count. The correction reloads
COUNT for this specific FIFO-to-immediate transition while preserving stream
addresses; ordinary enabled DMA control writes retain their prior behavior.

VERIFIED: a new regression failed before this transition fix and passed after
it. Both sound channels are covered after initial counts 0, 1 and 16, with
source continuation, four-word completion, bus-cycle accounting and cleared
Enable/immediate-pending state. The runtime trace motivated the fix; the prior
PCM comparisons do not prove improved audio fidelity.

OBSERVED: a separate diagnostic captured the first 40 boot images at VBlank
entry and at the current runtime's end-of-frame rendering point. All 40
VBlank-entry images matched the reference at the same frame number after
five-bit conversion. End-of-frame images matched 32, with differences on fade
frames 15, 17, 19, 21, 23, 25, 27 and 29. This isolates the early fade's sampling
phase discrepancy in that interval; it does not explain the later logo offset.
The diagnostic does not change the production runtime's rendering point.

OBSERVED: with the FIFO-stop correction linked into the diagnostic core, both
boot stop transfers completed as four words and ten bus cycles. The earlier
large transfer was therefore an incorrect use of the retained internal count,
not work required by the newly programmed COUNT=4. A 600-frame diagnostic
capture still found exact reference images for every frame within sixteen
frames. Of uniquely matched end-of-frame images, all 266 lagged six frames;
VBlank-entry sampling lagged seven for its 266 unique matches. Same-frame
matches numbered 240 and 251 respectively. Fixing the FIFO stop does not
explain the remaining logo phase offset; changing the rendering sample point
alone also does not remove it.

INFERRED: independent review of this candidate also identified an inherited
snapshot-validation issue: `DmaController::load_state` compares incoming active
count against the receiver's existing programmed count. A receiver configured
with COUNT=1 can reject a valid incoming COUNT=16 snapshot. This is separate
from the FIFO corrections and remains for a focused follow-up. The same review
found no introduced blocking defect in the expanded FIFO delta, but did not
run builds or tests. Source hashes were rechecked before the full suite.

VERIFIED: the final completion-and-stop candidate passed all 35 native core
verifiers and the benchmark checksum suite after the transition change.
Optional ROM video smoke was not included. Independent review remained bound
to the unchanged controller and regression-test file hashes.

OBSERVED: a fresh profile-guided desktop build embeds source commit
`1c0560007421` and reports four unexercised translation units during profile
use. Two 400-frame checkpoint replays on that build matched all identities,
completed at 92.74 and 91.05 headless FPS, and had no frame steps over 16.67 ms.
Their p95 step times were 11.41 and 11.69 ms. They advanced Norman's dialogue;
they do not establish sustained performance across the game.

OBSERVED: the same build's unmodified production desktop host then completed
10,800 frames with directional and X-key input posted for about 172 seconds.
It averaged 59.6984 FPS against the native target of approximately 59.7275 FPS,
with 18 ms p95 and 19 ms p99 presentation-return interval upper bounds. None
of 10,799 intervals exceeded 25 ms; the maximum was 23.8271 ms. It reported
32 audio queue-empty proxy events. No unsupported stop occurred. The final
window image shows the player near the gym exit; no desktop final machine
checkpoint was recorded in this production probe. Inputs were wall-clock timed,
so this is a bounded interactive observation rather than an exact replay.
Audio delivery and physical scanout remain unverified; the proxy events require
further investigation before claiming smooth audible output.

### DMA snapshot validation and further playback evidence

VERIFIED: a focused regression reproduced the inherited snapshot rejection:
a receiver with programmed COUNT=1 rejected a valid incoming latched COUNT=16.
Validation now bounds the incoming counter by the channel's capacity (0x4000
for DMA0–2, 0x10000 for DMA3). It cannot use either programmed COUNT because
software may change that register while the enabled channel retains its old
internal counter. The regression covers all four channels, independent counts,
exact capacity, excessive counts and unchanged receiver state after rejection.
It failed before the correction and passed afterward. All 35 native core tests
and the benchmark checksum suite passed. The serialized representation and
codec version are unchanged; this is a restore-correctness fix, not a speedup.
Two fresh independent read-only source reviews found no actionable defects in
the unchanged controller/test candidate. Neither reviewer ran the local tests.

OBSERVED: eight paired 400-frame story segments reached frame 161,240 outside
Petalburg gym after Norman's dialogue. All sixteen runs matched their replay
identities and reported no unsupported instructions, fetch failures or engine
audio failures. Headless throughput ranged from 86.38 to 92.74 FPS, with p95
step times of 11.36–12.28 ms and five steps above 16.67 ms across 6,400 steps.

OBSERVED: a separate local desktop diagnostic captured the application's SDL
postmix output in preallocated memory, writing it after the timed run. From
that checkpoint it completed 10,800 frames at 59.7244 FPS, against native
59.7275 FPS. Presentation-return intervals had 18 ms p95 and 19 ms p99 upper
bounds, a 24.7814 ms maximum, and none above 25 ms. No unsupported stop or
queue-empty proxy occurred. This used the corrected FIFO core at source
commit `1c0560007421`, before the snapshot validation follow-up.

OBSERVED: all 16,932 captured playback blocks contained finite float samples;
99.224% of samples were nonzero. The only five fully silent blocks were at
startup. None of 16,932 stream callbacks requested additional input, and the
capture reported no overflow. The 44,100 Hz stereo output covers approximately
180.84 seconds. This establishes bounded digital delivery for that diagnostic;
it does not verify physical speakers, scanout or waveform accuracy, or explain
the production run's earlier 32 queue-empty proxies. The final local machine
checkpoint is frame 172,040. Game-derived PCM, images and states remain private.

### IWRAM byte access timing investigation

VERIFIED: snapshot validation was published as commit `1e84eb6dddce` in draft
PR #47, stacked on #46; all four Windows/Linux core/desktop CI jobs passed.
A fresh profile-guided build of that commit completed three further paired
story segments, reaching Scott's encounter at frame 174,540. Their six runs
matched replay identities and saved-state bytes, measured 85.79–89.09 headless
FPS with p95 step times 11.86–12.34 ms, and had no steps over 16.67 ms.

OBSERVED: a separate unprofiled LTO build completed an earlier matching
600-frame replay pair at 57.25–57.35 FPS. The prior profile-guided build ran
the identical input/checkpoint at 83.51–86.54 FPS and produced byte-identical
final states across all four runs. These differing build configurations are
not a controlled source-change performance comparison; an unprofiled build
does not satisfy this segment's throughput target.

OBSERVED: a boot RAM timeline narrows the remaining intro offset. The reference
switches its main callback at frame 176 and reaches the logo fade task at 209;
the candidate switches at 175 but reaches the next task at 215. The candidate
spends most of the intervening cycles executing an IWRAM byte-copy loop during
save initialization. The reference finishes that work at approximately frame
204, then spends several frames in BIOS decompression/copy routines. The
candidate finishes the byte copy around 213 and its HLE setup much sooner.
The final image offset therefore includes more than one timing discrepancy.

VERIFIED: a local synthetic cartridge isolates IWRAM Thumb code copying bytes
from SRAM (eight waits) to EWRAM, measured by timer 2. At lengths 64, 128 and
192 bytes, the current core reports 1,861 / 3,717 / 5,573 cycles; the reference
reports 1,349 / 2,693 / 4,037. The slopes are 29 versus 21 cycles per byte.
A local hypothesis probe separating the one-cycle IWRAM instruction fetch
from data access, including the cartridge bus cycle in addition to its waits,
matches all three reference timer results exactly. The fixture is synthetic;
no game assets or instruction sequences are included in it.

INFERRED: the existing byte-transfer estimate applied data-bus timing to both
the data access and the instruction fetch. The candidate correction uses one
shared calculation for ARM and Thumb byte loads/stores fetched from IWRAM.
Other widths, fetch regions and static estimate APIs retain their behavior.
The focused regression failed before the correction and passed afterward,
covering RAM, SRAM, ROM loads, signed loads, all four wait settings and payloads.
A fresh independent read-only review of the unchanged source/test candidate
found no actionable defect; it did not run tests. Its timing analysis used
the reference's [Thumb handlers](https://raw.githubusercontent.com/mgba-emu/mgba/master/src/arm/isa-thumb.c),
[ARM handlers](https://raw.githubusercontent.com/mgba-emu/mgba/master/src/arm/isa-arm.c)
and [memory timings](https://raw.githubusercontent.com/mgba-emu/mgba/master/src/gba/memory.c).
Full-suite, integrated fixture and ROM follow-up results are recorded below
when observed. BIOS HLE decompression timing and physical hardware accuracy
remain separate investigations.

VERIFIED: the byte-timing candidate passed all 35 native core verifiers and
the benchmark checksum suite; optional ROM video smoke was skipped. Linking
the actual candidate CPU object into the synthetic fixture runner reproduced
the reference's 1,349 / 2,693 / 4,037 timer values exactly. This validates the
integrated execution path, in addition to the direct CPU unit assertions.
A 600-frame intro diagnostic completed without a fetch failure or unsupported
instruction. These cycle-correctness results do not establish a host speedup.

OBSERVED: after this correction, all 600 intro images still have exact nearby
reference matches after five-bit conversion. Uniquely matched end-of-frame
images now lead the reference by four frames (275 unique matches), rather
than lagging six; VBlank-entry images lead by three (274 unique matches).
The logo fade task now starts at candidate frame 205 versus reference 209.
The reference capture was extended to 610 frames to avoid truncating matches
for the last candidate images; its first 600 images match the prior reference
capture exactly. The remaining lead is consistent with the separately observed
missing HLE decompression/copy time, but that causal model is not yet verified.

OBSERVED: the profile-guided build of `c6bd681719fa` reached Route 104 in two
matching 1,200-frame runs at 91.30 FPS, with p95 step times 11.72–11.88 ms
and no steps over 16.67 ms. Draft PR #48's four core/desktop CI jobs passed.
An instrumented 10,800-frame desktop run from that checkpoint averaged
59.7237 FPS versus native 59.7275, with p95/p99 interval upper bounds of
18/19 ms. Two of 10,799 presentation-return intervals exceeded 25 ms; the
maximum was 27.2611 ms. No unsupported stop or queue-empty proxy occurred.
Both hitches include extended `step_frame` wall times (19.50 and 21.61 ms),
with event/presentation spans below 1 ms. This does not separate emulator CPU
work from operating-system scheduling delays inside the measured span.

OBSERVED: that run's SDL capture contained 16,933 blocks, all finite samples,
no overflow and no additional-input requests. Five fully silent blocks were
at startup; three more occurred around 0.58 seconds. A 120-frame core replay
using the recorded input masks produced matching source silence. After
resampling and alignment, the first second correlates 0.9987 with delivered
audio, with gain 0.9982 and a 62.49 ms sample lag. Each of the three later
silent blocks maps entirely into an all-zero source PCM interval. This
supports source-generated silence there rather than a delivery starvation
diagnosis. Physical speakers and waveform accuracy remain unverified.

VERIFIED: replaying all 360 recorded input changes through a diagnostic lab
host with the same profile-guided core produced a byte-identical final state
to the desktop capture. It completed 10,800 frames at 88.98 headless FPS,
p95 12.25 ms and p99 13.82 ms, with 14 steps above 16.67 ms. The two desktop
hitch frames took 11.25 and 11.16 ms in this replay; their slowdown did not
repeat at those game frames. This single replay establishes cross-host state
agreement, not repeated-run performance acceptance. The helper correctly
reports its repeated-run target flag as false for one run. Per-frame timings
were written from the existing measurement buffer after the timed region.

VERIFIED: a separate synthetic LZ77-to-VRAM fixture outputs the correct
literal bytes on both cores, but exposes missing work timing. For 8, 64 and
128 output bytes, this core's timer reads 9 cycles in every case; the reference
reads 409, 2,705 and 5,329. A 32 KiB reference fixture remains in its BIOS
stall through five frame captures and completes by the sixth. A replacement
HLE model therefore needs to preserve frame-boundary progression, device/IRQ
events and checkpoint restoration while work is pending. Merely adding one
large atomic cycle charge would not establish those properties. No LZ77
production timing change has been made yet.

OBSERVED: six further paired story segments from the desktop's frame-186,540
checkpoint reached frame 189,540, including Route 104 movement, a trainer
encounter and the battle introduction. All twelve runs matched replay and
saved-state identities. Across 6,000 frame steps, throughput was 84.00–91.13
FPS, p95 step time 11.54–12.68 ms, and thirteen steps exceeded 16.67 ms. The
original unsupported stop remains unreproduced and unexplained; these bounded
successes do not establish that it is fixed.

### IRQ admission and checkpoint word integrity (2026-10-27)

OBSERVED: the next paired 1,000-frame story segment reached frame 190,540
with identical final states at 84.32 and 85.12 FPS. Both runs completed with
zero reported audio underruns; p95 step times were 12.65 and 12.43 ms, with
three steps above 16.67 ms across the two runs. The battle remains active.

VERIFIED: a fresh cumulative integration review identified two IRQ defects,
both reproduced against `d1ede55debbe` before repair. Ordinary IRQ admission
already populated the return LR, so dispatch mistook the first handler for
a nested handler. The reproducer observed one phantom outer frame during
dispatch and a populated return LR after return. Dispatch now requires the
saved registers of an actually dispatched handler before pushing an outer
frame. Tests cover successive ordinary admissions and a real nested admission,
including register/PC restoration and complete context cleanup.

VERIFIED: the generic byte-container hash discarded the upper 24 bits of
saved IRQ register words. Flipping bit 8 of an outer saved r0 passed the old
codec's integrity check. Auditing the other callers exposed the equivalent
upper-byte omission for APU wave RAM halfwords, also reproduced as an accepted
corrupted snapshot. The current hash uses explicit little-endian word hashing
for active/outer IRQ registers and both wave RAM banks. Regression assertions
corrupt each of their serialized bytes and require rejection without changing
the receiving session.

The writer now emits version 5 with the unchanged v4 payload layout and the
corrected integrity calculation. Versions 3 and 4 remain readable using their
historical hash calculation; their inherited integrity gaps cannot be repaired
retroactively. Golden hashes captured from the previous encoder cover idle,
active IRQ, nested IRQ and nonzero two-bank wave RAM snapshots. New v5 files
cannot be read by earlier builds. No game assets or private evidence are
included in this change.

VERIFIED: both focused verifiers pass, followed by all 35 core verifiers
(211.9 seconds) and the benchmark checksum suite. Optional ROM video smoke
was skipped. Full integration review and rebuilt playthrough measurements
remain separate gates; the prior FPS results above describe the previous build.

OBSERVED: one fresh read-only review of the cumulative integration candidate
found no actionable defects after this repair. Its 37-file production/test/tooling
manifest remained unchanged throughout the review. A second fresh perspective is
still required for the accumulated persisted-format and CI changes.

### Integration and Petalburg Woods checkpoint (2026-10-27)

OBSERVED: PR #49 integrated the accumulated stack into `main` as
`449d13cf9fb17d97601950a2ccef292a39202350`. All four Linux/Windows core and
desktop CI jobs passed on candidate `7819099bb2f3b3a7768672576827cfc31b2fad67`.
Two fresh independent read-only cumulative reviews found no actionable defects.
The first bound the 37 production/test/tool/CI files to manifest SHA256
`E5C8E2C0A304C8DDBF874FBB697F9CC9668C9573F0AFF6D23F6D55458604BA70`, unchanged
after committing; the second checked the exact committed candidate before and
after review. The merge gate confirmed that head and a clean merge state.
The canonical checkout was fast-forwarded and observed clean on `main`.

VERIFIED: a new profile-guided executable built from `7819099bb2f3` has SHA256
`642987d8dd91c76ec562ccb1758a2d70ea1817f858a2cd2d01d6bd26d8221781`. Training
used 600 boot frames and 600 frames from the latest battle checkpoint. The
changed training workload means these measurements are not a controlled
source-speedup comparison. Four unexercised translation units reported missing
profile counts; the build completed successfully.

Three paired story segments (1,000 / 1,000 / 800 frames) loaded the existing
v4 checkpoint, finished the Lady Cindy battle, raised Torchic to level 10 and
entered Petalburg Woods at frame 193,340. Every pair produced identical final
states. The six runs measured 85.93–89.49 FPS with fourteen steps over 16.67 ms
across 5,600 steps, zero execution failures and zero reported core audio
underruns. The final 800-frame pair had no over-budget steps. A read-only
checkpoint inspection observed v5, released buttons and no active IRQ context
at the new checkpoint. Earlier private notes naming Rich Boy Winston were a
trainer-identification mistake; the victory screen and public map identify
Lady Cindy.

VERIFIED: a diagnostic host using the same profile-guided core repeated the
earlier recorded 10,800-frame movement workload twice with matching final
states. Throughput was 90.65 / 88.16 FPS, p95 11.92 / 12.39 ms and p99
12.73 / 13.29 ms. Nineteen steps exceeded 16.67 ms across the two runs;
none occurred at the same game frame in both repetitions. Buffered raw thread
cycle counts accompany the per-frame wall measurements. They are not converted
to elapsed time, as required by [Microsoft's API documentation](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime).
The changing outlier locations weaken a deterministic guest-frame hotspot
hypothesis but do not identify a host scheduling or cache cause.

OBSERVED: another instrumented SDL run completed 10,800 frames at 59.7245 FPS
against native 59.7275 Hz. Both p95/p99 presentation-interval upper bounds were
18 ms; the maximum interval was 23.0567 ms, with no intervals over 25 ms and
no queue-empty proxy events. Step p95/p99 were 11.64 / 12.15 ms, maximum
18.74 ms, with four steps over 16.67 ms. The capture contained 16,932 blocks
of finite 44.1 kHz stereo float output, no overflow and no additional-input
requests. Five startup blocks and blocks 54–56 were fully silent, matching
the earlier capture's silent-block positions. Exact source-silence alignment
was established for the earlier capture, not repeated for this one. All PCM,
states, screenshots and profiles remain private ignored evidence.

VERIFIED: the normal executable replayed all 360 input changes from that
desktop run and produced a byte-identical final state (SHA256
`2ffdaf326c631f556eadce08687c2f4e607b4363b96405825944b4cdbf3c9226`). It
completed 10,800 frames at 90.99 FPS, p95 11.77 ms and p99 12.45 ms, with
twelve steps over 16.67 ms. This is one cross-host replay, so the helper
correctly leaves its repeated-run acceptance flag false.

The campaign remains active: missing HLE decompression work time, the remaining
intro timing offset and the original unexplained unsupported stop are open.
These bounded successful runs do not establish their resolution.

### Wider IWRAM transfers and LZ77 cost isolation (2026-10-27)

VERIFIED: 44 independent synthetic decompression cases produced the expected
bytes on the reference and current core. They vary literal/backreference tokens,
ROM/EWRAM/IWRAM sources, VRAM/EWRAM/IWRAM destinations and four cartridge wait
settings. This core still reports 9 timer cycles for every one, confirming
that successful output alone does not establish work timing. For example:

| Source / destination | 64-byte payload | Reference timer |
| --- | --- | ---: |
| ROM / VRAM | literals | 2,705 |
| ROM / VRAM | backreferences | 1,377 |
| EWRAM / VRAM | literals | 2,559 |
| IWRAM / VRAM | literals | 2,410 |
| ROM / EWRAM | backreferences | 1,413 |
| ROM / IWRAM | backreferences | 1,161 |

A further 54 caller cases vary ARM/Thumb execution, IWRAM/EWRAM/ROM code,
prefetch and wait settings. All payloads verified correctly. This exposed an
independent CPU defect: prefetch reduced an ARM IWRAM-to-IWRAM word load by
one cycle, although no cartridge instruction fetch was involved. The timing
helper now handles wider single transfers when both instruction and data are
in IWRAM: loads take three cycles and stores two. Its existing byte behavior
is shared with the new path. Block transfers, swaps and ARM PC destinations
are outside this helper.

VERIFIED: new ARM/Thumb word, halfword and signed-halfword load assertions and
word/halfword store assertions cover four WAITCNT settings, including a forced
Thumb overlap request. They failed before the repair and pass afterward. The
actual CPU object linked into the synthetic caller runner now yields the same
9-cycle baseline for all IWRAM ARM/Thumb caller cases. All 35 core verifiers
pass (197.7 seconds), as does the benchmark checksum suite. A fresh independent
read-only review found no actionable defect in the frozen source/test candidate.

VERIFIED: a separate token/memory cost model predicts all 44 data cases and
all 18 IWRAM caller cases exactly after that CPU correction. It accounts for
flag groups, literal tokens, backreference lengths, typed memory reads/writes
and fixed call overhead. The arithmetic was cross-checked against the public
[reference BIOS handler](https://raw.githubusercontent.com/mgba-emu/mgba/master/src/gba/bios.c)
and [memory timing implementation](https://raw.githubusercontent.com/mgba-emu/mgba/master/src/gba/memory.c).
ROM callers with prefetch and EWRAM callers expose additional differences in
the existing call/fetch timing; the IWRAM agreement is not generalized to them.
The model has not yet been installed as production HLE work timing. That change
must also handle frame progression, IRQ interruption, nested calls and state
restoration while work remains pending.

VERIFIED: a fresh 600-frame intro capture linked to the corrected CPU object
completed without a fetch/unsupported stop. Every end-of-frame and VBlank
image still has an exact nearby reference match after five-bit conversion.
Same-frame matches remain 249 / 267; uniquely matched images retain the
four-frame / three-frame lead respectively. This CPU repair does not resolve
that remaining HLE/render-phase timing offset.

### Pending LZ77 work and checkpoint migration (2026-10-27)

OBSERVED: PR #50 merged the IWRAM transfer correction into `main` as
`c3a98985ce691e11bc7f562b1aa0f02e42cc11db`. Its exact candidate
`0c75e4bbf25a13784f3e4be82267750ab18b3f2f` passed all four Linux/Windows
core and desktop CI jobs and a fresh independent read-only review.

VERIFIED: the profile-guided executable from that candidate completed another
800-frame paired story segment, reaching frame 194,140 in Petalburg Woods
(map 24/11, x19/y31). Final states match. Throughput was 87.40 / 87.58 FPS,
p95 12.41 / 12.08 ms, p99 14.84 / 12.50 ms and maximum 32.56 / 20.23 ms;
three / one steps exceeded 16.67 ms. Execution failures and reported core audio
underruns were zero. This segment adds playthrough evidence but does not explain
the outliers or prove a sustained absence of hitches.

VERIFIED: a new synthetic regression failed because the caller executed its
next store immediately after large LZ77 decompression. The pending-work
implementation passes the same regression: a 32 KiB EWRAM-to-VRAM literal
stream holds the caller across four complete frames and resumes it in the fifth.
Timers continue advancing and every decompressed byte is checked.

The shared scheduler now holds remaining work cycles and the IRQ depth that
owns them. Device advances are bounded by the next frame, PPU transition,
timer/serial event and IRQ admission delay. DMA time advances devices without
consuming work cycles. Interrupting handlers pause the caller's work and can
queue their own work; completion resumes the appropriate interrupted context.
Raw ARM, raw Thumb and fetched stepping enter the same work path.

VERIFIED: focused assertions cover raw/fetched equivalence, IRQ admission,
handler-owned decompression, pause/resume, deterministic nested snapshot
continuation, immediate/HBlank DMA, timer accounting, APU sample production,
reset and rejected pending-work corruption without target mutation.

VERIFIED: all 98 independent synthetic payloads still match their expected
output. Timer results now match the reference exactly for all 44 data-region
cases and all 18 IWRAM ARM/Thumb caller cases. EWRAM callers retain differences
of +1/+8/+10 cycles; ROM callers range from -4 to +350 cycles across the tested
prefetch/wait configurations. These remain open call/fetch timing differences.

The v6 writer appends work frames and includes their complete cycle/depth values
in integrity hashing. Readers retain v3/v4 historical hashes and the v5 full-word
hash. Empty work leaves the existing machine hash unchanged. Zero-duration,
out-of-context and unordered work owners are structurally rejected. Earlier
builds cannot read v6 checkpoints.

Scope: output memory and clobbered registers are updated eagerly, followed by
charged device time, as in the reference HLE architecture. Intermediate BIOS
write visibility to DMA or an IRQ handler is not modeled. This is bounded HLE
timing evidence, not physical BIOS fidelity. Full core gates, fresh persisted
format reviews and new intro/performance measurements are pending at this point.

OBSERVED: two fresh independent read-only reviews covered the code, tests and
both runner registrations. The first identified a missed HBlank DMA boundary:
with HBlank IRQs disabled, a work chunk could pass cycle 1004 before running
the transfer. The repair explicitly bounds that event. VERIFIED: a synthetic
HBlank DMA that starts Timer0 failed its timing assertion before the repair and
passes afterward. The first reviewer refreshed the affected review; both final
code/test/tool manifests matched and had no outstanding findings.

INFERRED: the reference's remaining ROM-prefetch discrepancies include an HLE
artifact worth isolating before further tuning. Its eager decoder calls
`GBAMemoryStall` for RAM transfers while the caller's ROM region and prefetch
state are still active. The inspected calculation can reduce those charged
costs, consistent with payload-dependent deltas in the synthetic fixtures.
This source inference does not establish hardware timing or resolve the
remaining call/fetch differences.

VERIFIED: the repaired candidate passes all 36 core verifiers (183.9 seconds)
and the benchmark checksum suite. The optional ROM video smoke was skipped.
These gates include v3/v4/v5 migration and the new v6 pending-work round trips;
optimized host intro and story measurements remain separate observations.

VERIFIED: a fresh profile-guided host built from
`73d597430b95ae1b8653a8282ead2cf99b528814` has SHA256
`a04a43eb1037b77db1ff0e5f16001a786ddbdc27a76e49c08a729254a148e2bb`.
Training used 600 boot frames and 600 frames from checkpoint 194,140; both
completed successfully. Unexercised translation units reported missing profile
counts. The changed training workload precludes a controlled speedup claim.

VERIFIED: all 600 intro images at each capture phase still have an exact nearby
reference match. End-of-frame same-frame matches increase from 249 to 297;
uniquely matched frames now lag by one frame, replacing the four-frame lead.
VBlank same-frame matches are 261 (previously 267), with a two-frame lag replacing
the three-frame lead. The earliest unique image moves from core frame 207 to
212 against reference frame 211. Thus charged decompression shifts this intro
sequence by five frames; the residual timing/presentation difference is open.

VERIFIED: four paired story segments (800 / 1,200 / 500 / 400 frames) loaded
the older v5 Woods checkpoint and continued through a wild level-5 Cascoon
encounter. All pairs were deterministic with zero fetch/unsupported failures
and reported core audio underruns. Throughput ranges from 81.42 to 92.45 FPS.
Eighteen steps exceeded 16.67 ms across 5,800 total steps, all in the first run
of the 500-frame segment; its repeat had none. This variation does not identify
the outlier cause. The last 400-frame pair measured 84.54 / 84.78 FPS with no
over-budget steps. Latest story checkpoint: frame 197,040, map 24/11, x9/y27,
Torchic level 10 and HP 23/29, battle still in progress. The v6 checkpoint was
decoded successfully during this progression. Private evidence remains ignored.

OBSERVED: PR #51 merged that exact candidate into `main` as
`b79da42cd29afdd04468870e2cf39ad9ae57f07e` after all four Linux/Windows core
and desktop jobs passed. The merge gate checked head `73d597430b95` and a clean
merge state. The canonical checkout was fast-forwarded and observed clean.

OBSERVED: a fresh diagnostic SDL run completed 10,800 frames from checkpoint
194,140 with 360 recorded input changes, including movement and battle inputs.
Average delivery was 59.7243 FPS against native 59.7275 Hz. Presentation interval
p95/p99 upper bounds were 18/19 ms, maximum 26.0338 ms, with five intervals over
25 ms. These outliers remain in the record; average real-time delivery alone
does not establish the requested smoothness.

VERIFIED: that capture contains 16,932 finite 44.1 kHz stereo postmix blocks,
no overflow, no additional-input requests/bytes and no queue-empty proxy events.
Step p95/p99 were 12.3265/13.1031 ms, maximum 20.8127 ms. Two emulation steps
exceeded 16.67 ms (frames 564 and 6,559), fewer than the five late presentation
intervals. Their raw thread-cycle counts were 65,975,582 and 54,445,241;
these are not converted to elapsed time. This does not yet identify the cause
of either emulation or presentation outliers. Silent postmix spans occur during
the new battle workload; source-silence alignment has not been repeated for
these spans. Postmix evidence is not physical speaker or OS loopback evidence.

The diagnostic executable SHA256 is
`ffb3756387803465073948eca3075819abc7458ce363fed7f0a9210fe5ede2c9`.
It uses the profile-guided core with an unprofiled instrumented host, buffered
timing/audio data and disk writes after the measured session. Its final v6
checkpoint is at frame 204,940, Woods x21/y34, with released inputs and no active
IRQ context. Normal-host exact replay and further outlier profiling follow.

VERIFIED: two normal-host 10,800-frame replays of all 360 recorded input
changes completed with identical video/audio/state results, zero execution
failures and reported core audio underruns. Their final states are byte-identical
to each other and to the SDL checkpoint: SHA256
`11e10e093a555f43c77553b459b8411ccef5455b15347b5769819808f1722203`.
Throughput was 88.2230/88.2942 FPS, p95 12.2532/12.2790 ms and p99
13.1075/12.9763 ms. Maximum steps were 21.4162/22.0676 ms, with three/two
over 16.67 ms. This verifies cross-host deterministic continuation for this
recording and sustained headless throughput above target; isolated hitches
and remaining intro/caller timing differences keep the full campaign open.

VERIFIED: a diagnostic 1,000-frame replay pair records per-frame wall time,
raw thread cycles and executed/skipped step counts without writing during the
timed loop. Both runs have identical logical step counts for every frame and
matching final states. SDL outlier frame 564 takes 12.7597/11.6196 ms in these
repetitions, with exactly 96,602 executed steps and four skipped steps each.
One new outlier occurs at frame 579 in the first run (19.2317 ms), versus
11.8964 ms at the same frame in the repeat, both with 101,354 executed steps.
The pair measures 88.73/89.85 FPS. This weakens a fixed guest-frame hotspot
hypothesis; raw cycle variation does not establish a host scheduling cause.

OBSERVED: buffered SDL phase logs associate four of the five late presentation
intervals with normal 11.29–12.41 ms emulation steps and 0.25–0.31 ms presentation
calls. The remaining interval includes the 20.81 ms emulation outlier. Work
outside those measured phases remains to be accounted for; full flush/pacing
durations and deadline errors are added only to the next ignored local probe.

VERIFIED: a new 240-frame core timeline was compared with cached reference
IWRAM captures. The main callback transition occurs at frame 176 in both.
The core's VBlank counter is already one higher at equal capture frame; matching
the moving intro image with a one-frame shift gives a counter difference of two.
This further separates capture phase from setup progress; it does not establish
a reason to adjust frame numbering or guest work by a fitted offset. Field
interpretation uses the public [main structure](https://raw.githubusercontent.com/pret/pokeemerald/master/include/main.h).

VERIFIED: the Cascoon battle finished, followed by the Devon employee event
and the Aqua grunt's arrival at story frame 200,040. Paired 1,200/600/1,200-frame
segments produced deterministic results with no execution failures or reported
core audio underruns. Their FPS ranges from 88.09 to 89.65, with six steps over
16.67 ms across 6,000 total steps. Latest story checkpoint remains separate from
the recorded SDL movement workload.

### Presentation deadline placement (2026-10-27)

OBSERVED: the second 10,800-frame diagnostic SDL run records every frame's
flush, wait and deadline error. It completes at 59.7241 FPS with p95/p99 interval
upper bounds 18/19 ms, maximum 25.8484 ms and four intervals over 25 ms. Its
16,933 postmix blocks are finite, without overflow or additional-input requests.
Emulation step p95/p99 are 12.3225/13.0456 ms, maximum 19.2606 ms, three over
16.67 ms. These are another bounded observation, not an improvement claim.

VERIFIED: all four late intervals follow an early frame with 2.77–3.07 ms of
work, 13.41–13.73 ms of intentional wait and only 0.0016–0.0026 ms of deadline
error. The following frame takes 10.85–11.78 ms of work. Save flushes are at
most 0.50 ms across the run. This demonstrates an ordering defect: presenting
before waiting makes the next presentation interval include the change in work
duration, even when the wait itself finishes on time.

The host now queues audio, waits for its scheduled deadline, then presents and
flushes the save. The deadline arithmetic, fast-forward bypass and reset/load
behavior are retained. This is a one-call movement; the strongest alternative
of reducing average core work would leave the measured interval variation in
place. Rendering and scheduling variation still require actual host measurement.

VERIFIED: all 36 core verifiers pass using the unchanged core binaries (3.2 s)
and the benchmark checksum suite passes. One fresh independent read-only review
of the frozen desktop source found no actionable defect, binding SHA256
`a90d8bc02dae6751309f9415447e8c0504970d46024883a2d8d827da8645352a`.
Optimized desktop build and post-change SDL validation remain pending here.

VERIFIED: the optimized build from `7e5546bba52922f22e2a8bc1757339189d56b716`
completed successfully. Normal executable SHA256:
`a30938c24c0f880166e9434c394bfae421ff95e3160d5e57f8907972847b8b67`.
Its 600-frame boot and 600-frame checkpoint training artifacts have identical
ROM/run/video/audio/state fields to the previous build. The diagnostic host
linked to this core has SHA256
`17be3e1df1d3ed14bb660d2ca8e77d9d209bd985b467c5bdbd8babe2879d371d`.

OBSERVED: the post-change diagnostic run completes 10,800 frames at 59.7290 FPS.

| Presentation measurement | Before | After |
| --- | ---: | ---: |
| Raw p95 interval (ms) | 17.5476 | 16.9231 |
| Raw p99 interval (ms) | 18.4297 | 17.2557 |
| Raw p99.9 interval (ms) | 22.2876 | 17.6564 |
| Maximum interval (ms) | 25.8484 | 17.9604 |
| Intervals over 25 ms | 4 | 0 |
| Fast-to-slow work transition median interval (ms) | 25.4542 | 16.7614 |

There are twelve fast-to-slow work transitions in the new recording, with maximum
interval 16.9958 ms. Correlation between interval and work-duration change falls
from 0.910 to 0.086. This supports the measured ordering repair. The recordings
use the same timed input pattern but different recorded frame positions and
separate profile training, so they are not a controlled core-speedup comparison.
The new run has no emulation steps over 16.67 ms and 16,931 finite postmix blocks,
with no overflow, additional-input requests/bytes or queue-empty proxy events.

OBSERVED: a separate run of the normal executable completes another 10,800 frames
at 59.7291 FPS, with measured last-window rate 59.7204 FPS. Interval p95/p99 upper
bounds are 17/18 ms, maximum 20.4454 ms, zero intervals over 25 ms and zero
reported audio underruns. It uses movement/battle input and the same checkpoint;
the normal host has no diagnostic postmix capture or automatic final-state dump.
These two bounded runs establish native-rate delivery with improved pacing in
the tested scenario; the campaign's remaining accuracy investigations stay open.

VERIFIED: a separate arithmetic check explains the payload-dependent delta in
all twelve nonempty ROM-caller controls, using their six zero-length controls
to isolate the fixed call/fetch offset. INFERRED from the pinned reference
[RAM-stall calculation](https://raw.githubusercontent.com/mgba-emu/mgba/7a12d6d/src/gba/memory.c):
its eager HLE decoder charges VRAM halfword accesses at -2 cycles while the
caller's ROM prefetch state remains active. Relative to normal read/write
costs, the difference is four cycles per backreference byte and three per stored
halfword. The fixed call/fetch offsets remain open. This is an oracle limitation;
physical BIOS timing remains unverified.

## Pacing integration and later intro coverage (2026-10-27)

VERIFIED: PR #52 merged as `4db5171067b1dce56ce6610d18199f5f74627096`
after all four Linux/Windows core and desktop jobs passed final head
`9f9425bd7239d7787720c482add7786a2f337579`. The canonical checkout was clean
and fast-forwarded to that merge. The final documentation commit did not change
the independently reviewed production source.

VERIFIED: the post-change diagnostic host's recorded inputs replay for 10,800
frames in the normal headless executable at 87.8712 FPS, with no execution
failure or core audio underrun. Its final state is byte-identical to the SDL
checkpoint, SHA256
`62830ea943736c801f3941321981969d3f6bb4b7d85d3cdcdbd3477e88a21a05`.
This particular replay has one run, so its repeated-run acceptance flag remains
false; the cross-host state comparison is separate evidence.

OBSERVED: six further 1,200-frame story segments each repeat twice from a frozen
checkpoint with identical final states and no execution failures/core audio
underruns. The route reaches the Aqua battle, defeats Poochyena, and returns to
the Devon employee. Frame 207,240 is still in post-battle dialogue at Woods
(26,23), with Torchic level 10 and HP 11/29. These headless runs do not measure
native presentation intervals.

### Initial intro offset: reference accounting experiment

VERIFIED: tracing the first 220 frames captures ten LZ77 calls, totalling
1,153,427 pending work cycles (4.1062 frame periods). Independently parsing the
local compressed inputs predicts every call's work exactly. Twenty-four private
isolated timer fixtures cover these ten payloads with ROM/IWRAM callers and
four empty controls. All decompressed outputs verify. Each IWRAM timer equals
queued work plus nine modulo 65,536, and all ten caller differences match the
reference RAM-stall prediction modulo 65,536 after fixed-control adjustment.
The 16-bit timer does not independently measure unwrapped multi-frame duration.

INFERRED from the pinned reference's previously documented prefetch behavior:
the aggregate payload-dependent difference is 283,245 cycles, or 1.00836 frame
periods. EWRAM accesses saturate the prefetch queue, so treating all RAM accesses
as having the same negative cost gives an incorrect total. Direct fixtures
reject that initial simplification. The corrected aggregate is arithmetic,
not a physical BIOS measurement.

VERIFIED: a private diagnostic changes only pending LZ work by those derived
RAM-accounting differences. It adjusts ten calls by 283,245 cycles. All 600
frame-end images then match the reference at the same frame; the VBlank capture
retains a one-frame phase difference. Production timing is unchanged. This
explains the original bounded comparison; it does not establish accuracy of the
entire intro or justify copying an oracle's prefetch quirk into the core.

### Longer intro and missing OBJ work limit

OBSERVED: a new 3,610-frame reference capture and 3,600-frame production capture
extend coverage past the original 600 frames. For frame-end snapshots, 1,975 of
3,600 frames have an exact match within 120 reference frames, and 848 match at
the same frame. VBlank counts are 1,979 and 672. The first unmatched frame-end
image is core 637 versus reference 636, differing at 30 pixels in a water ripple.
Later scenes also diverge; the complete capture is not an accuracy pass.

VERIFIED: at that first mismatch, palette, VRAM, OAM and the relevant display
controls are identical. Rendering the reference memory snapshot with our
renderer reproduces the extra 30 pixels. Disabling only OAM entry 5 in that
private snapshot removes all 30 differences. Thus the initial discrepancy is
in rendering rather than different decompressed assets or sprite attributes.

DOCUMENTED: [GBATEK's OBJ work limits](https://mgba-emu.github.io/gbatek/#maximum-number-of-sprites-per-line)
provide nominal 1,210/954-cycle allowances selected by DISPCNT bit 5. Normal
objects cost their width; affine objects cost ten plus twice their drawing-area
width, including double-size bounds. Transparent pixels still require work.
The previous renderer admitted every vertically intersecting object without a
work limit. Six overlapping affine ripple objects expose that missing behavior.

The candidate applies that nominal work model in OAM order before texture
transparency, occlusion or window selection. It retains the estimated prefix of
the last object and excludes later objects. This is a bounded accuracy increment.
Exact OAM fetch overlap, horizontal clipping savings, the hardware's last-pixel
endpoint and preceding-line fetch timing remain unmodeled. The
[hardware research description](https://nba-emu.github.io/hw-docs/ppu/sprite.html)
shows overlapping OAM/VRAM stages; its detailed endpoint and the reference
renderer's partial-object behavior cannot serve as exact hardware proof here.
No third-party implementation was copied into the candidate.

VERIFIED: the focused regression fails before the change on nineteen transparent
64-pixel objects followed by an opaque sentinel. It passes after, together with
reduced HBlank allowance, doubled affine width, disabled/other-line exclusion,
and explicit nominal-prefix checks. These prefix assertions verify the selected
model, not a measured hardware cutoff. All 36 core verifiers pass (38.8 s), and
the benchmark checksum suite passes. A fresh independent read-only review finds
no actionable issue within the stated nominal-model scope, binding renderer
SHA256 `95fc81eb03d1ae3e1132088af8964a3de9ffccdf2e6abbee7d748fa6b14e420d`
and test SHA256
`da5e17d1a259f9eb2a6fcb2d89ea3cc1d5ddb7a4b9b31c4663df59004643b57d`.
Optimized build and extended runtime comparison remain pending here. The review
also notes a separate pre-existing OBJ-window transparency defect; it is not
part of this budget change.
