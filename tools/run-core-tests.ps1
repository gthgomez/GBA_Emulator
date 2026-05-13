$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$buildDir = Join-Path $repoRoot "build"

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

g++ -std=c++17 -Wall -Wextra -Werror `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $repoRoot "src\core\arm7tdmi.cpp") `
  (Join-Path $repoRoot "src\core\interrupt_controller.cpp") `
  (Join-Path $repoRoot "src\core\keypad.cpp") `
  (Join-Path $repoRoot "src\core\memory_bus.cpp") `
  (Join-Path $repoRoot "src\core\wait_state_control.cpp") `
  (Join-Path $repoRoot "tests\keypad_test.cpp") `
  -o (Join-Path $buildDir "keypad_test.exe")

& (Join-Path $buildDir "keypad_test.exe")

g++ -std=c++17 -Wall -Wextra -Werror `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $repoRoot "src\core\memory_bus.cpp") `
  (Join-Path $repoRoot "src\core\wait_state_control.cpp") `
  (Join-Path $repoRoot "tests\memory_bus_test.cpp") `
  -o (Join-Path $buildDir "memory_bus_test.exe")

& (Join-Path $buildDir "memory_bus_test.exe")

g++ -std=c++17 -Wall -Wextra -Werror `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $repoRoot "src\core\arm7tdmi.cpp") `
  (Join-Path $repoRoot "src\core\memory_bus.cpp") `
  (Join-Path $repoRoot "src\core\wait_state_control.cpp") `
  (Join-Path $repoRoot "tests\arm7tdmi_test.cpp") `
  -o (Join-Path $buildDir "arm7tdmi_test.exe")

& (Join-Path $buildDir "arm7tdmi_test.exe")

g++ -std=c++17 -Wall -Wextra -Werror `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $repoRoot "src\core\arm7tdmi.cpp") `
  (Join-Path $repoRoot "src\core\interrupt_controller.cpp") `
  (Join-Path $repoRoot "src\core\memory_bus.cpp") `
  (Join-Path $repoRoot "src\core\timers.cpp") `
  (Join-Path $repoRoot "src\core\wait_state_control.cpp") `
  (Join-Path $repoRoot "tests\timers_test.cpp") `
  -o (Join-Path $buildDir "timers_test.exe")

& (Join-Path $buildDir "timers_test.exe")

g++ -std=c++17 -Wall -Wextra -Werror `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $repoRoot "src\core\apu.cpp") `
  (Join-Path $repoRoot "src\core\arm7tdmi.cpp") `
  (Join-Path $repoRoot "src\core\dma_controller.cpp") `
  (Join-Path $repoRoot "src\core\interrupt_controller.cpp") `
  (Join-Path $repoRoot "src\core\memory_bus.cpp") `
  (Join-Path $repoRoot "src\core\wait_state_control.cpp") `
  (Join-Path $repoRoot "tests\dma_test.cpp") `
  -o (Join-Path $buildDir "dma_test.exe")

& (Join-Path $buildDir "dma_test.exe")

g++ -std=c++17 -Wall -Wextra -Werror `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $repoRoot "src\core\arm7tdmi.cpp") `
  (Join-Path $repoRoot "src\core\interrupt_controller.cpp") `
  (Join-Path $repoRoot "src\core\memory_bus.cpp") `
  (Join-Path $repoRoot "src\core\ppu_timing.cpp") `
  (Join-Path $repoRoot "src\core\wait_state_control.cpp") `
  (Join-Path $repoRoot "tests\ppu_timing_test.cpp") `
  -o (Join-Path $buildDir "ppu_timing_test.exe")

& (Join-Path $buildDir "ppu_timing_test.exe")

g++ -std=c++17 -Wall -Wextra -Werror `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $repoRoot "src\core\memory_bus.cpp") `
  (Join-Path $repoRoot "src\core\ppu_background.cpp") `
  (Join-Path $repoRoot "src\core\wait_state_control.cpp") `
  (Join-Path $repoRoot "tests\ppu_background_test.cpp") `
  -o (Join-Path $buildDir "ppu_background_test.exe")

& (Join-Path $buildDir "ppu_background_test.exe")

g++ -std=c++17 -Wall -Wextra -Werror `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $repoRoot "src\core\memory_bus.cpp") `
  (Join-Path $repoRoot "src\core\ppu_sprites.cpp") `
  (Join-Path $repoRoot "src\core\wait_state_control.cpp") `
  (Join-Path $repoRoot "tests\ppu_sprites_test.cpp") `
  -o (Join-Path $buildDir "ppu_sprites_test.exe")

