#!/usr/bin/env bash
# Interactive desktop-host smoke for CI (Linux counterpart of
# run-desktop-interactive-smoke.ps1).
#
# The headless lab smoke (run-desktop-smoke.sh) proves determinism but never
# constructs a window, so it cannot catch host-only regressions (the earlier
# disabled-window rendering bug shipped while headless tests stayed green).
# This script drives the *interactive* SDL host with the dummy video/audio
# drivers, presents real frames from a synthetic nonuniform ROM, captures the
# window, and asserts the captured image actually contains graphics.
set -euo pipefail

repoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
labDir="$repoRoot/build/desktop-interactive-smoke"
mkdir -p "$labDir"

romPath="$labDir/synthetic-video.gba"
python3 "$repoRoot/tools/make-synthetic-rom.py" --kind video --output "$romPath"

exe="$repoRoot/build/desktop/apps/desktop/gba-desktop"
[ -x "$exe" ] || exe="$repoRoot/build/desktop/gba-desktop"
if [ ! -x "$exe" ]; then
    echo "run-desktop-interactive-smoke: FAIL (gba-desktop not built at $exe)" >&2
    exit 1
fi

screenshot="$labDir/window.png"
rm -f "$screenshot"

# Dummy drivers keep this headless-safe while still exercising the real
# window/renderer/texture/present path.
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$exe" --rom "$romPath" --quit-after 30 --window-screenshot "$screenshot"

python3 "$repoRoot/tools/check-png-nonuniform.py" "$screenshot" --min-unique 2 \
    --min-second-share 0.2 --min-width 240 --min-height 160

# Execution errors must surface as a non-zero exit code from the interactive
# host (not only the headless lab): an undefined instruction stops the engine.
badRom="$labDir/synthetic-bad.gba"
python3 "$repoRoot/tools/make-synthetic-rom.py" --kind bad --output "$badRom"
set +e
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$exe" --rom "$badRom" --quit-after 30 >/dev/null 2>&1
badExit=$?
set -e
if [ "$badExit" -eq 0 ]; then
    echo "run-desktop-interactive-smoke: FAIL (undefined-instruction ROM exited 0)" >&2
    exit 1
fi

echo "run-desktop-interactive-smoke: PASS (interactive window rendered nonuniform frames; interactive error exit=$badExit)"
# Explicit success exit: the negative-assertion run above intentionally left a
# nonzero status, and the script's own exit status must not inherit it.
exit 0
