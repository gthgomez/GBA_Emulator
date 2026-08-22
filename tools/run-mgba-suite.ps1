# SINGLE-WRITER ASSUMPTION: this script overwrites the shared
# build\test-results\mgba-suite-latest.json (and mgba-suite-all-latest.md for
# -Suite all) on every run. Timestamped per-run artifacts are unique, the
# "latest" copies are not: concurrent runs clobber each other's "latest"
# files, and the -Suite all mode additionally reads mgba-suite-latest.json
# after each child invocation. Serialize runs or isolate via separate checkouts.
param(
  [uint32]$MaxSteps = 0,
  [uint32]$TraceSteps = 0,
  [uint32]$TraceWindow = 32,
  [string]$InputScript = "",
  [ValidateSet("menu", "memory", "loadstore", "io-read", "timing", "ldmia", "stmia", "timers", "timer-irq", "shifter", "carry", "multiply-long", "bios-math", "dma", "sio-read", "sio-timing", "misc-edge", "video", "all")]
  [string]$Suite = "menu",
  [ValidateSet("basic-mode-3-actual", "basic-mode-3-expected", "basic-mode-4-actual", "basic-mode-4-expected", "degenerate-obj-actual", "degenerate-obj-expected", "layer-toggle-actual", "layer-toggle-expected", "layer-toggle-2-actual", "layer-toggle-2-expected", "oam-update-delay-actual", "oam-update-delay-expected", "window-offscreen-reset-actual", "window-offscreen-reset-expected")]
  [string]$VideoProbe = "basic-mode-3-actual",
  [string]$UntilOutput = "",
  [switch]$TraceFirstFailure,
  [switch]$FailOnRed,
  [switch]$UpdateDocs
)

# Requires PowerShell 7.3+ for $PSNativeCommandUseErrorActionPreference.
#requires -Version 7.3

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$buildDir = Join-Path $repoRoot "build"
$resultsDir = Join-Path $buildDir "test-results"
$runnerPath = Join-Path $buildDir "mgba-suite-runner-$PID.exe"
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
  "loadstore" = 8000000
  "io-read" = 8000000
  "timing" = 20000000
  "ldmia" = 20000000
  "stmia" = 20000000
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
$suiteEvidenceTargets = @{
  "loadstore" = [ordered]@{
    upstream_suite = "memory"
    kind = "embedded_alias"
    scope = "Broad load/store behavior embedded in the upstream mGBA memory suite."
    note = "loadstore is a harness evidence alias, not a standalone upstream mGBA suite."
  }
  "ldmia" = [ordered]@{
    upstream_suite = "timing"
    kind = "embedded_alias"
    scope = "LDMIA timing evidence embedded in the upstream mGBA timing suite."
    note = "ldmia is a harness evidence alias, not a standalone upstream mGBA suite."
  }
  "stmia" = [ordered]@{
    upstream_suite = "timing"
    kind = "embedded_alias"
    scope = "STMIA timing evidence embedded in the upstream mGBA timing suite."
    note = "stmia is a harness evidence alias, not a standalone upstream mGBA suite."
  }
}

function Resolve-VideoProbeDefinition {
  param([string]$Probe)

  switch ($Probe) {
    "basic-mode-3-actual" {
      return [ordered]@{
        test_name = "Basic Mode 3"
        view = "actual"
        video_index = 0
        until_output = "VIDEO:MODE3_BITMAP"
        expected_mode = 3
      }
    }
    "basic-mode-3-expected" {
      return [ordered]@{
        test_name = "Basic Mode 3"
        view = "expected"
        video_index = 0
        until_output = "VIDEO:BASIC_MODE3_EXPECTED"
        expected_mode = 0
      }
    }
    "basic-mode-4-actual" {
      return [ordered]@{
        test_name = "Basic Mode 4"
        view = "actual"
        video_index = 1
        until_output = "VIDEO:MODE4_BITMAP"
        expected_mode = 4
      }
    }
    "basic-mode-4-expected" {
      return [ordered]@{
        test_name = "Basic Mode 4"
        view = "expected"
        video_index = 1
        until_output = "VIDEO:BASIC_MODE4_EXPECTED"
        expected_mode = 0
      }
    }
    "degenerate-obj-actual" {
      return [ordered]@{
        test_name = "Degenerate OBJ transforms"
        view = "actual"
        video_index = 2
        until_output = "VIDEO:DEGENERATE_OBJ_ACTUAL"
        expected_mode = 0
      }
    }
    "degenerate-obj-expected" {
      return [ordered]@{
        test_name = "Degenerate OBJ transforms"
        view = "expected"
        video_index = 2
        until_output = "VIDEO:DEGENERATE_OBJ_EXPECTED"
        expected_mode = 0
      }
    }
    "layer-toggle-actual" {
      return [ordered]@{
        test_name = "Layer toggle"
        view = "actual"
        video_index = 3
        until_output = "VIDEO:LAYER_TOGGLE_ACTUAL"
        expected_mode = 0
      }
    }
    "layer-toggle-expected" {
      return [ordered]@{
        test_name = "Layer toggle"
        view = "expected"
        video_index = 3
        until_output = "VIDEO:LAYER_TOGGLE_EXPECTED"
        expected_mode = 0
      }
    }
    "layer-toggle-2-actual" {
      return [ordered]@{
        test_name = "Layer toggle 2"
        view = "actual"
        video_index = 4
        until_output = "VIDEO:LAYER_TOGGLE_2_ACTUAL"
        expected_mode = 0
      }
    }
    "layer-toggle-2-expected" {
      return [ordered]@{
        test_name = "Layer toggle 2"
        view = "expected"
        video_index = 4
        until_output = "VIDEO:LAYER_TOGGLE_2_EXPECTED"
        expected_mode = 0
      }
    }
    "oam-update-delay-actual" {
      return [ordered]@{
        test_name = "OAM Update Delay"
        view = "actual"
        video_index = 5
        until_output = "VIDEO:OAM_UPDATE_DELAY_ACTUAL"
        expected_mode = 0
      }
    }
    "oam-update-delay-expected" {
      return [ordered]@{
        test_name = "OAM Update Delay"
        view = "expected"
        video_index = 5
        until_output = "VIDEO:OAM_UPDATE_DELAY_EXPECTED"
        expected_mode = 0
      }
    }
    "window-offscreen-reset-actual" {
      return [ordered]@{
        test_name = "Window offscreen reset"
        view = "actual"
        video_index = 6
        until_output = "VIDEO:WINDOW_OFFSCREEN_RESET_ACTUAL"
        expected_mode = 0
      }
    }
    "window-offscreen-reset-expected" {
      return [ordered]@{
        test_name = "Window offscreen reset"
        view = "expected"
        video_index = 6
        until_output = "VIDEO:WINDOW_OFFSCREEN_RESET_EXPECTED"
        expected_mode = 0
      }
    }
  }

  throw "Unknown video probe: $Probe"
}

