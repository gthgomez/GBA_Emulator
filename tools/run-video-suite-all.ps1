# SINGLE-WRITER ASSUMPTION: this script overwrites the shared
# build\test-results\video-suite-all-latest.json artifact on every run.
# Concurrent runs (same machine or shared checkout) clobber each other's
# summary; serialize runs or archive per-run copies externally.
param(
    [uint32]$MaxSteps = 20000000,
    [switch]$FailOnRed
)

# Requires PowerShell 7.3+ for $PSNativeCommandUseErrorActionPreference.
#requires -Version 7.3

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$resultsDir = Join-Path $repoRoot "build\test-results"
New-Item -ItemType Directory -Force -Path $resultsDir | Out-Null

$actualProbes = [ordered]@{
    "basic-mode-3-actual" = "basic_mode_3_oracle"
    "basic-mode-4-actual" = "basic_mode_4_oracle"
    "degenerate-obj-actual" = "degenerate_obj_oracle"
    "layer-toggle-actual" = "layer_toggle_oracle"
    "layer-toggle-2-actual" = "layer_toggle_2_oracle"
    "oam-update-delay-actual" = "oam_update_delay_oracle"
    "window-offscreen-reset-actual" = "window_offscreen_reset_oracle"
}

$runScript = Join-Path $PSScriptRoot "run-mgba-suite.ps1"
$failures = @()
$passes = 0

foreach ($probe in $actualProbes.Keys) {
    Write-Output "run-video-suite-all: probe=$probe"
    try {
        $resultPath = $null
        & $runScript -Suite video -VideoProbe $probe -MaxSteps $MaxSteps -TraceSteps 0 |
            ForEach-Object {
                if ($_ -like "suite_test: json_result_path=*") {
                    $resultPath = $_.Substring("suite_test: json_result_path=".Length)
                }
                Write-Output $_
            }
        if ([string]::IsNullOrWhiteSpace($resultPath)) {
            throw "The probe produced no result artifact."
        }
        $result = Get-Content -Raw -LiteralPath $resultPath | ConvertFrom-Json
        $property = $actualProbes[$probe]
        $oracle = $result.diagnostics.$property
        if ($result.video_probe -ne $probe -or
            $oracle.actual.status -ne "probe_reached" -or
            $oracle.expected.status -ne "probe_reached" -or
            $oracle.frame_hash_comparison -ne "match" -or
            $oracle.scanline_frame_hash_comparison -ne "match") {
            throw "The actual and expected probes did not produce matching frames and scanlines."
        }
        ++$passes
    } catch {
        $failures += $probe
        Write-Output "run-video-suite-all: failed $probe ($($_.Exception.Message))"
    }
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
