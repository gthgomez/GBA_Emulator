param(
  [string]$BaselinePath = "",
  [string]$BenchmarkOutputPath = ""
)

# Requires PowerShell 7.3+ for $PSNativeCommandUseErrorActionPreference.
#requires -Version 7.3

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")

if ([string]::IsNullOrWhiteSpace($BaselinePath)) {
  $BaselinePath = Join-Path $PSScriptRoot "core-benchmark-baseline.json"
}

function Assert-NativeExitCode {
    param(
        [string]$Step,
        [int]$Expected = 0
    )

    if ($LASTEXITCODE -ne $Expected) {
        Write-Host ""
        Write-Host "core_benchmark_check: FAIL ($Step exited with $LASTEXITCODE)"
        if ($null -eq $LASTEXITCODE) {
            exit 1
        }
        exit $LASTEXITCODE
    }
}

if (![string]::IsNullOrWhiteSpace($BenchmarkOutputPath)) {
  # Fixture mode: check a supplied benchmark output instead of running one. This
  # keeps the gate's deterministic contract testable without wall-clock timing.
  if (-not (Test-Path -LiteralPath $BenchmarkOutputPath -PathType Leaf)) {
    throw "core_benchmark_check: benchmark output not found: $BenchmarkOutputPath"
  }
  $output = @(Get-Content -LiteralPath $BenchmarkOutputPath)
} else {
  $benchmarkScript = Join-Path $repoRoot "tools\run-core-benchmarks.ps1"
  $output = & $benchmarkScript
  Assert-NativeExitCode -Step "run-core-benchmarks.ps1"
}
$output | ForEach-Object { $_ }

$csvLines = $output | Where-Object { $_ -match '^[a-zA-Z0-9_]+,[0-9]+,' }
if ($csvLines.Count -eq 0) {
  throw "core_benchmark_check: no benchmark CSV rows found"
}

$rows = $csvLines | ConvertFrom-Csv -Header benchmark,operations,elapsed_ms,ops_per_second,ns_per_operation,checksum

# Single source of truth: benchmark names AND their deterministic correctness
# (operations, checksum) come from the baseline JSON, not from a duplicated
# literal list that drifts out of sync.
if (-not (Test-Path -LiteralPath $BaselinePath -PathType Leaf)) {
  throw "core_benchmark_check: baseline not found: $BaselinePath"
}
$baseline = Get-Content -Raw -LiteralPath $BaselinePath | ConvertFrom-Json
if ($null -eq $baseline.rows -or @($baseline.rows).Count -eq 0) {
  throw "core_benchmark_check: baseline has no rows"
}

foreach ($expectedRow in $baseline.rows) {
  $expected = [string]$expectedRow.benchmark
  $row = $rows | Where-Object { $_.benchmark -eq $expected } | Select-Object -First 1
  if ($null -eq $row) {
    throw "core_benchmark_check: missing benchmark row '$expected'"
  }

  if ([UInt64]$row.operations -le 0) {
    throw "core_benchmark_check: row '$expected' has non-positive operations"
  }
  if ([Double]$row.elapsed_ms -le 0.0) {
    throw "core_benchmark_check: row '$expected' has non-positive elapsed_ms"
  }
  if ([Double]$row.ops_per_second -le 0.0) {
    throw "core_benchmark_check: row '$expected' has non-positive ops_per_second"
  }
  if ([Double]$row.ns_per_operation -le 0.0) {
    throw "core_benchmark_check: row '$expected' has non-positive ns_per_operation"
  }
  if ([UInt64]$row.checksum -eq 0) {
    throw "core_benchmark_check: row '$expected' has zero checksum"
  }

  # Deterministic correctness, independent of any wall-clock threshold: a
  # changed operation count or checksum means modeled behavior changed even when
  # the timing is comfortably inside the performance ceilings. This gate reports
  # such drift against the checked-in baseline without depending on the noisy
  # wall-clock thresholds that check-core-performance-regression.ps1 also uses.
  if ([UInt64]$row.operations -ne [UInt64]$expectedRow.expected_operations) {
    throw "core_benchmark_check: row '$expected' operations changed; expected $($expectedRow.expected_operations), got $($row.operations)"
  }
  if ([string]$row.checksum -ne [string]$expectedRow.expected_checksum) {
    throw "core_benchmark_check: row '$expected' checksum changed; expected $($expectedRow.expected_checksum), got $($row.checksum)"
  }
}

if ($output[-1] -ne "core_benchmark: PASS") {
  throw "core_benchmark_check: benchmark did not end with PASS"
}

"core_benchmark_check: PASS"
# Explicit terminal exit code so an orchestrator can assert on $LASTEXITCODE
# even when fixture mode (-BenchmarkOutputPath) never invokes a native command.
exit 0
