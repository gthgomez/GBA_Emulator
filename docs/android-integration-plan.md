# Android Integration Plan

Status: Phase 73 dev shell (P0 synthetic device evidence recorded)
Date: 2026-06-03

## What Exists Today

| Layer | Location | Notes |
| --- | --- | --- |
| C bridge API | `include/gba/core/android_core_bridge.hpp` | Opaque handle, load ROM bytes, bounded run, state hash |
| C++ runtime | `include/gba/core/android_runtime.hpp` | `step_frame`, RGB565 framebuffer, `last_audio_batch()` |
| Local tests | `tests/android_core_bridge_test.cpp`, `android_runtime_test.cpp` | Pass via `tools/run-core-tests.ps1` |
| Android app | `Project_Android/GbaEmulatorAndroid/` | Compose, CMake `libgbaemulator.so`, package `com.gba.emulator.shell` |
| Composite workspace | `Project_Android/settings.gradle.kts` | `includeBuild("GbaEmulatorAndroid")` |
| P0 device artifact | `docs/evidence/2026-06-03-android-device-soak-synthetic-pass.md` | Synthetic self-test PASS + adb replay |

Authoritative app summary: [`GbaEmulatorAndroid/PROJECT_CONTEXT.md`](../../GbaEmulatorAndroid/PROJECT_CONTEXT.md).

## Module Layout (Implemented)

```
GbaEmulatorAndroid/
  app/
    build.gradle.kts          # minSdk 26, targetSdk 36, NDK CMake, arm64-v8a + x86_64
    src/main/
      cpp/
        CMakeLists.txt        # links GBA_Emulator core subset (incl. android_runtime, PPU)
        gba_jni.cpp           # JNI → bridge + AndroidRuntime
      java/com/gba/emulator/shell/
        GbaCoreBridge.kt      # bridge API
        GbaRuntimeBridge.kt   # runtime API (frame, framebuffer, stop_reason, audio counts)
        EmulatorSession.kt    # long-lived native handle for gameplay
        BridgeSelfTest.kt     # mirrors android_core_bridge_test
        RuntimeSelfTest.kt    # mirrors android_runtime_test
        RomLoader.kt          # SAF URI → ROM bytes
        MainActivity.kt       # Home + Game routes
        ui/
          GbaEmulatorScreen.kt  # self-tests, Open ROM
          GameScreen.kt           # vsync loop, framebuffer, touch overlay
          TouchGameControls.kt
          Rgb565Framebuffer.kt
```

## JNI Surface

### Bridge (`GbaCoreBridge`)

| Kotlin | C API |
| --- | --- |
| `nativeCreate` / `nativeDestroy` | `gba_android_core_create` / `destroy` |
| `nativeReset` | `gba_android_core_reset` |
| `nativeLoadRom` | `gba_android_core_load_rom` |
| `nativeRun` → `LongArray[4]` | `gba_android_core_run` |
| `nativeStateHash` | `gba_android_core_state_hash` |

### Runtime (`GbaRuntimeBridge`)

| Kotlin | Notes |
| --- | --- |
| `nativeLoadRom` / `nativeReset` | `AndroidRuntime` |
| `nativeStepFrame` → 7× `long` | status, steps, scanlines, audioSamples, audioUnderruns, stateHash, stopReason |
| `nativeCopyFramebuffer` / `nativePixel` | RGB565 240×160 |
| `nativeSetButtonMask` | Keypad bits ⊆ `0x03FF` |

Audio samples are **counted** in JNI; there is **no** Oboe/AAudio playback yet.

## Device Smoke (P0)

1. **Build:** `cd GbaEmulatorAndroid && ./gradlew :app:assembleDebug`
2. **Install:** `adb install -r app/build/outputs/apk/debug/app-debug.apk`
3. **Self-tests:** Launch → **Run bridge self-test** + **Run runtime self-test** → both PASS
4. **Record:** Fill model/ABI in `docs/evidence/2026-06-03-android-device-soak-synthetic-pass.md`
5. **Gate:** Update `controlled-beta-readiness.md` Device Evidence (P0) row if re-verified

## Follow-On (Not Done)

| Priority | Work |
| --- | --- |
| P1 | Cycle-bounded `AndroidRuntime::step_frame` (280,896 cycles/frame) |
| P1 | Oboe/AAudio for `last_audio_batch()` |
| P1 | `ProcessLifecycleOwner` / `onStop` pause; flush saves on pause (with persistence agent) |
| P2 | Save-state + cartridge save SAF (`save_state_codec` in CMake) |
| P2 | GLES upload path only if Compose blit fails soak p95 |
| P3 | On-device performance gate + 10+ min thermal soak |

## Blockers / Decisions

| Item | Status |
| --- | --- |
| Package / Play listing | `com.gba.emulator.shell` — dev shell, not store-ready |
| GBA_Emulator path | Sibling `../GBA_Emulator` from `app/src/main/cpp` |
| Full external beta | Blocked on audio, saves on device, pacing/thermal artifacts |
| P0 synthetic soak | PASS — see evidence file |

## Regression Before Beta Candidate

Workstation:

```powershell
cd GBA_Emulator
.\tools\run-core-tests.ps1
.\tools\run-credibility-matrix.ps1 -FailOnRegression
.\tools\check-core-performance-regression.ps1
.\tools\check-release-readiness.ps1
```

Android: `:app:assembleDebug` + P0 self-tests per [`android-device-soak-checklist.md`](android-device-soak-checklist.md).
