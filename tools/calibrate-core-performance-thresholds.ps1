param(
  [int]$Runs = 7,
  [double]$MedianMultiplier = 2.0,
  [double]$WorstMultiplier = 1.5,
  [string]$BaselinePath = "",
  [string]$ReportPath = "",
  [string]$ProposalPath = ""
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($BaselinePath)) {
  $BaselinePath = Join-Path $PSScriptRoot "core-benchmark-baseline.json"
}
if ([string]::IsNullOrWhiteSpace($ReportPath)) {
  $ReportPath = Join-Path $repoRoot "build\core_benchmark_calibration_report.json"
}
if ([string]::IsNullOrWhiteSpace($ProposalPath)) {
  $ProposalPath = Join-Path $repoRoot "build\core_benchmark_threshold_proposal.json"
}

if ($Runs -lt 3) {
  throw "core_performance_calibration: Runs must be at least 3 for calibration"
}
if ($MedianMultiplier -lt 1.0 -or $WorstMultiplier -lt 1.0) {
  throw "core_performance_calibration: multipliers must be at least 1.0"
}

$checkScript = Join-Path $PSScriptRoot "check-core-performance-regression.ps1"
& $checkScript -Runs $Runs -BaselinePath $BaselinePath -ReportPath $ReportPath

$report = Get-Content -Raw -LiteralPath $ReportPath | ConvertFrom-Json
$proposalRows = @()
foreach ($row in $report.summary) {
  $medianCeiling = [double]$row.median_ns_per_operation * $MedianMultiplier
  $worstCeiling = [double]$row.worst_ns_per_operation * $WorstMultiplier
  $proposed = [Math]::Ceiling([Math]::Max($medianCeiling, $worstCeiling))
  $current = [double]$row.max_ns_per_operation

  $proposalRows += [PSCustomObject]@{
    benchmark = [string]$row.benchmark
    measured_median_ns_per_operation = [double]$row.median_ns_per_operation
    measured_worst_ns_per_operation = [double]$row.worst_ns_per_operation
    current_max_ns_per_operation = $current
    proposed_max_ns_per_operation = $proposed
    current_to_proposed_delta = [Math]::Round($proposed - $current, 3)
  }
}

$proposal = [PSCustomObject]@{
  generated_at = (Get-Date -Format o)
  source_report = (Resolve-Path -LiteralPath $ReportPath).Path
  baseline_path = (Resolve-Path -LiteralPath $BaselinePath).Path
  runs = $Runs
  median_multiplier = $MedianMultiplier
  worst_multiplier = $WorstMultiplier
  rows = $proposalRows
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ProposalPath) | Out-Null
$proposal | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ProposalPath

$proposalRows | ConvertTo-Csv -NoTypeInformation | ForEach-Object { $_ }
"core_performance_calibration: PASS"
