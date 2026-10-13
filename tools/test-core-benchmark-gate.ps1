# Regression test for tools/check-core-benchmark.ps1 (the wall-clock-independent
# synthetic benchmark correctness gate).
#
# Contract under test:
#   * a benchmark output whose every row matches the baseline's
#     expected_operations and expected_checksum is ACCEPTED;
#   * a deterministic drift (changed checksum, changed operations, or a missing
#     baseline row) is REJECTED even though every wall-clock field is inside any
#     performance ceiling.
#
# This locks "benchmark checksum correctness is separate from noisy wall-clock
# performance": the correctness gate must not depend on the performance gate to
# notice determinism drift.
#
# Requires PowerShell 7.3+ for $PSNativeCommandUseErrorActionPreference.
#requires -Version 7.3

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$baselinePath = Join-Path $PSScriptRoot "core-benchmark-baseline.json"
$checkScript = Join-Path $PSScriptRoot "check-core-benchmark.ps1"
$testDir = Join-Path $repoRoot "build/core-benchmark-gate-test"

New-Item -ItemType Directory -Force -Path $testDir | Out-Null

$baseline = Get-Content -Raw -LiteralPath $baselinePath | ConvertFrom-Json
if ($null -eq $baseline.rows -or @($baseline.rows).Count -eq 0) {
  throw "core_benchmark_gate_test: baseline has no rows"
}

# Writes a benchmark output. Wall-clock fields always stay at the calibrated
# baseline (so every timing ceiling is satisfied); only the deterministic
# operations/checksum of one row can be perturbed.
function New-BenchmarkOutput {
  param(
    [string]$Path,
    [string]$DriftRow = "",
    [ValidateSet("checksum", "operations")][string]$DriftKind = "checksum"
  )

  $lines = @(
    "core_benchmark: synthetic_no_rom_no_bios",
    "benchmark,operations,elapsed_ms,ops_per_second,ns_per_operation,checksum"
  )

  foreach ($row in $baseline.rows) {
    $ns = [double]$row.baseline_ns_per_operation
    $operations = [UInt64]$row.expected_operations
    $checksum = [string]$row.expected_checksum
    if ($DriftRow -eq [string]$row.benchmark) {
      if ($DriftKind -eq "checksum") {
        $checksum = "1"
      } else {
        $operations = $operations + 1
      }
    }
    $elapsedMs = ($ns * [double]$operations) / 1000000.0
    $opsPerSecond = 1000000000.0 / $ns
    $lines += ("{0},{1},{2:F3},{3:F2},{4:F2},{5}" -f $row.benchmark,
      $operations, $elapsedMs, $opsPerSecond, $ns, $checksum)
  }

  $lines += "core_benchmark: PASS"
  Set-Content -LiteralPath $Path -Value $lines
}

function Test-Accepted {
  param([string]$Path)

  try {
    & $checkScript -BenchmarkOutputPath $Path | Out-Null
    return $true
  } catch {
    return $false
  }
}

$goodOutput = Join-Path $testDir "good-benchmark-output.txt"
$driftChecksumOutput = Join-Path $testDir "drift-checksum-benchmark-output.txt"
$driftOperationsOutput = Join-Path $testDir "drift-operations-benchmark-output.txt"
$missingRowOutput = Join-Path $testDir "missing-row-benchmark-output.txt"

New-BenchmarkOutput -Path $goodOutput
New-BenchmarkOutput -Path $driftChecksumOutput -DriftRow ([string]$baseline.rows[0].benchmark) -DriftKind "checksum"
New-BenchmarkOutput -Path $driftOperationsOutput -DriftRow ([string]$baseline.rows[0].benchmark) -DriftKind "operations"

# Drop one row but keep the terminal PASS line: a baseline row is now missing.
@(Get-Content -LiteralPath $goodOutput | Select-Object -SkipLast 2) |
  Set-Content -LiteralPath $missingRowOutput
Add-Content -LiteralPath $missingRowOutput -Value "core_benchmark: PASS"

if (-not (Test-Accepted -Path $goodOutput)) {
  throw "core_benchmark_gate_test: baseline-matching benchmark output was rejected"
}

foreach ($drifted in @($driftChecksumOutput, $driftOperationsOutput, $missingRowOutput)) {
  if (Test-Accepted -Path $drifted) {
    throw "core_benchmark_gate_test: drifted benchmark output was accepted: $drifted"
  }
}

"core_benchmark_gate_test: PASS"
