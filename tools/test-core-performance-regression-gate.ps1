$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$baselinePath = Join-Path $PSScriptRoot "core-benchmark-baseline.json"
$checkScript = Join-Path $PSScriptRoot "check-core-performance-regression.ps1"
$testDir = Join-Path $repoRoot "build\performance-regression-gate-test"

New-Item -ItemType Directory -Force -Path $testDir | Out-Null

$baseline = Get-Content -Raw -LiteralPath $baselinePath | ConvertFrom-Json

function New-BenchmarkOutput {
  param(
    [string]$Path,
    [bool]$MakeSlow
  )

  $lines = @(
    "core_benchmark: synthetic_no_rom_no_bios",
    "benchmark,operations,elapsed_ms,ops_per_second,ns_per_operation,checksum"
  )

  $first = $true
  foreach ($row in $baseline.rows) {
    $ns = [double]$row.baseline_ns_per_operation
    if ($MakeSlow -and $first) {
      $ns = [double]$row.max_ns_per_operation * 1.5
      $first = $false
    }
    $operations = [UInt64]$row.expected_operations
    $elapsedMs = ($ns * [double]$operations) / 1000000.0
    $opsPerSecond = 1000000000.0 / $ns
    $lines += ("{0},{1},{2:F3},{3:F2},{4:F2},{5}" -f $row.benchmark,
      $operations, $elapsedMs, $opsPerSecond, $ns, $row.expected_checksum)
  }

  $lines += "core_benchmark: PASS"
  Set-Content -LiteralPath $Path -Value $lines
}

$goodOutput = Join-Path $testDir "good-benchmark-output.txt"
$badOutput = Join-Path $testDir "bad-benchmark-output.txt"
$goodReport = Join-Path $testDir "good-report.json"
$badReport = Join-Path $testDir "bad-report.json"

New-BenchmarkOutput -Path $goodOutput -MakeSlow $false
New-BenchmarkOutput -Path $badOutput -MakeSlow $true

& $checkScript -Runs 1 -BenchmarkOutputPath $goodOutput -ReportPath $goodReport | Out-Null

$badRejected = $false
try {
  & $checkScript -Runs 1 -BenchmarkOutputPath $badOutput -ReportPath $badReport | Out-Null
} catch {
  $badRejected = $true
}

if (!$badRejected) {
  throw "core_performance_regression_gate_test: bad benchmark fixture was accepted"
}

"core_performance_regression_gate_test: PASS"
