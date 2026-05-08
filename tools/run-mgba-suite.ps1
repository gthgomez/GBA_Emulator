param(
  [uint32]$MaxSteps = 100000,
  [uint32]$TraceSteps = 0,
  [string]$InputScript = "",
  [ValidateSet("menu", "memory", "io-read", "timing", "timers", "timer-irq", "shifter", "carry", "multiply-long", "bios-math", "dma", "sio-read", "sio-timing", "misc-edge", "video", "all")]
  [string]$Suite = "menu",
  [string]$UntilOutput = "",
  [switch]$UpdateDocs
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$buildDir = Join-Path $repoRoot "build"
$resultsDir = Join-Path $buildDir "test-results"
$runnerPath = Join-Path $buildDir "mgba-suite-runner.exe"
$suitePath = Join-Path $buildDir "test-suite-build\mgba-suite\suite.gba"
$latestJsonPath = Join-Path $resultsDir "mgba-suite-latest.json"
$docPath = Join-Path $repoRoot "docs\mgba-suite-test-results.md"
$suiteMenuIndices = [ordered]@{
  "memory" = 0
  "io-read" = 1
  "timing" = 2
  "timers" = 3
  "timer-irq" = 4
  "shifter" = 5
  "carry" = 6
  "multiply-long" = 7
  "bios-math" = 8
  "dma" = 9
  "sio-read" = 10
  "sio-timing" = 11
  "misc-edge" = 12
  "video" = 13
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
New-Item -ItemType Directory -Force -Path $resultsDir | Out-Null

if (-not (Test-Path -LiteralPath $suitePath -PathType Leaf)) {
  & (Join-Path $PSScriptRoot "build-mgba-suite.ps1")
}

g++ -std=c++17 -Wall -Wextra -Werror `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $repoRoot "src\core\arm7tdmi.cpp") `
  (Join-Path $repoRoot "src\core\apu.cpp") `
  (Join-Path $repoRoot "src\core\bios.cpp") `
  (Join-Path $repoRoot "src\core\core_scheduler.cpp") `
  (Join-Path $repoRoot "src\core\core_session.cpp") `
  (Join-Path $repoRoot "src\core\dma_controller.cpp") `
  (Join-Path $repoRoot "src\core\interrupt_controller.cpp") `
  (Join-Path $repoRoot "src\core\io_registers.cpp") `
  (Join-Path $repoRoot "src\core\keypad.cpp") `
  (Join-Path $repoRoot "src\core\memory_bus.cpp") `
  (Join-Path $repoRoot "src\core\ppu_timing.cpp") `
  (Join-Path $repoRoot "src\core\timers.cpp") `
  (Join-Path $repoRoot "src\core\wait_state_control.cpp") `
  (Join-Path $PSScriptRoot "mgba-suite-runner.cpp") `
  -o $runnerPath

$suiteHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $suitePath).Hash
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$resultPath = Join-Path $resultsDir "mgba-suite-$timestamp.txt"
$jsonPath = Join-Path $resultsDir "mgba-suite-$timestamp.json"

$targetSuite = $Suite
if ($Suite -eq "all") {
  $targetSuite = "memory"
}

function New-SuiteInputScript {
  param([int]$SuiteIndex)

  if ($SuiteIndex -eq 0) {
    return "950000:A"
  }

  $events = New-Object System.Collections.Generic.List[string]
  $step = 800000
  # Keep one DOWN pulse per VBlank-scale interval. Shorter pulses can merge
  # under libgba key-repeat timing and select a neighboring suite.
  for ($i = 0; $i -lt $SuiteIndex; ++$i) {
    $events.Add("$($step):DOWN")
    $events.Add("$($step + 80000):release")
    $step += 350000
  }
  $events.Add("$($step + 80000):A")
  return ($events -join ',')
}

if ($suiteMenuIndices.Contains($targetSuite) -and [string]::IsNullOrWhiteSpace($InputScript)) {
  $InputScript = New-SuiteInputScript -SuiteIndex ([int]$suiteMenuIndices[$targetSuite])
}
if ($suiteMenuIndices.Contains($targetSuite) -and [string]::IsNullOrWhiteSpace($UntilOutput)) {
  $UntilOutput = "END:"
}

$header = @(
  "suite_test: timestamp=$timestamp",
  "suite_test: suite=$Suite",
  "suite_test: target_suite=$targetSuite",
  "suite_test: max_steps=$MaxSteps",
  "suite_test: trace_steps=$TraceSteps",
  "suite_test: input_script=$InputScript",
  "suite_test: until_output=$UntilOutput",
  "suite_test: suite_sha256=$suiteHash"
)

$header | Tee-Object -FilePath $resultPath
& $runnerPath $suitePath $MaxSteps $TraceSteps $InputScript $UntilOutput | Tee-Object -FilePath $resultPath -Append

$lines = Get-Content -LiteralPath $resultPath

function Get-SuiteMetric {
  param(
    [string[]]$Lines,
    [string]$Prefix,
    [string]$Name
  )
  $pattern = "^{0}: {1}=(.*)$" -f [regex]::Escape($Prefix), [regex]::Escape($Name)
  foreach ($line in $Lines) {
    if ($line -match $pattern) {
      return $Matches[1]
    }
  }
  return $null
}

function Get-OutputBlock {
  param(
    [string[]]$Lines,
    [string]$Begin,
    [string]$End
  )
  $capturing = $false
  $block = New-Object System.Collections.Generic.List[string]
  foreach ($line in $Lines) {
    if ($line -eq $Begin) {
      $capturing = $true
      continue
    }
    if ($line -eq $End) {
      break
    }
    if ($capturing) {
      $block.Add($line)
    }
  }
  return ($block -join "`n")
}

function Convert-ToNullableInt {
  param([string]$Value)
  if ($null -eq $Value -or $Value -eq "null" -or $Value -eq "") {
    return $null
  }
  if ($Value.StartsWith("0x")) {
    return [Convert]::ToInt64($Value.Substring(2), 16)
  }
  return [int64]$Value
}

function Get-MemorySuiteTestName {
  param([int64]$Index)
  if ($Index -lt 0) {
    return $null
  }
  $memorySource = Join-Path $repoRoot "external\test-suites\mgba-suite\src\memory.c"
  if (-not (Test-Path -LiteralPath $memorySource -PathType Leaf)) {
    $memorySource = Join-Path $buildDir "test-suite-build\mgba-suite\src\memory.c"
  }
  if (-not (Test-Path -LiteralPath $memorySource -PathType Leaf)) {
    return $null
  }

  $inside = $false
  $names = New-Object System.Collections.Generic.List[string]
  foreach ($line in Get-Content -LiteralPath $memorySource) {
    if ($line -match "memoryTests\[\]\s*=") {
      $inside = $true
      continue
    }
    if ($inside -and $line -match "^\s*\};") {
      break
    }
    if ($inside -and $line -match '^\s*\{\s*"([^"]+)"\s*,') {
      $names.Add($Matches[1])
    }
  }
  if ($Index -ge $names.Count) {
    return $null
  }
  return $names[[int]$Index]
}

function Get-MemorySuiteSubtestName {
  param([int64]$Index)
  $names = @(
    "U8",
    "S8",
    "U16",
    "U16 (unaligned)",
    "S16",
    "S16 (unaligned)",
    "32",
    "32 (unaligned 1)",
    "32 (unaligned 2)",
    "32 (unaligned 3)",
    "DMA0 16",
    "DMA0 16 (unaligned)",
    "DMA0 32",
    "DMA0 32 (unaligned 1)",
    "DMA0 32 (unaligned 2)",
    "DMA0 32 (unaligned 3)",
    "DMA1 16",
    "DMA1 16 (unaligned)",
    "DMA1 32",
    "DMA1 32 (unaligned 1)",
    "DMA1 32 (unaligned 2)",
    "DMA1 32 (unaligned 3)",
    "DMA2 16",
    "DMA2 16 (unaligned)",
    "DMA2 32",
    "DMA2 32 (unaligned 1)",
    "DMA2 32 (unaligned 2)",
    "DMA2 32 (unaligned 3)",
    "DMA3 16",
    "DMA3 16 (unaligned)",
    "DMA3 32",
    "DMA3 32 (unaligned 1)",
    "DMA3 32 (unaligned 2)",
    "DMA3 32 (unaligned 3)",
    "swi B 16",
    "swi B 16 (unaligned)",
    "swi B 32",
    "swi B 32 (unaligned 1)",
    "swi B 32 (unaligned 2)",
    "swi B 32 (unaligned 3)",
    "swi C 32",
    "swi C 32 (unaligned 1)",
    "swi C 32 (unaligned 2)",
    "swi C 32 (unaligned 3)"
  )
  if ($Index -lt 0 -or $Index -ge $names.Count) {
    return $null
  }
  return $names[[int]$Index]
}

function Get-MemoryFailureCategory {
  param(
    [string]$TestName,
    [string]$FailureLine
  )
  $probe = "$TestName $FailureLine"
  if ($probe -match "ROM out-of-bounds") {
    return "rom_out_of_bounds"
  }
  if ($TestName -match "BIOS load" -and $FailureLine -match "DMA") {
    return "bios_dma_source"
  }
  if ($probe -match "Palette|VRAM|OAM") {
    return "video_byte_store"
  }
  if ($probe -match "SRAM") {
    return "sram_width_mirror"
  }
  return "other_memory"
}

function Get-MemoryFailureCategories {
  param([string]$Text)

  $currentTest = $null
  $categories = [ordered]@{}
  foreach ($line in ($Text -split "`r?`n")) {
    if ($line -match "^Memory test:\s*(.+)$") {
      $currentTest = $Matches[1].Trim()
      continue
    }
    if ($line -notmatch "^FAIL:\s*(.+)$") {
      continue
    }

    $failure = $Matches[1].Trim()
    $name = Get-MemoryFailureCategory -TestName $currentTest -FailureLine $failure
    if (-not $categories.Contains($name)) {
      $categories[$name] = [ordered]@{
        name = $name
        count = 0
        first_failure = $failure
        tests = @()
        examples = @()
      }
    }

    $entry = $categories[$name]
    $entry.count = [int]$entry.count + 1
    if ($currentTest -and $entry.tests -notcontains $currentTest) {
      $entry.tests = @($entry.tests) + $currentTest
    }
    if (@($entry.examples).Count -lt 5) {
      $entry.examples = @($entry.examples) + $failure
    }
  }

  return @($categories.Values)
}

function Get-SuiteFailureCategory {
  param(
    [string]$SuiteName,
    [string]$TestName,
    [string]$FailureLine
  )

  $probe = "$TestName $FailureLine"
  if ($SuiteName -eq "Memory tests") {
    return Get-MemoryFailureCategory -TestName $TestName -FailureLine $FailureLine
  }
  if ($SuiteName -eq "BIOS math tests") {
    if ($probe -match "ArcTan2") { return "bios_math_arctan2" }
    if ($probe -match "ArcTan") { return "bios_math_arctan" }
    if ($probe -match "\bDiv\b") { return "bios_math_div" }
    return "bios_math_other"
  }
  if ($SuiteName -match "DMA") {
    if ($probe -match "R\+0x10") { return "dma_source_reload" }
    if ($probe -match "BIOS") { return "dma_bios_source_open_bus" }
    if ($probe -match "ROM") { return "dma_rom_source_open_bus" }
    return "dma_other"
  }
  if ($SuiteName -match "Timing") { return "timing" }
  if ($SuiteName -match "Timer IRQ") { return "timer_irq" }
  if ($SuiteName -match "Timer") { return "timers" }
  if ($SuiteName -match "Shifter") { return "shifter" }
  if ($SuiteName -match "Carry") { return "carry" }
  if ($SuiteName -match "Multiply") { return "multiply_long" }
  if ($SuiteName -match "IO read") { return "io_read" }
  if ($SuiteName -match "SIO read") { return "sio_read" }
  if ($SuiteName -match "SIO timing") { return "sio_timing" }
  if ($SuiteName -match "Misc") { return "misc_edge" }
  if ($SuiteName -match "Video") { return "video" }
  return "other"
}

function Get-SuiteFailures {
  param([string]$Text)

  $failures = New-Object System.Collections.Generic.List[object]
  $currentTest = $null
  foreach ($line in ($Text -split "`r?`n")) {
    if ($line -match "^[A-Za-z0-9 /-]+ test:\s*(.+)$") {
      $currentTest = $Matches[1].Trim()
      continue
    }
    if ($line -match "^FAIL:\s*(.+)$") {
      $failures.Add([ordered]@{
        test = $currentTest
        message = $Matches[1].Trim()
      })
    }
  }
  return $failures.ToArray()
}

function Get-SuiteFailureCategories {
  param(
    [string]$Text,
    [string]$SuiteName
  )

  $currentTest = $null
  $categories = [ordered]@{}
  foreach ($line in ($Text -split "`r?`n")) {
    if ($line -match "^[A-Za-z0-9 /-]+ test:\s*(.+)$") {
      $currentTest = $Matches[1].Trim()
      continue
    }
    if ($line -notmatch "^FAIL:\s*(.+)$") {
      continue
    }

    $failure = $Matches[1].Trim()
    $name = Get-SuiteFailureCategory -SuiteName $SuiteName -TestName $currentTest -FailureLine $failure
    if (-not $categories.Contains($name)) {
      $categories[$name] = [ordered]@{
        name = $name
        count = 0
        first_failure = $failure
        tests = @()
        examples = @()
      }
    }

    $entry = $categories[$name]
    $entry.count = [int]$entry.count + 1
    if ($currentTest -and $entry.tests -notcontains $currentTest) {
      $entry.tests = @($entry.tests) + $currentTest
    }
    if (@($entry.examples).Count -lt 5) {
      $entry.examples = @($entry.examples) + $failure
    }
  }

  return @($categories.Values)
}

$debugText = Get-OutputBlock -Lines $lines -Begin "suite_output_begin" -End "suite_output_end"
$sramText = Get-OutputBlock -Lines $lines -Begin "suite_sram_text_begin" -End "suite_sram_text_end"
$combinedText = @($debugText, $sramText) -join "`n"

$begunSuite = $null
if ($combinedText -match "BEGIN:\s*(.+)") {
  $begunSuite = $Matches[1].Trim()
}

$pass = $null
$total = $null
$ended = $false
if ($combinedText -match "END:\s*(\d+)/(\d+)") {
  $pass = [int]$Matches[1]
  $total = [int]$Matches[2]
  $ended = $true
}

$firstFailure = $null
foreach ($line in ($combinedText -split "`r?`n")) {
  if ($line -match "FAIL") {
    $firstFailure = $line.Trim()
    break
  }
}

$activeSuiteId = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "active_suite_id")
$activeTestId = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "active_test_id")
$activeSubtestId = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "active_subtest_id")
$stopReason = Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "stop_reason"
$runnerStopReason = Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "runner_stop_reason"
$activeTestName = if ($begunSuite -eq "Memory tests") { Get-MemorySuiteTestName -Index $activeTestId } else { $null }
$activeSubtestName = if ($begunSuite -eq "Memory tests") { Get-MemorySuiteSubtestName -Index $activeSubtestId } else { $null }
$failures = @(Get-SuiteFailures -Text $combinedText)
$failureCategories = @(Get-SuiteFailureCategories -Text $combinedText -SuiteName $begunSuite)

