#requires -Version 7.3

param(
    # Explicit opt-in destination for evidence markdown files (e.g. a
    # reviewed docs\evidence path). Default output stays build-local so local
    # runs never write dated artifacts into tracked documentation.
    [string]$EvidenceDir = ""
)

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

# Artifacts default to the untracked build tree; only an explicit -EvidenceDir
# routes them into a tracked evidence directory.
if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
    $artifactDir = Join-Path $repoRoot "build\test-results\rom-video-smoke-local"
} else {
    $artifactDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($EvidenceDir)
}
New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null

$smokeScript = Join-Path $PSScriptRoot "run-rom-video-smoke.ps1"

$failures = 0
foreach ($rom in $roms) {
    $slug = [System.IO.Path]::GetFileNameWithoutExtension($rom.Name) -replace '[^a-zA-Z0-9]+', '-'
    $slug = $slug.Trim('-').ToLowerInvariant()
    if ($slug.Length -gt 48) {
        $slug = $slug.Substring(0, 48)
    }
    $artifact = Join-Path $artifactDir ("rom-video-{0}.md" -f $slug)
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
