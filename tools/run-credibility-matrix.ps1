# Default -Suites matches tools/mgba-suite-green-baseline.json (not video_oracle_alias rows).
# Video-only runs with -FailOnRegression falsely report REGRESSION: baseline targets are MISSING.
#
# SINGLE-WRITER ASSUMPTION: this script overwrites the shared
# build\test-results\credibility-matrix-latest.json / .md artifacts on every
# run (timestamped per-run files are unique, "latest" copies are not).
# Concurrent runs clobber each other's "latest" artifacts; serialize runs or
# pass a distinct -OutputDir per process.
param(
  [string[]]$Suites = @(
    "memory", "loadstore", "io-read", "bios-math", "dma", "shifter", "carry",
    "multiply-long", "timer-irq", "timers", "timing", "ldmia", "stmia",
    "sio-read", "sio-timing", "misc-edge"
  ),
  [uint32]$MaxSteps = 20000000,
  [uint32]$TraceSteps = 0,
  [int]$PerformanceRuns = 3,
  [switch]$SkipPerformance,
  [switch]$FailOnRed,
  [switch]$FailOnRegression,
  [string]$BaselinePath = "",
  [string]$OutputDir = ""
)

# Requires PowerShell 7.3+ for $PSNativeCommandUseErrorActionPreference.
#requires -Version 7.3

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
  $OutputDir = Join-Path $repoRoot "build\test-results"
}
if ([string]::IsNullOrWhiteSpace($BaselinePath)) {
  $BaselinePath = Join-Path $PSScriptRoot "mgba-suite-green-baseline.json"
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$artifactStem = "credibility-matrix-$timestamp-$PID"
$jsonPath = Join-Path $OutputDir "$artifactStem.json"
$latestJsonPath = Join-Path $OutputDir "credibility-matrix-latest.json"
$markdownPath = Join-Path $OutputDir "$artifactStem.md"
$latestMarkdownPath = Join-Path $OutputDir "credibility-matrix-latest.md"
$baseline = $null
if (Test-Path -LiteralPath $BaselinePath -PathType Leaf) {
  $baseline = Get-Content -Raw -LiteralPath $BaselinePath | ConvertFrom-Json
}

function ConvertTo-MatrixStatus {
  param(
    [object]$SuiteResult,
    [string]$ExpectedSuiteName
  )

  if ($null -eq $SuiteResult) {
    return "RED"
  }
  $parsed = $SuiteResult.parsed
  $runner = $SuiteResult.runner
  if ($null -eq $parsed -or $null -eq $runner) {
    return "RED"
  }
  if ($null -ne $runner.runner_exit_code -and [int]$runner.runner_exit_code -ne 0) {
    return "RED"
  }
  if (-not [string]::IsNullOrWhiteSpace($ExpectedSuiteName) -and
      $parsed.suite -ne $ExpectedSuiteName) {
    return "RED"
  }
  if (-not [bool]$parsed.ended) {
    return "RED"
  }
  if ($null -eq $parsed.pass -or $null -eq $parsed.total) {
    return "RED"
  }
  if ([int]$parsed.pass -ne [int]$parsed.total) {
    return "RED"
  }
  if ([int]$parsed.failure_count -ne 0) {
    return "RED"
  }
  if ($null -ne $runner.unsupported_steps -and [int64]$runner.unsupported_steps -ne 0) {
    return "RED"
  }
  if ($null -ne $runner.fetch_failures -and [int64]$runner.fetch_failures -ne 0) {
    return "RED"
  }
  return "GREEN"
}

function Get-ObjectField {
  param(
    [object]$Object,
    [string]$Name
  )

  if ($null -eq $Object) {
    return $null
  }
  $property = $Object.PSObject.Properties[$Name]
  if ($null -eq $property) {
    return $null
  }
  return $property.Value
}

function Test-ZeroMetric {
  param([object]$Value)

  return $null -ne $Value -and [int64]$Value -eq 0
}

function Test-VideoProbeSnapshot {
  param([object]$Snapshot)

  return $null -eq (Get-VideoProbeSnapshotFailure -Snapshot $Snapshot -Label "probe")
}

function Get-VideoProbeSnapshotFailure {
  param(
    [object]$Snapshot,
    [string]$Label
  )

  if ($null -eq $Snapshot) {
    return "video oracle $Label probe missing"
  }
  if ($Snapshot.status -ne "probe_reached") {
    return "video oracle $Label probe status=$($Snapshot.status), expected probe_reached"
  }
  if ($Snapshot.runner_stop_reason -ne "video_probe") {
    return "video oracle $Label probe runner_stop_reason=$($Snapshot.runner_stop_reason), expected video_probe"
  }
  if ($null -eq $Snapshot.runner_exit_code -or [int]$Snapshot.runner_exit_code -ne 0) {
    return "video oracle $Label probe runner_exit_code=$($Snapshot.runner_exit_code), expected 0"
  }
  if (-not (Test-ZeroMetric -Value $Snapshot.unsupported_steps)) {
    return "video oracle $Label probe unsupported_steps=$($Snapshot.unsupported_steps), expected 0"
  }
  if (-not (Test-ZeroMetric -Value $Snapshot.fetch_failures)) {
    return "video oracle $Label probe fetch_failures=$($Snapshot.fetch_failures), expected 0"
  }
  if ([string]::IsNullOrWhiteSpace($Snapshot.frame_hash)) {
    return "video oracle $Label probe frame_hash missing"
  }
  return $null
}

function Get-VideoScanlineSnapshotFailure {
  param(
    [object]$Snapshot,
    [string]$Label
  )

  $probeFailure = Get-VideoProbeSnapshotFailure -Snapshot $Snapshot -Label $Label
  if ($probeFailure) {
    return $probeFailure
  }
  if ($Snapshot.scanline_capture_active -ne "true") {
    return "video oracle $Label scanline_capture_active=$($Snapshot.scanline_capture_active), expected true"
  }
  if ($Snapshot.scanline_capture_complete -ne "true") {
    return "video oracle $Label scanline_capture_complete=$($Snapshot.scanline_capture_complete), expected true"
  }
  if ([string]::IsNullOrWhiteSpace($Snapshot.scanline_frame_hash) -or
      $Snapshot.scanline_frame_hash -eq "0") {
    return "video oracle $Label scanline_frame_hash missing"
  }
  if ([int64]$Snapshot.scanline_captured_scanlines -ne 160) {
    return "video oracle $Label scanline_captured_scanlines=$($Snapshot.scanline_captured_scanlines), expected 160"
  }
  if ([int64]$Snapshot.scanline_supported_scanlines -ne 160) {
    return "video oracle $Label scanline_supported_scanlines=$($Snapshot.scanline_supported_scanlines), expected 160"
  }
  if (-not (Test-ZeroMetric -Value $Snapshot.scanline_unsupported_scanlines)) {
    return "video oracle $Label scanline_unsupported_scanlines=$($Snapshot.scanline_unsupported_scanlines), expected 0"
  }
  return $null
}

function Get-VideoOracleFailure {
  param(
    [object]$SuiteResult,
    [object]$EvidenceTarget
  )

  if ($null -eq $SuiteResult) {
    return "missing suite result"
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
  if (-not (Test-ZeroMetric -Value $runner.unsupported_steps)) {
    return "video oracle unsupported_steps=$($runner.unsupported_steps), expected 0"
  }
  if (-not (Test-ZeroMetric -Value $runner.fetch_failures)) {
    return "video oracle fetch_failures=$($runner.fetch_failures), expected 0"
  }

  $visualEvidence = $SuiteResult.diagnostics.visual_interactive_evidence
  if ($null -eq $visualEvidence -or -not [bool]$visualEvidence.reached) {
    return "video oracle diagnostics.visual_interactive_evidence.reached=$($visualEvidence.reached), expected true"
  }
  if ([string]::IsNullOrWhiteSpace($visualEvidence.frame_hash)) {
    return "video oracle diagnostics.visual_interactive_evidence.frame_hash missing"
  }

  $oracle = Get-ObjectField -Object $SuiteResult.diagnostics -Name $EvidenceTarget.oracle_field
  if ($null -eq $oracle) {
    return "video oracle missing diagnostics.$($EvidenceTarget.oracle_field)"
  }
  if ($oracle.frame_hash_comparison -ne "match") {
    return "video oracle diagnostics.$($EvidenceTarget.oracle_field).frame_hash_comparison=$($oracle.frame_hash_comparison), expected match"
  }
  if ($EvidenceTarget.require_scanline_capture) {
    if ($oracle.scanline_frame_hash_comparison -ne "match") {
      return "video oracle diagnostics.$($EvidenceTarget.oracle_field).scanline_frame_hash_comparison=$($oracle.scanline_frame_hash_comparison), expected match"
    }
    $actualScanlineFailure = Get-VideoScanlineSnapshotFailure -Snapshot $oracle.actual -Label "actual"
    if ($actualScanlineFailure) {
      return $actualScanlineFailure
    }
    $expectedScanlineFailure = Get-VideoScanlineSnapshotFailure -Snapshot $oracle.expected -Label "expected"
    if ($expectedScanlineFailure) {
      return $expectedScanlineFailure
    }
  }
  $actualFailure = Get-VideoProbeSnapshotFailure -Snapshot $oracle.actual -Label "actual"
  if ($actualFailure) {
    return $actualFailure
  }
  $expectedFailure = Get-VideoProbeSnapshotFailure -Snapshot $oracle.expected -Label "expected"
  if ($expectedFailure) {
    return $expectedFailure
  }

  return $null
}

function ConvertTo-VideoOracleMatrixStatus {
  param(
    [object]$SuiteResult,
    [object]$EvidenceTarget
  )

  $failure = Get-VideoOracleFailure -SuiteResult $SuiteResult -EvidenceTarget $EvidenceTarget
  if ($null -eq $failure) {
    return "GREEN"
  }
  return "RED"
}

$expectedSuiteNames = @{
  "memory" = "Memory tests"
  "loadstore" = "Memory tests"
  "io-read" = "I/O read tests"
  "timing" = "Timing tests"
  "ldmia" = "Timing tests / ldmia evidence"
  "stmia" = "Timing tests / stmia evidence"
  "timers" = "Timer count-up tests"
  "timer-irq" = "Timer IRQ tests"
  "shifter" = "Shifter tests"
  "carry" = "Carry tests"
  "multiply-long" = "Multiply long tests"
  "bios-math" = "BIOS math tests"
  "dma" = "DMA tests"
  "sio-read" = "SIO register R/W tests"
  "sio-timing" = "SIO timing tests"
  "misc-edge" = "Misc. edge case tests"
  "video" = "Video tests"
}
$suiteEvidenceTargets = @{
  "loadstore" = [ordered]@{
    upstream_suite = "memory"
    kind = "embedded_alias"
    scope = "Broad load/store behavior embedded in the upstream mGBA memory suite."
    note = "loadstore is a harness evidence alias, not a standalone upstream mGBA suite."
  }
  "ldmia" = [ordered]@{
    upstream_suite = "timing"
    kind = "embedded_alias"
    scope = "LDMIA timing evidence embedded in the upstream mGBA timing suite."
    note = "ldmia is a harness evidence alias, not a standalone upstream mGBA suite."
  }
  "stmia" = [ordered]@{
    upstream_suite = "timing"
    kind = "embedded_alias"
    scope = "STMIA timing evidence embedded in the upstream mGBA timing suite."
    note = "stmia is a harness evidence alias, not a standalone upstream mGBA suite."
  }
  "video-basic-mode-3" = [ordered]@{
    upstream_suite = "video"
    kind = "video_oracle_alias"
    scope = "Basic Mode 3 actual/expected deterministic video oracle evidence."
    note = "Interactive video evidence alias; does not mark the upstream video suite green."
    video_probe = "basic-mode-3-actual"
    oracle_field = "basic_mode_3_oracle"
  }
  "video-basic-mode-4" = [ordered]@{
    upstream_suite = "video"
    kind = "video_oracle_alias"
    scope = "Basic Mode 4 actual/expected deterministic video oracle evidence."
    note = "Interactive video evidence alias; does not mark the upstream video suite green."
    video_probe = "basic-mode-4-actual"
    oracle_field = "basic_mode_4_oracle"
  }
  "video-degenerate-obj" = [ordered]@{
    upstream_suite = "video"
    kind = "video_oracle_alias"
    scope = "Degenerate OBJ transforms actual/expected deterministic video oracle evidence."
    note = "Interactive video evidence alias; does not mark the upstream video suite green."
    video_probe = "degenerate-obj-actual"
    oracle_field = "degenerate_obj_oracle"
  }
  "video-layer-toggle" = [ordered]@{
    upstream_suite = "video"
    kind = "video_oracle_alias"
    scope = "Layer toggle actual/expected deterministic video oracle evidence."
    note = "Interactive video evidence alias; does not mark the upstream video suite green."
    video_probe = "layer-toggle-actual"
    oracle_field = "layer_toggle_oracle"
    require_scanline_capture = $true
  }
  "video-layer-toggle-2" = [ordered]@{
    upstream_suite = "video"
    kind = "video_oracle_alias"
    scope = "Layer toggle 2 actual/expected deterministic video oracle evidence."
    note = "Interactive video evidence alias; does not mark the upstream video suite green."
    video_probe = "layer-toggle-2-actual"
    oracle_field = "layer_toggle_2_oracle"
    require_scanline_capture = $true
  }
  "video-oam-update-delay" = [ordered]@{
    upstream_suite = "video"
    kind = "video_oracle_alias"
    scope = "OAM Update Delay actual/expected deterministic video oracle evidence."
    note = "Interactive video evidence alias; does not mark the upstream video suite green."
    video_probe = "oam-update-delay-actual"
    oracle_field = "oam_update_delay_oracle"
    require_scanline_capture = $true
  }
  "video-window-offscreen-reset" = [ordered]@{
    upstream_suite = "video"
    kind = "video_oracle_alias"
    scope = "Window offscreen reset actual/expected deterministic video oracle evidence."
    note = "Interactive video evidence alias; does not mark the upstream video suite green."
    video_probe = "window-offscreen-reset-actual"
    oracle_field = "window_offscreen_reset_oracle"
  }
  "video" = [ordered]@{
    upstream_suite = "video"
    kind = "video_suite_automation"
    scope = "All seven interactive mGBA video tests via automated actual/expected oracle probes."
    note = "pass/total is 7/7 when run-video-suite-all.ps1 reports GREEN."
  }
}

function Resolve-SuiteEvidenceTarget {
  param([string]$SuiteName)

  if ($suiteEvidenceTargets.ContainsKey($SuiteName)) {
    return $suiteEvidenceTargets[$SuiteName]
  }
  return [ordered]@{
    upstream_suite = $SuiteName
    kind = "upstream_suite"
    scope = "Direct upstream mGBA suite entry."
    note = "Requested target maps directly to the upstream mGBA suite entry."
  }
}

function ConvertTo-CategorySummary {
  param([object[]]$Categories)

  if ($null -eq $Categories -or @($Categories).Count -eq 0) {
    return ""
  }

  $parts = New-Object System.Collections.Generic.List[string]
  foreach ($category in @($Categories)) {
    $parts.Add("$($category.name)=$($category.count)")
  }
  return ($parts -join "; ")
}

function Add-MarkdownTableRow {
  param(
    [System.Collections.Generic.List[string]]$Lines,
    [string]$Target,
    [string]$Status,
    [string]$Source,
    [string]$PassTotal,
    [string]$FirstFailure,
    [string]$Categories,
    [string]$Artifact
  )

  $safeFailure = if ([string]::IsNullOrWhiteSpace($FirstFailure)) { "" } else { $FirstFailure.Replace("|", "\|") }
  $safeCategories = if ([string]::IsNullOrWhiteSpace($Categories)) { "" } else { $Categories.Replace("|", "\|") }
  $safeSource = if ([string]::IsNullOrWhiteSpace($Source)) { "" } else { $Source.Replace("|", "\|") }
  $Lines.Add("| ``$Target`` | $safeSource | $Status | ``$PassTotal`` | $safeFailure | $safeCategories | ``$Artifact`` |")
}

function Get-SuiteJsonResultPath {
  param([object[]]$OutputLines)

  foreach ($line in $OutputLines) {
    $text = [string]$line
    if ($text -match '^suite_test:\s+json_result_path=(.+)$') {
      return $Matches[1].Trim()
    }
  }
  return $null
}

$suiteRows = New-Object System.Collections.Generic.List[object]
$suiteScript = Join-Path $PSScriptRoot "run-mgba-suite.ps1"
foreach ($suite in $Suites) {
  $evidenceTarget = Resolve-SuiteEvidenceTarget -SuiteName $suite
  $suiteError = $null
  $suiteOutput = @()
  try {
    if ($evidenceTarget.kind -eq "video_suite_automation") {
      $videoAllScript = Join-Path $PSScriptRoot "run-video-suite-all.ps1"
      $suiteOutput = @(& $videoAllScript -MaxSteps $MaxSteps 2>&1)
    } elseif ($evidenceTarget.kind -eq "video_oracle_alias") {
      $suiteOutput = @(& $suiteScript -Suite $evidenceTarget.upstream_suite -VideoProbe $evidenceTarget.video_probe -MaxSteps $MaxSteps -TraceSteps $TraceSteps 2>&1)
    } else {
      $suiteOutput = @(& $suiteScript -Suite $suite -MaxSteps $MaxSteps -TraceSteps $TraceSteps -UntilOutput "END:" 2>&1)
    }
  } catch {
    $suiteError = $_.Exception.Message
    $suiteOutput += $_
  }

  $suiteResult = $null
  $suiteJsonPath = Get-SuiteJsonResultPath -OutputLines $suiteOutput
  if (-not [string]::IsNullOrWhiteSpace($suiteJsonPath) -and
      (Test-Path -LiteralPath $suiteJsonPath -PathType Leaf)) {
    $suiteResult = Get-Content -Raw -LiteralPath $suiteJsonPath | ConvertFrom-Json
  }

  $expectedSuiteName = if ($expectedSuiteNames.ContainsKey($suite)) {
    $expectedSuiteNames[$suite]
  } elseif ($evidenceTarget.kind -eq "video_suite_automation") {
    $expectedSuiteNames["video"]
  } elseif ($evidenceTarget.kind -eq "video_oracle_alias" -and $expectedSuiteNames.ContainsKey($evidenceTarget.upstream_suite)) {
    $expectedSuiteNames[$evidenceTarget.upstream_suite]
  } else {
    ""
  }
  $status = if ($evidenceTarget.kind -eq "video_suite_automation") {
    $summaryPath = Join-Path $repoRoot "build\test-results\video-suite-all-latest.json"
    if (Test-Path -LiteralPath $summaryPath -PathType Leaf) {
      $videoSummary = Get-Content -Raw -LiteralPath $summaryPath | ConvertFrom-Json
      if ($videoSummary.status -eq "GREEN" -and $videoSummary.pass -eq $videoSummary.total) { "GREEN" } else { "RED" }
    } else {
      "RED"
    }
  } elseif ($evidenceTarget.kind -eq "video_oracle_alias") {
    ConvertTo-VideoOracleMatrixStatus -SuiteResult $suiteResult -EvidenceTarget $evidenceTarget
  } else {
    ConvertTo-MatrixStatus -SuiteResult $suiteResult -ExpectedSuiteName $expectedSuiteName
  }
  if ($suiteError) {
    $status = "RED"
  }

  $parsed = if ($suiteResult) { $suiteResult.parsed } else { $null }
  $runner = if ($suiteResult) { $suiteResult.runner } else { $null }
  $suiteMismatch = $evidenceTarget.kind -ne "video_oracle_alias" -and
    $parsed -and -not [string]::IsNullOrWhiteSpace($expectedSuiteName) -and
    $parsed.suite -ne $expectedSuiteName
  $passTotal = if ($evidenceTarget.kind -eq "video_suite_automation") {
    $summaryPath = Join-Path $repoRoot "build\test-results\video-suite-all-latest.json"
    if (Test-Path -LiteralPath $summaryPath -PathType Leaf) {
      $videoSummary = Get-Content -Raw -LiteralPath $summaryPath | ConvertFrom-Json
      "$($videoSummary.pass)/$($videoSummary.total)"
    } else {
      "0/7"
    }
  } elseif ($evidenceTarget.kind -eq "video_oracle_alias") {
    "video_probe"
  } elseif ($parsed -and $null -ne $parsed.pass -and $null -ne $parsed.total) {
    "$($parsed.pass)/$($parsed.total)"
  } else {
    ""
  }
  $videoOracleFailure = if ($evidenceTarget.kind -eq "video_oracle_alias" -and -not $suiteError) {
    Get-VideoOracleFailure -SuiteResult $suiteResult -EvidenceTarget $evidenceTarget
  } else {
    $null
  }
  $videoOracle = if ($evidenceTarget.kind -eq "video_oracle_alias" -and $suiteResult) {
    Get-ObjectField -Object $suiteResult.diagnostics -Name $evidenceTarget.oracle_field
  } else {
    $null
  }
  $videoEvidence = if ($evidenceTarget.kind -eq "video_oracle_alias" -and $suiteResult) {
    $suiteResult.diagnostics.visual_interactive_evidence
  } else {
    $null
  }
  $failureCategories = if ($parsed) { $parsed.failure_categories } else { @() }
  if ($videoOracleFailure -and @($failureCategories).Count -eq 0) {
    $failureCategories = @([ordered]@{
      name = $evidenceTarget.oracle_field
      count = 1
      first_failure = $videoOracleFailure
      tests = @($suite)
      examples = @($videoOracleFailure)
    })
  }

  $suiteRows.Add([ordered]@{
    target = $suite
    evidence_target = $suite
    upstream_suite = $evidenceTarget.upstream_suite
    evidence_kind = $evidenceTarget.kind
    evidence_scope = $evidenceTarget.scope
    evidence_note = $evidenceTarget.note
    type = if ($evidenceTarget.kind -eq "video_oracle_alias") {
      "mgba_video_oracle_alias"
    } elseif ($evidenceTarget.kind -eq "video_suite_automation") {
      "mgba_video_suite_automation"
    } elseif ($evidenceTarget.kind -eq "embedded_alias") {
      "mgba_evidence_alias"
    } else {
      "mgba_suite"
    }
    status = $status
    expected_suite = $expectedSuiteName
    parsed_suite = if ($parsed) { $parsed.suite } else { $null }
    pass_total = $passTotal
    pass = if ($parsed) { $parsed.pass } else { $null }
    total = if ($parsed) { $parsed.total } else { $null }
    first_failure = if ($suiteError) {
      $suiteError
    } elseif ($videoOracleFailure) {
      $videoOracleFailure
    } elseif ($suiteMismatch) {
      "Selected '$($parsed.suite)' but expected '$expectedSuiteName'"
    } elseif ($evidenceTarget.kind -eq "video_oracle_alias") {
      $null
    } elseif ($parsed) {
      $parsed.first_failure
    } else {
      "missing suite result"
    }
    failure_count = if ($evidenceTarget.kind -eq "video_oracle_alias") {
      if ($videoOracleFailure) { 1 } else { 0 }
    } elseif ($parsed) {
      $parsed.failure_count
    } else {
      $null
    }
    failure_categories = if ($evidenceTarget.kind -eq "video_oracle_alias") {
      if ($videoOracleFailure) { $failureCategories } else { @() }
    } elseif ($parsed) {
      $parsed.failure_categories
    } else {
      @()
    }
    runner_stop_reason = if ($runner) { $runner.runner_stop_reason } else { $null }
    runner_exit_code = if ($runner) { $runner.runner_exit_code } else { $null }
    unsupported_steps = if ($runner) { $runner.unsupported_steps } else { $null }
    fetch_failures = if ($runner) { $runner.fetch_failures } else { $null }
    video_probe = if ($evidenceTarget.kind -eq "video_oracle_alias") { $evidenceTarget.video_probe } else { $null }
    video_oracle_field = if ($evidenceTarget.kind -eq "video_oracle_alias") { $evidenceTarget.oracle_field } else { $null }
    video_oracle_comparison = if ($videoOracle) { $videoOracle.frame_hash_comparison } else { $null }
    video_scanline_required = if ($evidenceTarget.kind -eq "video_oracle_alias") { [bool]$evidenceTarget.require_scanline_capture } else { $false }
    video_scanline_oracle_comparison = if ($videoOracle) { $videoOracle.scanline_frame_hash_comparison } else { $null }
    video_frame_hash = if ($videoEvidence) { $videoEvidence.frame_hash } else { $null }
    video_scanline_frame_hash = if ($videoEvidence) { $videoEvidence.scanline_frame_hash } else { $null }
    video_scanline_captured_scanlines = if ($videoEvidence) { $videoEvidence.scanline_captured_scanlines } else { $null }
    video_scanline_capture_complete = if ($videoEvidence) { $videoEvidence.scanline_capture_complete } else { $null }
    artifact = if ($suiteResult) { $suiteResult.json_result_path } elseif ($suiteJsonPath) { $suiteJsonPath } else { $null }
  })
}

$performanceRow = $null
if (-not $SkipPerformance) {
  $performanceScript = Join-Path $PSScriptRoot "run-core-performance-report.ps1"
  $performanceError = $null
  try {
    & $performanceScript -Runs $PerformanceRuns -OutputDir $OutputDir | Out-Null
  } catch {
    $performanceError = $_.Exception.Message
  }

  $latestPerformanceJson = Join-Path $OutputDir "core-performance-latest.json"
  $performanceReport = $null
  if (Test-Path -LiteralPath $latestPerformanceJson -PathType Leaf) {
    $performanceReport = Get-Content -Raw -LiteralPath $latestPerformanceJson | ConvertFrom-Json
  }

  $failedRows = @()
  if ($performanceReport) {
    $failedRows = @($performanceReport.summary | Where-Object { $_.status -ne "PASS" })
  }
  $performanceStatus = if ($performanceError -or $null -eq $performanceReport -or @($failedRows).Count -gt 0) {
    "RED"
  } else {
    "GREEN"
  }

  $performanceRow = [ordered]@{
    target = "core-performance"
    type = "performance_gate"
    status = $performanceStatus
    runs = $PerformanceRuns
    first_failure = if ($performanceError) {
      $performanceError
    } elseif (@($failedRows).Count -gt 0) {
      "$($failedRows[0].benchmark): $($failedRows[0].status)"
    } else {
      $null
    }
    failed_rows = @($failedRows)
    artifact = $latestPerformanceJson
  }
}

$allRows = @()
foreach ($row in $suiteRows) {
  $allRows += $row
}
if ($performanceRow) {
  $allRows += $performanceRow
}

$redRows = @($allRows | Where-Object { $_.status -ne "GREEN" })
$overallStatus = if (@($redRows).Count -eq 0) { "GREEN" } else { "RED" }
$expectedGreenTargets = @()
if ($baseline -and $baseline.green_suites) {
  $expectedGreenTargets = @($baseline.green_suites)
}
if ($baseline -and $baseline.include_performance -and -not $SkipPerformance) {
  $expectedGreenTargets += "core-performance"
}
$regressionRows = @()
foreach ($target in $expectedGreenTargets) {
  $row = @($allRows | Where-Object { $_.target -eq $target } | Select-Object -First 1)
  if (@($row).Count -eq 0) {
    $regressionRows += [ordered]@{
      target = $target
      status = "MISSING"
      first_failure = "target missing from current matrix"
    }
  } elseif ($row[0].status -ne "GREEN") {
    $regressionRows += $row[0]
  }
}
$regressionStatus = if (@($regressionRows).Count -eq 0) { "GREEN" } else { "RED" }

$matrix = [ordered]@{
  generated_at = (Get-Date).ToString("o")
  goal = "Evidence-backed accuracy and performance comparison readiness against top open-source GBA emulators."
  purpose = "Keep every benchmark comparison paired with correctness evidence, subsystem grouping, and reproducible artifacts."
  green_definition = "Suite targets reach END with pass=total, zero parsed failures, zero unsupported instructions, zero fetch failures; video oracle aliases stop at video_probe with visual evidence, a frame hash, and matching actual/expected oracle probes; performance rows pass checksum and threshold gates."
  max_steps = $MaxSteps
  trace_steps = $TraceSteps
  performance_runs = if ($SkipPerformance) { 0 } else { $PerformanceRuns }
  overall_status = $overallStatus
  regression_status = $regressionStatus
  regression_baseline_path = if (Test-Path -LiteralPath $BaselinePath -PathType Leaf) {
    (Resolve-Path -LiteralPath $BaselinePath).Path
  } else {
    $BaselinePath
  }
  expected_green_targets = $expectedGreenTargets
  regressions = @($regressionRows)
  rows = $allRows
  red_count = @($redRows).Count
  regression_count = @($regressionRows).Count
  next_red_target = if (@($redRows).Count -gt 0) { $redRows[0].target } else { $null }
}

$json = $matrix | ConvertTo-Json -Depth 10
Set-Content -LiteralPath $jsonPath -Value $json -Encoding UTF8
Set-Content -LiteralPath $latestJsonPath -Value $json -Encoding UTF8

$markdown = New-Object System.Collections.Generic.List[string]
$markdown.Add("# Credibility Matrix $timestamp")
$markdown.Add("")
$markdown.Add("Goal: evidence-backed accuracy and performance comparison readiness against top open-source GBA emulators.")
$markdown.Add("")
$markdown.Add("Purpose: keep every benchmark comparison paired with correctness evidence, subsystem grouping, and reproducible artifacts.")
$markdown.Add("")
$markdown.Add("Overall status: **$overallStatus**")
$markdown.Add("")
$markdown.Add("Regression status: **$regressionStatus**")
$markdown.Add("")
$markdown.Add("| Target | Source | Status | Pass/total | First failure | Categories | Artifact |")
$markdown.Add("| --- | --- | --- | --- | --- | --- | --- |")
foreach ($row in $suiteRows) {
  $rowSource = if ($row.evidence_kind -eq "video_oracle_alias") {
    "$($row.evidence_kind) -> $($row.upstream_suite) / $($row.video_probe) / $($row.video_oracle_field)"
  } else {
    "$($row.evidence_kind) -> $($row.upstream_suite)"
  }
  Add-MarkdownTableRow -Lines $markdown `
    -Target $row.target `
    -Status $row.status `
    -Source $rowSource `
    -PassTotal $row.pass_total `
    -FirstFailure $row.first_failure `
    -Categories (ConvertTo-CategorySummary -Categories $row.failure_categories) `
    -Artifact $row.artifact
}
if ($performanceRow) {
  Add-MarkdownTableRow -Lines $markdown `
    -Target $performanceRow.target `
    -Status $performanceRow.status `
    -Source "performance_gate" `
    -PassTotal "" `
    -FirstFailure $performanceRow.first_failure `
    -Categories "" `
    -Artifact $performanceRow.artifact
}
$markdown.Add("")
$markdown.Add("Next red target: ``$($matrix.next_red_target)``")
$markdown.Add("")
$markdown.Add("Regression baseline: ``$($matrix.regression_baseline_path)``")
if (@($regressionRows).Count -gt 0) {
  $markdown.Add("")
  $markdown.Add("Regressions:")
  foreach ($row in $regressionRows) {
    $markdown.Add("- ``$($row.target)``: $($row.status) $($row.first_failure)")
  }
}
$markdown.Add("")
$markdown.Add("JSON: ``$jsonPath``")

Set-Content -LiteralPath $markdownPath -Value $markdown -Encoding UTF8
Set-Content -LiteralPath $latestMarkdownPath -Value $markdown -Encoding UTF8

Write-Output "credibility_matrix: status=$overallStatus"
Write-Output "credibility_matrix: json_path=$jsonPath"
Write-Output "credibility_matrix: markdown_path=$markdownPath"
Write-Output "credibility_matrix: next_red_target=$($matrix.next_red_target)"

if ($FailOnRed -and $overallStatus -ne "GREEN") {
  throw "credibility_matrix: RED"
}
# Regression gate: baseline green_suites (+ core-performance) must appear GREEN in this run.
# Omitting baseline targets (e.g. video-only -Suites) is a false REGRESSION, not emulator drift.
if ($FailOnRegression -and $regressionStatus -ne "GREEN") {
  throw "credibility_matrix: REGRESSION"
}
