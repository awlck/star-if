#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Adrian Welcker
"""
check_stardata.py — a conformance checker for the Stardata format.

WHAT THIS IS
    A deliberately small, dependency-free checker that validates .star files
    against docs/stardata-spec.md. It exists so that the specification and the
    conformance corpus cannot silently diverge during the period before the
    real parser (libs/stardata) is written.

WHAT THIS IS NOT
    This is a Level 1 checker plus a handful of Level 3 checks that happen not
    to need the schema layer (spec §1.2). It does NOT validate types, arity,
    unknown keys, references, or anything else requiring schemas. Those arrive
    with Starforge. When they do, THIS SCRIPT SHOULD BE DELETED rather than
    maintained in parallel — two validators that disagree are worse than one.

    It also does not build a lossless CST, so it cannot verify the round-trip
    property of spec §14.2. That check belongs to the real parser.

USAGE
    python3 tests/check_stardata.py                 # check tests/corpus/*.star
    python3 tests/check_stardata.py FILE...         # check specific files
    python3 tests/check_stardata.py --self-test     # also run invalid/ fixtures
    python3 tests/check_stardata.py --strict        # treat warnings as failures
    python3 tests/check_stardata.py --stats         # print a token/form summary

    Exit status is 0 if no errors (and, under --strict, no warnings).

SUPPRESSION
    A file may silence a diagnostic for its whole length:

        # check: allow W-LOC-UNUSED

    A suppression that turns out to be unnecessary is itself reported
    (W-PRAGMA-UNUSED), so stale ones surface instead of accumulating.

DIAGNOSTIC CODES
    Every diagnostic carries a stable code so that CI assertions and the
    negative fixtures in tests/corpus/invalid/ do not depend on message text.
    Codes beginning E are errors, W are warnings. See CODES below.
"""

import argparse
import os
import re
import sys
from collections import Counter, defaultdict

# --------------------------------------------------------------------------
# Diagnostic codes, each mapped to the specification section it enforces.
# --------------------------------------------------------------------------

