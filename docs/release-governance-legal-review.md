# Release Governance And Legal Review

Status: Phase 71 bounded local review gate
Date: 2026-05-07

This document is a release-readiness control, not legal advice. It records what must be
true before any external beta or public-facing repository/app listing can exist.

## Legal Review Checklist

| Area | Requirement | Current Evidence | Status |
| --- | --- | --- | --- |
| BIOS | No BIOS image is bundled, generated, downloaded, scanned, or silently assumed. | `docs/production-engine-roadmap.md`, BIOS tests, explicit BIOS/SWI policy seed. | PASS |
| ROMs | No commercial ROM, copyrighted game asset, downloader, scanner, or broad storage import is present. | `docs/fixture-license-registry.md`; current tests use source-owned byte arrays. | PASS |
| Fixtures | Future fixtures require provenance, license/redistribution terms, no copyrighted game content, and exact paths. | `docs/fixture-license-registry.md`; `CompatibilityFixture` rejects unlicensed/non-redistributable entries. | PASS |
| Trademarks | Public wording must use "Game Boy Advance" only descriptively and avoid implying Nintendo affiliation. | README currently describes a portable GBA core scaffold and avoids affiliation claims. | PASS |
| Screenshots | No screenshots from commercial games may be used in docs, store listings, marketing, or tests. | No screenshot/image assets are present. | PASS |
| Compatibility Claims | No public claim of production readiness, superiority, broad compatibility, or hardware accuracy without evidence. | Roadmap explicitly says this is not production-ready and avoids mature-emulator comparisons. | PASS |

## Privacy And Data-Safety Review

| Area | Requirement | Current Evidence | Status |
| --- | --- | --- | --- |
| SAF Import | Future Android import must be explicit user-selected file access only. No scanners or broad storage permission. | Phase 68/69 bridge/runtime accept caller-provided byte arrays only; Android SAF remains deferred. | PASS |
| Save Persistence | Save files and save states must be user-controlled, recoverable, and migration-aware before beta. | Current save-state codec is local and bounded; persistence UX remains deferred. | BLOCKED FOR BETA |
| Telemetry | No telemetry, crash reporting, analytics, network upload, or third-party SDK exists. | No Android app or SDK integration exists. | PASS |
| Data Safety | If telemetry/crash reporting is later introduced, record collection purpose, retention, sharing, and opt-in/out. | No telemetry decision yet because no telemetry is present. | NOT APPLICABLE |

## Crash Reporting And Telemetry Decision

Decision: no telemetry or crash-reporting SDK is introduced in the current core scaffold.

If external beta later adds telemetry, a new decision record must state:

- SDK/provider and exact package.
- Data collected.
- Whether data leaves the device.
- Retention and deletion policy.
- User opt-in/out behavior.
- Play Data Safety impact.

## Public README Language Gate

Allowed:

- "Portable Game Boy Advance emulator core scaffold."
- "Legal caller-provided byte arrays."
- "Android integration intentionally deferred" or "bounded local runtime seed."
- "Not production-ready."

Disallowed without new evidence:

- "Production-ready emulator."
- "Best/fastest/most accurate GBA emulator."
- "Runs all/most commercial games."
- "Compatible with commercial ROMs."
- "No BIOS required" as a broad user-facing legality claim.
- Any Nintendo affiliation or endorsement implication.

## Release Governance Result

Phase 71 is complete as a bounded local governance gate: release-facing wording and
fixture handling are reviewed against the current repo state. External beta remains
blocked until Phase 72 beta evidence and device/platform proof exist.
