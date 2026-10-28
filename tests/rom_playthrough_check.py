#!/usr/bin/env python3
"""Verify the ROM experiment runner with generated, noncommercial fixtures."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def invoke(args, expected=0):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True, timeout=60)
    if result.returncode != expected:
        raise AssertionError(f"exit {result.returncode}, expected {expected}: {args}\n{result.stdout}\n{result.stderr}")
    return result


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    exe = Path(args.exe).resolve(strict=True)
    base = (repo / "build" / "rom-playthrough-fixtures").resolve()
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="case-", dir=base) as name:
        root = Path(name).resolve()
        if root.parent != base:
            raise RuntimeError("fixture directory escaped its owned parent")
        rom = root / "synthetic.gba"
        initial = root / "source.state"
        initial_input = root / "initial.json"
        resume_input = root / "resume.json"
        initial_input.write_text(json.dumps({"events": [{"frame": 0, "button": "A", "state": "down"}]}))
        resume_input.write_text(json.dumps({"events": [{"frame": 0, "button": "A", "state": "up"}]}))
        invoke([sys.executable, repo / "tools/make-synthetic-rom.py", "--kind", "smoke", "--output", rom])
        invoke([exe, "--headless", "--rom", rom, "--frames", "1", "--input-script", initial_input,
                "--save-state", initial, "--artifact", root / "initial-run.json"])
        initial_hash = digest(initial)
        runner = repo / "tools/run-rom-playthrough.ps1"
        output = root / "repeated"
        invoke(["pwsh", "-NoProfile", "-File", runner, "-RomPath", rom, "-ExePath", exe,
                "-LoadState", initial, "-SaveState", "-InputScript", resume_input,
                "-Frames", "2", "-Runs", "2", "-OutputDirectory", output])
        summary = json.loads((output / "summary.json").read_text(encoding="utf-8-sig"))
        assert summary["execution_ok"] and summary["deterministic"] and summary["repeat_comparisons"] == 1
        assert summary["initial_state_sha256"] == initial_hash == digest(initial)
        assert digest(output / "initial.state") == initial_hash
        for run in summary["measurements"]:
            state = Path(run["final_state"])
            assert state.is_file() and run["final_state_sha256"] == digest(state)
        assert summary["measurements"][0]["final_state_sha256"] == summary["measurements"][1]["final_state_sha256"]
        direct = root / "direct.json"
        invoke([exe, "--headless", "--rom", rom, "--load-state", initial, "--input-script", resume_input,
                "--frames", "2", "--frame-hash", "--audio-hash", "--state-hash", "--artifact", direct,
                "--save-state", root / "direct.state"])
        actual = json.loads((output / "run-1" / "artifact.json").read_text())
        expected = json.loads(direct.read_text())
        assert all(actual[key] == expected[key] for key in ("run", "video", "audio", "state"))
        assert digest(root / "direct.state") == summary["measurements"][0]["final_state_sha256"]
        boot = root / "boot"
        invoke(["pwsh", "-NoProfile", "-File", runner, "-RomPath", rom, "-ExePath", exe,
                "-Frames", "1", "-Runs", "1", "-OutputDirectory", boot])
        boot_summary = json.loads((boot / "summary.json").read_text(encoding="utf-8-sig"))
        assert "initial_state" not in boot_summary and "final_state" not in boot_summary["measurements"][0]
        refused = root / "refused"
        result = subprocess.run(["pwsh", "-NoProfile", "-File", str(runner), "-RomPath", str(rom),
                                 "-ExePath", str(exe), "-LoadState", str(root / "missing.state"),
                                 "-OutputDirectory", str(refused)], capture_output=True, text=True, timeout=30)
        assert result.returncode != 0 and not refused.exists()
        corrupt = root / "corrupt.state"
        corrupt.write_bytes(b"not a save state")
        corrupt_output = root / "corrupt-output"
        result = subprocess.run(["pwsh", "-NoProfile", "-File", str(runner), "-RomPath", str(rom),
                                 "-ExePath", str(exe), "-LoadState", str(corrupt), "-SaveState",
                                 "-Frames", "1", "-Runs", "1", "-OutputDirectory", str(corrupt_output)],
                                capture_output=True, text=True, timeout=30)
        assert result.returncode != 0
        corrupt_summary = json.loads((corrupt_output / "summary.json").read_text(encoding="utf-8-sig"))
        assert not corrupt_summary["execution_ok"] and corrupt_summary["initial_state_sha256"] == digest(corrupt)
        other_rom = root / "other.gba"
        invoke([sys.executable, repo / "tools/make-synthetic-rom.py", "--kind", "video", "--output", other_rom])
        wrong_output = root / "wrong-rom-output"
        result = subprocess.run(["pwsh", "-NoProfile", "-File", str(runner), "-RomPath", str(other_rom),
                                 "-ExePath", str(exe), "-LoadState", str(initial), "-Frames", "1", "-Runs", "1",
                                 "-OutputDirectory", str(wrong_output)], capture_output=True, text=True, timeout=30)
        assert result.returncode != 0 and digest(initial) == initial_hash
        assert not json.loads((wrong_output / "summary.json").read_text(encoding="utf-8-sig"))["execution_ok"]
        sentinel = root / "existing"
        sentinel.mkdir()
        (sentinel / "keep.txt").write_text("preserve")
        result = subprocess.run(["pwsh", "-NoProfile", "-File", str(runner), "-RomPath", str(rom),
                                 "-ExePath", str(exe), "-OutputDirectory", str(sentinel)],
                                capture_output=True, text=True, timeout=30)
        assert result.returncode != 0 and (sentinel / "keep.txt").read_text() == "preserve"
    print("rom_playthrough_check: PASS")


if __name__ == "__main__":
    main()
