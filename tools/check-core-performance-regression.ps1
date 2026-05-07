param(
  [int]$Runs = 3,
  [string]$BaselinePath = "",
  [string]$BenchmarkOutputPath = "",
  [string]$ReportPath = ""
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($BaselinePath)) {
  $BaselinePath = Join-Path $PSScriptRoot "core-benchmark-baseline.json"
}
if ([string]::IsNullOrWhiteSpace($ReportPath)) {
  $ReportPath = Join-Path $repoRoot "build\core_benchmark_regression_latest.json"
}

function Read-BenchmarkRows {
  param([string[]]$Lines)

  if ($Lines.Count -eq 0) {
    throw "core_performance_regression: benchmark output is empty"
  }
  if ($Lines[-1] -ne "core_benchmark: PASS") {
    throw "core_performance_regression: benchmark output did not end with PASS"
  }

  $csvLines = $Lines | Where-Object { $_ -match '^[a-zA-Z0-9_]+,[0-9]+,' }
  if ($csvLines.Count -eq 0) {
    throw "core_performance_regression: no benchmark CSV rows found"
  }

  return $csvLines | ConvertFrom-Csv -Header benchmark,operations,elapsed_ms,ops_per_second,ns_per_operation,checksum
}

function Median {
  param([double[]]$Values)

  if ($Values.Count -eq 0) {
    throw "core_performance_regression: cannot calculate median of empty set"
  }

  $sorted = @($Values | Sort-Object)
  $middle = [int][Math]::Floor($sorted.Count / 2)
  if (($sorted.Count % 2) -eq 1) {
    return [double]$sorted[$middle]
  }

  return ([double]$sorted[$middle - 1] + [double]$sorted[$middle]) / 2.0
}

if ($Runs -lt 1) {
  throw "core_performance_regression: Runs must be at least 1"
}

$baseline = Get-Content -Raw -LiteralPath $BaselinePath | ConvertFrom-Json
if ($baseline.schema_version -ne 1) {
  throw "core_performance_regression: unsupported baseline schema_version '$($baseline.schema_version)'"
}

$allRuns = @()
if (![string]::IsNullOrWhiteSpace($BenchmarkOutputPath)) {
  $allRuns += ,(Read-BenchmarkRows -Lines @(Get-Content -LiteralPath $BenchmarkOutputPath))
} else {
  $benchmarkScript = Join-Path $repoRoot "tools\run-core-benchmarks.ps1"
  for ($run = 1; $run -le $Runs; ++$run) {
    "core_performance_regression: run $run/$Runs"
    $output = & $benchmarkScript
    $output | ForEach-Object { $_ }
    $allRuns += ,(Read-BenchmarkRows -Lines @($output))
  }
}

$summary = @()
$failures = @()
foreach ($expected in $baseline.rows) {
  $samples = @()
  $checksumSamples = @()
  $operationSamples = @()

  foreach ($runRows in $allRuns) {
    $row = $runRows | Where-Object { $_.benchmark -eq $expected.benchmark } | Select-Object -First 1
    if ($null -eq $row) {
      $failures += "missing benchmark row '$($expected.benchmark)'"
      continue
    }

    $samples += [double]$row.ns_per_operation
    $checksumSamples += [string]$row.checksum
    $operationSamples += [UInt64]$row.operations
  }

  if ($samples.Count -eq 0) {
    continue
  }

  $medianNs = Median -Values $samples
  $bestNs = [double](@($samples | Sort-Object)[0])
  $worstNs = [double](@($samples | Sort-Object -Descending)[0])
  $maxNs = [double]$expected.max_ns_per_operation
  $status = "PASS"

  if ($operationSamples | Where-Object { $_ -ne [UInt64]$expected.expected_operations }) {
    $status = "FAIL"
    $failures += "row '$($expected.benchmark)' operations changed; expected $($expected.expected_operations)"
  }
  if ($checksumSamples | Where-Object { $_ -ne [string]$expected.expected_checksum }) {
    $status = "FAIL"
    $failures += "row '$($expected.benchmark)' checksum changed; expected $($expected.expected_checksum)"
  }
  if ($medianNs -gt $maxNs) {
    $status = "FAIL"
    $failures += "row '$($expected.benchmark)' median ns/op $medianNs exceeds threshold $maxNs"
  }

  $summary += [PSCustomObject]@{
    benchmark = [string]$expected.benchmark
    runs = $samples.Count
    baseline_ns_per_operation = [double]$expected.baseline_ns_per_operation
    median_ns_per_operation = [Math]::Round($medianNs, 3)
    best_ns_per_operation = [Math]::Round($bestNs, 3)
    worst_ns_per_operation = [Math]::Round($worstNs, 3)
    max_ns_per_operation = $maxNs
    checksum = [string]$checksumSamples[0]
    status = $status
  }
}

$report = [PSCustomObject]@{
  generated_at = (Get-Date -Format o)
  scope = [string]$baseline.scope
  metric = [string]$baseline.metric
  requested_runs = $Runs
  actual_runs = $allRuns.Count
  baseline_path = (Resolve-Path -LiteralPath $BaselinePath).Path
  summary = $summary
  failures = $failures
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ReportPath) | Out-Null
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ReportPath

$summary | ConvertTo-Csv -NoTypeInformation | ForEach-Object { $_ }
if ($failures.Count -gt 0) {
  $failures | ForEach-Object { "core_performance_regression: FAIL: $_" }
  throw "core_performance_regression: FAIL"
}

"core_performance_regression: PASS"
