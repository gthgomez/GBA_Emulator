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

# One documented local boot/portfolio capture is the only permitted image asset.
# It is evidence, not a fixture; ROM, BIOS and save files remain banned.
$allowedEvidenceImages = @(
  "docs/evidence/2026-11-18-emerald-boot/emerald-title.png"
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
      $isAllowedEvidenceImage = $allowedEvidenceImages -contains $gitPath
      -not $inAllowedGenerated -and -not $isAllowedEvidenceImage
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
  "compatible with commercial",
  "retail compatibility verified",
  "runs retail games",
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

# The narrow screenshot exception is valid only while its evidence record exists
# and continues to state exactly what was run and what is NOT claimed.
$bootEvidenceImage = "docs\evidence\2026-11-18-emerald-boot\emerald-title.png"
$bootEvidenceDoc = "docs\evidence\2026-11-18-emerald-boot.md"
if (Test-Path -LiteralPath (Join-Path $repoRoot $bootEvidenceImage)) {
  if (-not (Test-Path -LiteralPath (Join-Path $repoRoot $bootEvidenceDoc) -PathType Leaf)) {
    throw "release_readiness: boot screenshot requires $bootEvidenceDoc"
  }
  $bootEvidence = Get-Content -Raw -LiteralPath (Join-Path $repoRoot $bootEvidenceDoc)
  foreach ($required in @(
      "NOT CLAIMED",
      "emerald-title.png",
      "a9dec84dfe7f62ab2220bafaef7479da0929d066ece16a6885f6226db19085af"
    )) {
    if (-not $bootEvidence.Contains($required)) {
      throw "release_readiness: $bootEvidenceDoc must record '$required'"
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
