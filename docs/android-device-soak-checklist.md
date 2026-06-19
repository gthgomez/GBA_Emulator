# Android Device Soak Checklist

Use this for the **target device** evidence row in `controlled-beta-readiness.md`.
Do not commit ROMs, BIOS, saves, or user-provided copyrighted bytes.

**Latest P0 artifact:** [`evidence/2026-06-03-android-device-soak-synthetic-pass.md`](evidence/2026-06-03-android-device-soak-synthetic-pass.md)

## Preconditions

- [ ] `GbaEmulatorAndroid` debug APK built (`assembleDebug`)
- [ ] Device or emulator API 26+ (`arm64-v8a` preferred; `x86_64` emulator OK for P0)
- [ ] `adb devices` shows one target
- [ ] Workstation core gates passed (`run-core-tests.ps1`, etc.)

## Install And Launch

```bash
cd Project_Android/GbaEmulatorAndroid
./gradlew :app:assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.gba.emulator.shell.debug/com.gba.emulator.shell.MainActivity
```

- [ ] Cold start: app opens without native crash
- [ ] Tap **Run bridge self-test** → UI shows `PASS` (steps=3)
- [ ] Tap **Run runtime self-test** → UI shows `PASS` + 240×160 preview
- [ ] Push retail Emerald (local only, do not commit):  
      `adb push "…/Pokemon - Emerald Version (USA, Europe).gba" /sdcard/Download/emerald.gba`  
      then **Run ROM video self-test** with that path (or document expected `FAIL` until frame-2 CPU path is fixed — see `evidence/2026-06-04-rom-video-smoke-calibration.md`)
- [ ] **GameScreen** with same ROM: expect flat `0x7FFF` backdrop until frame-60 video gate passes on workstation (device test requires user to confirm visuals)
- [ ] Force-stop and relaunch → bridge, runtime, and ROM video self-tests still consistent

## Lifecycle (Minimal — P0)

- [ ] Press Home → return → bridge self-test still `PASS`
- [ ] Rotate (if supported) or note "portrait only" in evidence
- [ ] Low-memory: open heavy app, return — no permanent crash

### Lifecycle (Extended — when Agent 5 lands)

*Skip for first synthetic PASS; required before external beta.*

- [ ] Background app (`onStop` / recents): emulation pauses; no native crash on resume
- [ ] Return to **GameScreen** after Home: frame loop resumes or shows explicit paused state
- [ ] No monotonic native handle leak across 5× Home/resume cycles (`adb shell dumpsys meminfo` stable trend)
- [ ] `EmulatorSession` not destroyed until activity finish (document if behavior differs)

## Audio (when Agent 3 lands)

- [ ] Oboe/AAudio stream starts with game loop; stops on exit from **GameScreen**
- [ ] Legal homebrew or runtime self-test: sound audible on device speaker (note ROM title in private notes only)
- [ ] Debug overlay or log line records underrun count after ≥60 s play
- [ ] Home → resume: audio restarts without permanent underrun storm

Until Oboe exists, record in evidence: **Audio: BLOCKED (counts-only JNI).**

## AV + speed audit (playable baseline)

After ring-buffer Oboe + frame pacer land:

- [ ] Debug overlay shows `scanlines=160` / `FrameComplete` during play (not persistent `StepBudget` or `MaxSteps`)
- [ ] Overlay `cycles Δ` near `280896` on full frames (`EmulationFramePacer.CYCLES_PER_FRAME`)
- [ ] At frame 60, logcat `FlowframeEmu` line `audit_audio` shows batch ~549 frames at 1×; no `audit_frame60` warning unless ROM truly blank
- [ ] **ROM video gate:** `rom_video_smoke` lines at frames 1/8/60/120 show `frame_complete=true`, `scanlines=160`, and at frame 60+ `unique_colors>=8` or non-zero BG mask in `dispcnt` (not uniform `0x7FFF` backdrop)
- [ ] 1× pacing: overlay frame ms ~16–17 on 60 Hz and 120 Hz displays (not ~8 ms on 120 Hz)
- [ ] Speed chips: tap each of 1×, 2×, 3×, 4×, Max directly (no cycle); badge visible when not 1×
- [ ] 2×+ Sync: pitched-up audio; Steady music: normal pitch, helper text when speed > 1×
- [ ] Pause (Home): ring buffer cleared; resume: pre-roll silence, no underrun storm in first 10 s

## Logging

Record in dated artifact under `docs/evidence/`:

- Device model, Android version, ABI (`adb shell getprop ro.product.model` etc.)
- Build command + exit code
- Timestamp (local)
- Screenshot or copy of on-screen PASS lines (both self-tests)
- `adb logcat -d` excerpt if any crash (tag filter optional)

## ROM smoke + video gate (P1)

Workstation first (no device required):

```powershell
cd Project_Android/GBA_Emulator
.\tools\run-core-tests.ps1
.\tools\run-rom-smoke.ps1 -RomPath ..\local\test-roms\<legal>.gba -Frames 600 -RequireValidHeader
.\tools\run-rom-video-smoke.ps1 -RomPath ..\local\test-roms\<legal>.gba -Frames 600 -RequireValidHeader
```

| Gate | Workstation | Device (same ROM) |
| --- | --- | --- |
| CPU boot / no frame-0 crash | `rom_smoke` exit 0 | N/A |
| 160 scanlines / frame complete | `rom_video_smoke` | `RomVideoSelfTest` PASS |
| `dispcnt` / `unique_colors` / not uniform backdrop | JSON frames 8, 60, 120 | `adb logcat -s FlowframeEmu` `rom_video_smoke` same frames |
| `framebuffer_crc32` parity (optional) | JSON | logcat line |

Record using [`evidence/rom-smoke-evidence-template.md`](evidence/rom-smoke-evidence-template.md) (video checkpoint table).

Device (workstation **and** device video gate on same ROM):

- [ ] `adb push …/cache/rom-smoke.gba` → Home **Run ROM video self-test** → `PASS`
- [ ] **Open ROM** (SAF) or **Load pushed test ROM** for manual play check
- [ ] Header validation rejects corrupt files with readable Home error
- [ ] Game screen updates ≥ 30 s; debug overlay shows frame ms + PC + DISPCNT
- [ ] Touch overlay changes on-screen state
- [ ] **Restart** reloads last ROM without returning Home
- [ ] `adb logcat -s FlowframeEmu` captures abnormal `stop_reason` if emulation halts

## Optional (P3+, not P0)

- [ ] GLES path (only if migrating off Compose blit)
- [ ] 10+ min thermal / battery notes

## Evidence Template

Save as `docs/evidence/YYYY-MM-DD-android-device-soak-<scope>.md`:

```text
Date:
Device:
ABI:
APK: GbaEmulatorAndroid app-debug (com.gba.emulator.shell.debug)
Tests:
  - BridgeSelfTest: PASS / FAIL
  - RuntimeSelfTest: PASS / FAIL
  - RomVideoSelfTest: PASS / FAIL / SKIP (no pushed ROM)
  - Workstation rom_video_smoke (same ROM): PASS / FAIL
Lifecycle (P0 minimal): PASS / FAIL / N/A
Lifecycle (extended): PASS / FAIL / N/A
Audio: PASS / FAIL / BLOCKED (not implemented)
Result: PASS / FAIL
Notes:
Log excerpt (if fail):
```

After PASS, update `docs/controlled-beta-readiness.md` Device Evidence row with the artifact path
and `docs/open-issues-status.md` controlled-beta blockers table.
