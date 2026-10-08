#!/usr/bin/env bash
#
# run-core-tests.sh — portable Linux counterpart of run-core-tests.ps1
#
# Compiles and runs the local GBA core verifier binaries with:
#   g++ -std=c++17 -Wall -Wextra -Werror
#
# The test set, per-test source lists, compile flags, and incremental-build
# behavior mirror tools/run-core-tests.ps1 (PowerShell/Windows). Binaries are
# written to build/ with no .exe suffix. Exits non-zero on any compile or
# test failure.
#
# Usage:
#   ./tools/run-core-tests.sh              # compile + run all core verifiers
#   ./tools/run-core-tests.sh --sanitize   # add -fsanitize=address,undefined
#
# Requires: bash 4+, g++, find, date (GNU date preferred; BSD date falls back
# to second granularity for the timing summary only).
#
# Note: --sanitize sets ASAN_OPTIONS=allocator_may_return_null=1 so the
# core-bridge test's deliberate impossible-size allocation can take its
# intended bad_alloc path. Some libasan versions (e.g. GCC 13) still abort
# on allocations above their hard maximum regardless of that option; on
# such toolchains android_core_bridge_test cannot pass under --sanitize.
# The default (non-sanitized) mode is the CI baseline and passes all tests.

set -u

sanitize=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --sanitize)
            sanitize=1
            shift
            ;;
        -h|--help)
            sed -n '2,20p' "$0" | sed 's/^#\{1,2\} \{0,1\}//'
            exit 0
            ;;
        *)
            echo "run-core-tests: unknown option: $1" >&2
            exit 2
            ;;
    esac
done

