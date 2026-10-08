# Cartridge-save lifecycle smoke for the desktop host (Windows counterpart of
# run-desktop-save-smoke.sh).
#
# Exercises the P0 data-safety paths through the real SDL host using synthetic
# ROMs and temporary files only:
#   * a save is written on exit and reloaded on the next launch (progress
#     survives restart),
#   * a reset preserves cartridge-backed progress,
#   * a game without cartridge saving never creates a spurious empty .sav,
#   * an unrecognized/corrupt save is preserved (not destroyed) before the
#     host starts fresh.
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$labDir = Join-Path $repoRoot "build" "desktop-save-smoke"
if (Test-Path $labDir) { Remove-Item $labDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $labDir | Out-Null

$exe = Join-Path $repoRoot "build" "desktop" "apps" "desktop" "gba-desktop.exe"
if (-not (Test-Path $exe)) { $exe = Join-Path $repoRoot "build" "desktop" "gba-desktop.exe" }
if (-not (Test-Path $exe)) {
    Write-Host "run-desktop-save-smoke: FAIL (gba-desktop not built at $exe)"
    exit 1
}

$dll = Join-Path $repoRoot "external" "SDL3" "x86_64-w64-mingw32" "bin" "SDL3.dll"
if (Test-Path $dll) {
    Copy-Item $dll (Join-Path (Split-Path -Parent $exe) "SDL3.dll") -Force
}

$env:SDL_VIDEODRIVER = "dummy"
$env:SDL_AUDIODRIVER = "dummy"

function New-Fixture([string]$Kind, [string]$Output) {
    python (Join-Path $repoRoot "tools\make-synthetic-rom.py") --kind $Kind --output $Output
    if ($LASTEXITCODE -ne 0) { Write-Host "run-desktop-save-smoke: FAIL (fixture $Kind)"; exit 1 }
}

function Assert-Save([string]$Path, [int]$Size, [int]$Byte0) {
    $data = [System.IO.File]::ReadAllBytes($Path)
    if ($data.Length -ne $Size) { Write-Host "run-desktop-save-smoke: FAIL ($Path size $($data.Length) != $Size)"; exit 1 }
    if ($data[0] -ne $Byte0) { Write-Host "run-desktop-save-smoke: FAIL ($Path byte0 0x$('{0:X2}' -f $data[0]) != 0x$('{0:X2}' -f $Byte0))"; exit 1 }
    Write-Host "  assert ok: $Path ($($data.Length) bytes, byte0=0x$('{0:X2}' -f $data[0]))"
}

try {
    # 1) Save written on exit, reloaded on the next launch (0x00 -> 0x01).
    $rom = Join-Path $labDir "restart.gba"
    New-Fixture "sram" $rom
    & $exe --rom $rom --quit-after 20 *> $null
    if ($LASTEXITCODE -ne 0) { Write-Host "run-desktop-save-smoke: FAIL (restart run 1 exit $LASTEXITCODE)"; exit 1 }
    Assert-Save "$rom.sav" 32768 0

    $log2 = Join-Path $labDir "restart2.log"
    & $exe --rom $rom --quit-after 20 *> $log2
    if ($LASTEXITCODE -ne 0) { Write-Host "run-desktop-save-smoke: FAIL (restart run 2 exit $LASTEXITCODE)"; exit 1 }
    if (-not (Select-String -Path $log2 -SimpleMatch "loaded cartridge save" -Quiet)) {
        Write-Host "run-desktop-save-smoke: FAIL (second launch did not load the saved data)"; Get-Content $log2; exit 1
    }
    Assert-Save "$rom.sav" 32768 1

    # 2) Reset preserves cartridge-backed progress.
    $resetRom = Join-Path $labDir "reset.gba"
    New-Fixture "sram" $resetRom
    $resetLog = Join-Path $labDir "reset.log"
    & $exe --rom $resetRom --reset-after 10 --quit-after 20 *> $resetLog
    if ($LASTEXITCODE -ne 0) { Write-Host "run-desktop-save-smoke: FAIL (reset run exit $LASTEXITCODE)"; exit 1 }
    if (-not (Select-String -Path $resetLog -SimpleMatch "runtime reset (cartridge save preserved)" -Quiet)) {
        Write-Host "run-desktop-save-smoke: FAIL (reset did not report cartridge preservation)"; Get-Content $resetLog; exit 1
    }
    Assert-Save "$resetRom.sav" 32768 1

    # 3) A game with no cartridge save must not create a spurious (empty) .sav.
    $nosaveRom = Join-Path $labDir "nosave.gba"
    New-Fixture "video" $nosaveRom
    & $exe --rom $nosaveRom --quit-after 15 *> $null
    if ($LASTEXITCODE -ne 0) { Write-Host "run-desktop-save-smoke: FAIL (no-save run exit $LASTEXITCODE)"; exit 1 }
    if (Test-Path "$nosaveRom.sav") {
        Write-Host "run-desktop-save-smoke: FAIL (created a .sav for a game without cartridge saving)"; exit 1
    }
    Write-Host "  assert ok: no .sav created for a game without cartridge saving"

    # 4) An unrecognized save is preserved before the host starts fresh.
    $rejectRom = Join-Path $labDir "reject.gba"
    New-Fixture "sram" $rejectRom
    [System.IO.File]::WriteAllBytes("$rejectRom.sav", [System.Text.Encoding]::ASCII.GetBytes("JUNK!"))
    $rejectLog = Join-Path $labDir "reject.log"
    & $exe --rom $rejectRom --quit-after 20 *> $rejectLog
    if ($LASTEXITCODE -ne 0) { Write-Host "run-desktop-save-smoke: FAIL (reject run exit $LASTEXITCODE)"; exit 1 }
    if (-not (Select-String -Path $rejectLog -SimpleMatch "cartridge save rejected" -Quiet)) {
        Write-Host "run-desktop-save-smoke: FAIL (invalid save was not reported as rejected)"; Get-Content $rejectLog; exit 1
    }
    if (-not (Test-Path "$rejectRom.sav.rejected")) {
        Write-Host "run-desktop-save-smoke: FAIL (rejected save was not preserved)"; exit 1
    }
    if ((Get-Content -Raw "$rejectRom.sav.rejected") -ne "JUNK!") {
        Write-Host "run-desktop-save-smoke: FAIL (preserved save contents differ)"; exit 1
    }
    Assert-Save "$rejectRom.sav" 32768 0
    Write-Host "  assert ok: rejected save preserved at reject.gba.sav.rejected"

    # 5) --save-directory is created on demand and used for the .sav.
    $sdirRom = Join-Path $labDir "savedir.gba"
    New-Fixture "sram" $sdirRom
    $sdir = Join-Path $labDir "custom-saves"
    & $exe --rom $sdirRom --save-directory $sdir --quit-after 20 *> $null
    if ($LASTEXITCODE -ne 0) { Write-Host "run-desktop-save-smoke: FAIL (save-directory run exit $LASTEXITCODE)"; exit 1 }
    Assert-Save (Join-Path $sdir "savedir.gba.sav") 32768 0
    Write-Host "  assert ok: --save-directory created and used"
}
finally {
    Remove-Item Env:SDL_VIDEODRIVER -ErrorAction SilentlyContinue
    Remove-Item Env:SDL_AUDIODRIVER -ErrorAction SilentlyContinue
}

Write-Host "run-desktop-save-smoke: PASS (persistence, reset, no-spurious-save, rejected-save preservation)"
