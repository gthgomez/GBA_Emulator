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

# 5) --save-directory is created on demand and used for the .sav.
sdir_rom="$labDir/savedir.gba"
gen sram "$sdir_rom"
sdir="$labDir/custom-saves"
run_host --rom "$sdir_rom" --save-directory "$sdir" --quit-after 20 >/dev/null 2>&1
assert_sav "$sdir/savedir.gba.sav" 32768 0x00
echo "  assert ok: --save-directory created and used"

echo "run-desktop-save-smoke: PASS (persistence, reset, no-spurious-save, rejected-save preservation, save-directory)"
