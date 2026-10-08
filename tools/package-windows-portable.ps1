# Produces a portable Windows ZIP for gba-desktop: the executable, the runtime
# DLLs it actually imports, license/attribution files, and a short README.
#
# No ROMs, BIOS images, or game assets are ever included.
#
# Usage (after building the desktop target on Windows):
#   .\tools\package-windows-portable.ps1
#   .\tools\package-windows-portable.ps1 -Version 0.2.0 -OutputDir dist
param(
    [string]$BuildDir = "build/desktop",
    [string]$OutputDir = "dist",
    [string]$Version = "0.1.0"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")

$exe = Join-Path $repoRoot $BuildDir "apps/desktop/gba-desktop.exe"
if (-not (Test-Path -LiteralPath $exe)) { $exe = Join-Path $repoRoot $BuildDir "gba-desktop.exe" }
if (-not (Test-Path -LiteralPath $exe)) {
    throw "gba-desktop.exe not found under $BuildDir; build the desktop target first"
}

$stageName = "gba-desktop-$Version-windows-x64"
$stage = Join-Path $repoRoot $OutputDir $stageName
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item -LiteralPath $exe (Join-Path $stage "gba-desktop.exe")

# Enumerate the executable's real imports rather than assuming SDL3.dll is the
# only one. Windows system DLLs are resolved by the OS; everything else must be
# shipped beside the exe.
$systemDlls = @(
    "advapi32.dll", "bcrypt.dll", "cfgmgr32.dll", "comdlg32.dll", "dwmapi.dll",
    "gdi32.dll", "hid.dll", "imm32.dll", "kernel32.dll", "msvcrt.dll",
    "ntdll.dll", "ole32.dll", "oleaut32.dll", "powrprof.dll", "rpcrt4.dll",
    "secur32.dll", "setupapi.dll", "shell32.dll", "shlwapi.dll", "user32.dll",
    "userenv.dll", "uxtheme.dll", "version.dll", "winmm.dll", "winhttp.dll",
    "ws2_32.dll", "dinput8.dll", "xinput1_4.dll", "dsound.dll"
)

$objdump = Get-Command objdump -ErrorAction SilentlyContinue
if (-not $objdump) { throw "objdump not found (install MSYS2/MinGW binutils) to verify imports" }
$imports = & $objdump.Source -p $exe |
    Select-String "DLL Name: (.+)" |
    ForEach-Object { $_.Matches[0].Groups[1].Value.Trim().ToLowerInvariant() } |
    Sort-Object -Unique

$searchDirs = @(
    (Split-Path -Parent $exe),
    (Join-Path $repoRoot "external/SDL3/x86_64-w64-mingw32/bin"),
    "C:\msys64\mingw64\bin"
)
$copied = @()
$missing = @()
foreach ($dll in $imports) {
    if ($systemDlls -contains $dll) { continue }
    $source = $null
    foreach ($dir in $searchDirs) {
        $candidate = Join-Path $dir $dll
        if (Test-Path -LiteralPath $candidate) { $source = $candidate; break }
    }
    if ($null -eq $source) { $missing += $dll; continue }
    Copy-Item -LiteralPath $source (Join-Path $stage $dll) -Force
    $copied += $dll
}

if ($missing.Count -gt 0) {
    throw "missing runtime dependencies: $($missing -join ', ')"
}
if ($copied -notcontains "sdl3.dll") {
    throw "SDL3.dll was not among the executable's imports; packaging assumption is stale"
}

Copy-Item -LiteralPath (Join-Path $repoRoot "LICENSE") (Join-Path $stage "LICENSE.txt")
Copy-Item -LiteralPath (Join-Path $repoRoot "NOTICE") (Join-Path $stage "NOTICE.txt")
$sdlLicense = Join-Path $repoRoot "external/SDL3/x86_64-w64-mingw32/LICENSE.txt"
if (Test-Path -LiteralPath $sdlLicense) {
    Copy-Item -LiteralPath $sdlLicense (Join-Path $stage "SDL3-LICENSE.txt")
}

$readme = @"
gba-desktop $Version (portable Windows x64)
===========================================

Run:
  gba-desktop.exe                 opens a ROM file picker
  gba-desktop.exe path\to\game.gba

Controls:
  Arrows / d-pad        D-pad
  Z / X                 A / B
  Enter / Backspace     Start / Select
  A / S                 R / L
  Tab (hold)            Fast-forward
  P or Space            Pause / resume
  F5 / F8               Save / load state (slot 1)
  F9                    Reset (keeps cartridge save)
  F11                   Fullscreen
  M                     Mute
  O                     Open ROM dialog
  Esc                   Quit

Saves live beside the ROM as <rom>.sav (cartridge saves) and <rom>.state1
(save states). Everything is portable: no installer, no registry, no network.

This package contains the emulator, its runtime DLLs, and license/attribution
files only. It ships no ROMs, no BIOS images, and no game assets. Supply your
own legally obtained GBA ROM.
"@
Set-Content -LiteralPath (Join-Path $stage "README.txt") -Value $readme -Encoding UTF8

$zipPath = Join-Path $repoRoot $OutputDir "$stageName.zip"
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zipPath -Force

Write-Host "package-windows-portable: packaged $stageName"
Write-Host "  executable: gba-desktop.exe"
Write-Host "  runtime DLLs: $($copied -join ', ')"
Write-Host "  zip: $zipPath"
Get-ChildItem -LiteralPath $stage | Select-Object -ExpandProperty Name | ForEach-Object { Write-Host "  - $_" }
