# Android Device Soak Checklist

Use this for the first **target device** evidence row in `controlled-beta-readiness.md`.
Do not commit ROMs, BIOS, saves, or user-provided copyrighted bytes.

## Preconditions

- [ ] `GbaEmulatorAndroid` debug APK built (`assembleDebug`)
- [ ] Device or emulator API 26+ (arm64-v8a preferred)
- [ ] `adb devices` shows one target
- [ ] Workstation core gates passed (`run-core-tests.ps1` etc.)

## Install And Launch

- [ ] `adb install -r app/build/outputs/apk/debug/app-debug.apk`
- [ ] Cold start: app opens without native crash
- [ ] Tap **Run bridge self-test** → UI shows `PASS`
- [ ] Force-stop and relaunch → self-test still `PASS`

## Lifecycle (Minimal)

- [ ] Press Home → return → self-test still runnable
- [ ] Rotate (if supported) or note "portrait only" in evidence
- [ ] Low-memory: open heavy app, return — no permanent crash

## Logging

Record:

- Device model, Android version, ABI (`adb shell getprop ro.product.model` etc.)
- Build command + exit code
- Timestamp (local)
- Screenshot or copy of on-screen PASS line
- `adb logcat -d` excerpt if any crash (tag filter optional)

## Not Required For First Pass

- Real ROM play
- Audio output
- GLES framebuffer
- Thermal / battery long soak (track as follow-up)

## Evidence Template

```text
Date:
Device:
ABI:
APK: GbaEmulatorAndroid app-debug
Test: BridgeSelfTest UI button
Result: PASS / FAIL
Notes:
Log excerpt (if fail):
```

After PASS, update `docs/controlled-beta-readiness.md` Device Evidence row with this artifact
reference and note the date in `docs/open-issues-status.md` under Controlled beta blockers.
