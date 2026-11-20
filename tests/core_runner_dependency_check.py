#!/usr/bin/env python3
"""Guard the authoritative runners' incremental test-header dependencies."""

from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def main() -> None:
    bash = (ROOT / "tools/run-core-tests.sh").read_text(encoding="utf-8")
    powershell = (ROOT / "tools/run-core-tests.ps1").read_text(encoding="utf-8")
    helper = ROOT / "tests/test_helpers.hpp"

    assert helper.is_file(), "shared verifier header is missing"
    assert '"$repo_root/tests"' in bash, "bash runner must scan tests/ headers"
    assert 'Join-Path $repoRoot "tests"' in powershell, "PowerShell runner must scan tests/ headers"
    print("core_runner_dependency_check: PASS (both runners track tests/ headers)")


if __name__ == "__main__":
    main()
