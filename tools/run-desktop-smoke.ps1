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

$check = @'
import json, sys
runs = [json.load(open(p)) for p in sys.argv[1:4]]
r = runs[0]
assert r["run"]["completed_frames"] == 120, r["run"]
assert r["run"]["stop_reason"] == "completed", r["run"]
assert r["run"]["unsupported_instructions"] == 0, r["run"]
assert r["run"]["fetch_failures"] == 0, r["run"]
assert r["video"]["frame_hashes"] == runs[1]["video"]["frame_hashes"], "nondeterministic frame hashes"
assert r["state"]["final_hash"] == runs[1]["state"]["final_hash"], "nondeterministic state hash"
print("run-desktop-smoke: PASS (120 frames, deterministic, engine", r["engine"]["commit"] + ")")
'@
$checkPath = Join-Path $labDir "check.py"
Set-Content -LiteralPath $checkPath -Value $check -Encoding UTF8
python $checkPath (Join-Path $labDir "run1.json") (Join-Path $labDir "run2.json") (Join-Path $labDir "run1.json")
if ($LASTEXITCODE -ne 0) { Write-Host "run-desktop-smoke: FAIL (assertions)"; exit 1 }