if ($null -eq $firstFailure -and $begunSuite -and -not $ended) {
  $namedFrontier = if ($activeTestName) { "$activeTestName" } else { "test_id=$activeTestId" }
  if ($activeSubtestName) {
    $namedFrontier = "$namedFrontier / $activeSubtestName"
  } else {
    $namedFrontier = "$namedFrontier / subtest_id=$activeSubtestId"
  }
  $firstFailure = "Stopped before END in $begunSuite at $namedFrontier (stop_reason=$runnerStopReason)"
}

$result = [ordered]@{
  timestamp = $timestamp
  suite_request = $Suite
  target_suite = $targetSuite
  status = if ($ended) { "complete" } elseif ($begunSuite) { "started_incomplete" } else { "not_started" }
  command = ".\tools\run-mgba-suite.ps1 -Suite $Suite -MaxSteps $MaxSteps -TraceSteps $TraceSteps -InputScript `"$InputScript`" -UntilOutput `"$UntilOutput`""
  rom_path = $suitePath
  suite_sha256 = $suiteHash
  text_result_path = $resultPath
  json_result_path = $jsonPath
  max_steps = $MaxSteps
  trace_steps = $TraceSteps
  input_script = $InputScript
  until_output = $UntilOutput
  parsed = [ordered]@{
    suite = if ($begunSuite) { $begunSuite } else { $targetSuite }
    began = [bool]$begunSuite
    ended = $ended
    pass = $pass
    total = $total
    first_failure = $firstFailure
    failure_count = @($failures).Count
    failures = @($failures | Select-Object -First 25)
    failure_categories = $failureCategories
  }
  runner = [ordered]@{
    stop_reason = $stopReason
    runner_stop_reason = $runnerStopReason
    requested_steps = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "requested_steps")
    attempted_steps = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "attempted_steps")
    executed_steps = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "executed_steps")
    skipped_steps = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "skipped_steps")
    unsupported_steps = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "unsupported_steps")
    fetch_failures = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "fetch_failures")
    final_pc = Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "final_pc"
    state_hash = Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "state_hash"
    active_suite_id = $activeSuiteId
    active_test_id = $activeTestId
    active_test_name = $activeTestName
    active_subtest_id = $activeSubtestId
    active_subtest_name = $activeSubtestName
  }
  output = [ordered]@{
    debug_bytes = $debugText.Length
    sram_text_bytes = $sramText.Length
    debug_text = $debugText
    sram_text = $sramText
  }
}

