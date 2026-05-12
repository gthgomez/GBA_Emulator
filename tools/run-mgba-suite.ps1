param(
  [uint32]$MaxSteps = 0,
  [uint32]$TraceSteps = 0,
  [uint32]$TraceWindow = 32,
  [string]$InputScript = "",
  [ValidateSet("menu", "memory", "io-read", "timing", "timers", "timer-irq", "shifter", "carry", "multiply-long", "bios-math", "dma", "sio-read", "sio-timing", "misc-edge", "video", "all")]
  [string]$Suite = "menu",
  [string]$UntilOutput = "",
  [switch]$TraceFirstFailure,
  [switch]$FailOnRed,
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
$allSuiteOrder = @(
  "memory",
  "io-read",
  "timing",
  "timers",
  "timer-irq",
  "shifter",
  "carry",
  "multiply-long",
  "bios-math",
  "dma",
  "sio-read",
  "sio-timing",
  "misc-edge",
  "video"
)
$suiteDefaultMaxSteps = @{
  "menu" = 1000000
  "memory" = 8000000
  "io-read" = 8000000
  "timing" = 20000000
  "timers" = 20000000
  "timer-irq" = 8000000
  "shifter" = 8000000
  "carry" = 8000000
  "multiply-long" = 8000000
  "bios-math" = 8000000
  "dma" = 20000000
  "sio-read" = 8000000
  "sio-timing" = 12000000
  "misc-edge" = 12000000
  "video" = 20000000
  "all" = 20000000
}

if ($MaxSteps -eq 0) {
  $MaxSteps = [uint32]$suiteDefaultMaxSteps[$Suite]
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
New-Item -ItemType Directory -Force -Path $resultsDir | Out-Null

if (-not (Test-Path -LiteralPath $suitePath -PathType Leaf)) {
  & (Join-Path $PSScriptRoot "build-mgba-suite.ps1")
}

function Get-TextSha256 {
  param([string]$Text)

  $sha = [System.Security.Cryptography.SHA256]::Create()
  try {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
    $hash = $sha.ComputeHash($bytes)
    return (($hash | ForEach-Object { $_.ToString("x2") }) -join "")
  } finally {
    $sha.Dispose()
  }
}

function ConvertTo-SuiteGreenStatus {
  param([object]$SuiteResult)

  if ($null -eq $SuiteResult -or $null -eq $SuiteResult.parsed -or $null -eq $SuiteResult.runner) {
    return "RED"
  }
  if (-not [bool]$SuiteResult.parsed.ended) {
    return "RED"
  }
  if ($null -eq $SuiteResult.parsed.pass -or $null -eq $SuiteResult.parsed.total) {
    return "RED"
  }
  if ([int]$SuiteResult.parsed.pass -ne [int]$SuiteResult.parsed.total) {
    return "RED"
  }
  if ([int]$SuiteResult.parsed.failure_count -ne 0) {
    return "RED"
  }
  if ($null -ne $SuiteResult.runner.unsupported_steps -and
      [int64]$SuiteResult.runner.unsupported_steps -ne 0) {
    return "RED"
  }
  if ($null -ne $SuiteResult.runner.fetch_failures -and
      [int64]$SuiteResult.runner.fetch_failures -ne 0) {
    return "RED"
  }
  return "GREEN"
}

function ConvertTo-CategorySummaryText {
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

if ($Suite -eq "all") {
  $suiteHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $suitePath).Hash
  $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
  $jsonPath = Join-Path $resultsDir "mgba-suite-all-$timestamp.json"
  $markdownPath = Join-Path $resultsDir "mgba-suite-all-$timestamp.md"
  $latestMarkdownPath = Join-Path $resultsDir "mgba-suite-all-latest.md"
  $childUntilOutput = if ([string]::IsNullOrWhiteSpace($UntilOutput)) { "END:" } else { $UntilOutput }

  $rows = New-Object System.Collections.Generic.List[object]
  foreach ($suiteName in $allSuiteOrder) {
    $suiteError = $null
    try {
      & $PSCommandPath -Suite $suiteName -MaxSteps $MaxSteps -TraceSteps $TraceSteps -TraceWindow $TraceWindow -UntilOutput $childUntilOutput -TraceFirstFailure:$TraceFirstFailure | Out-Null
    } catch {
      $suiteError = $_.Exception.Message
    }

    $childJsonPath = Join-Path $resultsDir "mgba-suite-latest.json"
    $child = $null
    if (Test-Path -LiteralPath $childJsonPath -PathType Leaf) {
      $child = Get-Content -Raw -LiteralPath $childJsonPath | ConvertFrom-Json
    }
    $status = if ($suiteError) { "RED" } else { ConvertTo-SuiteGreenStatus -SuiteResult $child }
    $parsed = if ($child) { $child.parsed } else { $null }
    $runner = if ($child) { $child.runner } else { $null }
    $passTotal = if ($parsed -and $null -ne $parsed.pass -and $null -ne $parsed.total) {
      "$($parsed.pass)/$($parsed.total)"
    } else {
      ""
    }
    $rows.Add([ordered]@{
      target = $suiteName
      status = $status
      parsed_suite = if ($parsed) { $parsed.suite } else { $null }
      pass = if ($parsed) { $parsed.pass } else { $null }
      total = if ($parsed) { $parsed.total } else { $null }
      pass_total = $passTotal
      ended = if ($parsed) { [bool]$parsed.ended } else { $false }
      first_failure = if ($suiteError) {
        $suiteError
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
      state_hash = if ($runner) { $runner.state_hash } else { $null }
      artifact = if ($child) { $child.json_result_path } else { $childJsonPath }
    })
    Write-Output "suite_all: target=$suiteName status=$status pass_total=$passTotal artifact=$($child.json_result_path)"
  }

  $rowArray = @($rows.ToArray())
  $redRows = @($rowArray | Where-Object { $_.status -ne "GREEN" })
  $overallStatus = if (@($redRows).Count -eq 0) { "GREEN" } else { "RED" }
  $compatibilityInput = ($rowArray | ConvertTo-Json -Depth 8 -Compress)
  $compatibilityHash = Get-TextSha256 -Text "$suiteHash`n$compatibilityInput"
  $aggregate = [ordered]@{
    timestamp = $timestamp
    suite_request = "all"
    status = "complete"
    overall_status = $overallStatus
    command = ".\tools\run-mgba-suite.ps1 -Suite all -MaxSteps $MaxSteps -TraceSteps $TraceSteps -TraceWindow $TraceWindow -UntilOutput `"$childUntilOutput`" -TraceFirstFailure:$TraceFirstFailure -FailOnRed:$FailOnRed"
    rom_path = $suitePath
    suite_sha256 = $suiteHash
    json_result_path = $jsonPath
    markdown_result_path = $markdownPath
    max_steps = $MaxSteps
    trace_steps = $TraceSteps
    until_output = $childUntilOutput
    compatibility_hash = $compatibilityHash
    rows = $rowArray
    red_count = @($redRows).Count
    next_red_target = if (@($redRows).Count -gt 0) { $redRows[0].target } else { $null }
  }

  $json = $aggregate | ConvertTo-Json -Depth 10
  Set-Content -LiteralPath $jsonPath -Value $json -Encoding UTF8
  Set-Content -LiteralPath $latestJsonPath -Value $json -Encoding UTF8

  $markdown = New-Object System.Collections.Generic.List[string]
  $markdown.Add("# mGBA All-Suite Summary $timestamp")
  $markdown.Add("")
  $markdown.Add("Overall status: **$overallStatus**")
  $markdown.Add("")
  $markdown.Add("Compatibility hash: ``$compatibilityHash``")
  $markdown.Add("")
  $markdown.Add("| Suite | Status | Pass/total | First failure | Categories | Artifact |")
  $markdown.Add("| --- | --- | --- | --- | --- | --- |")
  foreach ($row in $rowArray) {
    $failure = if ([string]::IsNullOrWhiteSpace($row.first_failure)) { "" } else { $row.first_failure.Replace("|", "\|") }
    $categories = (ConvertTo-CategorySummaryText -Categories $row.failure_categories).Replace("|", "\|")
    $markdown.Add("| ``$($row.target)`` | $($row.status) | ``$($row.pass_total)`` | $failure | $categories | ``$($row.artifact)`` |")
  }
  $markdown.Add("")
  $markdown.Add("Next red target: ``$($aggregate.next_red_target)``")
  $markdown.Add("")
  $markdown.Add("JSON: ``$jsonPath``")
  Set-Content -LiteralPath $markdownPath -Value $markdown -Encoding UTF8
  Set-Content -LiteralPath $latestMarkdownPath -Value $markdown -Encoding UTF8

  if ($UpdateDocs) {
    $greenCount = @($rowArray | Where-Object { $_.status -eq "GREEN" }).Count
    $totalCount = @($rowArray).Count
    $docSection = @"

## Generated All-Suite Run $timestamp

| Field | Value |
| --- | --- |
| Status | ``$overallStatus`` |
| Green suites | ``$greenCount/$totalCount`` |
| Next red target | ``$($aggregate.next_red_target)`` |
| Compatibility hash | ``$compatibilityHash`` |
| JSON result | ``$jsonPath`` |
| Markdown result | ``$markdownPath`` |

"@
    Add-Content -LiteralPath $docPath -Value $docSection -Encoding UTF8
  }

  Write-Output "suite_test: all_status=$overallStatus"
  Write-Output "suite_test: compatibility_hash=$compatibilityHash"
  Write-Output "suite_test: json_result_path=$jsonPath"
  Write-Output "suite_test: markdown_result_path=$markdownPath"
  Write-Output "suite_test: next_red_target=$($aggregate.next_red_target)"
  if ($FailOnRed -and $overallStatus -ne "GREEN") {
    throw "suite_test: RED"
  }
  return
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
  "suite_test: trace_window=$TraceWindow",
  "suite_test: trace_first_failure=$TraceFirstFailure",
  "suite_test: input_script=$InputScript",
  "suite_test: until_output=$UntilOutput",
  "suite_test: suite_sha256=$suiteHash"
)

$header | Tee-Object -FilePath $resultPath
& $runnerPath $suitePath $MaxSteps $TraceSteps $InputScript $UntilOutput $TraceWindow | Tee-Object -FilePath $resultPath -Append

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

function Get-SuiteSourceInfo {
  param([string]$SuiteName)

  switch ($SuiteName) {
    "Timing tests" { return @{ File = "timing.c"; Array = "timingTests" } }
    "Timer count-up tests" { return @{ File = "timers.c"; Array = "timerTests" } }
    "Timer IRQ tests" { return @{ File = "timer-irq.c"; Array = "timerIRQTests" } }
    "BIOS math tests" { return @{ File = "bios-math.c"; Array = "mathTests" } }
    "DMA tests" { return @{ File = "dma.c"; Array = "dmaTests" } }
    default { return $null }
  }
}

function Get-SuiteSourcePath {
  param([string]$FileName)

  $sourcePath = Join-Path $repoRoot "external\test-suites\mgba-suite\src\$FileName"
  if (Test-Path -LiteralPath $sourcePath -PathType Leaf) {
    return $sourcePath
  }
  $sourcePath = Join-Path $buildDir "test-suite-build\mgba-suite\src\$FileName"
  if (Test-Path -LiteralPath $sourcePath -PathType Leaf) {
    return $sourcePath
  }
  return $null
}

function Get-CArrayTestName {
  param(
    [string]$SuiteName,
    [int64]$Index
  )

  if ($Index -lt 0) {
    return $null
  }
  $sourceInfo = Get-SuiteSourceInfo -SuiteName $SuiteName
  if ($null -eq $sourceInfo) {
    return $null
  }
  $sourcePath = Get-SuiteSourcePath -FileName $sourceInfo.File
  if (-not $sourcePath) {
    return $null
  }

  $inside = $false
  $depth = 0
  $names = New-Object System.Collections.Generic.List[string]
  foreach ($line in Get-Content -LiteralPath $sourcePath) {
    if (-not $inside -and $line -match ("static\s+const\s+struct\s+\w+\s+" + [regex]::Escape($sourceInfo.Array) + "\[\]\s*=")) {
      $inside = $true
    }
    if (-not $inside) {
      continue
    }

    foreach ($char in $line.ToCharArray()) {
      if ($char -eq "{") {
        ++$depth
      } elseif ($char -eq "}") {
        --$depth
      }
    }
    if ($line -match '^\s*\{\s*"([^"]+)"\s*,') {
      $names.Add($Matches[1])
    }
    if ($inside -and $depth -le 0 -and $line -match ";\s*$") {
      break
    }
  }

  if ($Index -ge $names.Count) {
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
  if ($SuiteName -match "Timing") {
    if ($probe -match "DMA") { return "timing_dma" }
    if ($probe -match "swi|Div|Sqrt|Atan|CpuSet") { return "timing_bios_hle" }
    if ($probe -match "mul|mla|smull|smlal|umull|umlal") { return "timing_multiply" }
    if ($probe -match "\[#0x08000000\]") { return "timing_rom_data_access" }
    if ($FailureLine -match "ROM P") { return "timing_rom_prefetch" }
    if ($FailureLine -match "ROM .N|ROM PN|ROM .NS|ROM PNS") {
      return "timing_rom_nonsequential"
    }
    if ($FailureLine -match "EWRAM|IWRAM") { return "timing_internal_memory" }
    return "timing_other"
  }
  if ($SuiteName -match "Timer IRQ") { return "timer_irq" }
  if ($SuiteName -match "Timer") {
    $prescaled = $TestName -match "^[68]b|^10b"
    $multiIrq = $FailureLine -match "\b[24]i\b"
    $loopCount = $FailureLine -match "\b(1xs|16xs)\b"
    $sample = $FailureLine -match "\b(1xv|16xv)\b"
    if ($prescaled -and $loopCount) { return "timers_prescaled_loop_count" }
    if ($prescaled -and $sample) { return "timers_prescaled_counter_sample" }
    if ($multiIrq -and $loopCount) { return "timers_multi_irq_loop_count" }
    if ($multiIrq -and $sample) { return "timers_multi_irq_counter_sample" }
    if ($loopCount) { return "timers_loop_count" }
    if ($sample) { return "timers_counter_sample" }
    return "timers_other"
  }
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
$watchText = Get-OutputBlock -Lines $lines -Begin "suite_watch_changes_begin" -End "suite_watch_changes_end"
$watchChanges = @($watchText -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
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
$activeTestName = if ($begunSuite -eq "Memory tests") {
  Get-MemorySuiteTestName -Index $activeTestId
} else {
  Get-CArrayTestName -SuiteName $begunSuite -Index $activeTestId
}
$activeSubtestName = if ($begunSuite -eq "Memory tests") { Get-MemorySuiteSubtestName -Index $activeSubtestId } else { $null }
$failures = @(Get-SuiteFailures -Text $combinedText)
$failureCategories = @(Get-SuiteFailureCategories -Text $combinedText -SuiteName $begunSuite)

$firstFailureTrace = $null
if ($TraceFirstFailure -and $firstFailure -and $firstFailure -match "^FAIL:") {
  $traceResultPath = Join-Path $resultsDir "mgba-suite-$timestamp-first-failure-trace.txt"
  $firstFailureTraceWindow = [Math]::Max([uint32]512, $TraceWindow)
  & $runnerPath $suitePath $MaxSteps 0 $InputScript "FAIL:" $firstFailureTraceWindow | Tee-Object -FilePath $traceResultPath | Out-Null
  $traceLines = Get-Content -LiteralPath $traceResultPath
  $traceDebugText = Get-OutputBlock -Lines $traceLines -Begin "suite_output_begin" -End "suite_output_end"
  $traceRecentText = Get-OutputBlock -Lines $traceLines -Begin "suite_recent_trace_begin" -End "suite_recent_trace_end"
  $traceWatchText = Get-OutputBlock -Lines $traceLines -Begin "suite_watch_changes_begin" -End "suite_watch_changes_end"
  $traceFailure = $null
  foreach ($line in ($traceDebugText -split "`r?`n")) {
    if ($line -match "^FAIL:") {
      $traceFailure = $line.Trim()
      break
    }
  }
  $firstFailureTrace = [ordered]@{
    text_result_path = $traceResultPath
    until_output = "FAIL:"
    trace_window = $firstFailureTraceWindow
    matched = (Get-SuiteMetric -Lines $traceLines -Prefix "suite_runner" -Name "until_output_matched")
    first_failure = $traceFailure
    attempted_steps = Convert-ToNullableInt (Get-SuiteMetric -Lines $traceLines -Prefix "suite_runner" -Name "attempted_steps")
    executed_steps = Convert-ToNullableInt (Get-SuiteMetric -Lines $traceLines -Prefix "suite_runner" -Name "executed_steps")
    unsupported_steps = Convert-ToNullableInt (Get-SuiteMetric -Lines $traceLines -Prefix "suite_runner" -Name "unsupported_steps")
    fetch_failures = Convert-ToNullableInt (Get-SuiteMetric -Lines $traceLines -Prefix "suite_runner" -Name "fetch_failures")
    final_pc = Get-SuiteMetric -Lines $traceLines -Prefix "suite_runner" -Name "final_pc"
    recent_trace = @($traceRecentText -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    watch_changes = @($traceWatchText -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
  }
}

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
  command = ".\tools\run-mgba-suite.ps1 -Suite $Suite -MaxSteps $MaxSteps -TraceSteps $TraceSteps -TraceWindow $TraceWindow -InputScript `"$InputScript`" -UntilOutput `"$UntilOutput`" -TraceFirstFailure:$TraceFirstFailure -FailOnRed:$FailOnRed"
  rom_path = $suitePath
  suite_sha256 = $suiteHash
  text_result_path = $resultPath
  json_result_path = $jsonPath
  max_steps = $MaxSteps
  trace_steps = $TraceSteps
  trace_window = $TraceWindow
  trace_first_failure = [bool]$TraceFirstFailure
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
  diagnostics = [ordered]@{
    watch_changes = $watchChanges
    first_failure_trace = $firstFailureTrace
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
$singleStatus = ConvertTo-SuiteGreenStatus -SuiteResult ([pscustomobject]$result)
Write-Output "suite_test: status=$singleStatus"
if ($FailOnRed -and $singleStatus -ne "GREEN") {
  throw "suite_test: RED"
}
