#!/usr/bin/env bash
# Cartridge-save lifecycle smoke for the desktop host (Linux counterpart of
# run-desktop-save-smoke.ps1).
#
# Exercises the P0 data-safety paths through the real SDL host using synthetic
# ROMs and temporary files only:
#   * a save is written on exit and reloaded on the next launch (progress
#     survives restart),
#   * a reset preserves cartridge-backed progress,
#   * a game without cartridge saving never creates a spurious empty .sav,
#   * an unrecognized/corrupt save is preserved (not destroyed) before the
#     host starts fresh.
set -euo pipefail

repoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
labDir="$repoRoot/build/desktop-save-smoke"
rm -rf "$labDir"
mkdir -p "$labDir"

exe="$repoRoot/build/desktop/apps/desktop/gba-desktop"
[ -x "$exe" ] || exe="$repoRoot/build/desktop/gba-desktop"
if [ ! -x "$exe" ]; then
    echo "run-desktop-save-smoke: FAIL (gba-desktop not built at $exe)" >&2
    exit 1
fi

gen() {
    python3 "$repoRoot/tools/make-synthetic-rom.py" --kind "$1" --output "$2" >/dev/null
}

run_host() {
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$exe" "$@"
}

assert_sav() {
    python3 - "$1" "$2" "$3" <<'PY'
import sys
path, size, byte0 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3], 0)
data = open(path, "rb").read()
if len(data) != size:
    raise SystemExit(f"FAIL: {path} size {len(data)} != {size}")
if data[0] != byte0:
    raise SystemExit(f"FAIL: {path} byte0 0x{data[0]:02X} != 0x{byte0:02X}")
print(f"  assert ok: {path} ({len(data)} bytes, byte0=0x{data[0]:02X})")
PY
}

# 1) Save written on exit, reloaded on the next launch (0x00 -> 0x01).
rom="$labDir/restart.gba"
gen sram "$rom"
run_host --rom "$rom" --quit-after 20 >/dev/null 2>&1
assert_sav "$rom.sav" 32768 0x00

run_host --rom "$rom" --quit-after 20 >"$labDir/restart2.log" 2>&1
grep -q "loaded cartridge save" "$labDir/restart2.log" || {
    echo "run-desktop-save-smoke: FAIL (second launch did not load the saved data)" >&2
    cat "$labDir/restart2.log" >&2
    exit 1
}
assert_sav "$rom.sav" 32768 0x01

# 2) Reset preserves cartridge-backed progress (fresh save -> 0x00 by first
#    boot, then 0x01 because the reset retains the 0x00 state).
reset_rom="$labDir/reset.gba"
gen sram "$reset_rom"
run_host --rom "$reset_rom" --reset-after 10 --quit-after 20 >"$labDir/reset.log" 2>&1
grep -q "runtime reset (cartridge save preserved)" "$labDir/reset.log" || {
    echo "run-desktop-save-smoke: FAIL (reset did not report cartridge preservation)" >&2
    cat "$labDir/reset.log" >&2
    exit 1
}
assert_sav "$reset_rom.sav" 32768 0x01

# 3) A game with no cartridge save must not create a spurious (empty) .sav.
nosave_rom="$labDir/nosave.gba"
gen video "$nosave_rom"
run_host --rom "$nosave_rom" --quit-after 15 >/dev/null 2>&1
if [ -e "$nosave_rom.sav" ]; then
    echo "run-desktop-save-smoke: FAIL (created a .sav for a game without cartridge saving)" >&2
    exit 1
fi
echo "  assert ok: no .sav created for a game without cartridge saving"

# 4) An unrecognized save is preserved before the host starts fresh.
reject_rom="$labDir/reject.gba"
gen sram "$reject_rom"
printf 'JUNK!' > "$reject_rom.sav"   # wrong size -> import must be rejected
run_host --rom "$reject_rom" --quit-after 20 >"$labDir/reject.log" 2>&1
grep -q "cartridge save rejected" "$labDir/reject.log" || {
    echo "run-desktop-save-smoke: FAIL (invalid save was not reported as rejected)" >&2
    cat "$labDir/reject.log" >&2
    exit 1
}
[ -f "$reject_rom.sav.rejected" ] || {
    echo "run-desktop-save-smoke: FAIL (rejected save was not preserved)" >&2
    exit 1
}
if [ "$(cat "$reject_rom.sav.rejected")" != "JUNK!" ]; then
    echo "run-desktop-save-smoke: FAIL (preserved save contents differ)" >&2
    exit 1
