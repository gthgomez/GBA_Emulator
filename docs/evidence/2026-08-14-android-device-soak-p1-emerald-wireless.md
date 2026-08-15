# ROM Smoke Evidence — Android Device Soak P1 (Wireless), Pokemon Emerald

Session ran 2026-08-14 21:08 → 2026-08-15 00:20 (PC-local), wireless adb throughout.

**Do not commit ROM bytes, BIOS, or user save blobs.**

## Session

| Field | Value |
| --- | --- |
| Date | 2026-08-14 (into 2026-08-15 local) |
| ROM filename only | `pokemon-emerald.gba` (workstation: `Pokemon - Emerald Version (USA, Europe).gba`) |
| License class | private retail (user's own cartridge dump) |
| Device or workstation | Device: Samsung SM-S938U (Galaxy S25 Ultra), Android 16, arm64-v8a, wireless adb |

## On-device builds

| Build | APK | Size | Notes |
| --- | --- | --- | --- |
| Debug | `app-debug.apk` (`com.gba.emulator.shell.debug`) | 21,849,588 B | P0 self-tests (4a), ROM video self-test (4b), first playtest CSV |
| Release | `app-release-signed.apk` (`com.gba.emulator.shell`), built 2026-08-14 21:46:08 | 16,075,964 B | Manual play (4c) — user-approved pivot to the real build |

Release core compiled with `CMAKE_BUILD_TYPE=RelWithDebInfo` (`-O2 -g -DNDEBUG`, verified in
`app/.cxx/*/w72j5346/CMakeCache.txt`); debug cores are `-O0`. The release `.so` is 295,616 B vs
299,328 B debug — comparable size, not the pacing bottleneck.

## Android (on-device, wireless adb)

| Check | Result |
| --- | --- |
| Load path | SAF (`ACTION_OPEN_DOCUMENT` + persistable URI permission) on both builds; ROM at `/sdcard/Download/pokemon-emerald.gba` |
| RomVideoSelfTest (debug Home) | **PASS** — frames 1/8/60/120, all `frame_complete`, 160 scanlines |
| Visible framebuffer ≥ 30s | PASS on boot (game played for minutes); FAIL at end (presentation loop stall) |
| Input observable | PASS — touch haptics fire; user's Start presses advanced the game |
| Audio (human) | **Audible but CHOPPY/GARBLED** (user listening, confirmed) |
| Stop overlay | `Audio playback underruns: 103` — stale by design (`GameScreen.kt:356` sets once); actual count reached 2,787 @ frame 60 on first boot |

### Android video checkpoints (`adb logcat -s FlowframeEmu`, tag `rom_video_smoke`)

| Frame | `frame_complete` | `scanlines` | `dispcnt` | `unique_colors` | `dominant` | `dominant_ratio` | `framebuffer_crc32` | Match workstation? |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | true | 160 | 0x0 | 1 | 0x7fff | 1.0000 | 0x6a9f3166 | ✅ (workstation 1,788,817,766 = 0x6a9f3166) |
| 8 | true | 160 | 0x0 | 1 | 0x7fff | 1.0000 | 0x6a9f3166 | ✅ |
| 60 | true | 160 | 0x140 | 5 | 0x0 | 0.9653 | 0xa1baed | ✅ (workstation 10,599,149 = 0xa1baed) |
| 120 | true | 160 | 0x140 | 5 | 0x0 | 0.9653 | 0xa1baed | ✅ |

Byte-identical framebuffer CRC at all four checkpoints vs the 2026-08-14 workstation 1500-frame
run (`docs/evidence/2026-08-14-rom-video-pokemon-emerald-1500f.md`).

## Manual play findings (release build, user-driven)

Playtest sessions (logcat `PlaytestDataLogger`, all under `/data/user/0/com.gba.emulator.shell/cache/playtest_*.csv` — **unreachable on release**: `run-as: package not debuggable`):

| Session | Start | Frame-60 audit | Stop |
| --- | --- | --- | --- |
| 1 | 23:36:58 | 23:37:10 (+12.0 s) — `playbackUnderruns=2787 coreUnderruns=0` | restart 00:16:06 |
| 2 | 00:00:32 | never (instant double-stop: 00:00:32.430 and .633, second logged `File saved: null`) | immediate |
| 3 | 00:03:03 | never (stalled before frame 60; no audit in 13 min) | restart 00:16:06 |
| 4 | 00:16:06 (restart button) | 00:16:21 (+15.7 s) — `playbackUnderruns=710778 coreUnderruns=0` | force-stop 00:17 |

| Check | Result | Evidence |
| --- | --- | --- |
| Boot sequence | ✅ | copyright → pre-title forest-chase cinematic → title "EMERALD VERSION" with blinking "PRESS START" (screenshots 14–18) |
| Title appears | ✅ | screenshot 15-title.png |
| Start advances title | ✅ | user's touch → NEW GAME flow → in-world with RTC dialogue "However, clock-based events will no longer occur." (RTC not emulated — expected) |
| Pacing | ❌ **FAIL** | SurfaceFlinger presents 500–670 ms apart (~1.4–2 fps) vs 16.7 ms target; frame 60 at +12 s first boot (~5 fps at start), +15.7 s after restart (~3.8 fps); **loop degrades to 0 presents** (0 presents in 8 s after `--latency-clear`); sessions 2–3 never reached frame 60 |
| Speed chips | ⚠️ PARTIAL | 2× chip selects (visual state change, screenshot 23); no observable pacing change while loop is stalled |
| Touch overlay | ✅ | haptic feedback on press-registered keys; hit-testing works (D-pad/A/B/Start/Select bounds verified) |
| Audio | ❌ **FAIL** | audible but choppy/garbled (user); 2,787 underruns @ frame 60 first boot; 710,778 @ frame 60 after restart (~255× worse) |
| Restart button | ⚠️ PARTIAL | ✅ reloads last ROM, clean session stop + start in logcat, video loop resumes; ❌ audio stream left broken (710k underruns) — restart does not reset Oboe state |
| Lifecycle | ⚠️ PARTIAL | phone lock/unlock during stall → no recovery (0 presents persisted); force-stop + relaunch → clean home screen, new pid, no ROM auto-resume (expected for this shell); no crash |
| In-game save | ⚠️ NOT REACHED | game never reached a save point at 1.4 fps; save-state round-trip from 4e = future work |

### Diagnosis

The -O2 core is **not** the bottleneck (`coreUnderruns=0` throughout; release APK verified
RelWithDebInfo). The release **presentation path** (Compose + `SurfaceView` `lockCanvas`
present) is: ~5 fps at session start, degrading to a full stall (0 presents) with the Compose
UI (chips, haptics) still responsive — consistent with a blocked/depleted BLAST surface buffer
pool in the frame loop, not a core hang. Workstation parity (identical CRCs) proves the core is
byte-for-byte deterministic on device.

### Evidence limitations

- Release build not debuggable → `run-as` fails; playtest CSV (frame-level ms/underruns) locked in app cache. Debug-build CSV from the earlier session is the pacing fallback.
- `uiautomator dump` chronically stale on this Samsung (`null root node`) → UI coords via screencap + vision fallback.
- `SurfaceFlinger --latency` layer name (`…SurfaceView…@0(BLAST)#id`) changes when the surface is recreated (restart) — re-query `--list` per measurement.
- Media (screenshots 00–27, ui dumps, logcat) kept in `Project_Android/local/device-session/` (gitignored), not committed.

## Workstation gates (same day)

| Command | Exit |
| --- | --- |
| `.\tools\run-core-tests.ps1` | 0 (PASS — all local C++ verifiers) |
| `.\tools\run-credibility-matrix.ps1 -FailOnRegression -SkipPerformance` | **1 — REGRESSION** (matrix `credibility-matrix-20260814-210843-10140`; first valid pwsh-7 run since 2026-06-01) |
| `.\tools\run-local-rom-video-smoke.ps1` | N/A — parity source is the 2026-08-14 workstation 1500-frame artifact (exit 1 at frame 1237 fade, documented there) |

### Matrix detail

**GREEN (14):** memory 1552/1552, loadstore 1552/1552, bios-math 615/615, dma 1256/1256,
shifter 140/140, carry 93/93, multiply-long 72/72, timer-irq 90/90, timers 936/936, timing
2020/2020, ldmia 160/160, stmia 60/60, sio-read 90/90, sio-timing 4/4.

**RED (2):**

| Target | Pass/total | First failure |
| --- | --- | --- |
| `io-read` | **51/130** | BG0HOFS (`other` bucket: 79) |
| `misc-edge` | **6/12** | H-blank bit start Hblank (`misc_edge`: 6) |

**MISSING (1):** `video` — baseline (`tools/mgba-suite-green-baseline.json`) expects the video
oracle alias group; current matrix produces no `video` target. Both failures reproduce across
two runs (19:56 and 22:30).

Both RED targets are IO/PPU-timing-adjacent (BG0HOFS = BG0 horizontal offset register;
H-blank bit = VCOUNT status) and the `video` suite is absent — suspicious cluster consistent
with recent PPU/timing work, **needs triage before any beta candidate**. These are
workstation-accuracy regressions unrelated to the device presentation stall.

## Disclaimer (private retail only)

Retail cartridge used only as private compatibility smoke on hardware I own; not a
compatibility guarantee or redistribution.

## Artifacts

- Screenshots `00-*.png`–`27-*.png` (self-test PASS, SAF load, title, in-world, chips, restart), logcat dumps, grok UI measurements → `Project_Android/local/device-session/` (gitignored).
- Playtest CSVs (debug) → session media dir; release CSVs unreachable (see limitations).
- Matrix artifacts → `build/test-results/credibility-matrix-20260814-210843-10140.{json,md}`.
