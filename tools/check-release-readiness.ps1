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

# Scan the git INDEX (tracked files), not the working tree: out-of-tree
# external ROMs on disk must not spuriously fail the gate, while force-added
# tracked assets (git add foo.gba) are still caught because they appear in
# the index before commit.
$trackedFiles = @(git -C $repoRoot.Path ls-files --cached)
if ($LASTEXITCODE -ne 0) {
  throw "release_readiness: git ls-files failed with exit code $LASTEXITCODE"
}
if ($trackedFiles.Count -eq 0) {
  throw "release_readiness: git index is empty; cannot scan tracked assets"
}

foreach ($pattern in $forbiddenAssetExtensions) {
  $extension = [System.IO.Path]::GetExtension($pattern)
  $offending = @($trackedFiles | Where-Object {
      $gitPath = $_
      $isForbiddenType =
          [System.IO.Path]::GetExtension($gitPath).Equals(
              $extension, [System.StringComparison]::OrdinalIgnoreCase)
      if (-not $isForbiddenType) {
        return $false
      }
      $inAllowedGenerated = $false
      foreach ($allowed in $allowedGenerated) {
        if ($gitPath -like "$allowed/*" -or $gitPath -eq $allowed) {
          $inAllowedGenerated = $true
          break
        }
      }
      -not $inAllowedGenerated
    })
  if ($offending.Count -gt 0) {
    $paths = $offending -join ", "
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
