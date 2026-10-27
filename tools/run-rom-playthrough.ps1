# Repeated local-only ROM experiments. ROMs, screenshots and machine states
# remain outside tracked documentation; the caller supplies the ROM explicitly.
#requires -Version 7.3
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RomPath,
    [ValidateRange(1, 10000000)][int]$Frames = 1800,
    [ValidateRange(1, 10)][int]$Runs = 3,
    [ValidateRange(1, 10000000)][int]$MaxStepsPerFrame = 500000,
    [string]$InputScript = '',
    [string]$LoadState = '',
    [switch]$SaveState,
    [string]$ExePath = '',
    [string]$OutputDirectory = '',
    [int[]]$ScreenshotFrames = @(),
    [ValidateRange(1, 1000)][double]$TargetFps = 60
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $ExePath) { $ExePath = Join-Path $repoRoot 'build/desktop/apps/desktop/gba-desktop.exe' }
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repoRoot ('build/rom-playthrough/' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))
}
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
$RomPath = (Resolve-Path -LiteralPath $RomPath).Path
if ($InputScript) { $InputScript = (Resolve-Path -LiteralPath $InputScript).Path }
if ($LoadState) {
    $LoadState = (Resolve-Path -LiteralPath $LoadState).Path
    if (-not (Test-Path -LiteralPath $LoadState -PathType Leaf)) { throw "LoadState must be a file: $LoadState" }
}
foreach ($frame in $ScreenshotFrames) {
    if ($frame -lt 0 -or $frame -ge $Frames) { throw "Screenshot frame $frame is outside 0..$($Frames - 1)" }
}
if (Test-Path -LiteralPath $OutputDirectory) {
    throw "OutputDirectory already exists; choose a new path to preserve previous evidence: $OutputDirectory"
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$exeSha256 = (Get-FileHash -LiteralPath $ExePath -Algorithm SHA256).Hash.ToLowerInvariant()
$inputSource = $InputScript
$inputSha256 = $null
if ($InputScript) {
    $inputCopy = Join-Path $OutputDirectory 'input.json'
    Copy-Item -LiteralPath $InputScript -Destination $inputCopy
    $InputScript = $inputCopy
    $inputSha256 = (Get-FileHash -LiteralPath $inputCopy -Algorithm SHA256).Hash.ToLowerInvariant()
}

# Freeze the checkpoint used by every repetition. A state can contain ROM and
# save data, so the output directory is local evidence and must not be committed.
$stateSource = $LoadState
$stateSha256 = $null
if ($LoadState) {
    $stateCopy = Join-Path $OutputDirectory 'initial.state'
    Copy-Item -LiteralPath $LoadState -Destination $stateCopy
    $LoadState = $stateCopy
    $stateSha256 = (Get-FileHash -LiteralPath $stateCopy -Algorithm SHA256).Hash.ToLowerInvariant()
}

$measurements = @()
$reference = $null
$executionOk = $true
$deterministic = $true
$repeatComparisons = 0
for ($index = 1; $index -le $Runs; $index++) {
    $runDir = Join-Path $OutputDirectory "run-$index"
    New-Item -ItemType Directory -Path $runDir | Out-Null
    $artifactPath = Join-Path $runDir 'artifact.json'
    $arguments = @('--headless', '--rom', $RomPath, '--frames', "$Frames",
        '--max-steps-per-frame', "$MaxStepsPerFrame", '--frame-hash', '--audio-hash',
        '--state-hash', '--artifact', $artifactPath)
    if ($InputScript) { $arguments += @('--input-script', $InputScript) }
    if ($LoadState) { $arguments += @('--load-state', $LoadState) }
    $finalStatePath = $null
    if ($SaveState) {
        $finalStatePath = Join-Path $runDir 'final.state'
        $arguments += @('--save-state', $finalStatePath)
    }
    foreach ($frame in $ScreenshotFrames) { $arguments += @('--screenshot-frame', "$frame") }
    if ($ScreenshotFrames.Count) { $arguments += @('--screenshot-output', (Join-Path $runDir 'screenshots')) }
    # Resource observation happens outside the executable's timed region.
    $hostSample = $null
    if ($IsWindows) {
        $memory = Get-CimInstance Win32_PerfFormattedData_PerfOS_Memory
        $cpu = Get-CimInstance Win32_Processor
        $hostSample = @{ sampled_utc = [DateTime]::UtcNow.ToString('o');
            available_mib = $memory.AvailableMBytes;
            commit_percent = $memory.PercentCommittedBytesInUse;
            cpu_load_percent = @($cpu.LoadPercentage) }
    }
    & $ExePath @arguments *> (Join-Path $runDir 'console.log')
    $runExit = $LASTEXITCODE
    if (-not (Test-Path -LiteralPath $artifactPath)) {
        $executionOk = $false
        $measurement = @{ run = $index; exit_code = $runExit; artifact_missing = $true; host = $hostSample }
        if ($SaveState) {
            $measurement.final_state = $finalStatePath
            if (Test-Path -LiteralPath $finalStatePath -PathType Leaf) {
                $measurement.final_state_sha256 = (Get-FileHash -LiteralPath $finalStatePath -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        }
        $measurements += $measurement
        break
    }
    $artifact = Get-Content -LiteralPath $artifactPath -Raw | ConvertFrom-Json
    $ok = $runExit -eq 0 -and $artifact.run.completed_frames -eq $Frames -and
        $artifact.run.stop_reason -eq 'completed' -and
        $artifact.run.unsupported_instructions -eq 0 -and $artifact.run.fetch_failures -eq 0
    $finalStateHash = $null
    if ($SaveState) {
        if (Test-Path -LiteralPath $finalStatePath -PathType Leaf) {
            $finalStateHash = (Get-FileHash -LiteralPath $finalStatePath -Algorithm SHA256).Hash.ToLowerInvariant()
        } else {
            $ok = $false
        }
    }
    $executionOk = $executionOk -and $ok
    $identityParts = @($artifact.engine, $artifact.rom.sha256, $artifact.run,
        $artifact.video, $artifact.audio, $artifact.state)
    if ($SaveState) { $identityParts += $finalStateHash }
    $identity = $identityParts | ConvertTo-Json -Depth 10 -Compress
    if ($null -eq $reference) { $reference = $identity }
    else {
        $repeatComparisons++
        if ($identity -cne $reference) { $deterministic = $false }
    }
    $measurement = @{ run = $index; exit_code = $runExit; execution_ok = $ok;
        performance = $artifact.performance; audio_underruns = $artifact.audio.underruns;
        host = $hostSample; artifact = $artifactPath }
    if ($SaveState) { $measurement.final_state = $finalStatePath; $measurement.final_state_sha256 = $finalStateHash }
    $measurements += $measurement
    if (-not $ok) { break }
}
$targetMet = $executionOk -and $deterministic -and $repeatComparisons -gt 0 -and
    $measurements.Count -eq $Runs
foreach ($measurement in $measurements) {
    $targetMet = $targetMet -and $measurement.execution_ok -and
        $measurement.performance.completed_fps -ge $TargetFps -and
        $measurement.performance.step_p95_ms -le (1000.0 / $TargetFps)
}
$summary = @{ schema_version = 1; rom_sha256 = (Get-FileHash -LiteralPath $RomPath -Algorithm SHA256).Hash.ToLowerInvariant();
    frames_per_run = $Frames; requested_runs = $Runs; execution_ok = $executionOk;
    deterministic = $(if ($repeatComparisons -gt 0) { $deterministic } else { $null });
    repeat_comparisons = $repeatComparisons; target_fps = $TargetFps; headless_target_met = $targetMet;
    executable = $ExePath; executable_sha256 = $exeSha256;
    input_script = $inputSource; input_sha256 = $inputSha256; measurements = $measurements }
if ($stateSource) {
    $summary.initial_state = $stateSource
    $summary.initial_state_copy = $LoadState
    $summary.initial_state_sha256 = $stateSha256
}
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json') -Encoding utf8
Write-Output "ROM experiment: execution_ok=$executionOk repeat_comparisons=$repeatComparisons headless_target_met=$targetMet"
Write-Output "Evidence: $OutputDirectory"
if (-not $executionOk -or -not $deterministic) { exit 1 }
# Being slower than target is data, not an execution failure. Consult the
# summary before claiming performance; headless throughput is not a host soak.
exit 0
