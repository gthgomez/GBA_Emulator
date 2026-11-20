# Verify the exact portable ZIP from a clean extraction directory. The ROM is
# generated locally for this run and is never copied into the package.
param(
    [Parameter(Mandatory = $true)]
    [string]$ZipPath,
    [int]$Frames = 120
)

$ErrorActionPreference = "Stop"
$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$zip = (Resolve-Path -LiteralPath $ZipPath).Path
$runRoot = Join-Path $env:TEMP ("gba-portable-smoke-" + [guid]::NewGuid().ToString("N"))
$packageDir = Join-Path $runRoot "extracted"
New-Item -ItemType Directory -Force -Path $packageDir | Out-Null

try {
    Expand-Archive -LiteralPath $zip -DestinationPath $packageDir

    $exe = Join-Path $packageDir "gba-desktop.exe"
    foreach ($required in @("gba-desktop.exe", "README.txt", "LICENSE.txt", "NOTICE.txt", "SDL3-LICENSE.txt")) {
        if (-not (Test-Path -LiteralPath (Join-Path $packageDir $required) -PathType Leaf)) {
            throw "Portable package is missing $required"
        }
    }
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Extracted emulator executable is missing" }

    $forbiddenExtensions = @(".gba", ".gb", ".gbc", ".bin", ".bios", ".sav", ".state1")
    $forbiddenFiles = Get-ChildItem -LiteralPath $packageDir -Recurse -File |
        Where-Object { $forbiddenExtensions -contains $_.Extension.ToLowerInvariant() }
    if ($forbiddenFiles) { throw "Portable package contains a ROM, BIOS, or save file: $($forbiddenFiles.FullName -join ', ')" }

    $rom = Join-Path $runRoot "synthetic-video.gba"
    & python (Join-Path $repoRoot "tools/make-synthetic-rom.py") --kind video --output $rom
    if ($LASTEXITCODE -ne 0) { throw "Synthetic legal test ROM generation failed ($LASTEXITCODE)" }

    $artifact1 = Join-Path $runRoot "run1.json"
    $artifact2 = Join-Path $runRoot "run2.json"
    $pngDir = Join-Path $runRoot "headless-frames"
    New-Item -ItemType Directory -Force -Path $pngDir | Out-Null

    & $exe --headless --rom $rom --frames $Frames --frame-hash --audio-hash --state-hash `
        --screenshot-frame 29 --screenshot-output $pngDir --artifact $artifact1
    if ($LASTEXITCODE -ne 0) { throw "Extracted executable headless run failed ($LASTEXITCODE)" }
    & $exe --headless --rom $rom --frames $Frames --frame-hash --audio-hash --state-hash --artifact $artifact2
    if ($LASTEXITCODE -ne 0) { throw "Extracted executable replay failed ($LASTEXITCODE)" }

    $first = Get-Content -LiteralPath $artifact1 -Raw | ConvertFrom-Json
    $second = Get-Content -LiteralPath $artifact2 -Raw | ConvertFrom-Json
    if ($first.run.completed_frames -ne $Frames -or $first.run.stop_reason -ne "completed") {
        throw "Packaged run did not complete $Frames frames: $($first.run | ConvertTo-Json -Compress)"
    }
    if ($first.run.unsupported_instructions -ne 0 -or $first.run.fetch_failures -ne 0) {
        throw "Packaged run reported CPU or fetch failures: $($first.run | ConvertTo-Json -Compress)"
    }
    if ($first.video.frame_hashes.Count -ne $Frames -or
        ($first.video.frame_hashes -join ",") -ne ($second.video.frame_hashes -join ",") -or
        $first.state.final_hash -ne $second.state.final_hash -or
        $first.audio.hash -ne $second.audio.hash) {
        throw "Packaged executable replay was not deterministic"
    }

    $framePng = Join-Path $pngDir "frame-0029.png"
    if (-not (Test-Path -LiteralPath $framePng -PathType Leaf)) { throw "Packaged run did not emit frame 29" }
    & python (Join-Path $repoRoot "tools/check-png-nonuniform.py") $framePng --gba-test-bars
    if ($LASTEXITCODE -ne 0) { throw "Packaged headless framebuffer was blank or malformed" }

    # Run the extracted executable through SDL's real window/renderer/present
    # path as well. This catches missing runtime DLLs and packaging-only startup
    # failures that a core-only or in-tree build test cannot see.
    $windowPng = Join-Path $runRoot "window.png"
    $oldVideoDriver = $env:SDL_VIDEODRIVER
    $oldAudioDriver = $env:SDL_AUDIODRIVER
    $env:SDL_VIDEODRIVER = "dummy"
    $env:SDL_AUDIODRIVER = "dummy"
    try {
        & $exe --rom $rom --quit-after 30 --window-screenshot $windowPng
        if ($LASTEXITCODE -ne 0) { throw "Packaged SDL window run failed ($LASTEXITCODE)" }
    }
    finally {
        if ($null -eq $oldVideoDriver) { Remove-Item Env:SDL_VIDEODRIVER -ErrorAction SilentlyContinue }
        else { $env:SDL_VIDEODRIVER = $oldVideoDriver }
        if ($null -eq $oldAudioDriver) { Remove-Item Env:SDL_AUDIODRIVER -ErrorAction SilentlyContinue }
        else { $env:SDL_AUDIODRIVER = $oldAudioDriver }
    }
    & python (Join-Path $repoRoot "tools/check-png-nonuniform.py") $windowPng --gba-test-bars
    if ($LASTEXITCODE -ne 0) { throw "Packaged SDL window did not present nonuniform graphics" }

    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    $checksumPath = "$zip.sha256"
    if (-not (Test-Path -LiteralPath $checksumPath -PathType Leaf)) { throw "Package checksum sidecar is missing" }
    $recordedHash = ((Get-Content -LiteralPath $checksumPath -Raw).Trim() -split "\s+")[0].ToLowerInvariant()
    if ($recordedHash -ne $hash) { throw "Package SHA-256 sidecar does not match the ZIP" }
    Write-Host "run-windows-package-smoke: PASS"
    Write-Host "  zip: $zip"
    Write-Host "  sha256: $hash"
    Write-Host "  extracted run: $Frames deterministic frames; headless and SDL window rendered"
}
finally {
    Remove-Item -LiteralPath $runRoot -Recurse -Force -ErrorAction SilentlyContinue
}
