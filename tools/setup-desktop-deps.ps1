# Downloads a pinned SDL3 development distribution for the desktop host.
#
# Usage:
#   .\tools\setup-desktop-deps.ps1
#   cmake -S . -B build/desktop -DCMAKE_PREFIX_PATH="$PWD\external\SDL3"
#
# The archive is fetched from the official libsdl-org GitHub release and
# extracted under external/SDL3. The download is skipped when the SDL3 CMake
# config is already present.
param(
    # SDL3 release tag (pinned for reproducible builds; bump deliberately).
    [string]$SdlVersion = "3.4.18"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$destRoot = Join-Path (Join-Path $repoRoot "external") "SDL3"
# The mingw archive keeps per-architecture prefixes; the 64-bit one is what
# the desktop host builds against.
$sdlConfig = Join-Path (Join-Path (Join-Path (Join-Path $destRoot "x86_64-w64-mingw32") "lib") "cmake") `
    (Join-Path "SDL3" "SDL3Config.cmake")

if (Test-Path -LiteralPath $sdlConfig) {
    Write-Host "setup-desktop-deps: SDL3 $SdlVersion already present at $destRoot"
    exit 0
}

$archiveName = "SDL3-devel-$SdlVersion-mingw.tar.gz"
$url = "https://github.com/libsdl-org/SDL/releases/download/release-$SdlVersion/$archiveName"
$archivePath = Join-Path $env:TEMP $archiveName

Write-Host "setup-desktop-deps: downloading $url"
Invoke-WebRequest -Uri $url -OutFile $archivePath

New-Item -ItemType Directory -Force -Path $destRoot | Out-Null
# The archive root is SDL3-<version>/; flatten it into external/SDL3. Use the
# Windows tar explicitly: MSYS tar interprets "C:\..." as a remote host spec.
& "$env:SystemRoot\System32\tar.exe" -xzf $archivePath -C $destRoot --strip-components 1
if ($LASTEXITCODE -ne 0) {
    Write-Error "setup-desktop-deps: extraction failed (tar exit $LASTEXITCODE)"
    exit 1
}
Remove-Item -LiteralPath $archivePath

if (-not (Test-Path -LiteralPath $sdlConfig)) {
    Write-Error "setup-desktop-deps: extraction did not produce $sdlConfig"
    exit 1
}

Write-Host "setup-desktop-deps: SDL3 $SdlVersion installed at $destRoot"
Write-Host "Configure with: cmake -S . -B build/desktop -DCMAKE_PREFIX_PATH=$destRoot"
