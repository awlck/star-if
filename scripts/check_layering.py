#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Adrian Welcker
"""
check_layering.py — the grep test of docs/proposal.md §2.1.1.

WHAT THIS ASSERTS
    `libs/stardata` implements the format and the schema *mechanism*;
    `libs/starcore` implements the *vocabulary* expressed in it. The mechanical
    form of that rule, from §2.1.1:

        Does the code reference an identifier that appears in
        libs/starcore/builtin/? `holder`, `relation`, `restrictions`,
        `failureMsg`, `when`, `effects`, `starcore.object` — if any of those is
        a string literal in libs/stardata/, the layering has leaked.

    §2.1.1 asks for this as a CI check for the same reason spec §7.2.2 insists
    core assert rather than hope: a boundary nobody verifies is a boundary that
    erodes.

WHICH IDENTIFIERS, EXACTLY
    Not every identifier in builtin/, and this is the one place the script is
    less literal than the sentence above. builtin/ holds two different kinds of
    name, because it declares the core-owned *forms* of spec §7.2.4 as well as
    the object model of §8:

      * SCHEMA-LANGUAGE names — the `id` of each `schema`, and the `name` of
        each `key` inside one: `schema`, `class`, `trait`, `enum`, `of_class`,
        `prop_def`, `sealed`, `arity`, `type`, `id`, `doc`. These *are* the
        mechanism. Spec §1.2.1 puts §2–§7 in Stardata's column, and a schema
        layer that could not name `key` could not read a schema. `stardata`
        must reference them.

      * VOCABULARY names — class and trait ids, their property names, enum ids
        and enum values: `starcore.object`, `holder`, `relation`, `present_in`,
        `busy_until`, `relation_enum`, `in`, `carried`, `on_success`. These are
        interactive fiction. Every example §2.1.1 gives is one of these.

    So the check is over the vocabulary set. A name in *both* sets is treated
    as schema-language and allowed: `name` is a property of `starcore.object`
    and also the `name` key of the `key` form, and no amount of moving code
    would let the schema layer stop saying "name". Reporting those would be a
    false positive nobody could ever clear, which is how a check gets disabled.

    The set is derived from the files by structure, never from a list here. A
    property added to `starcore.object` is covered the next time this runs.

USAGE
    python3 scripts/check_layering.py          # exit 1 on any leak
    python3 scripts/check_layering.py --list   # print both sets and exit 0

Exit status is 0 when nothing leaks.
"""

import argparse
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
BUILTIN = REPO / "libs" / "starcore" / "builtin"
GUARDED = REPO / "libs" / "stardata"

# The bootstrap: `schema` and `key` themselves, which are the two forms not in
# builtin/ because they are what reads it (spec §7.1, and the header comment
# of builtin/schema.star). Their field names -- `name`, `type`, `arity`,
# `top_level` -- are schema language every bit as much as the key names
# declared in a file, and are read from the C++ for the same reason the rest is
# read from the .star files: so that adding one is covered without editing
# this script.
BOOTSTRAP = REPO / "libs" / "stardata" / "src" / "schema" / "schema.cpp"
BOOTSTRAP_RE = re.compile(r'bootstrap_key\(\s*"([A-Za-z_][A-Za-z0-9_.]*)"')

# A `key = { name = X ... }` inside a schema, and a top-level `schema = { id = X`.
KEY_NAME_RE = re.compile(r"\bkey\s*=\s*\{[^}]*?\bname\s*=\s*([A-Za-z_][A-Za-z0-9_.]*)", re.S)
DECL_RE = re.compile(r"^(schema|class|trait|enum)\s*=\s*\{", re.M)
ID_RE = re.compile(r"\bid\s*=\s*([A-Za-z_][A-Za-z0-9_.]*)")
VALUES_RE = re.compile(r"\bvalues\s*=\s*\{([^}]*)\}")
PROP_BLOCK_RE = re.compile(r"\bprop_def\s*=\s*\{(.*?)\n    \}", re.S)
PROP_RE = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_.]*)\s*=", re.M)

# A C or C++ string literal. Escapes are consumed so that a literal containing
# `\"` does not end early; the contents are compared whole, since a name that
# merely appears inside a sentence in a diagnostic is prose, not a reference.
STRING_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')

# A `//` or `/* */` comment. Stripped before scanning, because a comment
# explaining *why* placement lives elsewhere has to be able to say "holder".
COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)


def declarations(text):
    """Each top-level declaration in one builtin file, as (form, body)."""
    starts = [(m.group(1), m.start()) for m in DECL_RE.finditer(text)]
    for i, (form, start) in enumerate(starts):
        end = starts[i + 1][1] if i + 1 < len(starts) else len(text)
        yield form, text[start:end]


def collect():
    """The vocabulary and schema-language identifier sets, read from builtin/."""
    vocabulary = set()
    mechanism = set()

    for path in sorted(BUILTIN.glob("*.star")):
        text = path.read_text(encoding="utf-8")
        for form, body in declarations(text):
            ids = ID_RE.findall(body)
            own_id = ids[0] if ids else None

            if form == "schema":
                # The form's own name and every key it declares: the schema
                # language, which stardata implements and so must name.
                if own_id:
                    mechanism.add(own_id)
                mechanism.update(KEY_NAME_RE.findall(body))
                continue

            # class, trait, enum: the vocabulary.
            if own_id:
                vocabulary.add(own_id)
            for block in PROP_BLOCK_RE.findall(body):
                vocabulary.update(PROP_RE.findall(block))
            for listed in VALUES_RE.findall(body):
                vocabulary.update(listed.split())

    mechanism.update(BOOTSTRAP_RE.findall(BOOTSTRAP.read_text(encoding="utf-8")))

    # `type` and `doc` appear inside prop_def marker blocks as well as being
    # key names; the mechanism set already has them, and the subtraction below
    # removes them from the guarded set.
    return vocabulary - mechanism, mechanism


def sources():
    for pattern in ("*.cpp", "*.hpp"):
        yield from sorted(GUARDED.rglob(pattern))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="print both sets and exit")
    args = parser.parse_args()

    guarded, mechanism = collect()

    if args.list:
        print(f"vocabulary ({len(guarded)}) — may not appear in libs/stardata/:")
        print("   ", " ".join(sorted(guarded)))
        print(f"\nschema language ({len(mechanism)}) — stardata implements these:")
        print("   ", " ".join(sorted(mechanism)))
        return 0

    leaks = []
    for path in sources():
        text = COMMENT_RE.sub(" ", path.read_text(encoding="utf-8"))
        for line_number, line in enumerate(text.split("\n"), start=1):
            for match in STRING_RE.finditer(line):
                if match.group(1) in guarded:
                    leaks.append((path.relative_to(REPO), line_number, match.group(1)))

    if leaks:
        print("layering leak — proposal §2.1.1: a core vocabulary identifier is a")
        print("string literal in libs/stardata/, which implements the mechanism only.")
        print()
        for path, line_number, name in leaks:
            print(f"  {path}:{line_number}: \"{name}\"")
        print()
        print("Move the pass that needs it into libs/starcore, or -- if the identifier")
        print("really is part of the schema language -- declare it as a key of a core")
        print("form so that this script classifies it as such.")
        return 1

    print(
        f"ok: none of the {len(guarded)} core vocabulary identifier(s) appears as a "
        f"string literal in libs/stardata/"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
