#!/usr/bin/env python3
"""Exercise save-type selection through the real host using synthetic ROMs only."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True)
    args = parser.parse_args()
    exe = str(Path(args.exe).resolve())
    generator = Path(__file__).with_name("make-synthetic-rom.py")
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")

    with tempfile.TemporaryDirectory(prefix="gba-save-type-") as directory:
        root = Path(directory)

        def fixture(name, kind="smoke", markers=()):
            path = root / (name + ".gba")
            subprocess.run([sys.executable, str(generator), "--kind", kind,
                            "--output", str(path)], check=True, capture_output=True)
            rom = bytearray(path.read_bytes())
            for index, marker in enumerate(markers):
                offset = 0x40 + index * 0x10
                rom[offset:offset + len(marker)] = marker
            path.write_bytes(rom)
            return path

        def run(*options, code=0, message=None):
            result = subprocess.run([exe, *map(str, options)], env=env,
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode == code, (
                f"exit {result.returncode}, expected {code}: {options}\n{result.stderr}")
            if message:
                assert message in result.stderr, result.stderr
            return result

        def save(path):
            return Path(str(path) + ".sav")

        def check_save(path, size, first):
            data = save(path).read_bytes()
            assert len(data) == size and data[0] == first, (path, len(data), data[0])

        ambiguous = fixture("ambiguous", "sram", (b"SRAM_V", b"EEPROM_V"))
        seed = bytes([0x42]) + bytes(32767)
        save(ambiguous).write_bytes(seed)
        run("--rom", ambiguous, "--quit-after", 4, code=3)
        assert save(ambiguous).read_bytes() == seed, "auto mode damaged existing save"
        run("--rom", ambiguous, "--save-type", "sram32k", "--quit-after", 4,
            message="loaded cartridge save (sram32k)")
        check_save(ambiguous, 32768, 0x43)
        run("--rom", ambiguous, "--save-type", "sram32k", "--quit-after", 4)
        check_save(ambiguous, 32768, 0x44)
        run("--rom", ambiguous, "--save-type", "sram32k", "--reset-after", 2,
            "--quit-after", 4, message="cartridge save preserved")
        check_save(ambiguous, 32768, 0x46)

        corrupt = fixture("corrupt", "sram", (b"SRAM_V", b"EEPROM_V"))
        save(corrupt).write_bytes(b"JUNK!")
        run("--rom", corrupt, "--save-type", "sram32k", "--quit-after", 4,
            message="cartridge save rejected")
        assert Path(str(save(corrupt)) + ".rejected").read_bytes() == b"JUNK!"
        check_save(corrupt, 32768, 0)

        eeprom = fixture("other-title", markers=(b"EEPROM_V",))
        run("--rom", ambiguous, "--save-type", "sram32k", "--switch-after", 2,
            "--switch-to", eeprom, "--quit-after", 4)
        assert len(save(eeprom).read_bytes()) == 8192, "override leaked into another ROM"

        # Equivalent spellings of the original ROM still retain its override.
        alias = str(root) + os.sep + "." + os.sep + ambiguous.name
        run("--rom", ambiguous, "--save-type", "sram32k", "--switch-after", 2,
            "--switch-to", alias, "--quit-after", 4)

        idle = fixture("idle", markers=(b"SRAM_V",))
        run("--rom", idle, "--save-type", "none", "--reset-after", 2,
            "--quit-after", 4)
        assert not save(idle).exists(), "none override produced a cartridge save"

        # Headless runs expose the effective type and can execute the same ROM.
        artifact = root / "run.json"
        run("--rom", ambiguous, "--save-type", "sram32k", "--headless",
            "--frames", 2, "--artifact", artifact)
        for kind in ("none", "sram32k", "flash64k", "flash128k", "eeprom512", "eeprom8k"):
            run("--rom", idle, "--save-type", kind, "--headless", "--frames", 1,
                "--artifact", artifact)
            assert json.loads(artifact.read_text())["rom"]["save_type"] == kind
        run("--rom", idle, "--save-type", "auto", "--headless", "--frames", 1,
            "--artifact", artifact)
        assert json.loads(artifact.read_text())["rom"]["save_type"] == "sram32k"

        # Exercise state save/resume for every explicit backup capacity, plus
        # automatic Flash128K detection. The 64 KiB bus aperture is smaller
        # than this protocol's two-bank snapshot storage.
        flash_auto = fixture("flash-auto", markers=(b"FLASH1M_V",))
        flash_seed = bytes([0x42]) + bytes(131070) + bytes([0x5A])
        save(flash_auto).write_bytes(flash_seed)
        oversized = root / "oversized.gba"
        with oversized.open("wb") as output:
            output.truncate(32 * 1024 * 1024 + 1)
        run("--rom", flash_auto, "--switch-after", 2, "--switch-to", oversized,
            "--quit-after", 4, message="previous session restored")
        assert save(flash_auto).read_bytes() == flash_seed, (
            "failed ROM switch damaged the Flash128K save")

        roundtrip_cases = [(idle, kind) for kind in
                           ("none", "sram32k", "flash64k", "flash128k",
                            "eeprom512", "eeprom8k")]
        roundtrip_cases.append((flash_auto, "auto"))
        for path, kind in roundtrip_cases:
            state_path = root / (kind + "-roundtrip.state")
            run("--rom", path, "--save-type", kind, "--headless", "--frames", 2,
                "--artifact", artifact)
            reference = json.loads(artifact.read_text())["state"]["final_hash"]
            run("--rom", path, "--save-type", kind, "--headless", "--frames", 1,
                "--save-state", state_path)
            run("--rom", path, "--save-type", kind, "--headless", "--frames", 1,
                "--load-state", state_path, "--artifact", artifact)
            resumed = json.loads(artifact.read_text())
            expected_type = "flash128k" if kind == "auto" else kind
            assert resumed["rom"]["save_type"] == expected_type
            assert resumed["state"]["final_hash"] == reference, (
                f"{kind} save/resume changed the machine")

        state = root / "sram.state"
        run("--rom", idle, "--save-type", "sram32k", "--headless", "--frames", 1,
            "--save-state", state)
        run("--rom", idle, "--save-type", "sram32k", "--headless", "--frames", 1,
            "--load-state", state)

        # A resumed SRAM-writing ROM must retain its written backup data.
        written_state = root / "written-sram.state"
        run("--rom", ambiguous, "--save-type", "sram32k", "--headless",
            "--frames", 2, "--artifact", artifact)
        uninterrupted_hash = json.loads(artifact.read_text())["state"]["final_hash"]
        run("--rom", ambiguous, "--save-type", "sram32k", "--headless",
            "--frames", 1, "--save-state", written_state)
        run("--rom", ambiguous, "--save-type", "sram32k", "--headless",
            "--frames", 1, "--load-state", written_state, "--artifact", artifact)
        assert json.loads(artifact.read_text())["state"]["final_hash"] == uninterrupted_hash, (
            "resuming with a save-type override changed the saved machine")

        run("--rom", idle, "--save-type", "eeprom8k", "--headless", "--frames", 1,
            "--load-state", state, code=3, message="different save type")
        run("--rom", eeprom, "--save-type", "sram32k", "--headless", "--frames", 1,
            "--load-state", state, code=3, message="different ROM")

        for invalid in ("sram", "garbage", "SRAM32K", ""):
            run("--rom", idle, "--save-type", invalid, code=2, message="invalid save type")
        run("--rom", idle, "--save-type", code=2, message="missing value")
        run("--save-type", "sram32k", code=2, message="requires a ROM")

    print("check-desktop-save-type-override: PASS")


if __name__ == "__main__":
    main()
