# Fixture And License Registry

Status: Phase 58 fixture gate
Date: 2026-05-07

## Current Fixtures

No ROMs, BIOS images, copyrighted game assets, homebrew binaries, third-party emulator code, or downloaded compatibility fixtures are present.

Current tests use only hand-authored byte values inside source code. Phase 58 adds an
in-memory legal-program harness, but it still does not admit checked-in ROM files or
external fixtures.

## Admission Rule

Before any fixture or reference implementation is added, record:

- Source URL or local provenance.
- License text or redistribution permission.
- Whether the artifact contains copyrighted game content.
- Whether counsel review is needed.
- Exact file paths added.

Commercial ROMs, BIOS images, copyrighted screenshots, copyrighted audio, ROM patches requiring copyrighted inputs, and unclear-provenance compatibility packs remain disallowed.

## Future Fixture Gate

Any future checked-in homebrew or test ROM must be explicitly authorized before it is
added. The admission record must include:

- Fixture name and exact path.
- Upstream project or author.
- Source URL or local provenance.
- License text or clear redistribution permission.
- Build recipe or checksum if the binary is generated.
- Confirmation that it contains no copyrighted game content and no BIOS data.
- Whether legal counsel review is required before public release or distribution.

The default test path remains source-owned byte arrays inside C++ verifier code.