CODES = {
    # Lexical (spec §3)
    "E-UNICODE-WS":         "§3.1 non-ASCII whitespace",
    "E-STR-MULTILINE":      "§3.5 string literal spans a line terminator",
    "E-STR-ESCAPE":         "§3.5 invalid escape sequence",
    "E-STR-UNTERMINATED":   "§3.5 unterminated string literal",
    "E-DEC-PRECISION":      "§3.4 decimal must have exactly three fractional digits",
    "E-DEC-LEADING-DOT":    "§3.4 decimal may not start with '.'",
    "E-NUM-TRAILING-DOT":   "§3.4 decimal may not end with '.'",
    "E-INT-RANGE":          "§3.4 integer literal outside signed 64-bit range",
    "E-BRACKET-OUTSIDE":    "§3.7 '[' or ']' outside a string literal",
    "E-RESERVED-WORD":      "§3.9 reserved word used as a value",
    "E-OP-REMOVED":         "§6.3.1 the '?=' operator was removed; use '='",
    "E-BAD-CHAR":           "§3 unexpected character",
    # Structural (spec §4, §5)
    "E-BRACE-UNBALANCED":   "§4 unbalanced braces",
    "E-BLOCK-MIXED":        "§5.2 block mixes statements and bare scalars",
    "E-STRAY-TOKEN":        "§4 stray token where a statement was expected",
    # Templates and localisation (spec §9)
    "E-TEMPLATE-BRACKETS":  "§9.1 unbalanced brackets in a template",
    "E-STYLE-UNDECLARED":   "§9.3 undeclared style name",
    "E-LOC-DUPLICATE":      "§9.6 duplicate localisation key",
    "E-LOC-UNDEFINED":      "§9.6 localisation key referenced but not defined",
    "W-LOC-UNUSED":         "§9.6 localisation key defined but never referenced",
    # failureMsg placement (spec §10.5)
    "E-FAILMSG-SILENT":     "§10.5 failureMsg in a silent context",
    "E-FAILMSG-UNREACHABLE":"§10.5.1 failureMsg below a NOT/OR/COUNT_AT_LEAST",
    "W-FAILMSG-MISSING":    "§10.5.3 restriction with no reachable failure message",
    # Globals, constants and flags (spec §6.4). Emitted since the first
    # version of this script and missing from this table until F10 added
    # E-GLOBAL-UNDECLARED beside them and noticed.
    "E-FLAG-UNDECLARED":    "§6.4.1 set_flag/clear_flag/flag_set names no declared global",
    "E-FLAG-NOT-BOOL":      "§6.4.1 ...names a global whose type is not bool",
    "E-GLOBAL-UNDECLARED":  "§6.4 a reference to an undeclared global or const",
    "W-GLOBAL-UNUSED":      "§6.4 a declared global or const nothing reads",
    # Operator context (heuristic, see check_operator_context)
    "W-CMP-OUTSIDE-COND":   "§3.6 comparison operator outside a condition context",
    "W-EQ-INSIDE-COND":     "§3.6 bare '=' in a condition context; did you mean '=='?",
    # Checker hygiene
    "W-PRAGMA-UNUSED":      "a '# check: allow' pragma that suppressed nothing",
    "E-PRAGMA-UNKNOWN":     "a '# check: allow' pragma naming an unknown code",
    "E-DOC-EXAMPLE":        "a ```stardata example in the docs does not parse",
    # Schema layer (§7). Listed so a `# check: allow` pragma naming one is
    # recognised; none is produced here — see NEEDS_SCHEMA_LAYER below.
    "E-SCHEMA-INVALID":     "§7.2 a schema declaration the schema layer cannot use",
    "E-SCHEMA-DUPLICATE":   "§7.6 two declarations share an id, without @replaces",
    "E-SCHEMA-SEALED":      "§7.2.2 redefinition of a sealed core declaration",
    "E-KEY-MISSING":        "§7.2 a required key is absent",
    "E-CORE-REPARENT":      "§8.2 class_extension changes a class's of_class",
    "E-CORE-REQUIREMENT":   "§7.2.5 something core requires is absent or wrong",
    "E-CORE-RESERVED":      "§7.2.5.1 a reserved internal form declared by something other than starcore",
    "W-PROVIDES-MISMATCH":  "§13.3 provides_schema disagrees with what the library declares",
    "E-PLACEMENT-CONFLICT": "§8.5 a relation keyword and holder/relation in one block",
    "E-PROPDEF-TYPE-MISMATCH": "§8.7 a redeclaration with a different type",
    "W-PROPDEF-REDUNDANT":  "§8.7 a redeclaration with the same type",
    "E-DUP-KEY":            "§5.3 a second binding of a key whose arity is one",
    "E-EXCLUSIVE-GROUP":    "§7.2.1 two or more keys of one exclusive_group in a block",
    "E-EXCLUSIVE-MISSING":  "§7.2.1 no key of a required exclusive_group",
    "E-TYPE-MISMATCH":      "§6.2 a value that does not match its declared type",
    "E-REF-UNRESOLVED":     "§6.2 a ref<C> naming no declared object or form instance",
    "E-UNKNOWN-ANNOTATION": "§3.8 an annotation §5.4.1 does not define",
    "E-ANNOT-CONFLICT":     "§5.4.1 two combination annotations on one value",
    "E-ANNOT-MISAPPLIED":   "§5.4.1 an annotation on a value it does not apply to",
    "E-ANNOT-ARGUMENT":     "§3.8 an annotation's arguments are not what it takes",
    "E-PROP-ABSENT":        "§8.8.2 a property read that is definitely absent for the slot's type",
    "E-PROP-MAYBE-ABSENT":  "§8.8.3 a property read that is possibly absent and not narrowed",
}

# Codes this script structurally cannot produce, and the fixtures that
# declare them are therefore skipped by --self-test rather than failed.
#
# Most are schema-layer diagnostics (spec §7.2, §7.2.2) whose check is not
# hard but is meaningless without the core-owned schema set: "this redefines
# a sealed form" requires knowing which forms are sealed, which means loading
# libs/starcore/builtin/ -- a validator, not a linter.
#
# The four annotation codes are here for the other reason. §5.4.1 needs no
# schema set at all, and this script could check it; it does not, because
# adding a pass to something backlog H4 exists to delete would be paying
# twice for a check the C++ already makes from these same fixtures.
#
# The two property-read codes are `libs/starcore`'s rather than the schema
# layer's, and are here for the first reason at its strongest: §8.8's
# classification needs the whole class graph, every trait, and every
# object-local `prop_def` in the program before it can say whether one read
# is legal.
#
# The C++ suite asserts every one of them, from these same fixtures, in
# tests/unit/schema/corpus_test.cpp. This list is the seam between the two
# implementations described in tests/README.md, and it should shrink to
# nothing the same way this whole script does: by being deleted once
# Starforge can validate the corpus (backlog H2).
NEEDS_SCHEMA_LAYER = {
    "E-SCHEMA-INVALID",
    "E-SCHEMA-DUPLICATE",
    "E-SCHEMA-SEALED",
    "E-KEY-MISSING",
    "E-CORE-REPARENT",
    "E-CORE-REQUIREMENT",
    "E-PROPDEF-TYPE-MISMATCH",
    "E-UNKNOWN-KEY",
    "W-PROVIDES-MISMATCH",
    "E-CORE-RESERVED",
    "E-PLACEMENT-CONFLICT",
    "W-PROPDEF-REDUNDANT",
    "E-DUP-KEY",
    "E-EXCLUSIVE-GROUP",
    "E-EXCLUSIVE-MISSING",
    "E-TYPE-MISMATCH",
    "E-REF-UNRESOLVED",
    "E-UNKNOWN-ANNOTATION",
    "E-ANNOT-CONFLICT",
    "E-ANNOT-MISAPPLIED",
    "E-ANNOT-ARGUMENT",
    "E-PROP-ABSENT",
    "E-PROP-MAYBE-ABSENT",
}

