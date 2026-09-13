#requires -Version 7.3

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$benchmarkScript = Join-Path $repoRoot "tools\run-core-benchmarks.ps1"

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

$output = & $benchmarkScript
Assert-NativeExitCode -Step "run-core-benchmarks.ps1"
$output | ForEach-Object { $_ }

$csvLines = $output | Where-Object { $_ -match '^[a-zA-Z0-9_]+,[0-9]+,' }
if ($csvLines.Count -eq 0) {
  throw "core_benchmark_check: no benchmark CSV rows found"
}

$rows = $csvLines | ConvertFrom-Csv -Header benchmark,operations,elapsed_ms,ops_per_second,ns_per_operation,checksum

# Single source of truth: benchmark names come from the baseline JSON rows,
# not from a duplicated literal list that drifts out of sync.
$baselinePath = Join-Path $PSScriptRoot "core-benchmark-baseline.json"
if (-not (Test-Path -LiteralPath $baselinePath -PathType Leaf)) {
  throw "core_benchmark_check: baseline not found: $baselinePath"
}
$baseline = Get-Content -Raw -LiteralPath $baselinePath | ConvertFrom-Json
if ($null -eq $baseline.rows -or @($baseline.rows).Count -eq 0) {
  throw "core_benchmark_check: baseline has no rows"
}
$expectedRows = @($baseline.rows | ForEach-Object { [string]$_.benchmark })
if ($expectedRows.Count -eq 0) {
  throw "core_benchmark_check: baseline defines no benchmark names"
}

foreach ($expected in $expectedRows) {
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
}

if ($output[-1] -ne "core_benchmark: PASS") {
  throw "core_benchmark_check: benchmark did not end with PASS"
}

"core_benchmark_check: PASS"
