# Requires PowerShell 7.3+ for $PSNativeCommandUseErrorActionPreference.
#requires -Version 7.3

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$sourceDir = Join-Path $repoRoot "external\test-suites\mgba-suite"
$buildDir = Join-Path $repoRoot "build\test-suite-build\mgba-suite"
$image = "devkitpro/devkitarm:latest"

if (-not (Test-Path -LiteralPath (Join-Path $sourceDir "Makefile") -PathType Leaf)) {
  throw "mGBA suite source not found at $sourceDir"
}

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
  throw "Docker is required for the reproducible devkitARM build path"
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$previousNativeErrorPreference = $PSNativeCommandUseErrorActionPreference
$PSNativeCommandUseErrorActionPreference = $false
robocopy $sourceDir $buildDir /E /XD .git build | Out-Host
$robocopyExitCode = $LASTEXITCODE
$PSNativeCommandUseErrorActionPreference = $previousNativeErrorPreference
if ($robocopyExitCode -gt 7) {
  exit $robocopyExitCode
}

function Assert-NativeExitCode {
  param(
    [string]$Step,
    [int]$Expected = 0
  )

  if ($LASTEXITCODE -ne $Expected) {
    Write-Host ""
    Write-Host "mgba_suite_build: FAIL ($Step exited with $LASTEXITCODE)"
    if ($null -eq $LASTEXITCODE) {
      exit 1
    }
    exit $LASTEXITCODE
  }
}

docker run --rm `
  -v "${buildDir}:/work" `
  -w /work `
  $image `
  make

Assert-NativeExitCode -Step "docker devkitARM make"

$suitePath = Join-Path $buildDir "suite.gba"
if (-not (Test-Path -LiteralPath $suitePath -PathType Leaf)) {
  throw "suite.gba was not produced"
}

Get-Item -LiteralPath $suitePath | Select-Object FullName,Length
Get-FileHash -Algorithm SHA256 -LiteralPath $suitePath