& (Join-Path $buildDir "ppu_sprites_test.exe")

g++ -std=c++17 -Wall -Wextra -Werror `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $repoRoot "src\core\memory_bus.cpp") `
  (Join-Path $repoRoot "src\core\ppu_background.cpp") `
  (Join-Path $repoRoot "src\core\ppu_sprites.cpp") `
  (Join-Path $repoRoot "src\core\ppu_renderer.cpp") `
  (Join-Path $repoRoot "src\core\wait_state_control.cpp") `
  (Join-Path $repoRoot "tests\ppu_renderer_test.cpp") `
  -o (Join-Path $buildDir "ppu_renderer_test.exe")

& (Join-Path $buildDir "ppu_renderer_test.exe")

g++ -std=c++17 -Wall -Wextra -Werror `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $repoRoot "src\core\apu.cpp") `
  (Join-Path $repoRoot "tests\apu_test.cpp") `
  -o (Join-Path $buildDir "apu_test.exe")

& (Join-Path $buildDir "apu_test.exe")

g++ -std=c++17 -Wall -Wextra -Werror `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $repoRoot "src\core\bios.cpp") `
  (Join-Path $repoRoot "tests\bios_test.cpp") `
  -o (Join-Path $buildDir "bios_test.exe")

& (Join-Path $buildDir "bios_test.exe")

g++ -std=c++17 -Wall -Wextra -Werror `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $repoRoot "src\core\arm7tdmi.cpp") `
  (Join-Path $repoRoot "src\core\apu.cpp") `
  (Join-Path $repoRoot "src\core\dma_controller.cpp") `
  (Join-Path $repoRoot "src\core\interrupt_controller.cpp") `
  (Join-Path $repoRoot "src\core\io_registers.cpp") `
  (Join-Path $repoRoot "src\core\keypad.cpp") `
  (Join-Path $repoRoot "src\core\memory_bus.cpp") `
  (Join-Path $repoRoot "src\core\ppu_timing.cpp") `
  (Join-Path $repoRoot "src\core\timers.cpp") `
  (Join-Path $repoRoot "src\core\wait_state_control.cpp") `
  (Join-Path $repoRoot "tests\io_registers_test.cpp") `
  -o (Join-Path $buildDir "io_registers_test.exe")

& (Join-Path $buildDir "io_registers_test.exe")

g++ -std=c++17 -Wall -Wextra -Werror `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $repoRoot "src\core\arm7tdmi.cpp") `
  (Join-Path $repoRoot "src\core\apu.cpp") `
  (Join-Path $repoRoot "src\core\bios.cpp") `
  (Join-Path $repoRoot "src\core\core_scheduler.cpp") `
  (Join-Path $repoRoot "src\core\dma_controller.cpp") `
  (Join-Path $repoRoot "src\core\interrupt_controller.cpp") `
  (Join-Path $repoRoot "src\core\io_registers.cpp") `
  (Join-Path $repoRoot "src\core\keypad.cpp") `
  (Join-Path $repoRoot "src\core\memory_bus.cpp") `
  (Join-Path $repoRoot "src\core\ppu_timing.cpp") `
  (Join-Path $repoRoot "src\core\timers.cpp") `
  (Join-Path $repoRoot "src\core\wait_state_control.cpp") `
  (Join-Path $repoRoot "tests\core_scheduler_test.cpp") `
  -o (Join-Path $buildDir "core_scheduler_test.exe")

& (Join-Path $buildDir "core_scheduler_test.exe")

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
  (Join-Path $repoRoot "tests\core_session_test.cpp") `
  -o (Join-Path $buildDir "core_session_test.exe")

