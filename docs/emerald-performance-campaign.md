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
