param(
    [switch]$IncludeRomVideoSmoke,

    # Compile every verifier with -fsanitize=address,undefined (default OFF;
    # sanitized runs are slower and require the ASan/UBSan runtimes).
    [switch]$Sanitize
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$buildDir = Join-Path $repoRoot "build"
$includeDir = Join-Path $repoRoot "include"
$scriptPath = $PSCommandPath

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$includeHeaders = @()
if (Test-Path -LiteralPath $includeDir) {
    $includeHeaders = @(Get-ChildItem -LiteralPath $includeDir -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in @(".hpp", ".h", ".hh", ".hxx") })
}

function Resolve-SourcePaths {
    param([string[]]$RelativePaths)
    foreach ($relative in $RelativePaths) {
        Join-Path $repoRoot $relative
    }
}

function Assert-NativeExitCode {
    param(
        [string]$Step,
        [int]$Expected = 0
    )

    if ($LASTEXITCODE -ne $Expected) {
        Write-Host ""
        Write-Host "run-core-tests: FAIL ($Step exited with $LASTEXITCODE)"
        exit $LASTEXITCODE
    }
}

function Get-SourceManifestPath {
    param([string]$ExePath)
    return "$ExePath.sources"
}

function Test-SourceManifestMatches {
    param(
        [string]$ManifestPath,
        [string[]]$RelativeSources,
        [string]$CompileModeTag = ""
    )

    if (-not (Test-Path -LiteralPath $ManifestPath)) {
        return $false
    }

    $expectedSources = @($RelativeSources | Sort-Object)
    if (-not [string]::IsNullOrEmpty($CompileModeTag)) {
        $expectedSources = @($CompileModeTag) + $expectedSources
    }
    $actual = @(Get-Content -LiteralPath $ManifestPath -ErrorAction Stop)
    if ($actual.Count -ne $expectedSources.Count) {
        return $false
    }

    for ($i = 0; $i -lt $expectedSources.Count; $i++) {
        if ($actual[$i] -ne $expectedSources[$i]) {
            return $false
        }
    }

    return $true
}

function Write-SourceManifest {
    param(
        [string]$ManifestPath,
        [string[]]$RelativeSources,
        [string]$CompileModeTag = ""
    )

    $lines = @($RelativeSources | Sort-Object)
    if (-not [string]::IsNullOrEmpty($CompileModeTag)) {
        # Tagged first line forces a rebuild when sanitizer mode flips, so a
        # sanitized request can never silently rerun a plain binary (or vice
        # versa). Untagged manifests keep the legacy format for default runs.
        $lines = @($CompileModeTag) + $lines
    }
    $lines | Set-Content -LiteralPath $ManifestPath -Encoding utf8
}

function Test-NeedsCompile {
    param(
        [string]$ExePath,
        [string[]]$RelativeSources,
        [string[]]$ResolvedSources,
        [string]$CompileModeTag = ""
    )

    if (-not (Test-Path -LiteralPath $ExePath)) {
        return $true
    }

    if (-not (Test-SourceManifestMatches -ManifestPath (Get-SourceManifestPath -ExePath $ExePath) -RelativeSources $RelativeSources -CompileModeTag $CompileModeTag)) {
        return $true
    }

    $exeTime = (Get-Item -LiteralPath $ExePath).LastWriteTimeUtc

    if ((Get-Item -LiteralPath $scriptPath).LastWriteTimeUtc -gt $exeTime) {
        return $true
    }

    foreach ($source in $ResolvedSources) {
        if (-not (Test-Path -LiteralPath $source)) {
            return $true
        }
        if ((Get-Item -LiteralPath $source).LastWriteTimeUtc -gt $exeTime) {
            return $true
        }
    }

    foreach ($header in $includeHeaders) {
        if ($header.LastWriteTimeUtc -gt $exeTime) {
            return $true
        }
    }

    return $false
}