& (Join-Path $buildDir "core_session_test.exe")

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
  (Join-Path $repoRoot "src\core\program_harness.cpp") `
  (Join-Path $repoRoot "src\core\ppu_timing.cpp") `
  (Join-Path $repoRoot "src\core\timers.cpp") `
  (Join-Path $repoRoot "src\core\wait_state_control.cpp") `
  (Join-Path $repoRoot "tests\program_harness_test.cpp") `
  -o (Join-Path $buildDir "program_harness_test.exe")

& (Join-Path $buildDir "program_harness_test.exe")

g++ -std=c++17 -Wall -Wextra -Werror `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $repoRoot "src\core\arm7tdmi.cpp") `
  (Join-Path $repoRoot "src\core\apu.cpp") `
  (Join-Path $repoRoot "src\core\bios.cpp") `
  (Join-Path $repoRoot "src\core\compatibility_corpus.cpp") `
  (Join-Path $repoRoot "src\core\core_scheduler.cpp") `
  (Join-Path $repoRoot "src\core\core_session.cpp") `
  (Join-Path $repoRoot "src\core\dma_controller.cpp") `
  (Join-Path $repoRoot "src\core\interrupt_controller.cpp") `
  (Join-Path $repoRoot "src\core\io_registers.cpp") `
  (Join-Path $repoRoot "src\core\keypad.cpp") `
  (Join-Path $repoRoot "src\core\memory_bus.cpp") `
  (Join-Path $repoRoot "src\core\program_harness.cpp") `
  (Join-Path $repoRoot "src\core\ppu_timing.cpp") `
  (Join-Path $repoRoot "src\core\timers.cpp") `
  (Join-Path $repoRoot "src\core\wait_state_control.cpp") `
  (Join-Path $repoRoot "tests\compatibility_corpus_test.cpp") `
  -o (Join-Path $buildDir "compatibility_corpus_test.exe")

& (Join-Path $buildDir "compatibility_corpus_test.exe")

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
  (Join-Path $repoRoot "src\core\save_state_codec.cpp") `
  (Join-Path $repoRoot "src\core\timers.cpp") `
  (Join-Path $repoRoot "src\core\wait_state_control.cpp") `
  (Join-Path $repoRoot "tests\save_state_codec_test.cpp") `
  -o (Join-Path $buildDir "save_state_codec_test.exe")

& (Join-Path $buildDir "save_state_codec_test.exe")

g++ -std=c++17 -Wall -Wextra -Werror `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $repoRoot "src\core\arm7tdmi.cpp") `
  (Join-Path $repoRoot "src\core\instruction_cache.cpp") `
  (Join-Path $repoRoot "src\core\memory_bus.cpp") `
  (Join-Path $repoRoot "src\core\wait_state_control.cpp") `
  (Join-Path $repoRoot "tests\instruction_cache_test.cpp") `
  -o (Join-Path $buildDir "instruction_cache_test.exe")

& (Join-Path $buildDir "instruction_cache_test.exe")

g++ -std=c++17 -Wall -Wextra -Werror `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $repoRoot "src\core\android_core_bridge.cpp") `
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
  (Join-Path $repoRoot "tests\android_core_bridge_test.cpp") `
  -o (Join-Path $buildDir "android_core_bridge_test.exe")

& (Join-Path $buildDir "android_core_bridge_test.exe")

g++ -std=c++17 -Wall -Wextra -Werror `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $repoRoot "src\core\android_runtime.cpp") `
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
  (Join-Path $repoRoot "tests\android_runtime_test.cpp") `
  -o (Join-Path $buildDir "android_runtime_test.exe")

& (Join-Path $buildDir "android_runtime_test.exe")

g++ -std=c++17 -Wall -Wextra -Werror `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $repoRoot "src\core\android_performance_gate.cpp") `
  (Join-Path $repoRoot "src\core\android_runtime.cpp") `
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
  (Join-Path $repoRoot "tests\android_performance_gate_test.cpp") `
  -o (Join-Path $buildDir "android_performance_gate_test.exe")

& (Join-Path $buildDir "android_performance_gate_test.exe")

g++ -std=c++17 -Wall -Wextra -Werror `
  -I (Join-Path $repoRoot "include") `
  (Join-Path $repoRoot "src\core\wait_state_control.cpp") `
  (Join-Path $repoRoot "tests\wait_state_control_test.cpp") `
  -o (Join-Path $buildDir "wait_state_control_test.exe")

& (Join-Path $buildDir "wait_state_control_test.exe")
