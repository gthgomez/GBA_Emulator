cmake_minimum_required(VERSION 3.24)

project(gba_emulator
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GBA_BUILD_DESKTOP "Build the desktop reference host" ON)
option(GBA_BUILD_TESTS "Build core verifiers under CTest (PowerShell runner stays authoritative)" OFF)
option(GBA_WARNINGS_AS_ERRORS "Compile core with -Werror" OFF)

find_package(SDL3 QUIET CONFIG)

if(GBA_BUILD_DESKTOP AND NOT SDL3_FOUND)
    message(FATAL_ERROR
        "SDL3 was not found. Run tools/setup-desktop-deps.ps1 (Windows/MinGW) "
        "or install SDL3 development files, then reconfigure with "
        "-DCMAKE_PREFIX_PATH pointing at the SDL3 install prefix.")
endif()

# One authoritative emulator engine: CPU/PPU/APU/bus/scheduler plus the
# platform-neutral runtime facade. Hosts (desktop, Android, lab) link this
# target only; platform APIs never enter the core.
add_library(gba_core STATIC
    src/core/android_core_bridge.cpp
    src/core/android_performance_gate.cpp
    src/core/android_runtime.cpp
    src/core/apu.cpp
    src/core/arm7tdmi.cpp
    src/core/bios.cpp
    src/core/compatibility_corpus.cpp
    src/core/core_scheduler.cpp
    src/core/core_session.cpp
    src/core/dma_controller.cpp
    src/core/interrupt_controller.cpp
    src/core/io_registers.cpp
    src/core/keypad.cpp
    src/core/memory_bus.cpp
    src/core/ppu_background.cpp
    src/core/ppu_renderer.cpp
    src/core/ppu_sprites.cpp
    src/core/ppu_timing.cpp
    src/core/program_harness.cpp
    src/core/save_state_codec.cpp
    src/core/timers.cpp
    src/core/wait_state_control.cpp
)

target_include_directories(gba_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
    target_compile_options(gba_core PRIVATE /W4)
else()
    target_compile_options(gba_core PRIVATE -Wall -Wextra -Wpedantic)
    if(GBA_WARNINGS_AS_ERRORS)
        target_compile_options(gba_core PRIVATE -Werror)
    endif()
endif()

if(GBA_BUILD_DESKTOP)
    add_subdirectory(apps/desktop)
endif()

if(GBA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