# A file may suppress a diagnostic for its whole length with a pragma:
#     # check: allow W-LOC-UNUSED, W-CMP-OUTSIDE-COND
# Suppressions that turn out to be unnecessary are themselves reported, so
# they cannot go stale.
PRAGMA_RE = re.compile(r"^\s*#\s*check:\s*allow\s+(.+)$")

RESERVED_VALUES = {"true", "false"}
VALID_ESCAPES = set('"\\nt[]$@u')

# Keys whose value is a condition_block in stdlib/stdlib. The checker has no
# schema layer, so this list stands in for one; it is the only place in this
# script that hard-codes library knowledge.
CONDITION_KEYS = {
    "restrictions", "conditions", "when", "complete_when", "abandon_when",
    "on_fail", "requires",
}
SILENT_CONDITION_KEYS = {"conditions", "when", "abandon_when", "complete_when"}

# Combinators that fail as a whole, so their children cannot explain a failure.
BARRIERS = {"NOT", "OR", "COUNT_AT_LEAST"}


class Diag:
    __slots__ = ("code", "path", "line", "msg")

    def __init__(self, code, path, line, msg=""):
        self.code, self.path, self.line, self.msg = code, path, line, msg

    @property
    def severity(self):
        return "error" if self.code.startswith("E-") else "warning"

    def __str__(self):
        detail = self.msg or CODES.get(self.code, "")
        return "%s:%d: %s: %s: %s" % (
            self.path, self.line, self.severity, self.code, detail)


# --------------------------------------------------------------------------
# Tokenizer
# --------------------------------------------------------------------------

class Token:
    __slots__ = ("kind", "text", "pos", "line")

    def __init__(self, kind, text, pos, line):
        self.kind, self.text, self.pos, self.line = kind, text, pos, line


