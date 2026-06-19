# ROM Smoke Evidence Template

Copy to `docs/evidence/YYYY-MM-DD-rom-smoke-<short-name>.md` after a real-ROM session.

**Do not commit ROM bytes, BIOS, or user save blobs.**

## Session

| Field | Value |
| --- | --- |
| Date | |
| ROM filename only | e.g. `homebrew-demo.gba` |
| License class | homebrew / mGBA suite / private retail |
| Device or workstation | |

## Workstation (`run-rom-smoke.ps1` / `run-rom-video-smoke.ps1`)

```powershell
cd Project_Android/GBA_Emulator
.\tools\run-rom-smoke.ps1 -RomPath ..\local\test-roms\<file>.gba -Frames 600 -RequireValidHeader -ArtifactPath docs/evidence/YYYY-MM-DD-rom-smoke-<name>.md
```

Video gate (when `rom_video_smoke` is built):

```powershell
.\tools\run-rom-video-smoke.ps1 -RomPath ..\local\test-roms\<file>.gba -Frames 600 -RequireValidHeader -ArtifactPath docs/evidence/YYYY-MM-DD-rom-video-<name>.md
```

| Check | Result |
| --- | --- |
| Exit code | |
| Completed frames | |
| First abnormal stop | max_steps / fetch_failed / unsupported_instruction |
| Notes | |

### Video checkpoints (frames 1 / 8 / 60 / 120 / 600)

Record from JSON or stdout for each listed frame:

| Frame | `frame_complete` | `rendered_scanlines` | `dispcnt` | `unique_colors` | `dominant_color` (RGB565) | `dominant_ratio` | `framebuffer_crc32` | Pass |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | | | | | | | | |
| 8 | | | | | | | | |
| 60 | | | | | | | | |
| 120 | | | | | | | | |
| 600 | | | | | | | | |

**Fail hints:** `dispcnt=0` with `unique_colors=1` and `dominant_color=0x7FFF` → core blank / backdrop-only (not a presentation bug).

## Android (optional same day)

| Check | Result |
| --- | --- |
| APK | `app-debug.apk` |
| Load path | SAF / adb push cache |
| **RomVideoSelfTest** (debug Home) | PASS / FAIL / SKIP |
| Visible framebuffer ≥ 30s | |
| Input observable | |
| Audio (human) | NOT VERIFIED / audible / silent |
| Stop overlay | none / describe |

### Android video checkpoints (`adb logcat -s FlowframeEmu`, tag lines `rom_video_smoke`)

| Frame | `frame_complete` | `scanlines` | `dispcnt` | `unique_colors` | `dominant` | `dominant_ratio` | `framebuffer_crc32` | Match workstation? |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | | | | | | | | |
| 8 | | | | | | | | |
| 60 | | | | | | | | |
| 120 | | | | | | | | |

## Workstation gates (same day)

| Command | Exit |
| --- | --- |
| `.\tools\run-core-tests.ps1` | |
| `.\tools\run-credibility-matrix.ps1 -FailOnRegression -SkipPerformance` | |
| `.\tools\run-local-rom-video-smoke.ps1` (game ROMs only) | |

## Disclaimer (private retail only)

Retail cartridges used only as private compatibility smoke on hardware I own; not a compatibility guarantee or redistribution.