function Resolve-SuiteEvidenceTarget {
  param([string]$SuiteName)

  if ($suiteEvidenceTargets.ContainsKey($SuiteName)) {
    return $suiteEvidenceTargets[$SuiteName]
  }
  return [ordered]@{
    upstream_suite = $SuiteName
    kind = if ($SuiteName -eq "all") { "aggregate" } else { "upstream_suite" }
    scope = if ($SuiteName -eq "all") {
      "Aggregate runner over upstream mGBA suite entries."
    } else {
      "Direct upstream mGBA suite entry."
    }
    note = "Requested target maps directly to the upstream mGBA suite entry."
  }
}

$evidenceTarget = Resolve-SuiteEvidenceTarget -SuiteName $Suite
$videoProbeDefinition = if ($Suite -eq "video") { Resolve-VideoProbeDefinition -Probe $VideoProbe } else { $null }

if ($MaxSteps -eq 0) {
  $MaxSteps = [uint32]$suiteDefaultMaxSteps[$Suite]
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
New-Item -ItemType Directory -Force -Path $resultsDir | Out-Null

if (-not (Test-Path -LiteralPath $suitePath -PathType Leaf)) {
  & (Join-Path $PSScriptRoot "build-mgba-suite.ps1")
  if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "suite_test: FAIL (build-mgba-suite.ps1 exited with $LASTEXITCODE)"
    if ($null -eq $LASTEXITCODE) {
      exit 1
    }
    exit $LASTEXITCODE
  }
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
  if ($null -ne $SuiteResult.runner.runner_exit_code -and
      [int]$SuiteResult.runner.runner_exit_code -ne 0) {
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
  $artifactStem = "mgba-suite-all-$timestamp-$PID"
  $jsonPath = Join-Path $resultsDir "$artifactStem.json"
  $markdownPath = Join-Path $resultsDir "$artifactStem.md"
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
      evidence_target = $suiteName
      upstream_suite = $suiteName
      evidence_kind = "upstream_suite"
      evidence_scope = "Direct upstream mGBA suite entry."
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
  (Join-Path $repoRoot "src\core\ppu_background.cpp") `
  (Join-Path $repoRoot "src\core\ppu_renderer.cpp") `
  (Join-Path $repoRoot "src\core\ppu_sprites.cpp") `
  (Join-Path $repoRoot "src\core\ppu_timing.cpp") `
  (Join-Path $repoRoot "src\core\timers.cpp") `
  (Join-Path $repoRoot "src\core\wait_state_control.cpp") `
  (Join-Path $PSScriptRoot "mgba-suite-runner.cpp") `
  -o $runnerPath

# The runner path is PID-scoped, but a failed compile must still never fall
# through to a missing/partial executable.
if ($LASTEXITCODE -ne 0) {
  Write-Host ""
  Write-Host "suite_test: FAIL (g++ mgba-suite-runner exited with $LASTEXITCODE)"
  if ($null -eq $LASTEXITCODE) {
    exit 1
  }
  exit $LASTEXITCODE
}

$suiteHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $suitePath).Hash
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$artifactSuiteName = $Suite -replace '[^A-Za-z0-9_.-]', '-'
$artifactStem = "mgba-suite-$artifactSuiteName-$timestamp-$PID"
$resultPath = Join-Path $resultsDir "$artifactStem.txt"
$jsonPath = Join-Path $resultsDir "$artifactStem.json"

$targetSuite = $Suite
if ($Suite -eq "all") {
  $targetSuite = "memory"
} else {
  $targetSuite = $evidenceTarget.upstream_suite
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

function New-VideoProbeInputScript {
  param([object]$ProbeDefinition)

  $events = New-Object System.Collections.Generic.List[string]
  foreach ($event in ((New-SuiteInputScript -SuiteIndex ([int]$suiteMenuIndices["video"])) -split ",")) {
    if (-not [string]::IsNullOrWhiteSpace($event)) {
      $events.Add($event)
    }
  }
  $events.Add("5510000:release")
  $step = 5850000
  for ($i = 0; $i -lt [int]$ProbeDefinition.video_index; ++$i) {
    $events.Add("$($step):DOWN")
    $events.Add("$($step + 80000):release")
    $step += 350000
  }
  $selectStep = if ([int]$ProbeDefinition.video_index -eq 0) { 6200000 } else { $step }
  $events.Add("$($selectStep):A")
  $events.Add("$($selectStep + 80000):release")
  if ($ProbeDefinition.view -eq "expected") {
    $toggleStep = $selectStep + 550000
    $events.Add("$($toggleStep):RIGHT")
    $events.Add("$($toggleStep + 80000):release")
  }
  return ($events -join ',')
}

if ($suiteMenuIndices.Contains($targetSuite) -and [string]::IsNullOrWhiteSpace($InputScript)) {
  $InputScript = if ($Suite -eq "video") {
    New-VideoProbeInputScript -ProbeDefinition $videoProbeDefinition
  } else {
    New-SuiteInputScript -SuiteIndex ([int]$suiteMenuIndices[$targetSuite])
  }
}
if ($suiteMenuIndices.Contains($targetSuite) -and [string]::IsNullOrWhiteSpace($UntilOutput)) {
  $UntilOutput = if ($Suite -eq "video") { $videoProbeDefinition.until_output } else { "END:" }
}

$header = @(
  "suite_test: timestamp=$timestamp",
  "suite_test: suite=$Suite",
  "suite_test: evidence_target=$Suite",
  "suite_test: upstream_suite=$targetSuite",
  "suite_test: evidence_kind=$($evidenceTarget.kind)",
  "suite_test: evidence_scope=$($evidenceTarget.scope)",
  "suite_test: evidence_note=$($evidenceTarget.note)",
  "suite_test: target_suite=$targetSuite",
  "suite_test: max_steps=$MaxSteps",
  "suite_test: trace_steps=$TraceSteps",
  "suite_test: trace_window=$TraceWindow",
  "suite_test: trace_first_failure=$TraceFirstFailure",
  "suite_test: input_script=$InputScript",
  "suite_test: video_probe=$VideoProbe",
  "suite_test: until_output=$UntilOutput",
  "suite_test: suite_sha256=$suiteHash"
)

$header | Tee-Object -FilePath $resultPath

$runnerExitCode = 0
$previousNativeCommandPreference = $PSNativeCommandUseErrorActionPreference
try {
  $PSNativeCommandUseErrorActionPreference = $false
  & $runnerPath $suitePath $MaxSteps $TraceSteps $InputScript $UntilOutput $TraceWindow 2>&1 |
    Tee-Object -FilePath $resultPath -Append
  $runnerExitCode = $LASTEXITCODE
} finally {
  $PSNativeCommandUseErrorActionPreference = $previousNativeCommandPreference
}

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

function New-VideoProbeMetricSnapshot {
  param(
    [string[]]$Lines,
    [string]$Probe,
    [object]$ProbeDefinition,
    [int]$RunnerExitCode,
    [string]$TextResultPath
  )

  $probeStopReason = Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "runner_stop_reason"
  $probeUnsupportedSteps = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "unsupported_steps")
  $probeFetchFailures = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "fetch_failures")
  $probeUntilMatched = Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "until_output_matched"
  $probeFrameHash = Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_frame_hash"
  $probeActiveSuiteId = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "active_suite_id")
  $probeReached = $RunnerExitCode -eq 0 -and
      $probeStopReason -eq "video_probe" -and
      $probeUntilMatched -eq "true" -and
      ($null -eq $probeUnsupportedSteps -or $probeUnsupportedSteps -eq 0) -and
      ($null -eq $probeFetchFailures -or $probeFetchFailures -eq 0)

  return [ordered]@{
    probe = $Probe
    test_name = $ProbeDefinition.test_name
    view = $ProbeDefinition.view
    video_index = [int]$ProbeDefinition.video_index
    expected_mode = [int]$ProbeDefinition.expected_mode
    until_output = $ProbeDefinition.until_output
    status = if ($probeReached) { "probe_reached" } elseif ($RunnerExitCode -ne 0) { "runner_failed" } else { "probe_not_reached" }
    runner_exit_code = $RunnerExitCode
    runner_stop_reason = $probeStopReason
    until_output_matched = $probeUntilMatched
    active_suite_id = $probeActiveSuiteId
    active_test_id = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "active_test_id")
    active_subtest_id = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "active_subtest_id")
    unsupported_steps = $probeUnsupportedSteps
    fetch_failures = $probeFetchFailures
    frame_hash = $probeFrameHash
    rendered_scanlines = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_rendered_scanlines")
    supported_scanlines = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_supported_scanlines")
    forced_blank_scanlines = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_forced_blank_scanlines")
    unsupported_scanlines = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_unsupported_scanlines")
    bg_pixels = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_bg_pixels")
    obj_pixels = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_obj_pixels")
    bitmap_pixels = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_bitmap_pixels")
    window_masked_pixels = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_window_masked_pixels")
    blend_pixels = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_blend_pixels")
    dispcnt = Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_dispcnt"
    vcount = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_vcount")
    frame_cycle = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_frame_cycle")
    oam0_masked = Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_oam0_masked"
    scanline_capture_active = Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_scanline_capture_active"
    scanline_capture_complete = Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_scanline_capture_complete"
    scanline_frame_hash = Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_scanline_frame_hash"
    scanline_captured_scanlines = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_scanline_captured_scanlines")
    scanline_supported_scanlines = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_scanline_supported_scanlines")
    scanline_forced_blank_scanlines = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_scanline_forced_blank_scanlines")
    scanline_unsupported_scanlines = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_scanline_unsupported_scanlines")
    scanline_bg_pixels = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_scanline_bg_pixels")
    scanline_obj_pixels = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_scanline_obj_pixels")
    scanline_bitmap_pixels = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_scanline_bitmap_pixels")
    scanline_window_masked_pixels = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_scanline_window_masked_pixels")
    scanline_blend_pixels = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_scanline_blend_pixels")
    scanline_first_captured = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_scanline_first_captured")
    scanline_last_captured = Convert-ToNullableInt (Get-SuiteMetric -Lines $Lines -Prefix "suite_runner" -Name "video_scanline_last_captured")
    text_result_path = $TextResultPath
  }
}

function Compare-VideoHash {
  param(
    [string]$Actual,
    [string]$Expected
  )

  if ([string]::IsNullOrWhiteSpace($Actual) -or
      [string]::IsNullOrWhiteSpace($Expected) -or
      $Actual -eq "0" -or $Expected -eq "0") {
    return "unavailable"
  }
  if ($Actual -eq $Expected) {
    return "match"
  }
  return "mismatch"
}

function Invoke-VideoProbeCapture {
  param(
    [string]$Probe,
    [string]$TextResultPath
  )

  $probeDefinition = Resolve-VideoProbeDefinition -Probe $Probe
  $probeInputScript = New-VideoProbeInputScript -ProbeDefinition $probeDefinition
  $probeUntilOutput = $probeDefinition.until_output
  $probeExitCode = 0
  $previousNativeCommandPreference = $PSNativeCommandUseErrorActionPreference
  try {
    $PSNativeCommandUseErrorActionPreference = $false
    & $runnerPath $suitePath $MaxSteps 0 $probeInputScript $probeUntilOutput $TraceWindow 2>&1 |
      Tee-Object -FilePath $TextResultPath |
      Out-Null
    $probeExitCode = $LASTEXITCODE
  } finally {
    $PSNativeCommandUseErrorActionPreference = $previousNativeCommandPreference
  }
  $probeLines = Get-Content -LiteralPath $TextResultPath
  return New-VideoProbeMetricSnapshot -Lines $probeLines -Probe $Probe -ProbeDefinition $probeDefinition -RunnerExitCode $probeExitCode -TextResultPath $TextResultPath
}

# Shared implementation for the seven paired actual/expected video-oracle
# probes. Captures the requested view from the primary run, runs the paired
# view once, compares frame hashes, and builds the diagnostic oracle record
# plus (on mismatch) the parsed first-failure/category payload.
# Reads script-scope state ($resultsDir, $artifactStem, $lines,
# $runnerExitCode, $resultPath) like the other suite helpers above.
function Invoke-PairedVideoOracleEvidence {
  param(
    [Parameter(Mandatory = $true)][string]$ActualProbe,
    [Parameter(Mandatory = $true)][string]$ExpectedProbe,
    [Parameter(Mandatory = $true)][string]$RequestedProbe,
    [Parameter(Mandatory = $true)][object]$RequestedProbeDefinition,
    [Parameter(Mandatory = $true)][string]$OracleName,
    [Parameter(Mandatory = $true)][string]$FailureCategory,
    [Parameter(Mandatory = $true)][ValidateSet("plain", "obj_pixels", "bg_pixel_pair")]
    [string]$FailureMessageStyle
  )

  $pairedProbeName = if ($RequestedProbe -eq $ActualProbe) { $ExpectedProbe } else { $ActualProbe }
  $pairedProbePath = Join-Path $resultsDir "$artifactStem-$pairedProbeName.txt"
  $currentSnapshot = New-VideoProbeMetricSnapshot -Lines $lines -Probe $RequestedProbe -ProbeDefinition $RequestedProbeDefinition -RunnerExitCode $runnerExitCode -TextResultPath $resultPath
  $pairedSnapshot = Invoke-VideoProbeCapture -Probe $pairedProbeName -TextResultPath $pairedProbePath
  $actualSnapshot = if ($RequestedProbe -eq $ActualProbe) { $currentSnapshot } else { $pairedSnapshot }
  $expectedSnapshot = if ($RequestedProbe -eq $ExpectedProbe) { $currentSnapshot } else { $pairedSnapshot }
  $hashComparison = if ([string]::IsNullOrWhiteSpace($actualSnapshot.frame_hash) -or
      [string]::IsNullOrWhiteSpace($expectedSnapshot.frame_hash)) {
    "unavailable"
  } elseif ($actualSnapshot.frame_hash -eq $expectedSnapshot.frame_hash) {
    "match"
  } else {
    "mismatch"
  }

  $oracle = [ordered]@{
    name = "$OracleName actual/expected video oracle"
    primary_probe = $RequestedProbe
    paired_probe = $pairedProbeName
    actual = $actualSnapshot
    expected = $expectedSnapshot
    frame_hash_comparison = $hashComparison
    scanline_frame_hash_comparison = Compare-VideoHash -Actual $actualSnapshot.scanline_frame_hash -Expected $expectedSnapshot.scanline_frame_hash
    note = "Diagnostic-only oracle evidence for the paired upstream $OracleName actual/expected views; it does not mark the interactive video suite green."
  }

  $firstFailure = $null
  $category = $null
  if ($hashComparison -ne "match") {
    switch ($FailureMessageStyle) {
      "plain" {
        $firstFailure = "$OracleName video oracle: actual status=$($actualSnapshot.status) frame_hash=$($actualSnapshot.frame_hash); expected status=$($expectedSnapshot.status) frame_hash=$($expectedSnapshot.frame_hash); frame_hash_comparison=$hashComparison"
      }
      "obj_pixels" {
        $firstFailure = "$OracleName video oracle: actual status=$($actualSnapshot.status) frame_hash=$($actualSnapshot.frame_hash) obj_pixels=$($actualSnapshot.obj_pixels); expected status=$($expectedSnapshot.status) frame_hash=$($expectedSnapshot.frame_hash) bg_pixels=$($expectedSnapshot.bg_pixels); frame_hash_comparison=$hashComparison"
      }
      "bg_pixel_pair" {
        $firstFailure = "$OracleName video oracle: actual status=$($actualSnapshot.status) frame_hash=$($actualSnapshot.frame_hash) bg_pixels=$($actualSnapshot.bg_pixels); expected status=$($expectedSnapshot.status) frame_hash=$($expectedSnapshot.frame_hash) bg_pixels=$($expectedSnapshot.bg_pixels); frame_hash_comparison=$hashComparison"
      }
    }
    $category = [ordered]@{
      name = $FailureCategory
      count = 1
      first_failure = $firstFailure
      tests = @("$OracleName actual", "$OracleName expected")
      examples = @($firstFailure)
    }
  }

  return [ordered]@{
    oracle = $oracle
    first_failure = $firstFailure
    category = $category
  }
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

function Test-TimingEvidenceMatch {
  param(
    [string]$Target,
    [string]$TestName
  )

  if ([string]::IsNullOrWhiteSpace($TestName)) {
    return $false
  }

  switch ($Target) {
    "loadstore" {
      return $TestName -match "^(ldr|str|nop / ldr|nop / str)" -and
          $TestName -notmatch "^(ldmia|stmia)"
    }
    "ldmia" {
      return $TestName -match "^ldmia\b"
    }
    "stmia" {
      return $TestName -match "^stmia\b"
    }
    default {
      return $false
    }
  }
}

function Get-FocusedTimingEvidence {
  param(
    [string]$Text,
    [string]$Target,
    [bool]$UpstreamEnded
  )

  $currentTest = $null
  $passCount = 0
  $failureList = New-Object System.Collections.Generic.List[object]
  $tests = New-Object System.Collections.Generic.List[string]

  foreach ($line in ($Text -split "`r?`n")) {
    if ($line -match "^Timing test:\s*(.+)$") {
      $currentTest = $Matches[1].Trim()
      if ((Test-TimingEvidenceMatch -Target $Target -TestName $currentTest) -and
          $tests -notcontains $currentTest) {
        $tests.Add($currentTest)
      }
      continue
    }

    if (-not (Test-TimingEvidenceMatch -Target $Target -TestName $currentTest)) {
      continue
    }

    if ($line -match "^PASS:") {
      ++$passCount
      continue
    }

    if ($line -match "^FAIL:\s*(.+)$") {
      $failureList.Add([ordered]@{
        test = $currentTest
        message = $Matches[1].Trim()
      })
    }
  }

  $failureArray = @($failureList.ToArray())
  $total = $passCount + $failureArray.Count
  $firstFailure = if ($failureArray.Count -gt 0) {
    "FAIL: $($failureArray[0].message)"
  } elseif ($total -eq 0) {
    "No focused $Target timing evidence was found in upstream timing output"
  } else {
    $null
  }

  $categoryName = "timing_$Target"
  $categories = @()
  if ($failureArray.Count -gt 0) {
    $categories = @([ordered]@{
      name = $categoryName
      count = $failureArray.Count
      first_failure = $failureArray[0].message
      tests = @($failureArray | ForEach-Object { $_.test } | Select-Object -Unique)
      examples = @($failureArray | Select-Object -First 5 | ForEach-Object { $_.message })
    })
  }

  return [ordered]@{
    suite = "Timing tests / $Target evidence"
    began = $total -gt 0
    ended = $UpstreamEnded -and $total -gt 0
    pass = if ($total -gt 0) { $passCount } else { $null }
    total = if ($total -gt 0) { $total } else { $null }
    first_failure = $firstFailure
    failure_count = $failureArray.Count
    failures = @($failureArray | Select-Object -First 25)
    failure_categories = $categories
    focused_test_count = $tests.Count
    focused_tests = @($tests.ToArray())
  }
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
  # Anchored like every other failure-line consumer (^FAIL:), so prose lines
  # merely containing "FAIL" are not misread as the first failure.
  if ($line -match "^FAIL:") {
    $firstFailure = $line.Trim()
    break
  }
}

$activeSuiteId = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "active_suite_id")
$activeTestId = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "active_test_id")
$activeSubtestId = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "active_subtest_id")
$stopReason = Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "stop_reason"
$runnerStopReason = Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "runner_stop_reason"
$unsupportedSteps = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "unsupported_steps")
$fetchFailures = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "fetch_failures")
$videoFrameHash = Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_frame_hash"
$videoRenderedScanlines = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_rendered_scanlines")
$videoSupportedScanlines = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_supported_scanlines")
$videoForcedBlankScanlines = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_forced_blank_scanlines")
$videoUnsupportedScanlines = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_unsupported_scanlines")
$videoBgPixels = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_bg_pixels")
$videoObjPixels = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_obj_pixels")
$videoBitmapPixels = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_bitmap_pixels")
$videoWindowMaskedPixels = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_window_masked_pixels")
$videoBlendPixels = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_blend_pixels")
$videoDispcnt = Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_dispcnt"
$videoVcount = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_vcount")
$videoFrameCycle = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_frame_cycle")
$videoScanlineCaptureActive = Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_scanline_capture_active"
$videoScanlineCaptureComplete = Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_scanline_capture_complete"
$videoScanlineFrameHash = Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_scanline_frame_hash"
$videoScanlineCapturedScanlines = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_scanline_captured_scanlines")
$videoScanlineSupportedScanlines = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_scanline_supported_scanlines")
$videoScanlineForcedBlankScanlines = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_scanline_forced_blank_scanlines")
$videoScanlineUnsupportedScanlines = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_scanline_unsupported_scanlines")
$videoScanlineBgPixels = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_scanline_bg_pixels")
$videoScanlineObjPixels = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_scanline_obj_pixels")
$videoScanlineBitmapPixels = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_scanline_bitmap_pixels")
$videoScanlineWindowMaskedPixels = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_scanline_window_masked_pixels")
$videoScanlineBlendPixels = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_scanline_blend_pixels")
$videoScanlineFirstCaptured = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_scanline_first_captured")
$videoScanlineLastCaptured = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "video_scanline_last_captured")
$videoProbeCase = if ($Suite -eq "video" -and $videoProbeDefinition) { $videoProbeDefinition.test_name } else { $null }
$videoProbeView = if ($Suite -eq "video" -and $videoProbeDefinition) { $videoProbeDefinition.view } else { $null }
$videoProbeExpectedMode = if ($Suite -eq "video" -and $videoProbeDefinition) { [int]$videoProbeDefinition.expected_mode } else { $null }
$activeTestName = if ($begunSuite -eq "Memory tests") {
  Get-MemorySuiteTestName -Index $activeTestId
} else {
  Get-CArrayTestName -SuiteName $begunSuite -Index $activeTestId
}
$activeSubtestName = if ($begunSuite -eq "Memory tests") { Get-MemorySuiteSubtestName -Index $activeSubtestId } else { $null }
$failures = @(Get-SuiteFailures -Text $combinedText)
$failureCategories = @(Get-SuiteFailureCategories -Text $combinedText -SuiteName $begunSuite)
$parsedSuite = if ($begunSuite) { $begunSuite } else { $targetSuite }
$parsedBegan = [bool]$begunSuite
$parsedEnded = $ended
$parsedPass = $pass
$parsedTotal = $total
$parsedFirstFailure = $firstFailure
$parsedFailureCount = @($failures).Count
$parsedFailures = @($failures | Select-Object -First 25)
$parsedFailureCategories = $failureCategories
$focusedTimingEvidence = $null
$basicMode3Oracle = $null
$basicMode4Oracle = $null
$degenerateObjOracle = $null
$layerToggleOracle = $null
$layerToggle2Oracle = $null
$oamUpdateDelayOracle = $null
$windowOffscreenResetOracle = $null

if ($evidenceTarget.kind -eq "embedded_alias" -and $targetSuite -eq "timing") {
  $focusedTimingEvidence = Get-FocusedTimingEvidence -Text $combinedText -Target $Suite -UpstreamEnded $ended
  $parsedSuite = $focusedTimingEvidence.suite
  $parsedBegan = [bool]$focusedTimingEvidence.began
  $parsedEnded = [bool]$focusedTimingEvidence.ended
  $parsedPass = $focusedTimingEvidence.pass
  $parsedTotal = $focusedTimingEvidence.total
  $parsedFirstFailure = $focusedTimingEvidence.first_failure
  $parsedFailureCount = [int]$focusedTimingEvidence.failure_count
  $parsedFailures = @($focusedTimingEvidence.failures)
  $parsedFailureCategories = @($focusedTimingEvidence.failure_categories)
}

if ($Suite -eq "video" -and
    ($VideoProbe -eq "basic-mode-3-actual" -or $VideoProbe -eq "basic-mode-3-expected")) {
  $basicMode3Outcome = Invoke-PairedVideoOracleEvidence `
    -ActualProbe "basic-mode-3-actual" `
    -ExpectedProbe "basic-mode-3-expected" `
    -RequestedProbe $VideoProbe `
    -RequestedProbeDefinition $videoProbeDefinition `
    -OracleName "Basic Mode 3" `
    -FailureCategory "video_basic_mode_3_oracle" `
    -FailureMessageStyle plain
  $basicMode3Oracle = $basicMode3Outcome.oracle
  if ($null -ne $basicMode3Outcome.first_failure) {
    $parsedFirstFailure = $basicMode3Outcome.first_failure
    if (@($parsedFailureCategories).Count -eq 0) {
      $parsedFailureCategories = @($basicMode3Outcome.category)
    }
  }
}

if ($Suite -eq "video" -and
    ($VideoProbe -eq "basic-mode-4-actual" -or $VideoProbe -eq "basic-mode-4-expected")) {
  $basicMode4Outcome = Invoke-PairedVideoOracleEvidence `
    -ActualProbe "basic-mode-4-actual" `
    -ExpectedProbe "basic-mode-4-expected" `
    -RequestedProbe $VideoProbe `
    -RequestedProbeDefinition $videoProbeDefinition `
    -OracleName "Basic Mode 4" `
    -FailureCategory "video_basic_mode_4_oracle" `
    -FailureMessageStyle plain
  $basicMode4Oracle = $basicMode4Outcome.oracle
  if ($null -ne $basicMode4Outcome.first_failure) {
    $parsedFirstFailure = $basicMode4Outcome.first_failure
    if (@($parsedFailureCategories).Count -eq 0) {
      $parsedFailureCategories = @($basicMode4Outcome.category)
    }
  }
}

if ($Suite -eq "video" -and
    ($VideoProbe -eq "degenerate-obj-actual" -or $VideoProbe -eq "degenerate-obj-expected")) {
  $degenerateObjOutcome = Invoke-PairedVideoOracleEvidence `
    -ActualProbe "degenerate-obj-actual" `
    -ExpectedProbe "degenerate-obj-expected" `
    -RequestedProbe $VideoProbe `
    -RequestedProbeDefinition $videoProbeDefinition `
    -OracleName "Degenerate OBJ transforms" `
    -FailureCategory "video_degenerate_obj_oracle" `
    -FailureMessageStyle obj_pixels
  $degenerateObjOracle = $degenerateObjOutcome.oracle
  if ($null -ne $degenerateObjOutcome.first_failure) {
    $parsedFirstFailure = $degenerateObjOutcome.first_failure
    if (@($parsedFailureCategories).Count -eq 0) {
      $parsedFailureCategories = @($degenerateObjOutcome.category)
    }
  }
}

if ($Suite -eq "video" -and
    ($VideoProbe -eq "layer-toggle-actual" -or $VideoProbe -eq "layer-toggle-expected")) {
  $layerToggleOutcome = Invoke-PairedVideoOracleEvidence `
    -ActualProbe "layer-toggle-actual" `
    -ExpectedProbe "layer-toggle-expected" `
    -RequestedProbe $VideoProbe `
    -RequestedProbeDefinition $videoProbeDefinition `
    -OracleName "Layer toggle" `
    -FailureCategory "video_layer_toggle_oracle" `
    -FailureMessageStyle bg_pixel_pair
  $layerToggleOracle = $layerToggleOutcome.oracle
  if ($null -ne $layerToggleOutcome.first_failure) {
    $parsedFirstFailure = $layerToggleOutcome.first_failure
    if (@($parsedFailureCategories).Count -eq 0) {
      $parsedFailureCategories = @($layerToggleOutcome.category)
    }
  }
}

if ($Suite -eq "video" -and
    ($VideoProbe -eq "layer-toggle-2-actual" -or $VideoProbe -eq "layer-toggle-2-expected")) {
  $layerToggle2Outcome = Invoke-PairedVideoOracleEvidence `
    -ActualProbe "layer-toggle-2-actual" `
    -ExpectedProbe "layer-toggle-2-expected" `
    -RequestedProbe $VideoProbe `
    -RequestedProbeDefinition $videoProbeDefinition `
    -OracleName "Layer toggle 2" `
    -FailureCategory "video_layer_toggle_2_oracle" `
    -FailureMessageStyle bg_pixel_pair
  $layerToggle2Oracle = $layerToggle2Outcome.oracle
  if ($null -ne $layerToggle2Outcome.first_failure) {
    $parsedFirstFailure = $layerToggle2Outcome.first_failure
    if (@($parsedFailureCategories).Count -eq 0) {
      $parsedFailureCategories = @($layerToggle2Outcome.category)
    }
  }
}

if ($Suite -eq "video" -and
    ($VideoProbe -eq "oam-update-delay-actual" -or $VideoProbe -eq "oam-update-delay-expected")) {
  $oamUpdateDelayOutcome = Invoke-PairedVideoOracleEvidence `
    -ActualProbe "oam-update-delay-actual" `
    -ExpectedProbe "oam-update-delay-expected" `
    -RequestedProbe $VideoProbe `
    -RequestedProbeDefinition $videoProbeDefinition `
    -OracleName "OAM Update Delay" `
    -FailureCategory "video_oam_update_delay_oracle" `
    -FailureMessageStyle bg_pixel_pair
  $oamUpdateDelayOracle = $oamUpdateDelayOutcome.oracle
  if ($null -ne $oamUpdateDelayOutcome.first_failure) {
    $parsedFirstFailure = $oamUpdateDelayOutcome.first_failure
    if (@($parsedFailureCategories).Count -eq 0) {
      $parsedFailureCategories = @($oamUpdateDelayOutcome.category)
    }
  }
}

if ($Suite -eq "video" -and
    ($VideoProbe -eq "window-offscreen-reset-actual" -or $VideoProbe -eq "window-offscreen-reset-expected")) {
  $windowOffscreenResetOutcome = Invoke-PairedVideoOracleEvidence `
    -ActualProbe "window-offscreen-reset-actual" `
    -ExpectedProbe "window-offscreen-reset-expected" `
    -RequestedProbe $VideoProbe `
    -RequestedProbeDefinition $videoProbeDefinition `
    -OracleName "Window offscreen reset" `
    -FailureCategory "video_window_offscreen_reset_oracle" `
    -FailureMessageStyle bg_pixel_pair
  $windowOffscreenResetOracle = $windowOffscreenResetOutcome.oracle
  if ($null -ne $windowOffscreenResetOutcome.first_failure) {
    $parsedFirstFailure = $windowOffscreenResetOutcome.first_failure
    if (@($parsedFailureCategories).Count -eq 0) {
      $parsedFailureCategories = @($windowOffscreenResetOutcome.category)
    }
  }
}

$visualInteractiveEvidence = $false
if ($Suite -eq "video" -and
    $activeSuiteId -eq [int64]$suiteMenuIndices["video"] -and
    ($runnerStopReason -eq "max_steps" -or $runnerStopReason -eq "video_probe") -and
    ($null -eq $unsupportedSteps -or $unsupportedSteps -eq 0) -and
    ($null -eq $fetchFailures -or $fetchFailures -eq 0)) {
  $visualInteractiveEvidence = $true
  $parsedSuite = "Video tests"
  $parsedBegan = $true
  if ($null -eq $parsedFirstFailure) {
    $frameSummary = if ([string]::IsNullOrWhiteSpace($videoFrameHash)) {
      "no frame hash emitted"
    } else {
      "frame_hash=$videoFrameHash rendered_scanlines=$videoRenderedScanlines supported_scanlines=$videoSupportedScanlines"
    }
    $caseSummary = if ($videoProbeCase) { "$videoProbeCase $videoProbeView; " } else { "" }
    $stopSummary = if ($runnerStopReason -eq "video_probe") {
      "reached deterministic video probe"
    } else {
      "reached active video suite before max-step cap"
    }
    $parsedFirstFailure = "Video suite is interactive/visual upstream; $stopSummary without unsupported instructions or fetch failures; $caseSummary$frameSummary"
  }
  if (@($parsedFailureCategories).Count -eq 0) {
    $parsedFailureCategories = @([ordered]@{
      name = "video_visual_interactive"
      count = 1
      first_failure = $parsedFirstFailure
      tests = @($videoProbeCase | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
      examples = @($parsedFirstFailure)
    })
  }
}

$firstFailureTrace = $null
if ($TraceFirstFailure -and $firstFailure -and $firstFailure -match "^FAIL:") {
  $traceResultPath = Join-Path $resultsDir "$artifactStem-first-failure-trace.txt"
  $firstFailureTraceWindow = [Math]::Max([uint32]512, $TraceWindow)
  $traceRunnerExitCode = 0
  $previousNativeCommandPreference = $PSNativeCommandUseErrorActionPreference
  try {
    $PSNativeCommandUseErrorActionPreference = $false
    & $runnerPath $suitePath $MaxSteps 0 $InputScript "FAIL:" $firstFailureTraceWindow 2>&1 |
      Tee-Object -FilePath $traceResultPath |
      Out-Null
    $traceRunnerExitCode = $LASTEXITCODE
  } finally {
    $PSNativeCommandUseErrorActionPreference = $previousNativeCommandPreference
  }
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
    runner_exit_code = $traceRunnerExitCode
    final_pc = Get-SuiteMetric -Lines $traceLines -Prefix "suite_runner" -Name "final_pc"
    recent_trace = @($traceRecentText -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    watch_changes = @($traceWatchText -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
  }
}

if ($null -eq $parsedFirstFailure -and $begunSuite -and -not $parsedEnded) {
  $namedFrontier = if ($activeTestName) { "$activeTestName" } else { "test_id=$activeTestId" }
  if ($activeSubtestName) {
    $namedFrontier = "$namedFrontier / $activeSubtestName"
  } else {
    $namedFrontier = "$namedFrontier / subtest_id=$activeSubtestId"
  }
  $parsedFirstFailure = "Stopped before END in $begunSuite at $namedFrontier (stop_reason=$runnerStopReason)"
}
if ($null -eq $parsedFirstFailure -and $runnerExitCode -ne 0) {
  $parsedFirstFailure = "Native runner exited with code $runnerExitCode"
}

$result = [ordered]@{
  timestamp = $timestamp
  suite_request = $Suite
  evidence_target = $Suite
  upstream_suite = $targetSuite
  target_suite = $targetSuite
  status = if ($parsedEnded) { "complete" } elseif ($parsedBegan) { "started_incomplete" } else { "not_started" }
  command = ".\tools\run-mgba-suite.ps1 -Suite $Suite -VideoProbe $VideoProbe -MaxSteps $MaxSteps -TraceSteps $TraceSteps -TraceWindow $TraceWindow -InputScript `"$InputScript`" -UntilOutput `"$UntilOutput`" -TraceFirstFailure:$TraceFirstFailure -FailOnRed:$FailOnRed"
  rom_path = $suitePath
  suite_sha256 = $suiteHash
  text_result_path = $resultPath
  json_result_path = $jsonPath
  max_steps = $MaxSteps
  trace_steps = $TraceSteps
  trace_window = $TraceWindow
  trace_first_failure = [bool]$TraceFirstFailure
  input_script = $InputScript
  video_probe = if ($Suite -eq "video") { $VideoProbe } else { $null }
  until_output = $UntilOutput
  evidence = [ordered]@{
    requested_target = $Suite
    upstream_suite = $targetSuite
    kind = $evidenceTarget.kind
    scope = $evidenceTarget.scope
    note = $evidenceTarget.note
  }
  parsed = [ordered]@{
    suite = $parsedSuite
    began = $parsedBegan
    ended = $parsedEnded
    pass = $parsedPass
    total = $parsedTotal
    first_failure = $parsedFirstFailure
    failure_count = $parsedFailureCount
    failures = $parsedFailures
    failure_categories = $parsedFailureCategories
  }
  runner = [ordered]@{
    stop_reason = $stopReason
    runner_stop_reason = $runnerStopReason
    runner_exit_code = $runnerExitCode
    requested_steps = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "requested_steps")
    attempted_steps = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "attempted_steps")
    executed_steps = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "executed_steps")
    skipped_steps = Convert-ToNullableInt (Get-SuiteMetric -Lines $lines -Prefix "suite_runner" -Name "skipped_steps")
    unsupported_steps = $unsupportedSteps
    fetch_failures = $fetchFailures
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
    focused_timing_evidence = $focusedTimingEvidence
    basic_mode_3_oracle = $basicMode3Oracle
    basic_mode_4_oracle = $basicMode4Oracle
    degenerate_obj_oracle = $degenerateObjOracle
    layer_toggle_oracle = $layerToggleOracle
    layer_toggle_2_oracle = $layerToggle2Oracle
    oam_update_delay_oracle = $oamUpdateDelayOracle
    window_offscreen_reset_oracle = $windowOffscreenResetOracle
    visual_interactive_evidence = if ($visualInteractiveEvidence) {
      [ordered]@{
        reached = $true
        active_suite_id = $activeSuiteId
        active_suite_name = "video"
        video_test = $videoProbeCase
        view = $videoProbeView
        expected_mode = $videoProbeExpectedMode
        runner_stop_reason = $runnerStopReason
        unsupported_steps = $unsupportedSteps
        fetch_failures = $fetchFailures
        frame_hash = $videoFrameHash
        rendered_scanlines = $videoRenderedScanlines
        supported_scanlines = $videoSupportedScanlines
        forced_blank_scanlines = $videoForcedBlankScanlines
        unsupported_scanlines = $videoUnsupportedScanlines
        bg_pixels = $videoBgPixels
        obj_pixels = $videoObjPixels
        bitmap_pixels = $videoBitmapPixels
        window_masked_pixels = $videoWindowMaskedPixels
        blend_pixels = $videoBlendPixels
        dispcnt = $videoDispcnt
        vcount = $videoVcount
        frame_cycle = $videoFrameCycle
        scanline_capture_active = $videoScanlineCaptureActive
        scanline_capture_complete = $videoScanlineCaptureComplete
        scanline_frame_hash = $videoScanlineFrameHash
        scanline_captured_scanlines = $videoScanlineCapturedScanlines
        scanline_supported_scanlines = $videoScanlineSupportedScanlines
        scanline_forced_blank_scanlines = $videoScanlineForcedBlankScanlines
        scanline_unsupported_scanlines = $videoScanlineUnsupportedScanlines
        scanline_bg_pixels = $videoScanlineBgPixels
        scanline_obj_pixels = $videoScanlineObjPixels
        scanline_bitmap_pixels = $videoScanlineBitmapPixels
        scanline_window_masked_pixels = $videoScanlineWindowMaskedPixels
        scanline_blend_pixels = $videoScanlineBlendPixels
        scanline_first_captured = $videoScanlineFirstCaptured
        scanline_last_captured = $videoScanlineLastCaptured
        note = "Video is an upstream interactive/visual suite and does not emit END pass totals under the deterministic text harness."
      }
    } else {
      $null
    }
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
| Evidence target | ``$Suite`` |
| Upstream suite executed | ``$targetSuite`` |
| Evidence kind | ``$($evidenceTarget.kind)`` |
| Evidence note | $($evidenceTarget.note) |
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