def tokenize(src, path, diags):
    """Produce a token list. Kinds: id num str lockey ann op angle punct.

    'angle' is a bare '<' or '>', left ambiguous here and resolved by the
    parser per spec §4.2: in an operator position it is a comparison, in a
    value position it opens a type-argument list. The lexer cannot decide,
    and MUST NOT use whitespace to guess (spec §3.1 makes it insignificant).
    """
    toks = []
    i, n = 0, len(src)
    line = 1

    def add(kind, text, pos):
        toks.append(Token(kind, text, pos, src.count("\n", 0, pos) + 1))

    while i < n:
        c = src[i]

        if c in " \t\r\n":
            i += 1
            continue
        if c in "\u00a0\u2028\u2029\u200b\ufeff" and i != 0:
            diags.append(Diag("E-UNICODE-WS", path, src.count("\n", 0, i) + 1,
                              "U+%04X" % ord(c)))
            i += 1
            continue
        if c == "#":
            j = src.find("\n", i)
            i = n if j < 0 else j
            continue

        if c == '"':
            j, closed = i + 1, False
            while j < n:
                if src[j] == "\\":
                    nxt = src[j + 1] if j + 1 < n else ""
                    if nxt not in VALID_ESCAPES:
                        diags.append(Diag("E-STR-ESCAPE", path,
                                          src.count("\n", 0, j) + 1,
                                          "\\%s" % (nxt or "<eof>")))
                    j += 2
                    continue
                if src[j] == "\n":
                    diags.append(Diag("E-STR-MULTILINE", path,
                                      src.count("\n", 0, i) + 1))
                    break
                if src[j] == '"':
                    closed = True
                    break
                j += 1
            if not closed and j >= n:
                diags.append(Diag("E-STR-UNTERMINATED", path,
                                  src.count("\n", 0, i) + 1))
            add("str", src[i + 1:j], i)
            i = j + 1
            continue

        m = re.match(r"\$[A-Za-z_][A-Za-z0-9_.]*", src[i:])
        if m:
            add("lockey", m.group(0)[1:], i)
            i += m.end()
            continue

        m = re.match(r"@[A-Za-z_][A-Za-z0-9_]*(\([^)\n]*\))?", src[i:])
        if m:
            add("ann", m.group(0), i)
            i += m.end()
            continue

        # Multi-character operators MUST be matched before single-character
        # punctuation, or '>=' lexes as '>' followed by '='.
        # `?=` is still matched as one token, deliberately: a file written
        # against an older draft should be told the operator was removed
        # (spec §6.3.1), not that `?` is an unknown character.
        m = re.match(r"(==|!=|<=|>=|\+=|-=|\?=|=)", src[i:])
        if m:
            if m.group(0) == "?=":
                diags.append(Diag("E-OP-REMOVED", path,
                                  src.count("\n", 0, i) + 1, "use '=' instead"))
            add("op", m.group(0), i)
            i += m.end()
            continue

        if c in "{}(),":
            add("punct", c, i)
            i += 1
            continue
        if c in "<>":
            add("angle", c, i)
            i += 1
            continue
        if c in "[]":
            diags.append(Diag("E-BRACKET-OUTSIDE", path,
                              src.count("\n", 0, i) + 1, repr(c)))
            i += 1
            continue

        m = re.match(r"-?[0-9]+\.[0-9]*", src[i:])
        if m:
            txt = m.group(0)
            ln = src.count("\n", 0, i) + 1
            frac = txt.split(".", 1)[1]
            if frac == "":
                diags.append(Diag("E-NUM-TRAILING-DOT", path, ln, txt))
            elif len(frac) != 3:
                diags.append(Diag("E-DEC-PRECISION", path, ln,
                                  "%s has %d fractional digits" % (txt, len(frac))))
            add("num", txt, i)
            i += m.end()
            continue
        if c == "." and i + 1 < n and src[i + 1].isdigit():
            diags.append(Diag("E-DEC-LEADING-DOT", path,
                              src.count("\n", 0, i) + 1))
            i += 1
            continue
        m = re.match(r"-?[0-9]+", src[i:])
        if m:
            txt = m.group(0)
            # §3.4: an Integer is a signed 64-bit value, and an out-of-range
            # literal is rejected rather than wrapped. Python's integers are
            # unbounded, so this has to be checked explicitly.
            if not (-2**63 <= int(txt) < 2**63):
                diags.append(Diag("E-INT-RANGE", path,
                                  src.count("\n", 0, i) + 1, txt))
            add("num", txt, i)
            i += m.end()
            continue

        m = re.match(r"[A-Za-z_][A-Za-z0-9_.]*", src[i:])
        if m:
            add("id", m.group(0), i)
            i += m.end()
            continue

        diags.append(Diag("E-BAD-CHAR", path, src.count("\n", 0, i) + 1, repr(c)))
        i += 1

    # Adjacent string literals form one scalar (spec §3.5.1, §4.1).
    merged = []
    for t in toks:
        if t.kind == "str" and merged and merged[-1].kind == "str":
            merged[-1].text += t.text
        else:
            merged.append(t)
    return merged


# --------------------------------------------------------------------------
# Parser — just enough tree to check block shape and failureMsg placement.
# --------------------------------------------------------------------------

class Block:
    __slots__ = ("stmts", "scalars", "line")

    def __init__(self, line):
        self.stmts = []      # list of (key, op, value, line)
        self.scalars = []    # list of (text, line)
        self.line = line


