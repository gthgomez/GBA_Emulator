#!/usr/bin/env bash
# Capture the actual desktop host using its source-authored synthetic video fixture.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exe="${GBA_DESKTOP_BIN:-$repo_root/build/desktop/apps/desktop/gba-desktop}"
[ -x "$exe" ] || { echo "Build gba-desktop first (docs/DESKTOP.md)." >&2; exit 2; }
capture_tmp="$(mktemp -d)"
trap 'rm -rf "$capture_tmp"' EXIT
output="$repo_root/docs/showcase"
mkdir -p "$output"
python3 "$repo_root/tools/make-synthetic-rom.py" --kind video --output "$capture_tmp/synthetic-video.gba"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$exe" --rom "$capture_tmp/synthetic-video.gba" --quit-after 30 \
  --window-screenshot "$output/desktop-test-pattern.png"
python3 "$repo_root/tools/check-png-nonuniform.py" "$output/desktop-test-pattern.png" \
  --min-unique 2 --min-second-share 0.2 --min-width 240 --min-height 160 --gba-test-bars