function Invoke-CoreTest {
    param(
        [string]$Name,
        [string[]]$Sources,
        [int]$Index,
        [int]$Total,
        [ref]$CompileElapsed,
        [ref]$RunElapsed
    )

    $exePath = Join-Path $buildDir "$Name.exe"
    $resolvedSources = @(Resolve-SourcePaths -RelativePaths $Sources)
    $manifestPath = Get-SourceManifestPath -ExePath $exePath
    # Tag distinguishes sanitized from plain builds in the source manifest so
    # flipping -Sanitize invalidates stale binaries in either direction.
    $compileModeTag = if ($Sanitize) { "sanitize:address,undefined" } else { "" }

    if (Test-NeedsCompile -ExePath $exePath -RelativeSources $Sources -ResolvedSources $resolvedSources -CompileModeTag $compileModeTag) {
        Write-Host "[$Index/$Total] compile $Name ..."
        $compileSw = [System.Diagnostics.Stopwatch]::StartNew()
        $sanitizeFlags = @()
        if ($Sanitize) {
            $sanitizeFlags = @("-fsanitize=address", "-fsanitize=undefined")
        }
        & g++ -std=c++17 -Wall -Wextra -Werror `
            -I $includeDir `
            @sanitizeFlags `
            @resolvedSources `
            -o $exePath
        Assert-NativeExitCode -Step "g++ $Name"
        Write-SourceManifest -ManifestPath $manifestPath -RelativeSources $Sources -CompileModeTag $compileModeTag
        $compileSw.Stop()
        $CompileElapsed.Value = $CompileElapsed.Value.Add($compileSw.Elapsed)
    } else {
        Write-Host "[$Index/$Total] compile $Name (up-to-date, skip)"
    }

    Write-Host "[$Index/$Total] run $Name ..."
    $runSw = [System.Diagnostics.Stopwatch]::StartNew()
    & $exePath
    Assert-NativeExitCode -Step $Name
    $runSw.Stop()
    $RunElapsed.Value = $RunElapsed.Value.Add($runSw.Elapsed)
}

