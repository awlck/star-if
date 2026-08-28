#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Adrian Welcker
"""
check_starvfs_host_boundary.py -- asserts only NativeHostIo touches the
real filesystem.

WHY
    libs/starvfs/include/starvfs/host.hpp: "starvfs never interprets [host
    paths] itself -- that is the whole point of the boundary (backlog G1,
    proposal §12.5): a host may read one as a std::filesystem path, an
    IndexedDB key, or a fetch() URL, and every frontend gets starvfs's
    layering, path normalisation and zip decoding for free by implementing
    only this." NativeHostIo (native_host.hpp/.cpp) is the one Phase 0
    implementation of that interface, and the only place allowed to know
    the host is a real directory.

    A stray std::filesystem call anywhere else in libs/starvfs would still
    compile, still pass the layers' own tests (nothing else has a real
    filesystem to test against yet), and would only be discovered once
    someone tried the WASM target proposal §12.5 asks this boundary to keep
    tractable. That is exactly the shape of thing this repo already asserts
    rather than assumes -- see check_no_qt_in_libs.py, which this script is
    modelled on directly, down to the pattern-list-over-file-list
    structure.

    Comments are stripped before matching -- this file's own comments and
    host.hpp's docstring both talk about std::filesystem in prose, and a
    file explaining the boundary is not a file crossing it. This repo's
    C++ uses "//" comments throughout, never "/* */", so stripping
    "// rest of line" is enough; it does not special-case a "//" inside a
    string literal, which nothing in this library needs.

USAGE
    python3 scripts/check_starvfs_host_boundary.py
    Exit status is 0 if the boundary holds, 1 if something outside
    native_host.{hpp,cpp} crosses it.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

# The host-touching APIs NativeHostIo is allowed to use and nothing else in
# libs/starvfs is. Specific include/token spellings rather than a vaguer
# "filesystem" substring match, so this doesn't false-positive on words like
# "HostFilesystem" appearing in a comment.
PATTERNS = [
    re.compile(r"#\s*include\s*[<\"]filesystem[>\"]"),
    re.compile(r"#\s*include\s*[<\"]fstream[>\"]"),
    re.compile(r"#\s*include\s*[<\"]cstdio[>\"]"),
    re.compile(r"\bstd::filesystem\b"),
    re.compile(r"\bstd::ifstream\b|\bstd::ofstream\b|\bstd::fstream\b"),
    re.compile(r"\bfopen\s*\(|\bfreopen\s*\("),
]

# The one file pair permitted to match. Relative to libs/starvfs/.
EXEMPT = {
    "include/starvfs/native_host.hpp",
    "src/native_host.cpp",
}

SCANNED_SUFFIXES = {".cpp", ".cc", ".cxx", ".h", ".hpp", ".hh"}

STARVFS = "libs/starvfs"


def tracked_files(repo_root: Path) -> list[Path]:
    out = subprocess.run(
        ["git", "ls-files", "--", STARVFS],
        cwd=repo_root,
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    return [repo_root / line for line in out.splitlines() if line]


def main() -> int:
    repo_root = Path(
        subprocess.run(
            ["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True
        ).stdout.strip()
    )

    scanned = 0
    violations = []
    for path in tracked_files(repo_root):
        if path.suffix not in SCANNED_SUFFIXES or not path.is_file():
            continue
        relative = path.relative_to(repo_root / STARVFS).as_posix()
        if relative in EXEMPT:
            continue
        scanned += 1
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for lineno, line in enumerate(text.splitlines(), start=1):
            code = line.split("//", 1)[0]
            for pattern in PATTERNS:
                if pattern.search(code):
                    rel = path.relative_to(repo_root).as_posix()
                    violations.append(f"{rel}:{lineno}: {line.strip()}")

    if violations:
        print(
            "libs/starvfs/ must not touch the real filesystem outside "
            "native_host.{hpp,cpp} (docs/starvfs-api.md, the HostIo boundary):",
            file=sys.stderr,
        )
        for v in violations:
            print(f"  {v}", file=sys.stderr)
        return 1

    print(f"ok: the host boundary holds across {scanned} tracked file(s) under {STARVFS}/ "
          f"(native_host.hpp/.cpp exempt)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
