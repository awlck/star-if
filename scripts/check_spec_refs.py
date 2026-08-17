#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Adrian Welcker
"""
check_spec_refs.py -- asserts every spec section cited from the code exists.

WHY
    The diagnostics, and the comments that explain them, cite sections of
    docs/stardata-spec.md by number: "spec §3.4", "§6.6.4". Those numbers are
    positional, so inserting one subsection renumbers every later sibling and
    silently turns a correct citation into a citation of something else. That
    is not a hypothetical -- adding §6.6.1 and §6.6.2 pushed "Namespaced ids"
    from §6.6.2 to §6.6.4, and E-DATUM-AMBIGUOUS went on pointing at §6.6.2,
    which by then meant something entirely unrelated.

    Nothing else catches this. A wrong section number still compiles, still
    passes every test, and still reads plausibly; the only symptom is an
    author following a diagnostic to the wrong paragraph. So it is asserted
    here, in the same spirit as check_no_qt_in_libs.py.

WHAT THIS DOES NOT CATCH -- read before trusting it
    Only that a cited section EXISTS. The E-DATUM-AMBIGUOUS case above would
    have passed this check: §6.6.2 still existed after the renumbering, it
    had merely come to mean something else. What this catches is the
    adjacent failure, where a section is removed or a list shortened and the
    citation falls off the end of the document entirely.

    Catching the re-pointing case needs citations to carry something stable
    -- a title alongside the number, or an anchor -- which is a change to how
    every diagnostic is written, not a change to this script. Until then, a
    specification diff that renumbers anything wants a human reading the
    citations that survive it.

WHAT COUNTS AS A SECTION
    Headings ("### 6.6 Referring to data") and the numbered clauses inside a
    section that has no subheadings -- §2 states its requirements as a
    numbered list, so "§2.1" means its first clause and is a legitimate
    citation with no heading of its own.

USAGE
    python3 scripts/check_spec_refs.py
    Exit status is 0 if every citation resolves, 1 otherwise.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

SPEC = Path("docs/stardata-spec.md")
PROPOSAL = Path("docs/proposal.md")

# Where citations are looked for. Documentation cites the spec constantly and
# in prose forms this script has no business parsing; code is the place where
# a stale number goes unnoticed.
SEARCHED = ["libs", "apps"]
SEARCHED_SUFFIXES = {".cpp", ".cc", ".cxx", ".h", ".hpp", ".hh"}

# A citation, with the document it names. "proposal §2.1.1" resolves against
# docs/proposal.md; a bare "§6.2" or "spec §6.2" against the specification.
#
# Without the distinction a proposal citation is checked against the wrong
# document and passes or fails by coincidence -- "proposal §5.4" resolved
# today only because the spec happens to have a §5.4 of its own, about
# something else entirely. Proposal §2.1.1, which now justifies the
# stardata/starcore split from inside the code, has no such twin and failed.
CITATION = re.compile(r"(proposal\s+)?§(\d+(?:\.\d+)*)", re.IGNORECASE)
HEADING = re.compile(r"^#{2,4}\s+(\d+(?:\.\d+)*)\.?\s+\S", re.MULTILINE)
CLAUSE = re.compile(r"^(\d+)\.\s+\S", re.MULTILINE)


def spec_sections(spec_text: str) -> set[str]:
    """Every section number a citation may legitimately name."""
    sections = set(HEADING.findall(spec_text))

    # Numbered clauses, for the sections that use a list rather than
    # subheadings. Split the document on its headings and, inside each, count
    # top-level list items.
    parts = re.split(r"^(#{2,4})\s+(\d+(?:\.\d+)*)\.?\s+.*$", spec_text, flags=re.MULTILINE)
    for index in range(2, len(parts), 3):
        number, body = parts[index], parts[index + 1]
        for clause in CLAUSE.findall(body):
            sections.add(f"{number}.{clause}")
    return sections


def searched_files(repo_root: Path) -> list[Path]:
    out = subprocess.run(
        ["git", "ls-files", "--"] + SEARCHED,
        cwd=repo_root,
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    paths = [repo_root / line for line in out.splitlines() if line]
    return [p for p in paths if p.suffix in SEARCHED_SUFFIXES and p.is_file()]


def main() -> int:
    repo_root = Path(
        subprocess.run(
            ["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True
        ).stdout.strip()
    )

    documents = {}
    for label, relative in (("spec", SPEC), ("proposal", PROPOSAL)):
        path = repo_root / relative
        if not path.is_file():
            print(f"{relative} not found", file=sys.stderr)
            return 2
        documents[label] = (relative, spec_sections(path.read_text(encoding="utf-8")))

    dangling = []
    citations = 0
    for path in searched_files(repo_root):
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for lineno, line in enumerate(text.splitlines(), start=1):
            for qualifier, number in CITATION.findall(line):
                citations += 1
                document, sections = documents["proposal" if qualifier else "spec"]
                if number not in sections:
                    rel = path.relative_to(repo_root).as_posix()
                    dangling.append(f"{rel}:{lineno}: §{number} is not a section of {document}")

    if dangling:
        print("dangling specification references:", file=sys.stderr)
        for item in dangling:
            print(f"  {item}", file=sys.stderr)
        print(
            "\nThe specification was probably renumbered. Check what each cited section "
            "means now -- a citation that still resolves may have stopped being the right "
            "one.",
            file=sys.stderr,
        )
        return 1

    print(f"ok: all {citations} reference(s) resolve against {SPEC} and {PROPOSAL}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
