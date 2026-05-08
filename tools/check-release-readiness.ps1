$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")

$requiredDocs = @(
  "docs\fixture-license-registry.md",
  "docs\release-governance-legal-review.md",
  "docs\controlled-beta-readiness.md",
  "docs\roadmap-requirements-audit.md",
  "docs\production-engine-roadmap.md",
  "docs\ralph-state\gba-emulator-plan-2026-05-05.md"
)

foreach ($relative in $requiredDocs) {
  $path = Join-Path $repoRoot $relative
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "release_readiness: missing required document: $relative"
  }
}

$forbiddenAssetExtensions = @(
  "*.gba",
  "*.gb",
  "*.gbc",
  "*.sav",
  "*.srm",
  "*.eep",
  "*.fla",
  "*.bin",
  "*.bios",
  "*.png",
  "*.jpg",
  "*.jpeg",
  "*.gif",
  "*.mp3",
  "*.wav",
  "*.ogg"
)

$allowedGenerated = @(
  "build"
)

foreach ($pattern in $forbiddenAssetExtensions) {
  $matches = Get-ChildItem -LiteralPath $repoRoot -Recurse -File -Filter $pattern |
    Where-Object {
      $relative = Resolve-Path -LiteralPath $_.FullName -Relative
      -not ($allowedGenerated | ForEach-Object { $relative -like ".\$_\*" })
    }
  if ($matches) {
    $paths = ($matches | Select-Object -ExpandProperty FullName) -join ", "
    throw "release_readiness: forbidden or review-required asset present: $paths"
  }
}

$claimPatterns = @(
  "production-ready emulator",
  "fastest gba emulator",
  "most accurate gba emulator",
  "runs all commercial",
  "runs most commercial",
  "nintendo endorsed",
  "official nintendo"
)

$publicClaimScanFiles = @(
  "README.md",
  "docs\production-engine-roadmap.md",
  "docs\fixture-license-registry.md"
)

foreach ($relative in $publicClaimScanFiles) {
  $file = Get-Item -LiteralPath (Join-Path $repoRoot $relative)
  $content = Get-Content -Raw -LiteralPath $file.FullName
  foreach ($pattern in $claimPatterns) {
    if ($content.ToLowerInvariant().Contains($pattern)) {
      throw "release_readiness: unsupported public claim pattern '$pattern' in $($file.FullName)"
    }
  }
}

$audit = Get-Content -Raw -LiteralPath (Join-Path $repoRoot "docs\roadmap-requirements-audit.md")
if (-not $audit.Contains("Production readiness: **NO**")) {
  throw "release_readiness: audit must explicitly state production readiness is NO"
}
if (-not $audit.Contains("Controlled external beta readiness: **BLOCKED**")) {
  throw "release_readiness: audit must explicitly state controlled beta is BLOCKED"
}

Write-Output "release_readiness: PASS"
