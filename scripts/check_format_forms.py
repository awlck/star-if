#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Adrian Welcker
"""
check_format_forms.py — the format forms and their readers say the same thing.

WHAT THIS ASSERTS
    Spec §7.2.4 defines a *format form* as one `libs/stardata` parses with a
    hard-coded reader rather than validating generically against a schema.
    Every such form is therefore stated twice: once as data, in
    `libs/stardata/builtin/format.star`, so that documentation, editor
    generation and validation have the one source §7.1 asks for -- and once as
    C++, in the reader.

    Two statements of one thing drift. They did: `class` declared a `traits`
    key that `read_class` did not read for the whole of F2 through F11, so
    every property arriving through a trait resolved to nothing and no test
    noticed. This is the check whose absence allowed that.

WHAT IT CHECKS, EXACTLY
    1. Every form the loader dispatches to a reader is declared in
       format.star, and every format form has a reader. The dispatch is read
       out of `fold_declaration`, so adding a branch is covered without
       editing this script.

    2. Every block key a reader reads is declared by the form it serves. This
       is the direction that catches hidden behaviour: a reader that acts on
       `root = yes` while no schema mentions `root` gives the format a feature
       no author can discover and no editor can offer.

       The other direction is deliberately NOT an error. A format form's keys
       are validated generically like everybody else's -- `validate_block`
       runs on `global` before `read_global` sees it -- so a key that no
       reader names is not unread, merely uninteresting to the reader.
       `global`'s `initial` is the case: checked against `type` by §7.2's
       `type_of`, and named in no C++ at all.

    3. For `schema` and `key`, the two bootstrap forms, the two sides must
       match EXACTLY. Nothing validates those generically -- they are what
       reads the files everything else is validated against -- so a key
       declared and unread there really is unread.

    A reader this script cannot map to a form is a hard error, not a skip.
    Silently checking less is how a check like this stops meaning anything.

USAGE
    python3 scripts/check_format_forms.py         # exit 1 on any disagreement
    python3 scripts/check_format_forms.py --list  # print the pairing, exit 0
"""

import argparse
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
FORMAT = REPO / "libs" / "stardata" / "builtin"
SCHEMA_CPP = REPO / "libs" / "stardata" / "src" / "schema" / "schema.cpp"
LOADER_CPP = REPO / "libs" / "stardata" / "src" / "schema" / "loader.cpp"

# A reader, and the whole of its body: a signature at column 0 mentioning
# `read_x(` or `collect_x(`, up to the next `}` at column 0. Bounding it that
# way matters -- a function that ran to the next signature would swallow
# everything between two readers and credit it to the first.
READER_RE = re.compile(
    r"^(?:\[\[nodiscard\]\]\s*)?[A-Za-z_][^\n=;]*?\b((?:read|collect)_[a-z_]+)\(", re.M
)

# Reading one key out of a block, in each of the spellings the readers use.
KEY_READ_RE = re.compile(
    r'(?:text_of|flag_of|identifiers_of)\([^,]+,\s*"([A-Za-z_][A-Za-z0-9_]*)"\)'
    r'|->(?:find|find_all|value_of)\("([A-Za-z_][A-Za-z0-9_]*)"\)'
    r'|\.(?:find|find_all|value_of)\("([A-Za-z_][A-Za-z0-9_]*)"\)'
)

# `fold_declaration`'s dispatch: `if (key == "class" || key == "trait") { ...
# read_class(...) }`. The forms and the reader come out of the same branch, so
# the pairing is the loader's own and not this script's opinion of it.
FOLD_RE = re.compile(r"void fold_declaration\(.*?\n\}", re.S)
BRANCH_RE = re.compile(
    r'if \((key == "[A-Za-z_]+"(?:\s*\|\|\s*key == "[A-Za-z_]+")*)\) \{(.*?)\n    \}', re.S
)
FORM_RE = re.compile(r'key == "([A-Za-z_]+)"')
CALL_RE = re.compile(r"\b((?:read|collect)_[a-z_]+)\(")

# The nested and helper readers, which no dispatch names because no top-level
# statement is one. Each is listed with the forms whose keys it may read.
#
# `read_prop_defs` is the awkward one on purpose: it reads the `prop_def` key
# off whichever block owns it, and then each entry against `prop_marker`. It
# therefore answers to three forms at once, which is exactly why it is a
# shared helper rather than three copies.
HELPERS = {
    "read_schema": {"schema"},
    "read_key": {"key"},
    "read_prop_defs": {"class", "class_extension", "prop_def", "prop_marker"},
    "read_local_prop_defs": {"prop_def", "prop_marker"},
    "read_markers": {"prop_marker"},
    "collect_library_manifest": {"library"},
    # §7.6's `@replaces(lib)` is an annotation, not a key of anything.
    "read_replaces": set(),
    # Not a reader of Stardata at all: it reads a file off the disk.
    "read_bytes": set(),
}

# The two forms that cannot be in format.star, because they are what reads it.
BOOTSTRAP = ("schema", "key")
BOOTSTRAP_RE = re.compile(r"const Schema& \w+\(\) \{(.*?)\n\}", re.S)
BOOTSTRAP_KEY_RE = re.compile(r'bootstrap_key\(\s*"([A-Za-z_][A-Za-z0-9_.]*)"')

# A `schema = { id = X ... key = { name = Y } ... }` in a builtin file.
SCHEMA_DECL_RE = re.compile(r"^schema\s*=\s*\{", re.M)
ID_RE = re.compile(r"\bid\s*=\s*([A-Za-z_][A-Za-z0-9_.]*)")
KEY_NAME_RE = re.compile(r"\bkey\s*=\s*\{[^}]*?\bname\s*=\s*([A-Za-z_][A-Za-z0-9_.]*)", re.S)


