# Headless desktop lab smoke for CI: generates a synthetic legal ROM in the
# build directory (never committed), runs gba-desktop headless twice, and
# asserts completion, determinism, and artifact integrity.
$ErrorActionPreference = "Stop"
$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$labDir = Join-Path $repoRoot "build" "desktop-smoke"
New-Item -ItemType Directory -Force -Path $labDir | Out-Null

$romPath = Join-Path $labDir "synthetic-smoke.gba"
$py = @'
import struct, sys
rom = bytearray(256)
# Entry: mov r0, #0x8000000 (0xE2800001 LE: 01 00 80 E2), then branch-to-self.
rom[0:8] = bytes([0x01, 0x00, 0x80, 0xE2, 0xFD, 0xFF, 0xFF, 0xEA])
# GBA header: title, code, fixed 0x96 at 0xB2; complement bytes zero.
rom[0xA0:0xAB] = b"SMOKETEST"
rom[0xB2] = 0x96
checksum = 0
for b in rom[0xA0:0xBD]:
    checksum = (checksum - b) & 0xFF
rom[0xBD] = (checksum - 0x19) & 0xFF
rom[0xDC:0xE0] = bytes(4)
open(sys.argv[1], "wb").write(rom)
open(sys.argv[1], "wb").write(rom)
'@
$pyPath = Join-Path $labDir "gen_rom.py"
Set-Content -LiteralPath $pyPath -Value $py -Encoding UTF8
python $pyPath $romPath
if ($LASTEXITCODE -ne 0) { Write-Host "run-desktop-smoke: FAIL (rom generator)"; exit 1 }

$exe = Join-Path $repoRoot "build" "desktop" "apps" "desktop" "gba-desktop.exe"
if (-not (Test-Path $exe)) { $exe = Join-Path $repoRoot "build" "desktop" "gba-desktop" }

# The mingw SDL3 prebuilt links dynamically; the DLL must sit beside the exe.
$dll = Join-Path $repoRoot "external" "SDL3" "x86_64-w64-mingw32" "bin" "SDL3.dll"
if (Test-Path $dll) {
    Copy-Item $dll (Join-Path (Split-Path -Parent $exe) "SDL3.dll") -Force
}

& $exe --headless --rom $romPath --frames 120 --frame-hash --audio-hash --state-hash `
    --artifact (Join-Path $labDir "run1.json")
if ($LASTEXITCODE -ne 0) { Write-Host "run-desktop-smoke: FAIL (run1 exit $LASTEXITCODE)"; exit 1 }
& $exe --headless --rom $romPath --frames 120 --frame-hash --audio-hash --state-hash `
    --artifact (Join-Path $labDir "run2.json")
if ($LASTEXITCODE -ne 0) { Write-Host "run-desktop-smoke: FAIL (run2 exit $LASTEXITCODE)"; exit 1 }

& $exe --headless --rom $romPath --frames 3 --max-steps-per-frame 1 `
    --artifact (Join-Path $labDir "incomplete.json")
if ($LASTEXITCODE -ne 1) { Write-Host "run-desktop-smoke: FAIL (incomplete run must exit 1)"; exit 1 }

$check = @'
import json, sys
runs = [json.load(open(p)) for p in sys.argv[1:4]]
r = runs[0]
assert r["run"]["completed_frames"] == 120, r["run"]
assert r["run"]["stop_reason"] == "completed", r["run"]
assert r["run"]["unsupported_instructions"] == 0, r["run"]
assert r["run"]["fetch_failures"] == 0, r["run"]
assert 0x08000000 <= r["run"]["final_pc"] < 0x08000100, r["run"]
p = r["performance"]
assert p["completed_fps"] > 0, p
assert 0 < p["step_total_ms"] <= p["elapsed_ms"], p
assert 0 <= p["step_p50_ms"] <= p["step_p95_ms"] <= p["step_p99_ms"] <= p["step_max_ms"], p
assert 0 <= p["step_frames_over_60fps_budget"] <= 120, p
incomplete = json.load(open(sys.argv[3]))
assert incomplete["run"]["completed_frames"] == 0, incomplete
assert incomplete["run"]["stop_reason"] == "frame_budget_exhausted", incomplete
assert incomplete["run"]["stop_frame"] == 0, incomplete
assert incomplete["performance"]["completed_fps"] == 0, incomplete
assert r["video"]["frame_hashes"] == runs[1]["video"]["frame_hashes"], "nondeterministic frame hashes"
assert r["state"]["final_hash"] == runs[1]["state"]["final_hash"], "nondeterministic state hash"
print("run-desktop-smoke: PASS (120 frames, deterministic, engine", r["engine"]["commit"] + ")")
'@
$checkPath = Join-Path $labDir "check.py"
Set-Content -LiteralPath $checkPath -Value $check -Encoding UTF8
python $checkPath (Join-Path $labDir "run1.json") (Join-Path $labDir "run2.json") (Join-Path $labDir "incomplete.json")
if ($LASTEXITCODE -ne 0) { Write-Host "run-desktop-smoke: FAIL (assertions)"; exit 1 }
