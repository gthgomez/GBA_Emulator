"""Exercise desktop save failures and restart recovery using generated ROMs."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def run(host, *args):
    return subprocess.run([host, *map(str, args)], capture_output=True,
                          text=True, timeout=30)


def require(condition, details):
    if not condition:
        raise AssertionError(details)


def wait_for_log(process, log, message):
    deadline = time.monotonic() + 6
    while time.monotonic() < deadline and process.poll() is None:
        if message in log.read_text():
            return
        time.sleep(0.05)
    raise AssertionError(f"did not observe {message!r}\n{log.read_text()}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    args = parser.parse_args()
    host = args.exe.resolve()
    generator = Path(__file__).resolve().parents[1] / "tools" / "make-synthetic-rom.py"
    os.environ.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")

    with tempfile.TemporaryDirectory(prefix="gba-save-safety-") as directory:
        root = Path(directory)
        rom = root / "progress.gba"
        save = root / "progress.gba.sav"
        subprocess.run([sys.executable, generator, "--kind", "sram", "--output", rom],
                       check=True, capture_output=True, timeout=10)
        file_at_directory = root / "no-directory"
        file_at_directory.write_bytes(b"keep this file")
        failed = run(host, "--rom", rom, "--save-directory", file_at_directory,
                     "--quit-after", 12)
        require(failed.returncode == 3 and "unsaved cartridge progress" in failed.stderr,
                f"failed cartridge save reported exit {failed.returncode}\n{failed.stderr}")
        require(file_at_directory.read_bytes() == b"keep this file",
                "save attempt changed the file blocking its destination")

        # The initial boot writes byte 0, then the reset increments it to 1.
        # Observe the periodic flush before killing the process so a successful
        # graceful-exit flush cannot hide a false clean snapshot after reset.
        log = root / "reset.log"
        with log.open("w") as stderr:
            process = subprocess.Popen(
                [host, "--rom", rom, "--reset-after", "10", "--quit-after", "1800"],
                stderr=stderr)
            try:
                deadline = time.monotonic() + 15
                while process.poll() is None and time.monotonic() < deadline:
                    if save.is_file() and save.read_bytes()[:1] == b"\x01":
                        break
                    time.sleep(0.1)
                else:
                    raise AssertionError("no periodic save of the post-reset progress")
            finally:
                if process.poll() is None:
                    process.kill()
                process.wait(timeout=10)
        resumed = run(host, "--rom", rom, "--quit-after", 12)
        require(resumed.returncode == 0 and "loaded cartridge save" in resumed.stderr
                and save.read_bytes()[:1] == b"\x02",
                f"post-crash restart failed\n{resumed.stderr}")

        # Replace the input file after the first boot. A reset must still
        # reboot the active cartridge and preserve its save memory.
        replacement = root / "replacement.gba"
        subprocess.run([sys.executable, generator, "--kind", "video", "--output", replacement],
                       check=True, capture_output=True, timeout=10)
        active = root / "active.gba"
        active.write_bytes(rom.read_bytes())
        active_save = root / "active.gba.sav"
        active_save.write_bytes(save.read_bytes())
        log = root / "replaced-rom.log"
        with log.open("w") as stderr, subprocess.Popen(
                [host, "--rom", active, "--reset-after", "120", "--quit-after", "125"],
                stderr=stderr) as process:
            wait_for_log(process, log, "loaded cartridge save")
            replacement.replace(active)
            require(process.wait(timeout=10) == 0, log.read_text())
        require(active_save.read_bytes()[:1] == b"\x04",
                "reset picked up a replacement ROM from disk")

        # If the user moves an unchanged save while the game is still open,
        # quitting should put back the live copy and preserve the moved one.
        idle_save = root / "active.gba.sav"
        moved_save = root / "moved.sav"
        log = root / "moved-save.log"
        with log.open("w") as stderr, subprocess.Popen(
                [host, "--rom", active, "--save-type", "sram32k", "--quit-after", "75"],
                stderr=stderr) as process:
            wait_for_log(process, log, "loaded cartridge save")
            idle_save.replace(moved_save)
            require(process.wait(timeout=10) == 0, log.read_text())
        require(idle_save.read_bytes() == moved_save.read_bytes(),
                "quit lost the live copy of a save moved during play")

        blocked_rom = root / "blocked.gba"
        blocked_rom.write_bytes(rom.read_bytes())
        blocked_save = root / "blocked.gba.sav"
        blocked_save.mkdir()
        moved_blocker = root / "moved-save-directory"
        log = root / "retry.log"
        with log.open("w") as stderr, subprocess.Popen(
                [host, "--rom", blocked_rom, "--quit-after", "75"],
                stderr=stderr) as process:
            wait_for_log(process, log, "cartridge save writes are DISABLED")
            blocked_save.rename(moved_blocker)
            require(process.wait(timeout=10) == 0, log.read_text())
        require(moved_blocker.is_dir() and blocked_save.read_bytes()[:1] == b"\x00",
                "save did not recover after the destination became writable")

        slot = root / "progress.gba.state1"
        orphan = slot.with_suffix(".state1.old")
        created = run(host, "--headless", "--rom", rom, "--frames", 2,
                      "--save-state", orphan)
        require(created.returncode == 0 and orphan.is_file(), created.stderr)
        restored = run(host, "--rom", rom, "--load-state", slot, "--quit-after", 2)
        require(restored.returncode == 0 and "save state loaded" in restored.stderr
                and not slot.exists() and orphan.is_file(), restored.stderr)
        resumed_lab = run(host, "--headless", "--rom", rom, "--load-state", slot,
                          "--frames", 2)
        require(resumed_lab.returncode == 0 and orphan.is_file(), resumed_lab.stderr)

        if os.name != "nt":
            untouched = root / "existing.state"
            untouched.write_bytes(b"keep this checkpoint")
            linked = root / "linked.state"
            linked.symlink_to(untouched)
            refused = run(host, "--headless", "--rom", rom, "--frames", 2,
                          "--save-state", linked)
            require(refused.returncode != 0 and untouched.read_bytes() == b"keep this checkpoint",
                    "headless checkpoint write overwrote another file")

    print("desktop_save_safety_check: PASS")


if __name__ == "__main__":
    main()
