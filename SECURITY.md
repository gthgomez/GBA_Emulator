# Security Policy

## Scope

`GBA_Emulator` is a portable C++17 emulator core. It parses caller-provided Game Boy Advance
ROM data, decodes cartridge and bus I/O structures, and executes them as native machine code
inside the host process. A malformed or hostile ROM is untrusted native input, and a bug in the
core's memory, bounds, or register emulation is a memory-safety defect, not a cosmetic issue.

In scope: the emulator core under `src/` and `include/`, the host-side verifiers and harnesses
under `tools/` and `tests/`, and build scripts that influence what gets linked into a build.

Out of scope: third-party test-suite ROMs, the sibling Android shell
[`GbaEmulatorAndroid`](../GbaEmulatorAndroid) (reported there), and legal or licensing questions
about ROM distribution (see `docs/fixture-license-registry.md`).

## Supported Versions

This project is pre-1.0 and is not API-stable.

| Version | Supported |
| --- | --- |
| `main` (latest commit) | Yes |
| Latest tagged release | Yes |
| Any earlier commit, branch, or release | No |

Only the current tip of `main` and the most recent release receive security fixes. Reproductions
against older commits are still welcome if they demonstrate a defect that also exists on `main`.

## Reporting a Vulnerability

Use GitHub's private vulnerability reporting: go to the repository's **Security** tab and click
**Report a vulnerability**. This opens a private advisory that only the maintainer can see, and it
lets the two of you discuss details without a public issue.

If private reporting is unavailable to your account, open a
[security advisory](https://github.com/gthgomez/GBA_Emulator/security/advisories/new) directly
instead of filing a public issue. There is no published email address for this project, so the
advisory channel above is the supported route.

Please do not open a public issue for an unfixed defect, and do not include a copyrighted ROM in a
report. Minimal, self-authored or homebrew ROM bytes that trigger the crash are enough.

## What to Include

- Type of defect: out-of-bounds read or write, use-after-free, uninitialized memory, integer
  overflow leading to an undersized allocation, unbounded recursion, or a sanitizer abort.
- Affected commit SHA or release tag, build configuration, compiler version, and whether the build
  used ASan/UBSan (see the toolchain note in `README.md`; the bundled MinGW `g++` cannot link
  sanitizers, so an LLVM/clang or MSVC build may be needed).
- A minimal input that reproduces it: a small homebrew ROM, a raw byte range, or a failing
  `tools/run-core-tests.ps1` invocation.
- Observed behavior, including the crash output or sanitizer stack trace.
- Whether the issue is reachable without a debugger attached.

## Maintainer Response

The maintainer commits to the following:

- Acknowledge a report within 7 days.
- Provide a severity assessment and a remediation or mitigation plan within 30 days of
  acknowledgement.
- Credit reporters in the advisory and release notes unless anonymity is requested.

Fixes are published on `main` first, then folded into the next release. Known unfixed issues stay
in `docs/open-issues-status.md` until resolved.

## Coordinated Disclosure

Fixes land before public disclosure. A reporter should allow up to 90 days from first contact for
a fix or a documented mitigation before publishing, and the maintainer will not cut that period
short without agreeing with the reporter.

## No Bug Bounty

There is no bug bounty program for this project, and no payment is offered for reports. Credit and
a public advisory are the entire compensation.
