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
    Two directories, and which one a name is declared in decides the answer.
    That is a change: the sets used to be carved out of `libs/starcore/builtin/`
    alone, by treating the `id` of a `schema` and the `name` of each `key`
    inside one as mechanism. It was much weaker than it looked — `restrictions`,
    `failureMsg`, `when` and `effects` are keys of the core `action` and `rule`
    forms, so that carve-out exempted the four names §2.1.1 uses as its own
    examples of vocabulary. Twenty names were guarded and eighty-four exempt.

      * libs/stardata/builtin/ — the SCHEMA LANGUAGE. The forms the format
        layer parses with readers of its own (spec §7.2.4): `class`, `trait`,
        `enum`, `global`, `library`, and the keys inside them — `of_class`,
        `prop_def`, `sealed`, `arity`, `type`, `id`, `doc`. `stardata`
        implements these and so must name them.

      * libs/starcore/builtin/ — the VOCABULARY, all of it. Form ids and their
        key names as well as class ids, property names, enum ids and enum
        values: `action`, `restrictions`, `failureMsg`, `starcore.object`,
        `holder`, `relation`, `carried`. `stardata` may name none of them.

    Plus two sets read out of the C++, for names the format defines in code
    rather than in data: the bootstrap forms `schema` and `key` (which are what
    read the .star files, so they cannot be in one), and §5.4.1's annotation
    names. The second is why `style` is allowed — `@style` is an annotation of
    the format's own grammar, and `style` is separately a core-owned form.

    A name in *both* sets counts as schema language, which is what makes `doc`
    and `id` usable: they are keys of `action` and also keys of `key`, and no
    amount of moving code would let the schema layer stop saying them.
    Reporting those would be a false positive nobody could clear, which is how
    a check gets disabled.

    Every set is derived from the files by structure, never listed here. A
    property added to `starcore.object`, a key added to `action`, or an
    annotation added to §5.4.1 is covered the next time this runs.

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
FORMAT = REPO / "libs" / "stardata" / "builtin"
BUILTIN = REPO / "libs" / "starcore" / "builtin"
GUARDED = REPO / "libs" / "stardata"

# The bootstrap: `schema` and `key` themselves, which are the two forms not in
# format.star because they are what reads it (spec §7.1, and that file's header
# comment). Their field names -- `name`, `type`, `arity`, `top_level` -- are
# schema language every bit as much as the key names declared in a file, and
# are read from the C++ for the same reason the rest is read from the .star
# files: so that adding one is covered without editing this script.
BOOTSTRAP = REPO / "libs" / "stardata" / "src" / "schema" / "schema.cpp"
BOOTSTRAP_RE = re.compile(r'bootstrap_key\(\s*"([A-Za-z_][A-Za-z0-9_.]*)"')

# §5.4.1's annotations, which the format defines in code because they are
# grammar rather than data (§3.8). Read from the one function that maps a
# spelling to a kind, so adding an annotation is covered here too.
#
# This is what makes `style` legal in libs/stardata: `@style(id)` is an
# annotation of the format's own grammar, while `style` is separately a
# core-owned form (§9.3). Two things, one spelling.
ANNOTATIONS = REPO / "libs" / "stardata" / "src" / "schema" / "annotation.cpp"
ANNOTATION_FN_RE = re.compile(
    r"annotation_from_string\(std::string_view name\) noexcept \{(.*?)\n\}", re.S
)
ANNOTATION_NAME_RE = re.compile(r'name == "([A-Za-z_][A-Za-z0-9_]*)"')

# §6.2's type names, which the format also defines in code. Same collision as
# annotations and the same resolution: `duration` is a type ("Integer, or
# `default`, in ticks") and separately a key of the core `action` form, so the
# spelling appears in both directories and means two different things.
TYPES = REPO / "libs" / "stardata" / "src" / "schema" / "types.cpp"
TYPE_FN_RE = re.compile(r"type_names\(const SchemaSet& set\) \{(.*?)\n\}", re.S)
TYPE_NAME_RE = re.compile(r'"([a-z_]+)"')

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


def names_in(text):
    """Every identifier a builtin file declares: ids, key names, properties,
    enum values. What the name *means* is decided by which directory it was
    found in, not by its shape."""
    found = set()
    for _form, body in declarations(text):
        ids = ID_RE.findall(body)
        if ids:
            found.add(ids[0])
        found.update(KEY_NAME_RE.findall(body))
        for block in PROP_BLOCK_RE.findall(body):
            found.update(PROP_RE.findall(block))
        for listed in VALUES_RE.findall(body):
            found.update(listed.split())
    return found


def collect():
    """The vocabulary and schema-language sets, read from the two builtin
    directories plus the two things the format defines in C++."""
    mechanism = set()
    vocabulary = set()

    for path in sorted(FORMAT.glob("*.star")):
        mechanism |= names_in(path.read_text(encoding="utf-8"))
    for path in sorted(BUILTIN.glob("*.star")):
        vocabulary |= names_in(path.read_text(encoding="utf-8"))

    mechanism.update(BOOTSTRAP_RE.findall(BOOTSTRAP.read_text(encoding="utf-8")))

    for path, pattern, names_re, what in (
        (ANNOTATIONS, ANNOTATION_FN_RE, ANNOTATION_NAME_RE, "annotation_from_string"),
        (TYPES, TYPE_FN_RE, TYPE_NAME_RE, "type_names"),
    ):
        body = pattern.search(path.read_text(encoding="utf-8"))
        if body is None:
            # Loudly, because the failure mode of a silent miss here is a set
            # that guards fewer names than it claims to.
            raise SystemExit(
                f"check_layering.py: could not find {what} in "
                f"{path.relative_to(REPO)}. If it was renamed, update the regex "
                "-- silently guarding less is worse than failing."
            )
        mechanism.update(names_re.findall(body.group(1)))

    # A name in both sets is schema language: `doc` and `id` are keys of
    # `action` and also keys of `key`, and nothing would let the schema layer
    # stop saying them.
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