$coreTests = @(
    @{
        Name = "keypad_test"
        Sources = @(
            "src\core\arm7tdmi.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\keypad.cpp",
            "src\core\memory_bus.cpp",
            "src\core\wait_state_control.cpp",
            "tests\keypad_test.cpp"
        )
    },
    @{
        Name = "memory_bus_test"
        Sources = @(
            "src\core\arm7tdmi.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\memory_bus.cpp",
            "src\core\io_registers.cpp",
            "src\core\timers.cpp",
            "src\core\dma_controller.cpp",
            "src\core\ppu_timing.cpp",
            "src\core\apu.cpp",
            "src\core\keypad.cpp",
            "src\core\wait_state_control.cpp",
            "tests\memory_bus_test.cpp"
        )
    },
    @{
        Name = "arm7tdmi_test"
        Sources = @(
            "src\core\arm7tdmi.cpp",
            "src\core\memory_bus.cpp",
            "src\core\wait_state_control.cpp",
            "tests\arm7tdmi_test.cpp"
        )
    },
    @{
        Name = "timers_test"
        Sources = @(
            "src\core\arm7tdmi.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\memory_bus.cpp",
            "src\core\timers.cpp",
            "src\core\wait_state_control.cpp",
            "tests\timers_test.cpp"
        )
    },
    @{
        Name = "dma_test"
        Sources = @(
            "src\core\apu.cpp",
            "src\core\arm7tdmi.cpp",
            "src\core\dma_controller.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\memory_bus.cpp",
            "src\core\wait_state_control.cpp",
            "tests\dma_test.cpp"
        )
    },
    @{
        Name = "dma_master_time_test"
        Sources = @(
            "src\core\arm7tdmi.cpp",
            "src\core\apu.cpp",
            "src\core\bios.cpp",
            "src\core\core_scheduler.cpp",
            "src\core\core_session.cpp",
            "src\core\dma_controller.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\io_registers.cpp",
            "src\core\keypad.cpp",
            "src\core\memory_bus.cpp",
            "src\core\ppu_timing.cpp",
            "src\core\timers.cpp",
            "src\core\wait_state_control.cpp",
            "tests\dma_master_time_test.cpp"
        )
    },
    @{
        Name = "ppu_timing_test"
        Sources = @(
            "src\core\arm7tdmi.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\memory_bus.cpp",
            "src\core\ppu_timing.cpp",
            "src\core\wait_state_control.cpp",
            "tests\ppu_timing_test.cpp"
        )
    },
    @{
        Name = "ppu_background_test"
        Sources = @(
            "src\core\memory_bus.cpp",
            "src\core\ppu_background.cpp",
            "src\core\wait_state_control.cpp",
            "tests\ppu_background_test.cpp"
        )
    },
    @{
        Name = "ppu_sprites_test"
        Sources = @(
            "src\core\memory_bus.cpp",
            "src\core\ppu_sprites.cpp",
            "src\core\wait_state_control.cpp",
            "tests\ppu_sprites_test.cpp"
        )
    },
    @{
        Name = "ppu_renderer_test"
        Sources = @(
            "src\core\memory_bus.cpp",
            "src\core\ppu_background.cpp",
            "src\core\ppu_sprites.cpp",
            "src\core\ppu_renderer.cpp",
            "src\core\wait_state_control.cpp",
            "tests\ppu_renderer_test.cpp"
        )
    },
    @{
        Name = "apu_test"
        Sources = @(
            "src\core\apu.cpp",
            "tests\apu_test.cpp"
        )
    },
    @{
        Name = "bios_test"
        Sources = @(
            "src\core\bios.cpp",
            "tests\bios_test.cpp"
        )
    },
    @{
        Name = "io_registers_test"
        Sources = @(
            "src\core\arm7tdmi.cpp",
            "src\core\apu.cpp",
            "src\core\dma_controller.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\io_registers.cpp",
            "src\core\keypad.cpp",
            "src\core\memory_bus.cpp",
            "src\core\ppu_timing.cpp",
            "src\core\timers.cpp",
            "src\core\wait_state_control.cpp",
            "tests\io_registers_test.cpp"
        )
    },
    @{
        Name = "core_scheduler_test"
        Sources = @(
            "src\core\arm7tdmi.cpp",
            "src\core\apu.cpp",
            "src\core\bios.cpp",
            "src\core\core_scheduler.cpp",
            "src\core\dma_controller.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\io_registers.cpp",
            "src\core\keypad.cpp",
            "src\core\memory_bus.cpp",
            "src\core\ppu_timing.cpp",
            "src\core\timers.cpp",
            "src\core\wait_state_control.cpp",
            "tests\core_scheduler_test.cpp"
        )
    },
    @{
        Name = "core_session_test"
        Sources = @(
            "src\core\arm7tdmi.cpp",
            "src\core\apu.cpp",
            "src\core\bios.cpp",
            "src\core\core_scheduler.cpp",
            "src\core\core_session.cpp",
            "src\core\dma_controller.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\io_registers.cpp",
            "src\core\keypad.cpp",
            "src\core\memory_bus.cpp",
            "src\core\ppu_timing.cpp",
            "src\core\timers.cpp",
            "src\core\wait_state_control.cpp",
            "tests\core_session_test.cpp"
        )
    },
    @{
        Name = "program_harness_test"
        Sources = @(
            "src\core\arm7tdmi.cpp",
            "src\core\apu.cpp",
            "src\core\bios.cpp",
            "src\core\core_scheduler.cpp",
            "src\core\core_session.cpp",
            "src\core\dma_controller.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\io_registers.cpp",
            "src\core\keypad.cpp",
            "src\core\memory_bus.cpp",
            "src\core\program_harness.cpp",
            "src\core\ppu_timing.cpp",
            "src\core\timers.cpp",
            "src\core\wait_state_control.cpp",
            "tests\program_harness_test.cpp"
        )
    },
    @{
        Name = "compatibility_corpus_test"
        Sources = @(
            "src\core\arm7tdmi.cpp",
            "src\core\apu.cpp",
            "src\core\bios.cpp",
            "src\core\compatibility_corpus.cpp",
            "src\core\core_scheduler.cpp",
            "src\core\core_session.cpp",
            "src\core\dma_controller.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\io_registers.cpp",
            "src\core\keypad.cpp",
            "src\core\memory_bus.cpp",
            "src\core\program_harness.cpp",
            "src\core\ppu_timing.cpp",
            "src\core\timers.cpp",
            "src\core\wait_state_control.cpp",
            "tests\compatibility_corpus_test.cpp"
        )
    },
    @{
        Name = "save_state_codec_test"
        Sources = @(
            "src\core\arm7tdmi.cpp",
            "src\core\apu.cpp",
            "src\core\bios.cpp",
            "src\core\core_scheduler.cpp",
            "src\core\core_session.cpp",
            "src\core\dma_controller.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\io_registers.cpp",
            "src\core\keypad.cpp",
            "src\core\memory_bus.cpp",
            "src\core\ppu_timing.cpp",
            "src\core\save_state_codec.cpp",
            "src\core\timers.cpp",
            "src\core\wait_state_control.cpp",
            "tests\save_state_codec_test.cpp"
        )
    },
    @{
        Name = "android_core_bridge_test"
        Sources = @(
            "src\core\android_core_bridge.cpp",
            "src\core\arm7tdmi.cpp",
            "src\core\apu.cpp",
            "src\core\bios.cpp",
            "src\core\core_scheduler.cpp",
            "src\core\core_session.cpp",
            "src\core\dma_controller.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\io_registers.cpp",
            "src\core\keypad.cpp",
            "src\core\memory_bus.cpp",
            "src\core\ppu_timing.cpp",
            "src\core\timers.cpp",
            "src\core\wait_state_control.cpp",
            "tests\android_core_bridge_test.cpp"
        )
    },
    @{
        Name = "android_runtime_test"
        Sources = @(
            "src\core\android_runtime.cpp",
            "src\core\arm7tdmi.cpp",
            "src\core\apu.cpp",
            "src\core\bios.cpp",
            "src\core\core_scheduler.cpp",
            "src\core\core_session.cpp",
            "src\core\dma_controller.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\io_registers.cpp",
            "src\core\keypad.cpp",
            "src\core\memory_bus.cpp",
            "src\core\ppu_background.cpp",
            "src\core\ppu_renderer.cpp",
            "src\core\ppu_sprites.cpp",
            "src\core\ppu_timing.cpp",
            "src\core\timers.cpp",
            "src\core\wait_state_control.cpp",
            "tests\android_runtime_test.cpp"
        )
    },
    @{
        Name = "game_boot_test"
        Sources = @(
            "src\core\arm7tdmi.cpp",
            "src\core\apu.cpp",
            "src\core\bios.cpp",
            "src\core\core_scheduler.cpp",
            "src\core\core_session.cpp",
            "src\core\dma_controller.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\io_registers.cpp",
            "src\core\keypad.cpp",
            "src\core\memory_bus.cpp",
            "src\core\ppu_timing.cpp",
            "src\core\timers.cpp",
            "src\core\wait_state_control.cpp",
            "tests\game_boot_test.cpp"
        )
    },
    @{
        Name = "hle_swi_boot_test"
        Sources = @(
            "src\core\arm7tdmi.cpp",
            "src\core\apu.cpp",
            "src\core\bios.cpp",
            "src\core\core_scheduler.cpp",
            "src\core\core_session.cpp",
            "src\core\dma_controller.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\io_registers.cpp",
            "src\core\keypad.cpp",
            "src\core\memory_bus.cpp",
            "src\core\ppu_timing.cpp",
            "src\core\timers.cpp",
            "src\core\wait_state_control.cpp",
            "tests\hle_swi_boot_test.cpp"
        )
    },
    @{
        Name = "hle_decompress_swi_test"
        Sources = @(
            "src\core\arm7tdmi.cpp",
            "src\core\apu.cpp",
            "src\core\bios.cpp",
            "src\core\core_scheduler.cpp",
            "src\core\core_session.cpp",
            "src\core\dma_controller.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\io_registers.cpp",
            "src\core\keypad.cpp",
            "src\core\memory_bus.cpp",
            "src\core\ppu_timing.cpp",
            "src\core\timers.cpp",
            "src\core\wait_state_control.cpp",
            "tests\hle_decompress_swi_test.cpp"
        )
    },
    @{
        Name = "android_performance_gate_test"
        Sources = @(
            "src\core\android_performance_gate.cpp",
            "src\core\android_runtime.cpp",
            "src\core\arm7tdmi.cpp",
            "src\core\apu.cpp",
            "src\core\bios.cpp",
            "src\core\core_scheduler.cpp",
            "src\core\core_session.cpp",
            "src\core\dma_controller.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\io_registers.cpp",
            "src\core\keypad.cpp",
            "src\core\memory_bus.cpp",
            "src\core\ppu_background.cpp",
            "src\core\ppu_renderer.cpp",
            "src\core\ppu_sprites.cpp",
            "src\core\ppu_timing.cpp",
            "src\core\timers.cpp",
            "src\core\wait_state_control.cpp",
            "tests\android_performance_gate_test.cpp"
        )
    },
    @{
        Name = "wait_state_control_test"
        Sources = @(
            "src\core\wait_state_control.cpp",
            "tests\wait_state_control_test.cpp"
        )
    },
    @{
        Name = "thumb_misfetch_recovery_test"
        Sources = @(
            "src\core\arm7tdmi.cpp",
            "src\core\apu.cpp",
            "src\core\bios.cpp",
            "src\core\core_scheduler.cpp",
            "src\core\dma_controller.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\io_registers.cpp",
            "src\core\keypad.cpp",
            "src\core\memory_bus.cpp",
            "src\core\ppu_timing.cpp",
            "src\core\timers.cpp",
            "src\core\wait_state_control.cpp",
            "tests\thumb_misfetch_recovery_test.cpp"
        )
    },
    @{
        Name = "thumb_open_bus_asymmetric_test"
        Sources = @(
            "src\core\arm7tdmi.cpp",
            "src\core\apu.cpp",
            "src\core\bios.cpp",
            "src\core\core_scheduler.cpp",
            "src\core\core_session.cpp",
            "src\core\dma_controller.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\io_registers.cpp",
            "src\core\keypad.cpp",
            "src\core\memory_bus.cpp",
            "src\core\ppu_timing.cpp",
            "src\core\timers.cpp",
            "src\core\wait_state_control.cpp",
            "tests\thumb_open_bus_asymmetric_test.cpp"
        )
    },
    @{
        Name = "io_read_open_bus_test"
        Sources = @(
            "src\core\arm7tdmi.cpp",
            "src\core\apu.cpp",
            "src\core\bios.cpp",
            "src\core\core_scheduler.cpp",
            "src\core\core_session.cpp",
            "src\core\dma_controller.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\io_registers.cpp",
            "src\core\keypad.cpp",
            "src\core\memory_bus.cpp",
            "src\core\ppu_timing.cpp",
            "src\core\timers.cpp",
            "src\core\wait_state_control.cpp",
            "tests\io_read_open_bus_test.cpp"
        )
    },
    @{
        Name = "dma_ppu_invariant_test"
        Sources = @(
            "src\core\arm7tdmi.cpp",
            "src\core\apu.cpp",
            "src\core\bios.cpp",
            "src\core\core_scheduler.cpp",
            "src\core\core_session.cpp",
            "src\core\dma_controller.cpp",
            "src\core\interrupt_controller.cpp",
            "src\core\io_registers.cpp",
            "src\core\keypad.cpp",
            "src\core\memory_bus.cpp",
            "src\core\ppu_timing.cpp",
            "src\core\timers.cpp",
            "src\core\wait_state_control.cpp",
            "tests\dma_ppu_invariant_test.cpp"
        )
    }
)

