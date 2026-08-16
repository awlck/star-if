#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Adrian Welcker
"""
spdx_header.py — SPDX licence header template and checker.

WHAT THIS IS
    A dependency-free tool that knows which SPDX-License-Identifier belongs
    on which source file, based on where the file lives (see docs/proposal.md
    §14.5 and NOTICE). It can print the header a new file should start with,
    or check that existing files have the right one.

    This is the "script ... to keep them [SPDX headers]" referenced by
    backlog task A1. It is meant to be run by the pre-commit hook installed
    with `scripts/install-hooks.sh`, and later wired into CI (task B4/B5).

WHY PATH DETERMINES LICENCE
    The tree is not uniformly licensed (NOTICE, CONTRIBUTING.md):
        stdlib/stdlib/**        MIT-0
        stdlib/starscape/**   Apache-2.0
        everything else       Apache-2.0
    docs/ is prose, not source, and is excluded — its licence is CC-BY-4.0
    and is declared once in docs/LICENSE, not per file.

USAGE
    python3 scripts/spdx_header.py --template FILE   # print the header FILE should have
    python3 scripts/spdx_header.py --check FILE...   # exit nonzero if any FILE lacks it
    python3 scripts/spdx_header.py --check --staged  # check files staged in git

    Exit status is 0 if every checked file has a correct header.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

YEAR = "2026"
HOLDER = "Adrian Welcker"

# Extensions this tool has a comment style for. Anything else is skipped by
# --check (nothing to enforce) and rejected by --template (nothing to print).
COMMENT_STYLES = {
    ".cpp": "//",
    ".cc": "//",
    ".cxx": "//",
    ".h": "//",
    ".hpp": "//",
    ".hh": "//",
    ".py": "#",
    ".cmake": "#",
    ".lua": "--",
}
# CMakeLists.txt has no extension; match on name instead.
NAMED_COMMENT_STYLES = {
    "CMakeLists.txt": "#",
}

# Path-prefix rules, most specific first. The first match wins.
LICENSE_RULES = [
    ("stdlib/starscape/", "Apache-2.0"),
    ("stdlib/stdlib/", "MIT-0"),
    ("stdlib/", "MIT-0"),
]
DEFAULT_LICENSE = "Apache-2.0"

# docs/ is prose covered by a single docs/LICENSE, not per-file headers.
EXCLUDED_PREFIXES = ("docs/",)


def license_for(rel_path: str) -> str | None:
    for prefix in EXCLUDED_PREFIXES:
        if rel_path.startswith(prefix):
            return None
    for prefix, license_id in LICENSE_RULES:
        if rel_path.startswith(prefix):
            return license_id
    return DEFAULT_LICENSE


def comment_prefix(path: Path) -> str | None:
    if path.name in NAMED_COMMENT_STYLES:
        return NAMED_COMMENT_STYLES[path.name]
    return COMMENT_STYLES.get(path.suffix)


def header_lines(path: Path, license_id: str) -> list[str]:
    c = comment_prefix(path)
    if c is None:
        raise ValueError(f"no comment style known for {path}")
    return [
        f"{c} SPDX-License-Identifier: {license_id}",
        f"{c} SPDX-FileCopyrightText: {YEAR} {HOLDER}",
    ]


def check_file(path: Path, repo_root: Path) -> str | None:
    """Return an error string if path is missing/wrong header, else None."""
    rel = path.relative_to(repo_root).as_posix()
    license_id = license_for(rel)
    if license_id is None:
        return None  # excluded (e.g. docs/)
    if comment_prefix(path) is None:
        return None  # not a file type this tool covers
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as exc:
        return f"{rel}: could not read ({exc})"
    head = "\n".join(text.splitlines()[:5])
    expected = f"SPDX-License-Identifier: {license_id}"
    if expected not in head:
        return f"{rel}: missing or wrong header (expected '{expected}')"
    return None


def staged_files(repo_root: Path) -> list[Path]:
    out = subprocess.run(
        ["git", "diff", "--cached", "--name-only", "--diff-filter=ACM"],
        cwd=repo_root,
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    return [repo_root / line for line in out.splitlines() if line]


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("USAGE")[0])
    ap.add_argument("--template", metavar="FILE", help="print the header FILE should start with")
    ap.add_argument("--check", nargs="*", metavar="FILE", help="check FILE(s) for a correct header")
    ap.add_argument("--staged", action="store_true", help="with --check, check git-staged files instead")
    args = ap.parse_args(argv)

    repo_root = Path(
        subprocess.run(
            ["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True
        ).stdout.strip()
    )

    if args.template:
        path = Path(args.template)
        rel = path.resolve().relative_to(repo_root.resolve()).as_posix() if path.is_absolute() else path.as_posix()
        license_id = license_for(rel)
        if license_id is None:
            print(f"{rel}: no header needed (excluded path)", file=sys.stderr)
            return 1
        try:
            for line in header_lines(path, license_id):
                print(line)
        except ValueError as exc:
            print(exc, file=sys.stderr)
            return 1
        return 0

    if args.check is not None:
        files = staged_files(repo_root) if args.staged else [Path(f) for f in args.check]
        errors = []
        for f in files:
            p = f if f.is_absolute() else repo_root / f
            if not p.exists() or not p.is_file():
                continue
            err = check_file(p, repo_root)
            if err:
                errors.append(err)
        if errors:
            for e in errors:
                print(e, file=sys.stderr)
            print(f"\n{len(errors)} file(s) missing a correct SPDX header.", file=sys.stderr)
            print("Run: python3 scripts/spdx_header.py --template <file>", file=sys.stderr)
            return 1
        return 0

    ap.print_help()
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
