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

function Invoke-Icacls([string[]]$IcaclsArgs, [string]$Step) {
    icacls @IcaclsArgs | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "run-desktop-save-smoke: FAIL (icacls $Step exit ${LASTEXITCODE}: $($IcaclsArgs -join ' '))"
        exit 1
    }
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

    # 4b) When the rejected save cannot even be backed up, the host must go
    #     fail-closed: writes are disabled and the original file is never
    #     overwritten by a fresh save.
    $undumpableRom = Join-Path $labDir "undumpable.gba"
    New-Fixture "sram" $undumpableRom
    $roSaves = Join-Path $labDir "undumpable-saves"
    New-Item -ItemType Directory -Force -Path $roSaves | Out-Null
    [System.IO.File]::WriteAllBytes((Join-Path $roSaves "undumpable.gba.sav"),
        [System.Text.Encoding]::ASCII.GetBytes("JUNK!"))
    Invoke-Icacls @("$roSaves", "/deny", "${env:USERNAME}:(W)") "deny"
    try {
        $undumpableLog = Join-Path $labDir "undumpable.log"
        & $exe --rom $undumpableRom --save-directory $roSaves --quit-after 20 *> $undumpableLog
        if ($LASTEXITCODE -ne 0) {
            Write-Host "run-desktop-save-smoke: FAIL (undumpable-save run exit $LASTEXITCODE)"
            Get-Content $undumpableLog
            exit 1
        }
        if (-not (Select-String -Path $undumpableLog -SimpleMatch "could not preserve a copy" -Quiet)) {
            Write-Host "run-desktop-save-smoke: FAIL (backup failure was not reported)"
            Get-Content $undumpableLog
            exit 1
        }
        if ((Get-Content -Raw (Join-Path $roSaves "undumpable.gba.sav")) -ne "JUNK!") {
            Write-Host "run-desktop-save-smoke: FAIL (original rejected save was modified)"
            exit 1
        }
        if (Test-Path (Join-Path $roSaves "undumpable.gba.sav.rejected")) {
            Write-Host "run-desktop-save-smoke: FAIL (unexpected backup appeared in a read-only directory)"
            exit 1
        }
        Write-Host "  assert ok: undumpable rejected save left untouched (writes disabled)"
    }
    finally {
        Invoke-Icacls @("$roSaves", "/remove:d", "${env:USERNAME}") "cleanup"
    }

    # 5) --save-directory is created on demand and used for the .sav.
    $sdirRom = Join-Path $labDir "savedir.gba"
    New-Fixture "sram" $sdirRom
    $sdir = Join-Path $labDir "custom-saves"
    & $exe --rom $sdirRom --save-directory $sdir --quit-after 20 *> $null
    if ($LASTEXITCODE -ne 0) { Write-Host "run-desktop-save-smoke: FAIL (save-directory run exit $LASTEXITCODE)"; exit 1 }
    Assert-Save (Join-Path $sdir "savedir.gba.sav") 32768 0
    Write-Host "  assert ok: --save-directory created and used"

    # 6) A ROM switch must be REFUSED when the outgoing save cannot be
    #    flushed; the running session stays intact and no destructive step
    #    (not even the rollback path) is reached.
    $switchRom = Join-Path $labDir "switchA.gba"
    New-Fixture "sram" $switchRom
    $otherRom = Join-Path $labDir "switchB.gba"
    New-Fixture "bad" $otherRom
    $denied = Join-Path $labDir "denied-saves"
    New-Item -ItemType Directory -Force -Path $denied | Out-Null
    Invoke-Icacls @("$denied", "/deny", "${env:USERNAME}:(W)") "deny"
    try {
        $switchLog = Join-Path $labDir "switch.log"
        & $exe --rom $switchRom --save-directory $denied --switch-after 10 `
            --switch-to $otherRom --quit-after 40 *> $switchLog
        if ($LASTEXITCODE -ne 0) {
            Write-Host "run-desktop-save-smoke: FAIL (switch-refusal run exit $LASTEXITCODE)"
            Get-Content $switchLog
            exit 1
        }
        if (-not (Select-String -Path $switchLog -SimpleMatch "ROM switch refused" -Quiet)) {
            Write-Host "run-desktop-save-smoke: FAIL (ROM switch was not refused despite a failed save flush)"
            Get-Content $switchLog
            exit 1
        }
        if (Select-String -Path $switchLog -SimpleMatch "previous session restored" -Quiet) {
            Write-Host "run-desktop-save-smoke: FAIL (switch reached the destructive step instead of refusing)"
            Get-Content $switchLog
            exit 1
        }
        Write-Host "  assert ok: ROM switch refused while the save flush fails"
    }
    finally {
        Invoke-Icacls @("$denied", "/remove:d", "${env:USERNAME}") "cleanup"
    }

    # 7) Switching to a defective ROM: the outgoing save (seeded 0x42, boot
    #    increments to 0x43) is flushed at switch time -- the later engine-stop
    #    flush targets the *new* ROM's path, so byte0=0x43 here proves the
    #    switch-time flush carried the outgoing progress.
    $rollbackRom = Join-Path $labDir "rollback.gba"
    New-Fixture "sram" $rollbackRom
    $seed = [System.Byte[]]::new(32768); $seed[0] = 0x42
    [System.IO.File]::WriteAllBytes("$rollbackRom.sav", $seed)
    $badSwitchRom = Join-Path $labDir "rollback-bad.gba"
    New-Fixture "bad" $badSwitchRom
    $rollbackLog = Join-Path $labDir "rollback.log"
    & $exe --rom $rollbackRom --switch-after 10 --switch-to $badSwitchRom `
        --quit-after 40 *> $rollbackLog
    if ($LASTEXITCODE -ne 3) {
        Write-Host "run-desktop-save-smoke: FAIL (defective-ROM switch exit $LASTEXITCODE, expected 3)"
        Get-Content $rollbackLog
        exit 1
    }
    if (-not (Select-String -Path $rollbackLog -SimpleMatch "engine stopped" -Quiet)) {
        Write-Host "run-desktop-save-smoke: FAIL (defective ROM did not stop the engine)"
        Get-Content $rollbackLog
        exit 1
    }
    Assert-Save "$rollbackRom.sav" 32768 0x43
    Write-Host "  assert ok: defective-ROM switch flushes the outgoing save and exits 3"

    # 7b) An unreadable replacement ROM never reaches the engine: the switch
    #     is refused and the running session continues to a clean exit.
    $keepRunningRom = Join-Path $labDir "keeprunning.gba"
    New-Fixture "sram" $keepRunningRom
    $missingRom = Join-Path $labDir "missing.gba"
    $keepLog = Join-Path $labDir "keeprunning.log"
    & $exe --rom $keepRunningRom --switch-after 10 --switch-to $missingRom `
        --quit-after 40 *> $keepLog
    if ($LASTEXITCODE -ne 0) {
        Write-Host "run-desktop-save-smoke: FAIL (unreadable-replacement run exit $LASTEXITCODE)"
        Get-Content $keepLog
        exit 1
    }
    if (-not (Select-String -Path $keepLog -SimpleMatch "cannot read ROM" -Quiet)) {
        Write-Host "run-desktop-save-smoke: FAIL (unreadable replacement was not reported)"
        Get-Content $keepLog
        exit 1
    }
    Assert-Save "$keepRunningRom.sav" 32768 0
    Write-Host "  assert ok: unreadable replacement keeps the session running"

    # 8) A crash mid-replacement leaves the save only as the `.old` sibling.
    #    The next launch must restore and import it instead of starting fresh.
    $orphanRom = Join-Path $labDir "orphan.gba"
    New-Fixture "sram" $orphanRom
    $orphanSeed = [System.Byte[]]::new(32768); $orphanSeed[0] = 0x42
    [System.IO.File]::WriteAllBytes("$orphanRom.sav.old", $orphanSeed)
    $orphanLog = Join-Path $labDir "orphan.log"
    & $exe --rom $orphanRom --quit-after 20 *> $orphanLog
    if ($LASTEXITCODE -ne 0) {
        Write-Host "run-desktop-save-smoke: FAIL (crash-orphan run exit $LASTEXITCODE)"
        Get-Content $orphanLog
        exit 1
    }
    if (-not (Select-String -Path $orphanLog -SimpleMatch "recovered interrupted save write" -Quiet)) {
        Write-Host "run-desktop-save-smoke: FAIL (crash orphan was not recovered)"
        Get-Content $orphanLog
        exit 1
    }
    Assert-Save "$orphanRom.sav" 32768 0x43
    if (Test-Path "$orphanRom.sav.old") {
        Write-Host "run-desktop-save-smoke: FAIL (recovered orphan copy was not consumed)"
        exit 1
    }
    Write-Host "  assert ok: crash-orphan save restored, imported, and superseded"
}
finally {
    Remove-Item Env:SDL_VIDEODRIVER -ErrorAction SilentlyContinue
    Remove-Item Env:SDL_AUDIODRIVER -ErrorAction SilentlyContinue
}

python (Join-Path $repoRoot "tools\check-desktop-save-type-override.py") --exe $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "run-desktop-save-smoke: PASS (persistence, reset, no-spurious-save, rejected-save preservation, switch refusal, defective switch, crash-orphan recovery, save-type override)"
# Explicit terminal exit code so the step's success does not depend on the
# last native command's $LASTEXITCODE.
exit 0
