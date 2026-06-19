$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$romDir = Resolve-Path -LiteralPath (Join-Path $repoRoot "..\local\test-roms") -ErrorAction SilentlyContinue

if (-not $romDir) {
    Write-Output "run-local-rom-video-smoke: skip (no ../local/test-roms)"
    exit 0
}

$roms = Get-ChildItem -LiteralPath $romDir.Path -Filter "*.gba" -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notlike "synthetic-*" -and $_.Name -notlike "*crestv2*" }

if (-not $roms -or $roms.Count -eq 0) {
    Write-Output "run-local-rom-video-smoke: skip (no game .gba files)"
    exit 0
}

$smokeScript = Join-Path $PSScriptRoot "run-rom-video-smoke.ps1"
$evidenceDir = Join-Path $repoRoot "docs\evidence"
New-Item -ItemType Directory -Force -Path $evidenceDir | Out-Null

$failures = 0
foreach ($rom in $roms) {
    $slug = [System.IO.Path]::GetFileNameWithoutExtension($rom.Name) -replace '[^a-zA-Z0-9]+', '-'
    $slug = $slug.Trim('-').ToLowerInvariant()
    if ($slug.Length -gt 48) {
        $slug = $slug.Substring(0, 48)
    }
    $artifact = Join-Path $evidenceDir ("2026-06-04-rom-video-{0}.md" -f $slug)
    Write-Output "run-local-rom-video-smoke: $($rom.Name)"
    & $smokeScript -RomPath $rom.FullName -Frames 216 -RequireValidHeader -ArtifactPath $artifact
    if ($LASTEXITCODE -ne 0) {
        $failures++
    }
}

if ($failures -gt 0) {
    Write-Error "run-local-rom-video-smoke: $failures ROM(s) failed"
    exit 1
}

Write-Output "run-local-rom-video-smoke: PASS ($($roms.Count) game ROM(s))"
exit 0