fi
assert_sav "$reject_rom.sav" 32768 0x00
echo "  assert ok: rejected save preserved at $(basename "$reject_rom.sav.rejected")"

# 4b) When the rejected save cannot even be backed up, the host must go
#     fail-closed: writes are disabled and the original file is never
#     overwritten by a fresh save. Skipped as root, where write bits do not
#     restrict the emulator.
if [ "$(id -u)" -ne 0 ]; then
    undumpable_rom="$labDir/undumpable.gba"
    gen sram "$undumpable_rom"
    ro_saves="$labDir/undumpable-saves"
    mkdir -p "$ro_saves"
    printf 'JUNK!' > "$ro_saves/undumpable.gba.sav"
    chmod 555 "$ro_saves"
    set +e
    run_host --rom "$undumpable_rom" --save-directory "$ro_saves" --quit-after 20 \
        >"$labDir/undumpable.log" 2>&1
    undumpable_exit=$?
    set -e
    chmod 755 "$ro_saves"
    [ "$undumpable_exit" -eq 0 ] || {
        echo "run-desktop-save-smoke: FAIL (undumpable-save run exit $undumpable_exit)" >&2
        cat "$labDir/undumpable.log" >&2
        exit 1
    }
    grep -q "could not preserve a copy" "$labDir/undumpable.log" || {
        echo "run-desktop-save-smoke: FAIL (backup failure was not reported)" >&2
        cat "$labDir/undumpable.log" >&2
        exit 1
    }
    if [ "$(cat "$ro_saves/undumpable.gba.sav")" != "JUNK!" ]; then
        echo "run-desktop-save-smoke: FAIL (original rejected save was modified)" >&2
        exit 1
    fi
    [ ! -e "$ro_saves/undumpable.gba.sav.rejected" ] || {
        echo "run-desktop-save-smoke: FAIL (unexpected backup appeared in a read-only directory)" >&2
        exit 1
    }
    echo "  assert ok: undumpable rejected save left untouched (writes disabled)"
fi

# 5) --save-directory is created on demand and used for the .sav.
sdir_rom="$labDir/savedir.gba"
gen sram "$sdir_rom"
sdir="$labDir/custom-saves"
run_host --rom "$sdir_rom" --save-directory "$sdir" --quit-after 20 >/dev/null 2>&1
assert_sav "$sdir/savedir.gba.sav" 32768 0x00
echo "  assert ok: --save-directory created and used"

# 6) A ROM switch must be REFUSED when the outgoing save cannot be flushed;
#    the running session stays intact and no destructive step (not even the
#    rollback path) is reached. Skipped as root, where write bits do not
#    restrict the emulator.
if [ "$(id -u)" -ne 0 ]; then
    switch_rom="$labDir/switchA.gba"
    gen sram "$switch_rom"
    other_rom="$labDir/switchB.gba"
    gen bad "$other_rom"
    denied="$labDir/denied-saves"
    mkdir -p "$denied"
    chmod 555 "$denied"
    set +e
    run_host --rom "$switch_rom" --save-directory "$denied" --switch-after 10 \
        --switch-to "$other_rom" --quit-after 40 >"$labDir/switch.log" 2>&1
    switch_exit=$?
    set -e
    chmod 755 "$denied"
    [ "$switch_exit" -eq 0 ] || {
        echo "run-desktop-save-smoke: FAIL (switch-refusal run exit $switch_exit)" >&2
        cat "$labDir/switch.log" >&2
        exit 1
    }
    grep -q "ROM switch refused" "$labDir/switch.log" || {
        echo "run-desktop-save-smoke: FAIL (ROM switch was not refused despite a failed save flush)" >&2
        cat "$labDir/switch.log" >&2
        exit 1
    }
    ! grep -q "previous session restored" "$labDir/switch.log" || {
        echo "run-desktop-save-smoke: FAIL (switch reached the destructive step instead of refusing)" >&2
        cat "$labDir/switch.log" >&2
        exit 1
    }
    echo "  assert ok: ROM switch refused while the save flush fails"
