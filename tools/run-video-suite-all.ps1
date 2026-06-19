param(
    [uint32]$MaxSteps = 20000000,
    [switch]$FailOnRed
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$resultsDir = Join-Path $repoRoot "build\test-results"
New-Item -ItemType Directory -Force -Path $resultsDir | Out-Null

$actualProbes = @(
    "basic-mode-3-actual",
    "basic-mode-4-actual",
    "degenerate-obj-actual",
    "layer-toggle-actual",
    "layer-toggle-2-actual",
    "oam-update-delay-actual",
    "window-offscreen-reset-actual"
)

$runScript = Join-Path $PSScriptRoot "run-mgba-suite.ps1"
$failures = @()
$passes = 0

foreach ($probe in $actualProbes) {
    Write-Output "run-video-suite-all: probe=$probe"
    & $runScript -Suite video -VideoProbe $probe -MaxSteps $MaxSteps -TraceSteps 0
    if ($LASTEXITCODE -ne 0) {
        $failures += $probe
        continue
    }
    ++$passes
}

$summary = [PSCustomObject]@{
    generated_at = (Get-Date -Format o)
    pass = $passes
    total = $actualProbes.Count
    failures = $failures
    status = if ($failures.Count -eq 0) { "GREEN" } else { "RED" }
}
$summaryPath = Join-Path $resultsDir "video-suite-all-latest.json"
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $summaryPath
Write-Output "run-video-suite-all: $($summary.status) ($passes/$($actualProbes.Count))"
Write-Output "artifact: $summaryPath"

if ($FailOnRed -and $failures.Count -gt 0) {
    exit 1
}
if ($failures.Count -gt 0) {
    exit 1
}
exit 0
