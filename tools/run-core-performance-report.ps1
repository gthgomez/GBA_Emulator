param(
  [int]$Runs = 3,
  [string]$BaselinePath = "",
  [string]$OutputDir = ""
)

# SINGLE-WRITER ASSUMPTION: the default output dir receives
# core-performance-latest.json / .csv copies that every run overwrites.
# Concurrent runs clobber each other's "latest" artifacts; serialize runs or
# pass a distinct -OutputDir per process.
# Requires PowerShell 7.3+ for $PSNativeCommandUseErrorActionPreference.
#requires -Version 7.3

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
  $OutputDir = Join-Path $repoRoot "build\test-results"
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$reportPath = Join-Path $OutputDir "core-performance-$timestamp.json"
$latestReportPath = Join-Path $OutputDir "core-performance-latest.json"
$csvPath = Join-Path $OutputDir "core-performance-$timestamp.csv"
$latestCsvPath = Join-Path $OutputDir "core-performance-latest.csv"

$checkScript = Join-Path $PSScriptRoot "check-core-performance-regression.ps1"
$passed = $true
try {
  $checkArgs = @{
    Runs = $Runs
    ReportPath = $reportPath
  }
  if (-not [string]::IsNullOrWhiteSpace($BaselinePath)) {
    $checkArgs.BaselinePath = $BaselinePath
  }
  & $checkScript @checkArgs
} catch {
  $passed = $false
  Write-Output $_.Exception.Message
}

if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
  throw "core_performance_report: expected report was not written: $reportPath"
}

Copy-Item -LiteralPath $reportPath -Destination $latestReportPath -Force

$report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
$report.summary | ConvertTo-Csv -NoTypeInformation | Set-Content -LiteralPath $csvPath
Copy-Item -LiteralPath $csvPath -Destination $latestCsvPath -Force

Write-Output "core_performance_report: status=$(if ($passed) { 'PASS' } else { 'FAIL' })"
Write-Output "core_performance_report: report_path=$reportPath"
Write-Output "core_performance_report: csv_path=$csvPath"
Write-Output "core_performance_report: latest_report_path=$latestReportPath"
Write-Output "core_performance_report: latest_csv_path=$latestCsvPath"
Write-Output "core_performance_report: slowest_median_ns_per_operation"
$report.summary |
  Sort-Object -Property median_ns_per_operation -Descending |
  Select-Object -First 5 -Property benchmark, median_ns_per_operation, max_ns_per_operation, status |
  Format-Table -AutoSize | Out-String | Write-Output

if (-not $passed) {
  throw "core_performance_report: FAIL"
}