class Parser:
    def __init__(self, toks, path, diags):
        self.t, self.i, self.path, self.diags = toks, 0, path, diags

    def peek(self, k=0):
        j = self.i + k
        return self.t[j] if j < len(self.t) else Token("eof", "", 0, 0)

    def is_op(self, tok):
        # spec §4.2: an angle in the operator slot is a comparison operator.
        return tok.kind == "op" or tok.kind == "angle"

    def skip_bracketed(self, open_kind, open_ch, close_ch):
        depth = 0
        while self.i < len(self.t):
            tk = self.peek()
            if tk.kind == open_kind and tk.text == open_ch:
                depth += 1
            elif tk.kind == open_kind and tk.text == close_ch:
                depth -= 1
            self.i += 1
            if depth == 0:
                return

    def value(self):
        while self.peek().kind == "ann":
            self.i += 1
        if self.peek().kind == "punct" and self.peek().text == "{":
            return self.block()
        tok = self.peek()
        self.i += 1
        # TypeExpr: an angle in VALUE position opens type arguments (§4.2).
        if self.peek().kind == "angle" and self.peek().text == "<":
            self.skip_bracketed("angle", "<", ">")
            return ("type", tok)
        # Call (§4.3)
        if self.peek().kind == "punct" and self.peek().text == "(":
            self.skip_bracketed("punct", "(", ")")
            return ("call", tok)
        return ("scalar", tok)

    def block(self):
        blk = Block(self.peek().line)
        self.i += 1
        while True:
            tok = self.peek()
            if tok.kind == "eof":
                self.diags.append(Diag("E-BRACE-UNBALANCED", self.path, blk.line,
                                       "block opened here is never closed"))
                break
            if tok.kind == "punct" and tok.text == "}":
                self.i += 1
                break
            if self.is_op(self.peek(1)):
                key, op, line = tok.text, self.peek(1).text, tok.line
                self.i += 2
                blk.stmts.append((key, op, self.value(), line))
            else:
                self.i += 1
                blk.scalars.append((tok.text, tok.line))
        if blk.stmts and blk.scalars:
            self.diags.append(Diag(
                "E-BLOCK-MIXED", self.path, blk.line,
                "bare scalars %s among %d statements" % (
                    [s[0] for s in blk.scalars][:4], len(blk.stmts))))
        return ("block", blk)

    def parse(self):
        top = []
        while self.peek().kind != "eof":
            tok = self.peek()
            if self.is_op(self.peek(1)):
                self.i += 2
                top.append((tok.text, self.value(), tok.line))
            else:
                self.diags.append(Diag("E-STRAY-TOKEN", self.path, tok.line,
                                       repr(tok.text)))
                self.i += 1
        return top


# --------------------------------------------------------------------------
# Semantic checks that do not require the schema layer
# --------------------------------------------------------------------------

def iter_blocks(value):
    if value[0] == "block":
        yield value[1]
        for (_k, _op, val, _ln) in value[1].stmts:
            for b in iter_blocks(val):
                yield b


def check_failuremsg(top, path, diags):
    """Enforce spec §10.5: where a failureMsg may appear, and where it must."""

    def scan(value, key, barrier, silent):
        if value[0] != "block":
            return
        for (k, _op, val, line) in value[1].stmts:
            if k == "failureMsg":
                if silent:
                    diags.append(Diag("E-FAILMSG-SILENT", path, line,
                                      "inside a silent '%s' context" % silent))
                elif barrier:
                    diags.append(Diag("E-FAILMSG-UNREACHABLE", path, line,
                                      "below a %s, which fails as a whole" % barrier))
            else:
                # A barrier's OWN failureMsg is the valid one; the barrier
                # applies only when descending past it into a child block.
                nb = barrier or (key if key in BARRIERS else None)
                scan(val, k, nb, silent)

    def has_reachable_msg(value, key="restrictions"):
        if value[0] != "block":
            return False
        if any(k == "failureMsg" for (k, _o, _v, _l) in value[1].stmts):
            return True
        if key in BARRIERS:
            return False        # only its own direct message counts
        return any(has_reachable_msg(v, k)
                   for (k, _o, v, _l) in value[1].stmts if k != "failureMsg")

    def visit(value):
        if value[0] != "block":
            return
        stmts = value[1].stmts
        by_key = {}
        for (k, _op, val, line) in stmts:
            by_key.setdefault(k, (val, line))
        for (k, _op, val, _line) in stmts:
            if k in SILENT_CONDITION_KEYS:
                scan(val, k, None, k)
            elif k in CONDITION_KEYS:
                scan(val, k, None, None)
            else:
                visit(val)
        if "restrictions" in by_key:
            val, line = by_key["restrictions"]
            nonempty = val[0] == "block" and (val[1].stmts or val[1].scalars)
            if nonempty and not has_reachable_msg(val) and "failureMsg" not in by_key:
                diags.append(Diag("W-FAILMSG-MISSING", path, line,
                                  "no message here, and none on the enclosing "
                                  "action or rule"))

    for (_form, value, _line) in top:
        visit(value)


