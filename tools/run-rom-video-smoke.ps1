param(
    [Parameter(Mandatory = $true)]
    [string]$RomPath,

    [int]$Frames = 216,

    [int]$MaxStepsPerFrame = 2000000,

    [switch]$RequireValidHeader,

    [switch]$RequireFrameComplete,

    [int]$RequireScanlines = 160,

    [string]$RequireDispcntAfter = "60:0x0100",

    [string]$RequireUniqueColorsAfter = "215:8",

    [int]$RequireNotUniformAfter = 215,

    [string]$CheckFrame = "8,60,120,150,180,200,210,215",

    [string]$ArtifactPath = ""
)

# Requires PowerShell 7.3+ for $PSNativeCommandUseErrorActionPreference.
#requires -Version 7.3

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$buildDir = Join-Path $repoRoot "build"
$exePath = Join-Path $buildDir "rom_video_smoke.exe"

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$resolvedRom = Resolve-Path -LiteralPath $RomPath

$coreSources = @(
    (Join-Path $repoRoot "src\core\android_runtime.cpp"),
    (Join-Path $repoRoot "src\core\arm7tdmi.cpp"),
    (Join-Path $repoRoot "src\core\apu.cpp"),
    (Join-Path $repoRoot "src\core\bios.cpp"),
    (Join-Path $repoRoot "src\core\core_scheduler.cpp"),
    (Join-Path $repoRoot "src\core\core_session.cpp"),
    (Join-Path $repoRoot "src\core\dma_controller.cpp"),
    (Join-Path $repoRoot "src\core\interrupt_controller.cpp"),
    (Join-Path $repoRoot "src\core\io_registers.cpp"),
    (Join-Path $repoRoot "src\core\keypad.cpp"),
    (Join-Path $repoRoot "src\core\memory_bus.cpp"),
    (Join-Path $repoRoot "src\core\ppu_background.cpp"),
    (Join-Path $repoRoot "src\core\ppu_renderer.cpp"),
    (Join-Path $repoRoot "src\core\ppu_sprites.cpp"),
    (Join-Path $repoRoot "src\core\ppu_timing.cpp"),
    (Join-Path $repoRoot "src\core\timers.cpp"),
    (Join-Path $repoRoot "src\core\wait_state_control.cpp"),
    (Join-Path $repoRoot "tests\rom_video_smoke.cpp")
)

$compileArgs = @(
    "-std=c++17", "-Wall", "-Wextra", "-Werror",
    "-I", (Join-Path $repoRoot "include")
) + $coreSources + @("-o", $exePath)

function Assert-NativeExitCode {
    param(
        [string]$Step,
        [int]$Expected = 0
    )

    if ($LASTEXITCODE -ne $Expected) {
        Write-Host ""
        Write-Host "rom_video_smoke: FAIL ($Step exited with $LASTEXITCODE)"
        if ($null -eq $LASTEXITCODE) {
            exit 1
        }
        exit $LASTEXITCODE
    }
}

& g++ @compileArgs

# A failed compile must never fall through to rerun a stale smoke binary.
Assert-NativeExitCode -Step "g++ rom_video_smoke"

$runArgs = @(
    "--rom", $resolvedRom.Path,
    "--frames", $Frames,
    "--max-steps-per-frame", $MaxStepsPerFrame,
    "--require-scanlines", $RequireScanlines,
    "--require-dispcnt-after", $RequireDispcntAfter,
    "--require-unique-colors-after", $RequireUniqueColorsAfter,
    "--require-not-uniform-after", $RequireNotUniformAfter,
    "--check-frame", $CheckFrame,
    "--json"
)
if ($RequireValidHeader) {
    $runArgs += "--require-valid-header"
}
if ($RequireFrameComplete) {
    $runArgs += "--require-frame-complete"
}

$jsonOutput = & $exePath @runArgs 2>&1
$exitCode = $LASTEXITCODE

Write-Output $jsonOutput

if ($ArtifactPath -ne "") {
    $artifactDir = Split-Path -Parent $ArtifactPath
    if ($artifactDir -ne "" -and -not (Test-Path -LiteralPath $artifactDir)) {
        New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
    }
    @"
# ROM video smoke evidence

Date: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
ROM file: $(Split-Path -Leaf $resolvedRom.Path)
Frames requested: $Frames
Exit code: $exitCode

``````json
$jsonOutput
``````
"@ | Set-Content -LiteralPath $ArtifactPath -Encoding UTF8
}

if ($exitCode -ne 0) {
    exit $exitCode
}

exit 0
