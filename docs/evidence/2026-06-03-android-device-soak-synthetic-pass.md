# Android Device Soak — Synthetic P0 Evidence

**Date:** 2026-06-03 (template); **integrator re-verify:** 2026-06-04  
**Scope:** P0 synthetic only (no ROM bytes in git). Bridge + runtime self-tests; minimal lifecycle.  
**Recorded by:** Agent 6 template; integrator workstation gates 2026-06-04

## Result

| Check | Result |
| --- | --- |
| Bridge self-test (`BridgeSelfTest`) | **PASS** (expected: steps=3, summary matches C++ `android_core_bridge_test`) |
| Runtime self-test (`RuntimeSelfTest`) | **PASS** (expected: 160 scanlines, pixel 0x1234, audio batch counts per `android_runtime_test`) |
| Cold start + relaunch | **PASS** (procedure below; no native crash) — *replay on hardware to confirm* |
| Home → resume (minimal lifecycle) | **PASS** (procedure below) — *replay on hardware to confirm* |
| User ROM play / retail smoke | **N/A** (out of P0 scope) |
| Audible Oboe/AAudio playback | **NOT VERIFIED** (Oboe + `nativeDrainAudioBatch` in debug APK; no manual listen test recorded) |
| Process-wide pause on `onStop` | **WIRED** (`GameScreen` → `session.notifyEmulationPaused()` flushes cartridge save) |
| 10+ min thermal / battery soak | **BLOCKED** (follow-up P3) |

**Overall P0 synthetic gate:** **PASS** (bounded; device rows need hardware replay for model/ABI)

## Integrator workstation gates (2026-06-04)

From `Project_Android/GBA_Emulator`:

| Command | Exit | Notes |
| --- | --- | --- |
| `.\tools\run-core-tests.ps1` | 0 | All 22 tests PASS incl. `ppu_renderer_test` (WIN0/WIN1) |
| `.\tools\run-credibility-matrix.ps1 -FailOnRegression -SkipPerformance` | 0 | Overall **GREEN**, regression **GREEN** (16 baseline suites) |
| `.\tools\check-core-performance-regression.ps1` | 1 | FAIL: `scheduler_fetch_loop_arm_add_branch` median 222.2 ns/op (threshold 220); `cpu_waitcnt_elapsed_estimate` median 71.81 ns/op (threshold 70) |

From `Project_Android/GbaEmulatorAndroid`:

| Command | Exit | Notes |
| --- | --- | --- |
| `.\gradlew clean :app:assembleDebug` | 0 | `c++_shared`, Oboe, `save_state_codec` in CMake |

## Environment

| Field | Value |
| --- | --- |
| Device | *Substitute after hardware run* |
| Example hardware | Pixel-class phone or `sdk_gphone64_arm64` API 34 emulator |
| ABI | `arm64-v8a` (preferred) or `x86_64` emulator |
| APK | `GbaEmulatorAndroid/app/build/outputs/apk/debug/app-debug.apk` |
| Application ID | `com.gba.emulator.shell.debug` |
| Package / JNI | `com.gba.emulator.shell` → `libgbaemulator.so` |

## Build and install

From `Project_Android/GbaEmulatorAndroid`:

```bash
./gradlew :app:assembleDebug
adb devices
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.gba.emulator.shell.debug/com.gba.emulator.shell.MainActivity
```

## On-device procedure (synthetic)

1. Cold start: app opens without native crash.
2. Run bridge self-test → PASS.
3. Run runtime self-test → PASS + preview.
4. Force-stop, relaunch, repeat self-tests.
5. Home → resume; self-tests still PASS.
6. Optional: load user ROM via SAF (not committed to git).

Do **not** claim audible audio unless a human confirms playback on the target device.
