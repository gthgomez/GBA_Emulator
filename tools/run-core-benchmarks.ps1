#requires -Version 7.3

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$buildDir = Join-Path $repoRoot "build"

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

function Assert-NativeExitCode {
    param(
        [string]$Step,
        [int]$Expected = 0
    )

    if ($LASTEXITCODE -ne $Expected) {
        Write-Host ""
        Write-Host "core_benchmark: FAIL ($Step exited with $LASTEXITCODE)"
        if ($null -eq $LASTEXITCODE) {
            exit 1
        }
        exit $LASTEXITCODE
    }
}

g++ -std=c++17 -O2 -DNDEBUG -Wall -Wextra -Werror `
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
  (Join-Path $repoRoot "src\core\ppu_sprites.cpp") `
  (Join-Path $repoRoot "src\core\ppu_timing.cpp") `
  (Join-Path $repoRoot "src\core\timers.cpp") `
  (Join-Path $repoRoot "src\core\wait_state_control.cpp") `
  (Join-Path $repoRoot "benchmarks\core_benchmark.cpp") `
  -o (Join-Path $buildDir "core_benchmark.exe")

# A failed g++ must never fall through to rerun a stale benchmark binary.
Assert-NativeExitCode -Step "g++ core_benchmark"

& (Join-Path $buildDir "core_benchmark.exe")
Assert-NativeExitCode -Step "core_benchmark.exe"

# Explicit terminal exit code for orchestrators asserting on $LASTEXITCODE.
exit 0