def check_templates_and_loc(top, toks, path, diags):
    """Spec §9.3 styles, §9.6 localisation keys, §9.1 template brackets."""
    styles, loc_defined, loc_lines = set(), set(), {}

    for (form, value, _line) in top:
        if value[0] != "block":
            continue
        if form == "style":
            for (k, _o, v, _l) in value[1].stmts:
                if k == "id" and v[0] == "scalar":
                    styles.add(v[1].text)
        elif form == "loc":
            for (k, _o, _v, line) in value[1].stmts:
                if k == "lang":
                    continue
                if k in loc_defined:
                    diags.append(Diag("E-LOC-DUPLICATE", path, line,
                                      "'%s' also defined at line %d"
                                      % (k, loc_lines[k])))
                loc_defined.add(k)
                loc_lines.setdefault(k, line)

    referenced = set()
    for t in toks:
        if t.kind == "lockey":
            referenced.add(t.text)
            if t.text not in loc_defined:
                diags.append(Diag("E-LOC-UNDEFINED", path, t.line, "$" + t.text))
    for key in sorted(loc_defined - referenced):
        diags.append(Diag("W-LOC-UNUSED", path, loc_lines[key], "$" + key))

    # @style(...) as an annotation, and inside template text
    for t in toks:
        names = []
        if t.kind == "ann" and t.text.startswith("@style("):
            names.append(t.text[len("@style("):-1].strip())
        elif t.kind == "str":
            body = t.text.replace("\\@", "")
            names += re.findall(r"@style\(([A-Za-z_][A-Za-z0-9_]*)\)", body)
        for nm in names:
            if nm not in styles:
                diags.append(Diag("E-STYLE-UNDECLARED", path, t.line, nm))
        if t.kind == "str":
            body = re.sub(r"\\[\[\]]", "", t.text)
            depth = 0
            for ch in body:
                if ch == "[":
                    depth += 1
                elif ch == "]":
                    depth -= 1
                if depth < 0:
                    break
            if depth != 0:
                diags.append(Diag("E-TEMPLATE-BRACKETS", path, t.line))


def check_globals_and_flags(top, toks, path, diags):
    """Spec §6.4.1 — flags are sugar over declared bool globals, not a store.

    This is the check that turns `flag_set = captain_finded` from a silent,
    permanent bug into a build failure, so it is worth doing even though the
    full global type system needs the schema layer.
    """
    declared = {}          # id -> type
    for (form, value, _line) in top:
        if form not in ("global", "const") or value[0] != "block":
            continue
        gid = gtype = None
        for (k, _op, v, _l) in value[1].stmts:
            if k == "id" and v[0] == "scalar":
                gid = v[1].text
            elif k == "type" and v[0] in ("scalar", "type"):
                gtype = v[1].text
        if gid:
            declared[gid] = gtype

    # Usage detection is a token scan: any identifier anywhere that matches a
    # declared id counts as a read. Crude — it cannot tell a global reference
    # from an unrelated identifier of the same name — but it errs toward
    # silence, which is right for a warning, and it catches the forms a
    # structural walk would miss (`collection = seen_endings`, conditions,
    # template references).
    #
    # The WRITE sites are excluded, because those are exact: `set_flag = X`
    # and `set_global = { id = X … }` name a global unambiguously, and a
    # global that is only ever written is precisely what §14.3's row means by
    # "never read". Without this the scan counted a write as a read and the
    # warning could only fire for a global mentioned nowhere at all. The
    # structural walk below re-adds the ones it decides are reads.
    # Positions, not names: a global written at one place and read at another
    # is read, so the exclusions below are token indices rather than ids.
    #
    # Three kinds of occurrence are not reads: the declaration's own
    # `id = X`, and the two write forms `set_flag = X` and
    # `set_global = { id = X … }`. Each is found by the same small window
    # scan, which is as much structure as a token list affords — the C++ pass
    # in libs/starcore/src/globals.cpp does it properly, off the AST.
    not_a_read = set()

    def id_after(start):
        for j in range(start + 1, min(start + 8, len(toks))):
            if toks[j].kind == "id" and toks[j].text == "id" and j + 2 < len(toks):
                return j + 2
        return None

    for i, tok in enumerate(toks):
        if tok.kind != "id":
            continue
        if tok.text in ("global", "const", "set_global", "add_global"):
            at = id_after(i)
            if at is not None:
                not_a_read.add(at)
        elif tok.text in ("set_flag", "clear_flag"):
            if i + 2 < len(toks) and toks[i + 1].kind == "op" and toks[i + 2].kind == "id":
                not_a_read.add(i + 2)
    used = {t.text for i, t in enumerate(toks)
            if t.kind == "id" and t.text in declared and i not in not_a_read}

    def visit(value):
        if value[0] != "block":
            return
        for (k, _op, val, line) in value[1].stmts:
            if k in ("set_flag", "clear_flag", "flag_set") and val[0] == "scalar":
                name = val[1].text
                if k == "flag_set":
                    used.add(name)   # a test is a read; setting one is not
                if name not in declared:
                    diags.append(Diag("E-FLAG-UNDECLARED", path, line, name))
                elif declared[name] != "bool":
                    diags.append(Diag("E-FLAG-NOT-BOOL", path, line,
                                      "'%s' is declared %s" % (name, declared[name])))
            elif k in ("set_global", "add_global") and val[0] == "block":
                for (kk, _o, vv, ll) in val[1].stmts:
                    if kk == "id" and vv[0] == "scalar":
                        # Not E-FLAG-UNDECLARED: §14.3 gives the flag sugar
                        # and a plain global reference separate rows, and
                        # `set_global` is the second of those.
                        if vv[1].text not in declared:
                            diags.append(Diag("E-GLOBAL-UNDECLARED", path, ll,
                                              vv[1].text))
                visit(val)
            else:
                if k == "global" and val[0] == "block":
                    for (kk, _o, _vv, _ll) in val[1].stmts:
                        used.add(kk)
                visit(val)

    for (_form, value, _line) in top:
        visit(value)

    for gid in sorted(set(declared) - used):
        diags.append(Diag("W-GLOBAL-UNUSED", path, 0, gid))


