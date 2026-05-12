param(
  [string[]]$Suites = @("memory", "bios-math", "dma", "timing", "timers", "timer-irq", "shifter", "carry", "multiply-long"),
  [uint32]$MaxSteps = 20000000,
  [uint32]$TraceSteps = 0,
  [int]$PerformanceRuns = 3,
  [switch]$SkipPerformance,
  [switch]$FailOnRed,
  [switch]$FailOnRegression,
  [string]$BaselinePath = "",
  [string]$OutputDir = ""
)

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
$jsonPath = Join-Path $OutputDir "credibility-matrix-$timestamp.json"
$latestJsonPath = Join-Path $OutputDir "credibility-matrix-latest.json"
$markdownPath = Join-Path $OutputDir "credibility-matrix-$timestamp.md"
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

$expectedSuiteNames = @{
  "memory" = "Memory tests"
  "io-read" = "I/O read tests"
  "timing" = "Timing tests"
  "timers" = "Timer count-up tests"
  "timer-irq" = "Timer IRQ tests"
  "shifter" = "Shifter tests"
  "carry" = "Carry tests"
  "multiply-long" = "Multiply long tests"
  "bios-math" = "BIOS math tests"
  "dma" = "DMA tests"
  "sio-read" = "SIO read tests"
  "sio-timing" = "SIO timing tests"
  "misc-edge" = "Miscellaneous edge case tests"
  "video" = "Video tests"
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
    [string]$PassTotal,
    [string]$FirstFailure,
    [string]$Categories,
    [string]$Artifact
  )

  $safeFailure = if ([string]::IsNullOrWhiteSpace($FirstFailure)) { "" } else { $FirstFailure.Replace("|", "\|") }
  $safeCategories = if ([string]::IsNullOrWhiteSpace($Categories)) { "" } else { $Categories.Replace("|", "\|") }
  $Lines.Add("| ``$Target`` | $Status | ``$PassTotal`` | $safeFailure | $safeCategories | ``$Artifact`` |")
}

$suiteRows = New-Object System.Collections.Generic.List[object]
$suiteScript = Join-Path $PSScriptRoot "run-mgba-suite.ps1"
foreach ($suite in $Suites) {
  $suiteError = $null
  try {
    & $suiteScript -Suite $suite -MaxSteps $MaxSteps -TraceSteps $TraceSteps -UntilOutput "END:" | Out-Null
  } catch {
    $suiteError = $_.Exception.Message
  }

  $latestSuiteJson = Join-Path $OutputDir "mgba-suite-latest.json"
  $suiteResult = $null
  if (Test-Path -LiteralPath $latestSuiteJson -PathType Leaf) {
    $suiteResult = Get-Content -Raw -LiteralPath $latestSuiteJson | ConvertFrom-Json
  }

  $expectedSuiteName = if ($expectedSuiteNames.ContainsKey($suite)) {
    $expectedSuiteNames[$suite]
  } else {
    ""
  }
  $status = ConvertTo-MatrixStatus -SuiteResult $suiteResult -ExpectedSuiteName $expectedSuiteName
  if ($suiteError) {
    $status = "RED"
  }

  $parsed = if ($suiteResult) { $suiteResult.parsed } else { $null }
  $runner = if ($suiteResult) { $suiteResult.runner } else { $null }
  $suiteMismatch = $parsed -and -not [string]::IsNullOrWhiteSpace($expectedSuiteName) -and
    $parsed.suite -ne $expectedSuiteName
  $passTotal = if ($parsed -and $null -ne $parsed.pass -and $null -ne $parsed.total) {
    "$($parsed.pass)/$($parsed.total)"
  } else {
    ""
  }

  $suiteRows.Add([ordered]@{
    target = $suite
    type = "mgba_suite"
    status = $status
    expected_suite = $expectedSuiteName
    parsed_suite = if ($parsed) { $parsed.suite } else { $null }
    pass_total = $passTotal
    pass = if ($parsed) { $parsed.pass } else { $null }
    total = if ($parsed) { $parsed.total } else { $null }
    first_failure = if ($suiteError) {
      $suiteError
    } elseif ($suiteMismatch) {
      "Selected '$($parsed.suite)' but expected '$expectedSuiteName'"
    } elseif ($parsed) {
      $parsed.first_failure
    } else {
      "missing suite result"
    }
    failure_count = if ($parsed) { $parsed.failure_count } else { $null }
    failure_categories = if ($parsed) { $parsed.failure_categories } else { @() }
    runner_stop_reason = if ($runner) { $runner.runner_stop_reason } else { $null }
    unsupported_steps = if ($runner) { $runner.unsupported_steps } else { $null }
    fetch_failures = if ($runner) { $runner.fetch_failures } else { $null }
    artifact = if ($suiteResult) { $suiteResult.json_result_path } else { $latestSuiteJson }
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
  green_definition = "Suite reaches END with pass=total, zero parsed failures, zero unsupported instructions, zero fetch failures; performance rows pass checksum and threshold gates."
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
$markdown.Add("| Target | Status | Pass/total | First failure | Categories | Artifact |")
$markdown.Add("| --- | --- | --- | --- | --- | --- |")
foreach ($row in $suiteRows) {
  Add-MarkdownTableRow -Lines $markdown `
    -Target $row.target `
    -Status $row.status `
    -PassTotal $row.pass_total `
    -FirstFailure $row.first_failure `
    -Categories (ConvertTo-CategorySummary -Categories $row.failure_categories) `
    -Artifact $row.artifact
}
if ($performanceRow) {
  Add-MarkdownTableRow -Lines $markdown `
    -Target $performanceRow.target `
    -Status $performanceRow.status `
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
if ($FailOnRegression -and $regressionStatus -ne "GREEN") {
  throw "credibility_matrix: REGRESSION"
}
