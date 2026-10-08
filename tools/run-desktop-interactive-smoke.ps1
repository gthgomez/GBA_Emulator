# Interactive desktop-host smoke for CI (Windows counterpart of
# run-desktop-interactive-smoke.sh).
#
# The headless lab smoke (run-desktop-smoke.ps1) proves determinism but never
# constructs a window, so it cannot catch host-only regressions (the earlier
# disabled-window rendering bug shipped while headless tests stayed green).
# This script drives the *interactive* SDL host with the dummy video/audio
# drivers, presents real frames from a synthetic nonuniform ROM, captures the
# window, and asserts the captured image actually contains graphics.
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$labDir = Join-Path $repoRoot "build" "desktop-interactive-smoke"
New-Item -ItemType Directory -Force -Path $labDir | Out-Null

$romPath = Join-Path $labDir "synthetic-video.gba"
python (Join-Path $repoRoot "tools\make-synthetic-rom.py") --kind video --output $romPath
if ($LASTEXITCODE -ne 0) { Write-Host "run-desktop-interactive-smoke: FAIL (rom generator)"; exit 1 }

$exe = Join-Path $repoRoot "build" "desktop" "apps" "desktop" "gba-desktop.exe"
if (-not (Test-Path $exe)) { $exe = Join-Path $repoRoot "build" "desktop" "gba-desktop.exe" }
if (-not (Test-Path $exe)) {
    Write-Host "run-desktop-interactive-smoke: FAIL (gba-desktop not built at $exe)"
    exit 1
}

# The mingw SDL3 prebuilt links dynamically; the DLL must sit beside the exe.
$dll = Join-Path $repoRoot "external" "SDL3" "x86_64-w64-mingw32" "bin" "SDL3.dll"
if (Test-Path $dll) {
    Copy-Item $dll (Join-Path (Split-Path -Parent $exe) "SDL3.dll") -Force
}

$screenshot = Join-Path $labDir "window.png"
if (Test-Path $screenshot) { Remove-Item $screenshot -Force }

# Dummy drivers keep this headless-safe while still exercising the real
# window/renderer/texture/present path.
$env:SDL_VIDEODRIVER = "dummy"
$env:SDL_AUDIODRIVER = "dummy"
try {
    & $exe --rom $romPath --quit-after 30 --window-screenshot $screenshot
    if ($LASTEXITCODE -ne 0) {
        Write-Host "run-desktop-interactive-smoke: FAIL (interactive run exit $LASTEXITCODE)"
        exit 1
    }
    python (Join-Path $repoRoot "tools\check-png-nonuniform.py") $screenshot --min-unique 2 --min-second-share 0.2 --min-width 240 --min-height 160
    if ($LASTEXITCODE -ne 0) { Write-Host "run-desktop-interactive-smoke: FAIL (screenshot uniformly blank)"; exit 1 }

    $badRom = Join-Path $labDir "synthetic-bad.gba"
    python (Join-Path $repoRoot "tools\make-synthetic-rom.py") --kind bad --output $badRom
    if ($LASTEXITCODE -ne 0) { Write-Host "run-desktop-interactive-smoke: FAIL (bad rom generator)"; exit 1 }

    # Execution errors must surface as a non-zero exit code from the interactive
    # host (not only the headless lab): an undefined instruction stops the engine.
    & $exe --rom $badRom --quit-after 30 *> $null
    $badExit = $LASTEXITCODE
    if ($badExit -eq 0) {
        Write-Host "run-desktop-interactive-smoke: FAIL (undefined-instruction ROM exited 0)"
        exit 1
    }
}
finally {
    Remove-Item Env:SDL_VIDEODRIVER -ErrorAction SilentlyContinue
    Remove-Item Env:SDL_AUDIODRIVER -ErrorAction SilentlyContinue
}

Write-Host "run-desktop-interactive-smoke: PASS (interactive window rendered nonuniform frames; interactive error exit=$badExit)"