def check_reserved_and_operators(top, toks, path, diags):
    """Spec §3.9 reserved values, and the §3.6 operator-context heuristic."""
    for t in toks:
        if t.kind == "id" and t.text in RESERVED_VALUES:
            diags.append(Diag("E-RESERVED-WORD", path, t.line,
                              "'%s' is reserved; use yes / no" % t.text))

    CMP = {"==", "!=", "<", ">", "<=", ">="}

    def visit(value, in_cond):
        if value[0] != "block":
            return
        for (k, op, val, line) in value[1].stmts:
            if op in CMP and not in_cond:
                diags.append(Diag("W-CMP-OUTSIDE-COND", path, line,
                                  "'%s %s ...'" % (k, op)))
            nxt = in_cond or (k in CONDITION_KEYS)
            # Descending out of a condition context is not modelled: the
            # checker has no schema, so once inside, it stays inside.
            visit(val, nxt)

    for (_form, value, _line) in top:
        visit(value, False)


# --------------------------------------------------------------------------
# Driver
# --------------------------------------------------------------------------

def read_pragmas(src, path, diags):
    """Collect '# check: allow CODE, CODE' suppressions declared in a file."""
    allowed = {}
    for n, line in enumerate(src.splitlines(), 1):
        m = PRAGMA_RE.match(line)
        if not m:
            continue
        for code in (c.strip() for c in m.group(1).split(",")):
            if not code:
                continue
            if code not in CODES:
                diags.append(Diag("E-PRAGMA-UNKNOWN", path, n, code))
            else:
                allowed.setdefault(code, n)
    return allowed


def check_file(path):
    with open(path, encoding="utf-8") as fh:
        src = fh.read()
    diags = []
    toks = tokenize(src, path, diags)

    depth = 0
    for t in toks:
        if t.kind == "punct" and t.text in "{}":
            depth += 1 if t.text == "{" else -1
            if depth < 0:
                diags.append(Diag("E-BRACE-UNBALANCED", path, t.line, "extra '}'"))
                depth = 0
    if depth:
        diags.append(Diag("E-BRACE-UNBALANCED", path, len(src.splitlines()),
                          "%d unclosed '{'" % depth))

    top = Parser(toks, path, diags).parse()
    check_failuremsg(top, path, diags)
    check_templates_and_loc(top, toks, path, diags)
    check_reserved_and_operators(top, toks, path, diags)
    check_globals_and_flags(top, toks, path, diags)

    allowed = read_pragmas(src, path, diags)
    if allowed:
        fired = {d.code for d in diags}
        diags = [d for d in diags if d.code not in allowed]
        for code, line in sorted(allowed.items(), key=lambda kv: kv[1]):
            if code not in fired:
                diags.append(Diag("W-PRAGMA-UNUSED", path, line, code))
    return toks, top, diags


DOC_FENCE_RE = re.compile(r"```stardata\n(.*?)```", re.S)


def check_doc_examples(doc_path):
    """Lex and parse every ```stardata block in a Markdown document.

    Spec examples are the one place format drift hides: they are read far more
    often than the corpus, and nothing validates them. Fragments are wrapped in
    a synthetic container so that a bare `restrictions = { ... }` is checkable
    without pretending it is a whole file.

    Limitation: this sees fenced blocks only. Snippets inside Markdown table
    cells and inline code spans are not extracted, so they still need care.
    """
    with open(doc_path, encoding="utf-8") as fh:
        text = fh.read()
    diags = []
    skipped = 0
    for n, m in enumerate(DOC_FENCE_RE.finditer(text), 1):
        body = m.group(1)
        line0 = text.count("\n", 0, m.start()) + 1
        # Blocks containing an elision are illustrative, not conforming. They
        # are skipped rather than silently passed, so the count stays visible.
        if "..." in body or "\u2026" in body:
            skipped += 1
            continue
        label = "%s[example %d @ line %d]" % (os.path.basename(doc_path), n, line0)
        sub = []
        toks = tokenize(body, label, sub)
        depth = 0
        for t in toks:
            if t.kind == "punct" and t.text in "{}":
                depth += 1 if t.text == "{" else -1
        if depth != 0:
            # A fragment showing the inside of a block; wrap and retry.
            sub = []
            toks = tokenize("wrapper = {\n" + body + "\n}", label, sub)
        Parser(toks, label, sub).parse()
        for d in sub:
            if d.severity == "error":
                diags.append(Diag("E-DOC-EXAMPLE", doc_path, line0,
                                  "%s: %s" % (d.code, d.msg or CODES.get(d.code, ""))))
    return diags, skipped


