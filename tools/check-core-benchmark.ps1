$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$benchmarkScript = Join-Path $repoRoot "tools\run-core-benchmarks.ps1"

$output = & $benchmarkScript
$output | ForEach-Object { $_ }

$csvLines = $output | Where-Object { $_ -match '^[a-zA-Z0-9_]+,[0-9]+,' }
if ($csvLines.Count -eq 0) {
  throw "core_benchmark_check: no benchmark CSV rows found"
}

$rows = $csvLines | ConvertFrom-Csv -Header benchmark,operations,elapsed_ms,ops_per_second,ns_per_operation,checksum
$expectedRows = @(
  "memory_bus_iwram_rw32_pair",
  "cpu_step_arm_add",
  "io_register_mixed_access",
  "dma_immediate_word_copy_units",
  "ppu_bg_obj_pixel_fetches",
  "device_timer_ppu_apu_ticks",
  "scheduler_step_arm_add",
  "scheduler_fetch_loop_arm_add_branch",
  "scheduler_dispatch_thumb_add",
  "scheduler_prefetch_cart_loop",
  "memory_bus_waitcnt_timing_lookup",
  "cpu_waitcnt_elapsed_estimate",
  "core_session_state_hash"
)

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
