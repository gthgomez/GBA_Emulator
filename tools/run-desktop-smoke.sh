#!/usr/bin/env bash
# Headless desktop lab smoke for CI: generates a synthetic legal ROM in the
# build directory (never committed), runs gba-desktop headless twice, and
# asserts completion, determinism, and artifact integrity.
set -euo pipefail

repoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
labDir="$repoRoot/build/desktop-smoke"
mkdir -p "$labDir"

romPath="$labDir/synthetic-smoke.gba"
python3 - "$romPath" <<'PY'
import sys
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
PY

exe="$repoRoot/build/desktop/apps/desktop/gba-desktop"
[ -x "$exe" ] || exe="$repoRoot/build/desktop/gba-desktop"

"$exe" --headless --rom "$romPath" --frames 120 --frame-hash --audio-hash --state-hash \
    --artifact "$labDir/run1.json"
"$exe" --headless --rom "$romPath" --frames 120 --frame-hash --audio-hash --state-hash \
    --artifact "$labDir/run2.json"

if "$exe" --headless --rom "$romPath" --frames 3 --max-steps-per-frame 1 \
    --artifact "$labDir/incomplete.json"; then
    echo "run-desktop-smoke: FAIL (incomplete run exited 0)" >&2
    exit 1
else
    status=$?
    [ "$status" -eq 1 ] || exit "$status"
fi

python3 - "$labDir/run1.json" "$labDir/run2.json" "$labDir/incomplete.json" <<'PY'
import json, sys
runs = [json.load(open(p)) for p in sys.argv[1:3]]
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
PY
