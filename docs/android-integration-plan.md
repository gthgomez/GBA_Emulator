# Android Integration Plan

Status: Phase 73 scaffold (device soak still blocked)
Date: 2026-06-01

## What Exists Today

| Layer | Location | Notes |
| --- | --- | --- |
| C bridge API | `include/gba/core/android_core_bridge.hpp` | Opaque handle, load ROM bytes, bounded run, state hash |
| C++ runtime (next) | `include/gba/core/android_runtime.hpp` | Frame step, framebuffer, audio batch — not wired to JNI yet |
| Local tests | `tests/android_core_bridge_test.cpp`, `android_runtime_test.cpp` | Pass via `tools/run-core-tests.ps1` on workstation |
| Android app shell | `Project_Android/GbaEmulatorAndroid/` | Compose UI, CMake `libgbaemulator.so`, JNI → bridge C API |
| Composite workspace | `Project_Android/settings.gradle.kts` | `includeBuild("GbaEmulatorAndroid")` |

## Minimal Scaffold (Implemented)

```
GbaEmulatorAndroid/
  app/
    build.gradle.kts          # minSdk 26, NDK CMake, arm64-v8a + x86_64
    src/main/
      cpp/
        CMakeLists.txt        # links GBA_Emulator core sources (bridge subset)
        gba_jni.cpp           # JNI → gba_android_core_* 
      java/com/example/gbaemulator/
        GbaCoreBridge.kt      # loadLibrary + external declarations
        BridgeSelfTest.kt     # mirrors android_core_bridge_test (synthetic ROM only)
        MainActivity.kt       # Compose shell
```

### JNI surface (bridge only)

| Kotlin (`GbaCoreBridge`) | Native | C API |
| --- | --- | --- |
| `nativeCreate()` | `Java_..._nativeCreate` | `gba_android_core_create` |
| `nativeDestroy` | | `gba_android_core_destroy` |
| `nativeReset` | | `gba_android_core_reset` |
| `nativeLoadRom` | | `gba_android_core_load_rom` |
| `nativeRun` → `LongArray[4]` | status, steps, pc, hash | `gba_android_core_run` |
| `nativeStateHash` | | `gba_android_core_state_hash` |

### CMake core sources

Same translation units as `tools/run-core-tests.ps1` `android_core_bridge_test` link line (no PPU renderer yet).

## Ordered Next Steps (First Device Run)

1. **Build:** From `GbaEmulatorAndroid/`, run `./gradlew :app:assembleDebug` (requires Android SDK + NDK 27).
2. **Install:** `adb install -r app/build/outputs/apk/debug/app-debug.apk`
3. **Smoke:** Launch app → tap **Run bridge self-test** → expect `PASS` with steps=3, pc=`0x0800000C`.
4. **Logcat:** `adb logcat -s GbaEmulator` (add tagged logs in a follow-up if needed).
5. **Record evidence** per `android-device-soak-checklist.md` and update `controlled-beta-readiness.md` Device Evidence row.

## Follow-On (Not in Scaffold)

| Priority | Work |
| --- | --- |
| P1 | JNI + render loop for `AndroidRuntime::step_frame`, RGB565 framebuffer upload (Canvas or GLES) |
| P1 | Oboe or AAudio for `last_audio_batch()` |
| P2 | SAF ROM import (`ACTION_OPEN_DOCUMENT`); never bundle ROM/BIOS |
| P2 | Lifecycle: `onPause`/`onStop` pause emulation; leak checks |
| P3 | `android_performance_gate` metrics on device (frame p95, underruns) |
| P3 | Instrumented tests + thermal soak (10+ min) |

## Blockers / Decisions

| Item | Status |
| --- | --- |
| Package name / Play listing | Scaffold uses `com.example.gbaemulator` — change before store |
| GBA_Emulator in submodule vs sibling | CMake resolves `../../../../../GBA_Emulator` from `app/src/main/cpp` (sibling under `Project_Android`) |
| LLMHostAndroid not in composite | Pattern reference only; GBA app is standalone `includeBuild` |
| Full device soak | Requires physical device or emulator + manual checklist |

## Regression Before Beta Candidate

Workstation gates unchanged:

```powershell
cd GBA_Emulator
.\tools\run-core-tests.ps1
.\tools\run-core-benchmarks.ps1
.\tools\check-core-performance-regression.ps1
.\tools\check-release-readiness.ps1
```

Plus Android: `:app:assembleDebug` and one device bridge self-test PASS.