def die(message):
    raise SystemExit(f"check_format_forms.py: {message}")


def readers():
    """Every reader in the schema sources, as {name: the keys it reads}."""
    found = {}
    for path in (SCHEMA_CPP, LOADER_CPP):
        text = path.read_text(encoding="utf-8")
        for match in READER_RE.finditer(text):
            end = text.find("\n}", match.start())
            if end < 0:
                die(f"{match.group(1)} in {path.name} has no closing brace at column 0")
            body = text[match.start() : end]
            keys = {next(g for g in m.groups() if g) for m in KEY_READ_RE.finditer(body)}
            found.setdefault(match.group(1), set()).update(keys)
    return found


def dispatch():
    """`fold_declaration`'s pairing of form ids to readers, as {form: reader}."""
    fold = FOLD_RE.search(LOADER_CPP.read_text(encoding="utf-8"))
    if fold is None:
        die(
            "could not find fold_declaration in loader.cpp. If it was renamed, "
            "update the regex -- silently checking fewer forms is worse than failing."
        )
    paired = {}
    for branch in BRANCH_RE.finditer(fold.group(0)):
        calls = CALL_RE.findall(branch.group(2))
        if not calls:
            continue
        for form in FORM_RE.findall(branch.group(1)):
            paired[form] = calls[0]
    if not paired:
        die("fold_declaration dispatches to no reader, which cannot be right")
    return paired


def declared():
    """Every form declared in format.star, as {id: its key names}, plus the two
    bootstrap forms read out of the C++ that declares them."""
    forms = {}
    for path in sorted(FORMAT.glob("*.star")):
        text = path.read_text(encoding="utf-8")
        starts = [m.start() for m in SCHEMA_DECL_RE.finditer(text)]
        for i, start in enumerate(starts):
            end = starts[i + 1] if i + 1 < len(starts) else len(text)
            body = text[start:end]
            ids = ID_RE.findall(body)
            if ids:
                forms[ids[0]] = set(KEY_NAME_RE.findall(body))

    text = SCHEMA_CPP.read_text(encoding="utf-8")
    for match in BOOTSTRAP_RE.finditer(text):
        body = match.group(1)
        ids = re.findall(r'result\.id = "([A-Za-z_]+)"', body)
        if ids:
            forms[ids[0]] = set(BOOTSTRAP_KEY_RE.findall(body))
    for form in BOOTSTRAP:
        if form not in forms:
            die(f"the bootstrap form '{form}' was not found in {SCHEMA_CPP.name}")
    return forms


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="print the pairing and exit")
    args = parser.parse_args()

    found = readers()
    paired = dispatch()
    forms = declared()

    # Which forms each reader answers to: the loader's dispatch, plus the
    # helpers no dispatch names.
    serves = {name: set() for name in found}
    for form, reader in paired.items():
        serves.setdefault(reader, set()).add(form)
    for reader, form_ids in HELPERS.items():
        if reader in serves:
            serves[reader] |= form_ids

    if args.list:
        for reader in sorted(serves):
            print(f"{reader:26} -> {', '.join(sorted(serves[reader])) or '(no form)'}")
        return 0

    problems = []

    unmapped = sorted(name for name, form_ids in serves.items() if not form_ids and name not in HELPERS)
    if unmapped:
        die(
            "these readers answer to no form: "
            + ", ".join(unmapped)
            + ". Add the branch to fold_declaration, or list it in HELPERS with "
            "the forms whose keys it reads."
        )

    # 1. A reader implies a declaration, and a format form implies a reader.
    for form, reader in sorted(paired.items()):
        if form not in forms:
            problems.append(
                f"'{form}' is parsed by {reader} and declared nowhere. A form the "
                f"format layer reads itself is a format form, and belongs in "
                f"libs/stardata/builtin/format.star (spec §7.2.4)."
            )

    # 2. Every key a reader reads is declared by a form it serves.
    for reader in sorted(found):
        known = set()
        for form in serves.get(reader, ()):
            known |= forms.get(form, set())
        # A form id is not a key: `set.find("prop_marker")` names a form, and
        # `view.find_all("schema")` names a top-level statement.
        known |= set(forms)
        for key in sorted(found[reader] - known):
            where = ", ".join(sorted(serves.get(reader, ()))) or "no form"
            problems.append(
                f"{reader} reads '{key}', which {where} does not declare. A key "
                f"the reader acts on and no schema mentions is behaviour no "
                f"author can find and no editor can offer (spec §7.1, §7.2.4)."
            )

    # 3. The bootstrap pair, where the match is exact in both directions.
    for form in BOOTSTRAP:
        reader = next((r for r, f in serves.items() if form in f), None)
        if reader is None:
            die(f"nothing reads the bootstrap form '{form}'")
        for key in sorted(forms[form] - found[reader]):
            problems.append(
                f"the bootstrap form '{form}' declares '{key}' and {reader} never "
                f"reads it. Nothing validates the bootstrap generically, so a key "
                f"declared here and unread is a key that does nothing."
            )

    for problem in problems:
        print(f"error: {problem}", file=sys.stderr)
    if problems:
        print(f"\n{len(problems)} disagreement(s) between format.star and its readers.",
              file=sys.stderr)
        return 1

    print(
        f"ok: {len(paired)} dispatched form(s) and {len(found)} reader(s) agree "
        f"with the {len(forms)} declared format form(s)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