fi

# 7) Switching to a defective ROM: the outgoing save (seeded 0x42, boot
#    increments to 0x43) is flushed at switch time -- the later engine-stop
#    flush targets the *new* ROM's path, so byte0=0x43 here proves the
#    switch-time flush carried the outgoing progress.
rollback_rom="$labDir/rollback.gba"
gen sram "$rollback_rom"
python3 - "$rollback_rom.sav" <<'PY'
import sys
data = bytearray(32768)
data[0] = 0x42
open(sys.argv[1], "wb").write(bytes(data))
PY
bad_switch_rom="$labDir/rollback-bad.gba"
gen bad "$bad_switch_rom"
set +e
run_host --rom "$rollback_rom" --switch-after 10 --switch-to "$bad_switch_rom" \
    --quit-after 40 >"$labDir/rollback.log" 2>&1
rollback_exit=$?
set -e
[ "$rollback_exit" -eq 3 ] || {
    echo "run-desktop-save-smoke: FAIL (defective-ROM switch exit $rollback_exit, expected 3)" >&2
    cat "$labDir/rollback.log" >&2
    exit 1
}
grep -q "engine stopped" "$labDir/rollback.log" || {
    echo "run-desktop-save-smoke: FAIL (defective ROM did not stop the engine)" >&2
    cat "$labDir/rollback.log" >&2
    exit 1
}
assert_sav "$rollback_rom.sav" 32768 0x43
echo "  assert ok: defective-ROM switch flushes the outgoing save and exits 3"

# 7b) An unreadable replacement ROM never reaches the engine: the switch is
#     refused and the running session continues to a clean exit.
keep_rom="$labDir/keeprunning.gba"
gen sram "$keep_rom"
missing_rom="$labDir/missing.gba"
run_host --rom "$keep_rom" --switch-after 10 --switch-to "$missing_rom" \
    --quit-after 40 >"$labDir/keeprunning.log" 2>&1
grep -q "cannot read ROM" "$labDir/keeprunning.log" || {
    echo "run-desktop-save-smoke: FAIL (unreadable replacement was not reported)" >&2
    cat "$labDir/keeprunning.log" >&2
    exit 1
}
assert_sav "$keep_rom.sav" 32768 0x00
echo "  assert ok: unreadable replacement keeps the session running"

# 8) A crash mid-replacement leaves the save only as the `.old` sibling.
#    The next launch must restore and import it instead of starting fresh.
orphan_rom="$labDir/orphan.gba"
gen sram "$orphan_rom"
python3 - "$orphan_rom.sav.old" <<'PY'
import sys
data = bytearray(32768)
data[0] = 0x42
open(sys.argv[1], "wb").write(bytes(data))
PY
run_host --rom "$orphan_rom" --quit-after 20 >"$labDir/orphan.log" 2>&1
grep -q "recovered interrupted save write" "$labDir/orphan.log" || {
    echo "run-desktop-save-smoke: FAIL (crash orphan was not recovered)" >&2
    cat "$labDir/orphan.log" >&2
    exit 1
}
assert_sav "$orphan_rom.sav" 32768 0x43
[ ! -e "$orphan_rom.sav.old" ] || {
    echo "run-desktop-save-smoke: FAIL (recovered orphan copy was not consumed)" >&2
    exit 1
}
echo "  assert ok: crash-orphan save restored, imported, and superseded"

python3 "$repoRoot/tools/check-desktop-save-type-override.py" --exe "$exe"

echo "run-desktop-save-smoke: PASS (persistence, reset, no-spurious-save, rejected-save preservation, save-directory, switch refusal, defective switch, crash-orphan recovery, save-type override)"
exit 0
