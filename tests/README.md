# Tests

## `check_stardata.py`

A dependency-free conformance checker for the Stardata format, validating
`.star` files against `docs/stardata-spec.md`.

```sh
python3 tests/check_stardata.py                  # check tests/corpus/*.star
python3 tests/check_stardata.py --self-test      # also verify the negative fixtures
python3 tests/check_stardata.py --strict         # treat warnings as failures
python3 tests/check_stardata.py --stats          # token and top-level-form summary
python3 tests/check_stardata.py --check-docs      # parse every example in docs/*.md
python3 tests/check_stardata.py FILE...          # check specific files
```

The CI invocation is:

```sh
python3 tests/check_stardata.py --check-docs --self-test --strict
```

### Why it exists

`docs/proposal.md` §4.9 requires that the specification and the conformance
corpus cannot silently diverge. Until `libs/stardata` has a parser there is
nothing to enforce that, and a specification with no implementation drifts
within weeks. This script is the stand-in.

It is **temporary by design**. When Starforge can validate the corpus, this
script should be *deleted* rather than maintained alongside it — two validators
that disagree are worse than one.

### What it checks

Level 1 of the spec's conformance ladder (§1.2), plus the handful of Level 3
rules that happen not to need the schema layer:

- Lexical structure (§3): string literals, escapes, decimal precision, reserved
  words, `[`/`]` confinement to strings.
- The `<` disambiguation (§4.2) — comparison operator versus type-argument
  opener, resolved by position and never by whitespace.
- Block shape (§5.2) and brace balance.
- Template bracket balance, style declarations, localisation keys (§9).
- `failureMsg` placement and coverage (§10.5).
- Flags resolving to declared `bool` globals (§6.4.1).
- **Every ```stardata example in `docs/*.md`.** Spec examples are read far more
  often than the corpus and nothing else validates them, which is exactly where
  format drift hides. Blocks containing an elision (`...`) are illustrative and
  are skipped, but counted, so the exclusion stays visible.

### What it does not check

Anything requiring the schema layer: types, arity, unknown keys, reference
resolution, trait conflicts, containment cycles. It also does not build a
lossless CST, so it cannot verify the round-trip property of §14.2. Those
belong to the real parser.

Doc-example extraction sees fenced blocks only. Snippets inside Markdown table
cells and inline code spans are not extracted, so those still need care — the
`count_of = { ... } >= 2` mistake that prompted this check lived in a table.

The operator-context check (`W-CMP-OUTSIDE-COND`) is a heuristic standing in
for the schema, driven by a hard-coded list of condition-bearing keys near the
top of the script. It is the only place library knowledge is baked in.

## `unit/`

The C++ suite, Catch2 driven by `ctest`. `cmake --workflow --preset
linux-gcc-debug` (or any other preset) builds and runs it; the Debug presets
on Linux and macOS run it under ASan and UBSan.

| Path | Covers |
|---|---|
| `unit/diagnostics/` | Workstream C: source manager, spans, the diagnostic model, the human and machine renderers. |
| `unit/lex/` | Workstream D: the token and trivia model, the lexer, its diagnostics, and the fuzzer. |
| `unit/cst/` | Workstream E: the green tree, cursors, the trivia attachment policy, the parser, the byte-exact round-trip, the edit API and the edit fuzzer. |
| `unit/support/` | Shared helpers — corpus discovery, snapshot comparison, the token dump, the lexing harness. |

Tests that compare against a checked-in expected output (`unit/*/snapshots/`)
regenerate it when run with `--update-snapshots`:

```sh
./stardata_unit_tests --update-snapshots     # from the build directory
```

Review the resulting diff before committing it — that diff is the only thing
standing between a deliberate change and a silently degraded error message.

The lexer suite reads `corpus/` directly: every file must lex without a
diagnostic, every file's token stream is pinned by a golden, and every
fixture in `corpus/invalid/` must provoke the lexical codes its `# EXPECT`
lines declare. Codes belonging to workstreams that do not exist yet are
skipped rather than asserted.

## `corpus/`

| Path | Purpose |
|---|---|
| `corpus/tour.star` | The reference corpus. Exercises every construct in the spec. Must validate with zero errors and zero warnings. |
| `corpus/lf.star`, `corpus/crlf.star` | The same small scenario, checked in with LF and CRLF line endings respectively (spec §2). `.gitattributes` marks `*.star` as `-text` so Git never rewrites either on checkout, and `unit/cst/roundtrip_test.cpp` asserts both survive parse-and-write byte for byte (backlog E6). See `CONTRIBUTING.md` ("Line endings"). |
| `corpus/invalid/` | Negative fixtures — files that are invalid on purpose, each declaring the diagnostic codes it must provoke. |

### Adding a rule to the specification

1. Write the rule in `docs/stardata-spec.md`.
2. Add a positive example to `corpus/tour.star`.
3. Add a negative fixture to `corpus/invalid/` declaring the code it provokes.
4. Implement the check, if it is checkable without a schema.

Step 3 is the one that gets skipped and shouldn't be. A rule with no negative
fixture is a rule nobody has confirmed can actually fail.

### Suppressions

A file may silence a diagnostic for its whole length:

```stardata
# check: allow W-LOC-UNUSED
```

A suppression that turns out to be unnecessary is reported as
`W-PRAGMA-UNUSED`, so stale ones surface rather than accumulating. `tour.star`
uses exactly one, with its justification written above it.
