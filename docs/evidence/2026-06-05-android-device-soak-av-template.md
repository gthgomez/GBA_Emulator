# Android Device Soak — AV + ROM video (workstation replay template)

**Date:** 2026-06-05  
**Scope:** Extended soak beyond P0 synthetic; requires physical `arm64-v8a` hardware replay  
**Recorded by:** Milestone implementation (workstation gates + adb replay procedure)

## Workstation gates (2026-06-05)

| Command | Exit | Notes |
| --- | --- | --- |
| `.\tools\run-core-tests.ps1` | 0 | Includes `hle_decompress_swi_test`, extended `rom_video_smoke` checkpoints |
| `.\tools\check-core-performance-regression.ps1` | 0 | Median thresholds recalibrated Jun 2026 |
| `.\tools\run-video-suite-all.ps1` | 0 | 7/7 mGBA video oracle probes |
| `GbaEmulatorAndroid :app:assembleDebug` | 0 | Oboe + GameScreen frame loop |

## Device procedure (replay on hardware)

```powershell
cd GbaEmulatorAndroid
.\gradlew :app:assembleDebug
adb install -r app\build\outputs\apk\debug\app-debug.apk
adb push "..\local\test-roms\Pokemon - Emerald Version (USA, Europe).gba" /data/data/com.gba.emulator.shell.debug/cache/rom-smoke.gba
adb shell am start -n com.gba.emulator.shell.debug/com.gba.emulator.shell.MainActivity
adb logcat -c
adb logcat -s FlowframeEmu
```

| Check | Procedure | Pass signal |
| --- | --- | --- |
| Audible Oboe | Open ROM → GameScreen ≥60 s | Human confirms speaker output |
| p95 frame ms | Debug overlay 60 s at 1× | ~16–17 ms on 60 Hz display |
| cycles Δ | Overlay | ~280896 on full frames |
| ROM video gate | Home → Run ROM video self-test | PASS; logcat frames 1/8/60/120/150/180/200/210/215 |
| Lifecycle | 5× Home/resume on GameScreen | No crash; save flush on pause |
| Thermal | 10+ min continuous play | Note model, battery %, throttling |

## Result template

| Field | Value |
| --- | --- |
| Device | *fill after hardware run* |
| ABI | arm64-v8a |
| BridgeSelfTest | PASS / FAIL |
| RuntimeSelfTest | PASS / FAIL |
| RomVideoSelfTest | PASS / FAIL |
| Workstation rom_video_smoke (same ROM) | PASS / FAIL |
| Audio | PASS / FAIL / NOT VERIFIED |
| p95 frame ms (1×) | *ms* |
| Thermal 10+ min | PASS / FAIL / NOT RUN |
| **Overall** | *PASS / FAIL* |

## Notes

- Use **standard** Emerald dump for gates; crestv2 remains FAIL per [`2026-06-05-rom-video-crestv2-triage.md`](2026-06-05-rom-video-crestv2-triage.md).
- Mark Audio PASS only after manual listen test on device speaker.