if ($Suite -eq "all") {
  $result.status = "partial"
  $result.all_suite_note = "All-suite iteration is wired as a mode, but current deterministic selection is blocked after the memory-suite frontier. Later suites are intentionally not claimed as run."
}

$json = $result | ConvertTo-Json -Depth 8
Set-Content -LiteralPath $jsonPath -Value $json -Encoding UTF8
Set-Content -LiteralPath $latestJsonPath -Value $json -Encoding UTF8

if ($UpdateDocs) {
  $summaryPassTotal = if ($null -ne $pass -and $null -ne $total) { "$pass/$total" } else { "Unavailable" }
  $failureText = if ($firstFailure) { $firstFailure } else { "None captured" }
  $docSection = @"

## Generated Run $timestamp

| Field | Value |
| --- | --- |
| Suite request | ``$Suite`` |
| Target suite | ``$targetSuite`` |
| Status | ``$($result.status)`` |
| Pass/total | ``$summaryPassTotal`` |
| First failure | ``$failureText`` |
| Stop reason | ``$runnerStopReason`` |
| Final PC | ``$($result.runner.final_pc)`` |
| State hash | ``$($result.runner.state_hash)`` |
| Text result | ``$resultPath`` |
| JSON result | ``$jsonPath`` |

"@
  Add-Content -LiteralPath $docPath -Value $docSection -Encoding UTF8
}

Write-Output "suite_test: result_path=$resultPath"
Write-Output "suite_test: json_result_path=$jsonPath"
