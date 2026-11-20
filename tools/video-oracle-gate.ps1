# Shared status gate for deterministic actual/expected video probes.
# Dot-source this file from the single-probe runner and video aggregate runner.

$script:VideoOracleProbeTargets = @{
    "basic-mode-3-actual" = @{ oracle_field = "basic_mode_3_oracle"; require_scanline_capture = $false }
    "basic-mode-3-expected" = @{ oracle_field = "basic_mode_3_oracle"; require_scanline_capture = $false }
    "basic-mode-4-actual" = @{ oracle_field = "basic_mode_4_oracle"; require_scanline_capture = $false }
    "basic-mode-4-expected" = @{ oracle_field = "basic_mode_4_oracle"; require_scanline_capture = $false }
    "degenerate-obj-actual" = @{ oracle_field = "degenerate_obj_oracle"; require_scanline_capture = $false }
    "degenerate-obj-expected" = @{ oracle_field = "degenerate_obj_oracle"; require_scanline_capture = $false }
    "layer-toggle-actual" = @{ oracle_field = "layer_toggle_oracle"; require_scanline_capture = $true }
    "layer-toggle-expected" = @{ oracle_field = "layer_toggle_oracle"; require_scanline_capture = $true }
    "layer-toggle-2-actual" = @{ oracle_field = "layer_toggle_2_oracle"; require_scanline_capture = $true }
    "layer-toggle-2-expected" = @{ oracle_field = "layer_toggle_2_oracle"; require_scanline_capture = $true }
    "oam-update-delay-actual" = @{ oracle_field = "oam_update_delay_oracle"; require_scanline_capture = $true }
    "oam-update-delay-expected" = @{ oracle_field = "oam_update_delay_oracle"; require_scanline_capture = $true }
    "window-offscreen-reset-actual" = @{ oracle_field = "window_offscreen_reset_oracle"; require_scanline_capture = $false }
    "window-offscreen-reset-expected" = @{ oracle_field = "window_offscreen_reset_oracle"; require_scanline_capture = $false }
}

function Test-VideoOracleZeroMetric {
    param([object]$Value)

    if ($null -eq $Value -or [string]::IsNullOrWhiteSpace([string]$Value)) {
        return $false
    }
    try {
        return [int64]$Value -eq 0
    } catch {
        return $false
    }
}

function Get-VideoOracleSnapshotFailure {
    param(
        [object]$Snapshot,
        [string]$Label
    )

    if ($null -eq $Snapshot) {
        return "video oracle $Label snapshot missing"
    }
    if ($Snapshot.status -ne "probe_reached") {
        return "video oracle $Label status=$($Snapshot.status), expected probe_reached"
    }
    if ($Snapshot.runner_stop_reason -ne "video_probe") {
        return "video oracle $Label runner_stop_reason=$($Snapshot.runner_stop_reason), expected video_probe"
    }
    if ($null -eq $Snapshot.runner_exit_code -or [int]$Snapshot.runner_exit_code -ne 0) {
        return "video oracle $Label runner_exit_code=$($Snapshot.runner_exit_code), expected 0"
    }
    if ($Snapshot.until_output_matched -ne "true") {
        return "video oracle $Label until_output_matched=$($Snapshot.until_output_matched), expected true"
    }
    if (-not (Test-VideoOracleZeroMetric -Value $Snapshot.unsupported_steps)) {
        return "video oracle $Label unsupported_steps=$($Snapshot.unsupported_steps), expected 0"
    }
    if (-not (Test-VideoOracleZeroMetric -Value $Snapshot.fetch_failures)) {
        return "video oracle $Label fetch_failures=$($Snapshot.fetch_failures), expected 0"
    }
    if ([string]::IsNullOrWhiteSpace([string]$Snapshot.frame_hash) -or $Snapshot.frame_hash -eq "0") {
        return "video oracle $Label frame_hash missing"
    }
    return $null
}

