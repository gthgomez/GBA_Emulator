"""Exercise the desktop ROM picker contract with SDL's Linux chooser backend."""

import argparse
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    executable = parser.parse_args().exe.resolve()
    with tempfile.TemporaryDirectory(prefix="gba-rom-open-") as directory:
        root = Path(directory)
        chooser = root / "zenity"
        chooser.write_text(
            '#!/bin/sh\n'
            'if [ "$1" = "--version" ]; then echo 4.1.0; exit 0; fi\n'
            'if [ -n "$GBA_TEST_ROM" ]; then printf "%s\\n" "$GBA_TEST_ROM"; exit 0; fi\n'
            'exit 1\n',
            encoding="utf-8")
        chooser.chmod(0o755)
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                   SDL_FILE_DIALOG_DRIVER="zenity",
                   PATH=str(root) + os.pathsep + os.environ["PATH"])
        try:
            cancelled = subprocess.run([executable], env=env, text=True,
                                       capture_output=True, timeout=5)
        except subprocess.TimeoutExpired as failure:
            raise AssertionError("cancelling the startup file picker left the app running") from failure
        assert cancelled.returncode == 0, cancelled.stderr
        assert "cannot read ROM" not in cancelled.stderr, cancelled.stderr

        valid = root / "legal test ROM.gba"
        generator = Path(__file__).resolve().parents[1] / "tools" / "make-synthetic-rom.py"
        subprocess.run([sys.executable, generator, "--kind", "video", "--output", valid],
                       check=True, capture_output=True, timeout=10)
        opened = subprocess.run([executable, "--quit-after", "8"],
                                env=dict(env, GBA_TEST_ROM=str(valid)),
                                text=True, capture_output=True, timeout=10)
        assert opened.returncode == 0 and "session ended (8 frames," in opened.stderr, opened.stderr

        # A valid idle loop by itself is too short to be a cartridge image.
        malformed = root / "broken.gba"
        malformed.write_bytes(struct.pack("<II", 0xE1A00000, 0xEAFFFFFD))
        for options in (["--quit-after", "2"], ["--headless", "--frames", "2"]):
            result = subprocess.run([executable, "--rom", malformed, *options],
                                    env=env, text=True, capture_output=True, timeout=10)
            assert result.returncode != 0 and "GBA cartridge header" in result.stderr, result.stderr
        with malformed.open("wb") as oversized:
            oversized.truncate(32 * 1024 * 1024 + 1)
        result = subprocess.run([executable, "--headless", "--rom", malformed, "--frames", "2"],
                                env=env, text=True, capture_output=True, timeout=10)
        assert result.returncode != 0 and "up to 32 MiB" in result.stderr, result.stderr
    print("desktop_rom_open_check: PASS")


if __name__ == "__main__":
    main()
