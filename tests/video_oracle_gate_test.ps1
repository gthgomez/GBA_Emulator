#requires -Version 7.3

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "..\tools\video-oracle-gate.ps1")

function Assert-GatePass {
    param([object]$Result, [string]$Probe, [string]$Name)
    $failure = Get-VideoOracleFailure -SuiteResult $Result -Probe $Probe
    if ($null -ne $failure) { throw "$Name: expected pass, got $failure" }
}

function Assert-GateFails {
    param([object]$Result, [string]$Probe, [string]$Name)
    $failure = Get-VideoOracleFailure -SuiteResult $Result -Probe $Probe
    if ([string]::IsNullOrWhiteSpace($failure)) { throw "$Name: expected a gate failure" }
}

function New-Snapshot {
    param([string]$Hash, [string]$Probe, [string]$TestName)
    return [pscustomobject]@{
        probe = $Probe
        test_name = $TestName
        view = if ($Probe.EndsWith("-actual")) { "actual" } else { "expected" }
        status = "probe_reached"
        runner_exit_code = 0
        runner_stop_reason = "video_probe"
        until_output_matched = "true"
        unsupported_steps = 0
        fetch_failures = 0
        frame_hash = $Hash
        scanline_capture_active = "true"
        scanline_capture_complete = "true"
        scanline_frame_hash = $Hash
        scanline_captured_scanlines = 160
        scanline_supported_scanlines = 160
        scanline_unsupported_scanlines = 0
    }
}

function New-Result {
    param([string]$Probe = "basic-mode-3-actual")
    $probeBase = [regex]::Replace($Probe, '-(?:actual|expected)$', '')
    $testName = if ($probeBase -eq "layer-toggle") { "Layer toggle" } else { "Basic Mode 3" }
    $actual = New-Snapshot -Hash "1234" -Probe "$probeBase-actual" -TestName $testName
    $expected = New-Snapshot -Hash "1234" -Probe "$probeBase-expected" -TestName $testName
    $oracleField = if ($Probe.StartsWith("basic-mode-3")) { "basic_mode_3_oracle" } else { "layer_toggle_oracle" }
    $oracle = [pscustomobject]@{
        frame_hash_comparison = "match"
        scanline_frame_hash_comparison = "match"
        actual = $actual
        expected = $expected
    }
    $diagnostics = [pscustomobject]@{
        visual_interactive_evidence = [pscustomobject]@{
            reached = $true
            active_suite_name = "video"
            runner_stop_reason = "video_probe"
            frame_hash = "1234"
            view = if ($Probe.EndsWith("-actual")) { "actual" } else { "expected" }
            video_test = $testName
        }
    }
    $diagnostics | Add-Member -NotePropertyName $oracleField -NotePropertyValue $oracle
    return [pscustomobject]@{
        suite_request = "video"
        video_probe = $Probe
        parsed = [pscustomobject]@{ failure_count = 0 }
        runner = [pscustomobject]@{
            runner_exit_code = 0
            runner_stop_reason = "video_probe"
            unsupported_steps = 0
            fetch_failures = 0
        }
        diagnostics = $diagnostics
    }
}

$good = New-Result
Assert-GatePass $good "basic-mode-3-actual" "matching frame pair"
$expectedView = New-Result "basic-mode-3-expected"
Assert-GatePass $expectedView "basic-mode-3-expected" "matching expected view"

$mismatchedPair = New-Result
$mismatchedPair.diagnostics.basic_mode_3_oracle.frame_hash_comparison = "mismatch"
$mismatchedPair.diagnostics.basic_mode_3_oracle.expected.frame_hash = "5678"
Assert-GateFails $mismatchedPair "basic-mode-3-actual" "mismatched frame pair"

$DivergentHashes = New-Result
$DivergentHashes.diagnostics.basic_mode_3_oracle.expected.frame_hash = "5678"
Assert-GateFails $DivergentHashes "basic-mode-3-actual" "divergent hashes despite match label"

$missingFrame = New-Result
$missingFrame.diagnostics.basic_mode_3_oracle.expected.frame_hash = ""
Assert-GateFails $missingFrame "basic-mode-3-actual" "missing frame hash"

$notReached = New-Result
$notReached.runner.runner_stop_reason = "max_steps"
Assert-GateFails $notReached "basic-mode-3-actual" "probe not reached"

$notVisuallyReached = New-Result
$notVisuallyReached.diagnostics.visual_interactive_evidence.reached = $false
Assert-GateFails $notVisuallyReached "basic-mode-3-actual" "visual probe not reached"

$parsedFailure = New-Result
$parsedFailure.parsed.failure_count = 1
Assert-GateFails $parsedFailure "basic-mode-3-actual" "parsed runner failure"

$scanlineMismatch = New-Result "layer-toggle-actual"
$scanlineMismatch.diagnostics.layer_toggle_oracle.scanline_frame_hash_comparison = "mismatch"
Assert-GateFails $scanlineMismatch "layer-toggle-actual" "scanline hash mismatch"

$incompleteScanlines = New-Result "layer-toggle-actual"
$incompleteScanlines.diagnostics.layer_toggle_oracle.expected.scanline_capture_complete = "false"
Assert-GateFails $incompleteScanlines "layer-toggle-actual" "incomplete scanline capture"

Write-Output "video_oracle_gate_test: PASS (valid pair accepted; mismatched hashes and incomplete evidence rejected)"