function Get-VideoOracleScanlineSnapshotFailure {
    param(
        [object]$Snapshot,
        [string]$Label
    )

    $probeFailure = Get-VideoOracleSnapshotFailure -Snapshot $Snapshot -Label $Label
    if ($probeFailure) {
        return $probeFailure
    }
    if ($Snapshot.scanline_capture_active -ne "true") {
        return "video oracle $Label scanline_capture_active=$($Snapshot.scanline_capture_active), expected true"
    }
    if ($Snapshot.scanline_capture_complete -ne "true") {
        return "video oracle $Label scanline_capture_complete=$($Snapshot.scanline_capture_complete), expected true"
    }
    if ([string]::IsNullOrWhiteSpace([string]$Snapshot.scanline_frame_hash) -or
        $Snapshot.scanline_frame_hash -eq "0") {
        return "video oracle $Label scanline_frame_hash missing"
    }
    foreach ($metric in @("scanline_captured_scanlines", "scanline_supported_scanlines")) {
        try {
            if ($null -eq $Snapshot.$metric -or [int64]$Snapshot.$metric -ne 160) {
                return "video oracle $Label $metric=$($Snapshot.$metric), expected 160"
            }
        } catch {
            return "video oracle $Label $metric=$($Snapshot.$metric), expected 160"
        }
    }
    if (-not (Test-VideoOracleZeroMetric -Value $Snapshot.scanline_unsupported_scanlines)) {
        return "video oracle $Label scanline_unsupported_scanlines=$($Snapshot.scanline_unsupported_scanlines), expected 0"
    }
    return $null
}

