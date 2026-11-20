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
. (Join-Path $PSScriptRoot "video-oracle-gate.ps1")

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
    $childOutput = @()
    $childError = $null
    $jsonResultPath = $null
    try {
        $childOutput = @(& $runScript -Suite video -VideoProbe $probe -MaxSteps $MaxSteps -TraceSteps 0 -FailOnRed:$FailOnRed 2>&1)
    } catch {
        $childError = $_.Exception.Message
    }
    foreach ($line in $childOutput) {
        $lineText = [string]$line
        Write-Output $lineText
        if ($lineText -match '^suite_test: json_result_path=(.+)$') {
            $jsonResultPath = $Matches[1].Trim()
        }
    }

    $probeFailures = @()
    if ($childError) {
        $probeFailures += "runner error: $childError"
    }
    if ($null -ne $LASTEXITCODE -and [int]$LASTEXITCODE -ne 0) {
        $probeFailures += "runner exit code: $LASTEXITCODE"
    }
    if ([string]::IsNullOrWhiteSpace($jsonResultPath)) {
        $probeFailures += "runner did not report a JSON result path"
    } elseif (-not (Test-Path -LiteralPath $jsonResultPath -PathType Leaf)) {
        $probeFailures += "JSON result file is missing: $jsonResultPath"
    } else {
        try {
            $childResult = Get-Content -Raw -LiteralPath $jsonResultPath | ConvertFrom-Json
            $oracleFailure = Get-VideoOracleFailure -SuiteResult $childResult -Probe $probe
            if ($oracleFailure) {
                $probeFailures += $oracleFailure
            }
        } catch {
            $probeFailures += "could not validate JSON result: $($_.Exception.Message)"
        }
    }

    if ($probeFailures.Count -gt 0) {
        $failures += $probe
        Write-Output "run-video-suite-all: $probe RED ($($probeFailures -join '; '))"
        continue
    }
    Write-Output "run-video-suite-all: $probe GREEN (paired frame oracle validated)"
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