$overallSw = [System.Diagnostics.Stopwatch]::StartNew()
$compileElapsed = [TimeSpan]::Zero
$runElapsed = [TimeSpan]::Zero
$totalTests = $coreTests.Count
$index = 0

Write-Host "run-core-tests: starting $totalTests core verifiers (ROM video smoke: $(if ($IncludeRomVideoSmoke) { 'ON' } else { 'OFF (default)' }), sanitizers: $(if ($Sanitize) { 'ON' } else { 'OFF (default)' }))"

foreach ($test in $coreTests) {
    $index++
    Invoke-CoreTest `
        -Name $test.Name `
        -Sources $test.Sources `
        -Index $index `
        -Total $totalTests `
        -CompileElapsed ([ref]$compileElapsed) `
        -RunElapsed ([ref]$runElapsed)
}

$smokeElapsed = [TimeSpan]::Zero
if ($IncludeRomVideoSmoke) {
    $localRomVideoSmoke = Join-Path $PSScriptRoot "run-local-rom-video-smoke.ps1"
    if (Test-Path -LiteralPath $localRomVideoSmoke) {
        Write-Host "run-core-tests: ROM video smoke (optional gate enabled) ..."
        $smokeSw = [System.Diagnostics.Stopwatch]::StartNew()
        & $localRomVideoSmoke
        Assert-NativeExitCode -Step "run-local-rom-video-smoke"
        $smokeSw.Stop()
        $smokeElapsed = $smokeSw.Elapsed
    } else {
        # An explicitly requested optional gate that cannot run is a hard
        # failure, not a silent skip: CI must never report PASS here.
        Write-Host ""
        Write-Host "run-core-tests: FAIL (-IncludeRomVideoSmoke enabled but script not found: $localRomVideoSmoke)"
        exit 1
    }
} else {
    Write-Host "run-core-tests: ROM video smoke skipped (pass -IncludeRomVideoSmoke to enable)"
}

$overallSw.Stop()

function Format-Seconds {
    param([TimeSpan]$Duration)
    return "{0:N1}s" -f $Duration.TotalSeconds
}

Write-Host ""
Write-Host ("run-core-tests: PASS ({0} tests, compile {1}, run {2}, smoke {3}, total {4})" -f `
    $totalTests, `
    (Format-Seconds $compileElapsed), `
    (Format-Seconds $runElapsed), `
    (Format-Seconds $smokeElapsed), `
    (Format-Seconds $overallSw.Elapsed))
