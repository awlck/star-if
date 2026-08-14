#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Adrian Welcker
"""
check_no_qt_in_libs.py -- asserts libs/ has no Qt dependency.

WHY
    proposal.md §2.1: "stardata knows nothing about IF; starcore knows
    nothing about Qt". That boundary is load-bearing -- it's what keeps the
    compiler and CLI runtime buildable and testable without Qt, and keeps
    the WASM target tractable. backlog B2 asks for this to be asserted, not
    assumed, so this is the assertion: a plain-text scan of libs/ for Qt
    includes, CMake package lookups, and Qt's MOC macros.

    Deliberately not a clang-tidy or CMake-integrated check: this needs to
    run before a compiler is even involved (a stray #include <QString> is
    worth catching whether or not Qt happens to be installed on the
    machine running it), and it needs to run every time in CI regardless
    of which libs/ subdirectory has code yet.

USAGE
    python3 scripts/check_no_qt_in_libs.py
    Exit status is 0 if libs/ is clean, 1 if it finds a Qt reference.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

# Deliberately specific to Qt's own naming conventions, so this doesn't
# false-positive on unrelated words ("quit", "QUANTITY", ...).
PATTERNS = [
    re.compile(r"#\s*include\s*[<\"]Q[A-Z][A-Za-z0-9_]*[>\"]"),  # #include <QString>
    re.compile(r"\bfind_package\s*\(\s*Qt[56]?\b", re.IGNORECASE),  # find_package(Qt6 ...)
    re.compile(r"\bqt_add_executable\b|\bqt_add_library\b", re.IGNORECASE),
    re.compile(r"\bQ_OBJECT\b|\bQ_PROPERTY\b|\bQ_INVOKABLE\b"),  # MOC macros
    re.compile(r"\bnamespace\s+Qt\b"),
]

# Extensions worth scanning; anything else in libs/ (docs, data) is exempt.
SCANNED_SUFFIXES = {".cpp", ".cc", ".cxx", ".h", ".hpp", ".hh", "CMakeLists.txt", ".cmake"}


def tracked_files_under(repo_root: Path, subdir: str) -> list[Path]:
    out = subprocess.run(
        ["git", "ls-files", "--", subdir],
        cwd=repo_root,
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    return [repo_root / line for line in out.splitlines() if line]


def is_scanned(path: Path) -> bool:
    return path.name in SCANNED_SUFFIXES or path.suffix in SCANNED_SUFFIXES


def main() -> int:
    repo_root = Path(
        subprocess.run(
            ["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True
        ).stdout.strip()
    )

    violations = []
    for path in tracked_files_under(repo_root, "libs"):
        if not is_scanned(path) or not path.is_file():
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for lineno, line in enumerate(text.splitlines(), start=1):
            for pattern in PATTERNS:
                if pattern.search(line):
                    rel = path.relative_to(repo_root).as_posix()
                    violations.append(f"{rel}:{lineno}: {line.strip()}")

    if violations:
        print("libs/ must not depend on Qt (proposal.md §2.1):", file=sys.stderr)
        for v in violations:
            print(f"  {v}", file=sys.stderr)
        return 1

    print(f"ok: no Qt reference found in {len(tracked_files_under(repo_root, 'libs'))} "
          f"tracked file(s) under libs/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