function Get-VideoOracleFailure {
    param(
        [object]$SuiteResult,
        [string]$Probe
    )

    if ($null -eq $SuiteResult) {
        return "missing suite result"
    }
    if ($SuiteResult.suite_request -ne "video") {
        return "suite_request=$($SuiteResult.suite_request), expected video"
    }
    if ([string]::IsNullOrWhiteSpace($Probe)) {
        return "missing video probe"
    }
    if ($SuiteResult.video_probe -ne $Probe) {
        return "video_probe=$($SuiteResult.video_probe), expected $Probe"
    }
    if (-not $script:VideoOracleProbeTargets.ContainsKey($Probe)) {
        return "video probe is not mapped to an actual/expected oracle: $Probe"
    }
    if ($null -eq $SuiteResult.parsed -or $null -eq $SuiteResult.parsed.failure_count) {
        return "video oracle parsed failure count missing"
    }
    if ([int]$SuiteResult.parsed.failure_count -ne 0) {
        return "video oracle parsed failure_count=$($SuiteResult.parsed.failure_count), expected 0"
    }

    $runner = $SuiteResult.runner
    if ($null -eq $runner) {
        return "missing runner result"
    }
    if ($null -eq $runner.runner_exit_code -or [int]$runner.runner_exit_code -ne 0) {
        return "video oracle runner_exit_code=$($runner.runner_exit_code), expected 0"
    }
    if ($runner.runner_stop_reason -ne "video_probe") {
        return "video oracle runner_stop_reason=$($runner.runner_stop_reason), expected video_probe"
    }
    if (-not (Test-VideoOracleZeroMetric -Value $runner.unsupported_steps)) {
        return "video oracle unsupported_steps=$($runner.unsupported_steps), expected 0"
    }
    if (-not (Test-VideoOracleZeroMetric -Value $runner.fetch_failures)) {
        return "video oracle fetch_failures=$($runner.fetch_failures), expected 0"
    }

    $diagnostics = $SuiteResult.diagnostics
    $visualEvidence = if ($null -ne $diagnostics) { $diagnostics.visual_interactive_evidence } else { $null }
    if ($null -eq $visualEvidence -or $visualEvidence.reached -isnot [bool] -or -not $visualEvidence.reached) {
        return "video oracle diagnostics.visual_interactive_evidence.reached=$($visualEvidence.reached), expected true"
    }
    if ($visualEvidence.active_suite_name -ne "video" -or $visualEvidence.runner_stop_reason -ne "video_probe") {
        return "video oracle visual evidence did not reach the video probe"
    }
    if ([string]::IsNullOrWhiteSpace([string]$visualEvidence.frame_hash) -or $visualEvidence.frame_hash -eq "0") {
        return "video oracle diagnostics.visual_interactive_evidence.frame_hash missing"
    }

    $target = $script:VideoOracleProbeTargets[$Probe]
    $oracle = $null
    if ($diagnostics -is [System.Collections.IDictionary]) {
        $oracle = $diagnostics[$target.oracle_field]
    } else {
        $oracleProperty = $diagnostics.PSObject.Properties[$target.oracle_field]
        if ($null -ne $oracleProperty) {
            $oracle = $oracleProperty.Value
        }
    }
    if ($null -eq $oracle) {
        return "video oracle missing diagnostics.$($target.oracle_field)"
    }
    if ($oracle.frame_hash_comparison -ne "match") {
        return "video oracle diagnostics.$($target.oracle_field).frame_hash_comparison=$($oracle.frame_hash_comparison), expected match"
    }
    if ($null -eq $oracle.actual -or $null -eq $oracle.expected) {
        return "video oracle diagnostics.$($target.oracle_field) actual/expected snapshots missing"
    }
    $probeBase = [regex]::Replace($Probe, '-(?:actual|expected)$', '')
    if ($oracle.actual.probe -ne "$probeBase-actual") {
        return "video oracle diagnostics.$($target.oracle_field).actual.probe=$($oracle.actual.probe), expected $probeBase-actual"
    }
    if ($oracle.expected.probe -ne "$probeBase-expected") {
        return "video oracle diagnostics.$($target.oracle_field).expected.probe=$($oracle.expected.probe), expected $probeBase-expected"
    }

    $actualFailure = Get-VideoOracleSnapshotFailure -Snapshot $oracle.actual -Label "actual"
    if ($actualFailure) {
        return $actualFailure
    }
    $expectedFailure = Get-VideoOracleSnapshotFailure -Snapshot $oracle.expected -Label "expected"
    if ($expectedFailure) {
        return $expectedFailure
    }
    if ($oracle.actual.frame_hash -ne $oracle.expected.frame_hash) {
        return "video oracle diagnostics.$($target.oracle_field) frame hashes differ"
    }

    $requestedSnapshot = if ($Probe.EndsWith("-actual")) { $oracle.actual } else { $oracle.expected }
    if ($visualEvidence.view -ne $requestedSnapshot.view -or
        $visualEvidence.video_test -ne $requestedSnapshot.test_name) {
        return "video oracle visual evidence does not match the requested probe metadata"
    }
    if ($visualEvidence.frame_hash -ne $requestedSnapshot.frame_hash) {
        return "video oracle visual frame hash does not match the requested probe snapshot"
    }

    if ($target.require_scanline_capture) {
        if ($oracle.scanline_frame_hash_comparison -ne "match") {
            return "video oracle diagnostics.$($target.oracle_field).scanline_frame_hash_comparison=$($oracle.scanline_frame_hash_comparison), expected match"
        }
        if ($oracle.actual.scanline_frame_hash -ne $oracle.expected.scanline_frame_hash) {
            return "video oracle diagnostics.$($target.oracle_field) scanline hashes differ"
        }
        $actualScanlineFailure = Get-VideoOracleScanlineSnapshotFailure -Snapshot $oracle.actual -Label "actual"
        if ($actualScanlineFailure) {
            return $actualScanlineFailure
        }
        $expectedScanlineFailure = Get-VideoOracleScanlineSnapshotFailure -Snapshot $oracle.expected -Label "expected"
        if ($expectedScanlineFailure) {
            return $expectedScanlineFailure
        }
    }
    return $null
}