repo_root=$(cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="$repo_root/build"
include_dir="$repo_root/include"
script_path=$(cd -- "$(dirname -- "$0")" && pwd)/$(basename -- "$0")

mkdir -p "$build_dir"

# Headers under include/ participate in staleness checks, matching the
# PowerShell runner (any header newer than a binary forces a rebuild).
include_headers=()
if [[ -d "$include_dir" ]]; then
    while IFS= read -r -d '' hdr; do
        include_headers+=("$hdr")
    done < <(find "$include_dir" -type f \( -name '*.hpp' -o -name '*.h' -o -name '*.hh' -o -name '*.hxx' \) -print0)
fi

# Test table: name|source|source|... (relative to repo root, forward slashes).
# Mirrors $coreTests in tools/run-core-tests.ps1.
core_tests=(
    "keypad_test|src/core/arm7tdmi.cpp|src/core/interrupt_controller.cpp|src/core/keypad.cpp|src/core/memory_bus.cpp|src/core/wait_state_control.cpp|tests/keypad_test.cpp"
    "memory_bus_test|src/core/arm7tdmi.cpp|src/core/interrupt_controller.cpp|src/core/memory_bus.cpp|src/core/io_registers.cpp|src/core/timers.cpp|src/core/dma_controller.cpp|src/core/ppu_timing.cpp|src/core/apu.cpp|src/core/keypad.cpp|src/core/wait_state_control.cpp|tests/memory_bus_test.cpp"
    "arm7tdmi_test|src/core/arm7tdmi.cpp|src/core/memory_bus.cpp|src/core/wait_state_control.cpp|tests/arm7tdmi_test.cpp"
    "timers_test|src/core/arm7tdmi.cpp|src/core/interrupt_controller.cpp|src/core/memory_bus.cpp|src/core/timers.cpp|src/core/wait_state_control.cpp|tests/timers_test.cpp"
    "dma_test|src/core/apu.cpp|src/core/arm7tdmi.cpp|src/core/dma_controller.cpp|src/core/interrupt_controller.cpp|src/core/memory_bus.cpp|src/core/wait_state_control.cpp|tests/dma_test.cpp"
    "dma_master_time_test|src/core/arm7tdmi.cpp|src/core/apu.cpp|src/core/bios.cpp|src/core/core_scheduler.cpp|src/core/core_session.cpp|src/core/dma_controller.cpp|src/core/interrupt_controller.cpp|src/core/io_registers.cpp|src/core/keypad.cpp|src/core/memory_bus.cpp|src/core/ppu_timing.cpp|src/core/timers.cpp|src/core/wait_state_control.cpp|tests/dma_master_time_test.cpp"
    "ppu_timing_test|src/core/arm7tdmi.cpp|src/core/interrupt_controller.cpp|src/core/memory_bus.cpp|src/core/ppu_timing.cpp|src/core/wait_state_control.cpp|tests/ppu_timing_test.cpp"
    "ppu_background_test|src/core/memory_bus.cpp|src/core/ppu_background.cpp|src/core/wait_state_control.cpp|tests/ppu_background_test.cpp"
    "ppu_sprites_test|src/core/memory_bus.cpp|src/core/ppu_sprites.cpp|src/core/wait_state_control.cpp|tests/ppu_sprites_test.cpp"
    "ppu_renderer_test|src/core/memory_bus.cpp|src/core/ppu_background.cpp|src/core/ppu_sprites.cpp|src/core/ppu_renderer.cpp|src/core/wait_state_control.cpp|tests/ppu_renderer_test.cpp"
    "apu_test|src/core/apu.cpp|tests/apu_test.cpp"
    "bios_test|src/core/bios.cpp|tests/bios_test.cpp"
    "io_registers_test|src/core/arm7tdmi.cpp|src/core/apu.cpp|src/core/dma_controller.cpp|src/core/interrupt_controller.cpp|src/core/io_registers.cpp|src/core/keypad.cpp|src/core/memory_bus.cpp|src/core/ppu_timing.cpp|src/core/timers.cpp|src/core/wait_state_control.cpp|tests/io_registers_test.cpp"
    "core_scheduler_test|src/core/arm7tdmi.cpp|src/core/apu.cpp|src/core/bios.cpp|src/core/core_scheduler.cpp|src/core/dma_controller.cpp|src/core/interrupt_controller.cpp|src/core/io_registers.cpp|src/core/keypad.cpp|src/core/memory_bus.cpp|src/core/ppu_timing.cpp|src/core/timers.cpp|src/core/wait_state_control.cpp|tests/core_scheduler_test.cpp"
    "branch_to_self_test|src/core/arm7tdmi.cpp|src/core/apu.cpp|src/core/bios.cpp|src/core/core_scheduler.cpp|src/core/core_session.cpp|src/core/dma_controller.cpp|src/core/interrupt_controller.cpp|src/core/io_registers.cpp|src/core/keypad.cpp|src/core/memory_bus.cpp|src/core/ppu_timing.cpp|src/core/timers.cpp|src/core/wait_state_control.cpp|tests/branch_to_self_test.cpp"
    "core_session_test|src/core/arm7tdmi.cpp|src/core/apu.cpp|src/core/bios.cpp|src/core/core_scheduler.cpp|src/core/core_session.cpp|src/core/dma_controller.cpp|src/core/interrupt_controller.cpp|src/core/io_registers.cpp|src/core/keypad.cpp|src/core/memory_bus.cpp|src/core/ppu_timing.cpp|src/core/timers.cpp|src/core/wait_state_control.cpp|tests/core_session_test.cpp"
    "program_harness_test|src/core/arm7tdmi.cpp|src/core/apu.cpp|src/core/bios.cpp|src/core/core_scheduler.cpp|src/core/core_session.cpp|src/core/dma_controller.cpp|src/core/interrupt_controller.cpp|src/core/io_registers.cpp|src/core/keypad.cpp|src/core/memory_bus.cpp|src/core/program_harness.cpp|src/core/ppu_timing.cpp|src/core/timers.cpp|src/core/wait_state_control.cpp|tests/program_harness_test.cpp"
    "compatibility_corpus_test|src/core/arm7tdmi.cpp|src/core/apu.cpp|src/core/bios.cpp|src/core/compatibility_corpus.cpp|src/core/core_scheduler.cpp|src/core/core_session.cpp|src/core/dma_controller.cpp|src/core/interrupt_controller.cpp|src/core/io_registers.cpp|src/core/keypad.cpp|src/core/memory_bus.cpp|src/core/program_harness.cpp|src/core/ppu_timing.cpp|src/core/timers.cpp|src/core/wait_state_control.cpp|tests/compatibility_corpus_test.cpp"
    "save_state_codec_test|src/core/arm7tdmi.cpp|src/core/apu.cpp|src/core/bios.cpp|src/core/core_scheduler.cpp|src/core/core_session.cpp|src/core/dma_controller.cpp|src/core/interrupt_controller.cpp|src/core/io_registers.cpp|src/core/keypad.cpp|src/core/memory_bus.cpp|src/core/ppu_timing.cpp|src/core/save_state_codec.cpp|src/core/timers.cpp|src/core/wait_state_control.cpp|tests/save_state_codec_test.cpp"
    "android_core_bridge_test|src/core/android_core_bridge.cpp|src/core/arm7tdmi.cpp|src/core/apu.cpp|src/core/bios.cpp|src/core/core_scheduler.cpp|src/core/core_session.cpp|src/core/dma_controller.cpp|src/core/interrupt_controller.cpp|src/core/io_registers.cpp|src/core/keypad.cpp|src/core/memory_bus.cpp|src/core/ppu_timing.cpp|src/core/timers.cpp|src/core/wait_state_control.cpp|tests/android_core_bridge_test.cpp"
    "android_runtime_test|src/core/android_runtime.cpp|src/core/arm7tdmi.cpp|src/core/apu.cpp|src/core/bios.cpp|src/core/core_scheduler.cpp|src/core/core_session.cpp|src/core/dma_controller.cpp|src/core/interrupt_controller.cpp|src/core/io_registers.cpp|src/core/keypad.cpp|src/core/memory_bus.cpp|src/core/ppu_background.cpp|src/core/ppu_renderer.cpp|src/core/ppu_sprites.cpp|src/core/ppu_timing.cpp|src/core/timers.cpp|src/core/wait_state_control.cpp|tests/android_runtime_test.cpp"
    "game_boot_test|src/core/arm7tdmi.cpp|src/core/apu.cpp|src/core/bios.cpp|src/core/core_scheduler.cpp|src/core/core_session.cpp|src/core/dma_controller.cpp|src/core/interrupt_controller.cpp|src/core/io_registers.cpp|src/core/keypad.cpp|src/core/memory_bus.cpp|src/core/ppu_timing.cpp|src/core/timers.cpp|src/core/wait_state_control.cpp|tests/game_boot_test.cpp"
    "hle_swi_boot_test|src/core/arm7tdmi.cpp|src/core/apu.cpp|src/core/bios.cpp|src/core/core_scheduler.cpp|src/core/core_session.cpp|src/core/dma_controller.cpp|src/core/interrupt_controller.cpp|src/core/io_registers.cpp|src/core/keypad.cpp|src/core/memory_bus.cpp|src/core/ppu_timing.cpp|src/core/timers.cpp|src/core/wait_state_control.cpp|tests/hle_swi_boot_test.cpp"
    "hle_decompress_swi_test|src/core/arm7tdmi.cpp|src/core/apu.cpp|src/core/bios.cpp|src/core/core_scheduler.cpp|src/core/core_session.cpp|src/core/dma_controller.cpp|src/core/interrupt_controller.cpp|src/core/io_registers.cpp|src/core/keypad.cpp|src/core/memory_bus.cpp|src/core/ppu_timing.cpp|src/core/timers.cpp|src/core/wait_state_control.cpp|tests/hle_decompress_swi_test.cpp"
    "android_performance_gate_test|src/core/android_performance_gate.cpp|src/core/android_runtime.cpp|src/core/arm7tdmi.cpp|src/core/apu.cpp|src/core/bios.cpp|src/core/core_scheduler.cpp|src/core/core_session.cpp|src/core/dma_controller.cpp|src/core/interrupt_controller.cpp|src/core/io_registers.cpp|src/core/keypad.cpp|src/core/memory_bus.cpp|src/core/ppu_background.cpp|src/core/ppu_renderer.cpp|src/core/ppu_sprites.cpp|src/core/ppu_timing.cpp|src/core/timers.cpp|src/core/wait_state_control.cpp|tests/android_performance_gate_test.cpp"
    "wait_state_control_test|src/core/wait_state_control.cpp|tests/wait_state_control_test.cpp"
    "thumb_misfetch_recovery_test|src/core/arm7tdmi.cpp|src/core/apu.cpp|src/core/bios.cpp|src/core/core_scheduler.cpp|src/core/dma_controller.cpp|src/core/interrupt_controller.cpp|src/core/io_registers.cpp|src/core/keypad.cpp|src/core/memory_bus.cpp|src/core/ppu_timing.cpp|src/core/timers.cpp|src/core/wait_state_control.cpp|tests/thumb_misfetch_recovery_test.cpp"
    "thumb_open_bus_asymmetric_test|src/core/arm7tdmi.cpp|src/core/apu.cpp|src/core/bios.cpp|src/core/core_scheduler.cpp|src/core/core_session.cpp|src/core/dma_controller.cpp|src/core/interrupt_controller.cpp|src/core/io_registers.cpp|src/core/keypad.cpp|src/core/memory_bus.cpp|src/core/ppu_timing.cpp|src/core/timers.cpp|src/core/wait_state_control.cpp|tests/thumb_open_bus_asymmetric_test.cpp"
    "io_read_open_bus_test|src/core/arm7tdmi.cpp|src/core/apu.cpp|src/core/bios.cpp|src/core/core_scheduler.cpp|src/core/core_session.cpp|src/core/dma_controller.cpp|src/core/interrupt_controller.cpp|src/core/io_registers.cpp|src/core/keypad.cpp|src/core/memory_bus.cpp|src/core/ppu_timing.cpp|src/core/timers.cpp|src/core/wait_state_control.cpp|tests/io_read_open_bus_test.cpp"
    "dma_ppu_invariant_test|src/core/arm7tdmi.cpp|src/core/apu.cpp|src/core/bios.cpp|src/core/core_scheduler.cpp|src/core/core_session.cpp|src/core/dma_controller.cpp|src/core/interrupt_controller.cpp|src/core/io_registers.cpp|src/core/keypad.cpp|src/core/memory_bus.cpp|src/core/ppu_timing.cpp|src/core/timers.cpp|src/core/wait_state_control.cpp|tests/dma_ppu_invariant_test.cpp"
)

# Tag distinguishes sanitized from plain builds in the source manifest so
# flipping --sanitize invalidates stale binaries in either direction.
compile_tag=""
if [[ "$sanitize" -eq 1 ]]; then
    compile_tag="sanitize:address,undefined"
    # The core-bridge test deliberately requests an impossible ROM size to
    # force std::bad_alloc; ASan's default allocator aborts before the exception
    # can propagate. Let the allocator return null instead so the test exercises
    # its intended bad_alloc -> internal_error path.
    export ASAN_OPTIONS="${ASAN_OPTIONS:-}allocator_may_return_null=1"
fi

now_ns() {
    local ns
    ns=$(date +%s%N 2>/dev/null) || true
    if [[ "$ns" =~ ^[0-9]+$ ]]; then
        printf '%s' "$ns"
    else
        # BSD date has no %N: fall back to seconds (timing summary only).
        printf '%s000' "$(date +%s)"
    fi
}

fmt_seconds() {
    local ms=$1
    printf '%d.%ds' "$((ms / 1000))" "$((ms % 1000 / 100))"
}

# manifest_matches <manifest> <tag> <relative-sources...>
# Expected content: tag line first (when set), then sorted relative sources.
manifest_matches() {
    local manifest="$1" tag="$2"
    shift 2
    [[ -f "$manifest" ]] || return 1
    local expected actual
    if [[ -n "$tag" ]]; then
        expected=$( { printf '%s\n' "$tag"; printf '%s\n' "$@" | LC_ALL=C sort; } )
    else
        expected=$(printf '%s\n' "$@" | LC_ALL=C sort)
    fi
    actual=$(cat "$manifest")
    [[ "$actual" == "$expected" ]]
}

write_manifest() {
    local manifest="$1" tag="$2"
    shift 2
    if [[ -n "$tag" ]]; then
        { printf '%s\n' "$tag"; printf '%s\n' "$@" | LC_ALL=C sort; } > "$manifest"
    else
        printf '%s\n' "$@" | LC_ALL=C sort > "$manifest"
    fi
}

# needs_compile <exe> <relative-sources...>
# Rebuild when the binary is missing, the manifest is missing/mismatched, the
# runner script is newer, or any source/header is newer than the binary.
needs_compile() {
    local exe="$1"
    shift
    [[ -f "$exe" ]] || return 0
    if ! manifest_matches "$exe.sources" "$compile_tag" "$@"; then
        return 0
    fi
    [[ "$script_path" -nt "$exe" ]] && return 0
    local src
    for src in "$@"; do
        [[ -f "$repo_root/$src" ]] || return 0
        [[ "$repo_root/$src" -nt "$exe" ]] && return 0
    done
    if [[ ${#include_headers[@]} -gt 0 ]]; then
        local hdr
        for hdr in "${include_headers[@]}"; do
            [[ "$hdr" -nt "$exe" ]] && return 0
        done
    fi
    return 1
}

total=${#core_tests[@]}
index=0
compile_ms=0
run_ms=0
overall_start=$(now_ns)

echo "run-core-tests: starting $total core verifiers (sanitizers: $(if [[ "$sanitize" -eq 1 ]]; then echo 'ON'; else echo 'OFF (default)'; fi))"

for entry in "${core_tests[@]}"; do
    index=$((index + 1))
    IFS='|' read -ra fields <<< "$entry"
    name=${fields[0]}
    rel_sources=("${fields[@]:1}")

    exe="$build_dir/$name"

    # Absolute paths for compile + staleness checks; relative paths for the manifest.
    abs_sources=()
    for src in "${rel_sources[@]}"; do
        abs_sources+=("$repo_root/$src")
    done

    if needs_compile "$exe" "${rel_sources[@]}"; then
        echo "[$index/$total] compile $name ..."
        start=$(now_ns)
        sanitize_flags=()
        if [[ "$sanitize" -eq 1 ]]; then
            sanitize_flags=(-fsanitize=address -fsanitize=undefined)
        fi
        g++ -std=c++17 -Wall -Wextra -Werror \
            -I "$include_dir" \
            "${sanitize_flags[@]}" \
            "${abs_sources[@]}" \
            -o "$exe"
        rc=$?
        end=$(now_ns)
        compile_ms=$((compile_ms + (end - start) / 1000000))
        if [[ $rc -ne 0 ]]; then
            echo ""
            echo "run-core-tests: FAIL (g++ $name exited with $rc)"
            exit "$rc"
        fi
        write_manifest "$exe.sources" "$compile_tag" "${rel_sources[@]}"
    else
        echo "[$index/$total] compile $name (up-to-date, skip)"
    fi

    echo "[$index/$total] run $name ..."
    start=$(now_ns)
    "$exe"
    rc=$?
    end=$(now_ns)
    run_ms=$((run_ms + (end - start) / 1000000))
    if [[ $rc -ne 0 ]]; then
        echo ""
        echo "run-core-tests: FAIL ($name exited with $rc)"
        exit "$rc"
    fi
done

overall_end=$(now_ns)
overall_ms=$(( (overall_end - overall_start) / 1000000 ))

echo ""
echo "run-core-tests: PASS ($total tests, compile $(fmt_seconds "$compile_ms"), run $(fmt_seconds "$run_ms"), total $(fmt_seconds "$overall_ms"))"