def expectations(path):
    """Read '# EXPECT <CODE>' lines from a negative fixture."""
    want = []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            m = re.match(r"\s*#\s*EXPECT\s+([A-Z][A-Z0-9-]+)", line)
            if m:
                want.append(m.group(1))
            elif line.strip() and not line.lstrip().startswith("#"):
                break
    return want


def main():
    ap = argparse.ArgumentParser(description="Stardata conformance checker")
    ap.add_argument("files", nargs="*", help="files to check")
    ap.add_argument("--self-test", action="store_true",
                    help="also run tests/corpus/invalid/ and verify expectations")
    ap.add_argument("--check-docs", action="store_true",
                    help="also parse every ```stardata example in docs/*.md")
    ap.add_argument("--strict", action="store_true",
                    help="treat warnings as failures")
    ap.add_argument("--stats", action="store_true",
                    help="print a token and top-level-form summary")
    ap.add_argument("--quiet", "-q", action="store_true")
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    corpus = os.path.join(here, "corpus")

    files = args.files
    if not files:
        files = sorted(os.path.join(corpus, f) for f in os.listdir(corpus)
                       if f.endswith(".star"))
    if not files:
        print("no .star files found", file=sys.stderr)
        return 2

    failed = False
    for path in files:
        toks, top, diags = check_file(path)
        errs = [d for d in diags if d.severity == "error"]
        warns = [d for d in diags if d.severity == "warning"]
        for d in diags:
            if not args.quiet or d.severity == "error":
                print(d)
        if errs or (args.strict and warns):
            failed = True
        if not args.quiet:
            print("%s: %d tokens, %d top-level statements, %d errors, %d warnings"
                  % (os.path.relpath(path), len(toks), len(top), len(errs), len(warns)))
        if args.stats:
            forms = Counter(f for f, _v, _l in top)
            kinds = Counter(t.kind for t in toks)
            print("  token kinds: " + ", ".join(
                "%s=%d" % kv for kv in sorted(kinds.items())))
            print("  top-level forms:")
            for f, c in sorted(forms.items()):
                print("     %-18s x%d" % (f, c))

    if args.check_docs:
        docs = sorted(os.path.join(here, os.pardir, "docs", f)
                      for f in os.listdir(os.path.join(here, os.pardir, "docs"))
                      if f.endswith(".md"))
        print("\n--- doc examples ---")
        for doc in docs:
            ds, skipped = check_doc_examples(doc)
            n = len(DOC_FENCE_RE.findall(open(doc, encoding="utf-8").read()))
            if ds:
                failed = True
                for d in ds:
                    print("  " + str(d))
            print("  %-24s %2d examples, %d checked, %d elided, %d errors"
                  % (os.path.basename(doc), n, n - skipped, skipped, len(ds)))

    if args.self_test:
        bad_dir = os.path.join(corpus, "invalid")
        if not os.path.isdir(bad_dir):
            print("self-test: %s not found" % bad_dir, file=sys.stderr)
            return 2
        print("\n--- self-test: negative fixtures ---")
        for name in sorted(os.listdir(bad_dir)):
            if not name.endswith(".star"):
                continue
            path = os.path.join(bad_dir, name)
            want = expectations(path)
            _t, _p, diags = check_file(path)
            got = {d.code for d in diags}
            checkable = [c for c in want if c not in NEEDS_SCHEMA_LAYER]
            missing = [c for c in checkable if c not in got]
            if not want:
                print("  FAIL %s: fixture declares no EXPECT codes" % name)
                failed = True
            elif not checkable:
                # Every code this fixture declares needs the schema layer.
                # Asserted by the C++ suite instead -- see NEEDS_SCHEMA_LAYER.
                print("  skip %s (%s: needs the schema layer)"
                      % (name, ", ".join(sorted(want))))
            elif missing:
                print("  FAIL %s: expected %s, got %s"
                      % (name, missing, sorted(got) or "nothing"))
                failed = True
            else:
                print("  ok   %s (%s)" % (name, ", ".join(want)))

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
