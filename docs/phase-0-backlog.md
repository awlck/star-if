# Phase 0 — Foundations: backlog

**Derived from:** `docs/proposal.md` §15 (Phase 0) and §17 · **Spec:** `docs/stardata-spec.md`

**Phase exit criterion (from the proposal):** `tests/corpus/tour.star` parses, round-trips byte-identically, and reports good errors on a corpus of broken files — on Windows, macOS and Linux.

---

## How to read this

Tasks are grouped into workstreams. Within a workstream they are roughly sequential; across workstreams they are mostly independent, and §Critical path says which order actually matters.

Sizes are **S** (≤ 1 day), **M** (2–4 days), **L** (1–2 weeks), at full-time pace. Multiply for evenings-and-weekends.

**Where each task lives.** `libs/stardata` implements the format and the schema *mechanism*; `libs/starcore` implements the *vocabulary* — see proposal §2.1.1 for the line and the two tests that decide it. Tasks below carry an **owner** where it is not obvious. `libs/starcore` should exist from the first commit that needs it, even while nearly empty: a semantic pass living in `stardata` "for now" is how the boundary stops being real.

**Definition of done, applying to every task:**

- Tests exist and pass under `ctest`.
- CI is green on all three desktop platforms.
- No new compiler warnings (warnings are errors — see B4).
- Anything that changes the format is reflected in `docs/stardata-spec.md`, `tests/corpus/tour.star`, and a fixture in `tests/corpus/invalid/`.
- No identifier from `libs/starcore/builtin/` appears as a string literal in `libs/stardata/` — the grep test of proposal §2.1.1, asserted in CI.

---

## A · Repository and governance

Cheap, unblocks everything, and gets much more expensive after the first outside contribution.

### A1 · Licence files
**Size:** S · **Depends on:** nothing

Per proposal §14.5.

- [x] `LICENSE` at root — Apache 2.0.
- [x] `stdlib/LICENSE` — MIT-0.
- [x] `docs/LICENSE` — CC-BY-4.0.
- [x] SPDX headers in a source file template; a script or clang-format hook to keep them.
- [x] `NOTICE` file, as Apache 2.0 expects.

### A2 · `TRADEMARKS.md`
**Size:** S · **Depends on:** nothing

Apache 2.0 §6 withholds trademark rights, so the names need a usage guideline rather than a licence term.

- [x] Covers STAR IF, Starbase, Starforge, Starhelm, Stardata, Starpak, Starscape.
- [x] States the "Built on STAR IF technology" convention for third-party games.
- [x] Explicitly a courtesy guideline, not a licence obligation.

### A3 · Repository scaffolding
**Size:** S · **Depends on:** nothing

- [x] `README.md` — what the project is, what works today, how to build.
- [x] `CONTRIBUTING.md` — build, test, style, and the "spec + corpus + fixture" rule from the Definition of Done.
- [x] `.gitignore`, `.editorconfig`.
- [x] `docs/` index linking proposal, spec, backlog.

### A4 · Line-ending policy — **do this before any `.star` file is committed by a Windows contributor**
**Size:** S · **Depends on:** nothing

Spec §2 requires that CRLF and LF both round-trip **unchanged**. Git's `core.autocrlf` will silently rewrite line endings on checkout, which means the round-trip test (E6) passes on Linux and fails on Windows for reasons that have nothing to do with the parser. This has cost other projects days.

- [x] `.gitattributes` marks `*.star` as `-text` so Git never normalises them.
- [x] Corpus contains at least one deliberately CRLF file and one LF file, and CI asserts both survive.
- [x] Documented in `CONTRIBUTING.md` so nobody "helpfully" fixes it.

---

## B · Build and continuous integration

### B1 · CMake skeleton and presets
**Size:** M · **Depends on:** A3

- [x] Top-level `CMakeLists.txt` with the `libs/` and `apps/` layout of proposal §2.1.
- [x] `CMakePresets.json` for `windows-msvc`, `macos-clang`, `linux-gcc`, `linux-clang`, each in Debug and Release.
- [x] One-command build from a clean checkout on each platform.
- [x] C++20 enforced; the build fails clearly on an inadequate compiler.

### B2 · Dependency manifest
**Size:** S · **Depends on:** B1

- [x] `vcpkg.json` pinning Catch2 and the archive library chosen in G3.
- [x] Dependencies resolve offline after one warm cache, so CI is not hostage to a registry.
- [x] No Qt dependency anywhere in `libs/` — that boundary is load-bearing (proposal §2.1) and should be asserted, not assumed.

### B3 · Test harness
**Size:** S · **Depends on:** B1, B2

- [x] Catch2 wired to `ctest`.
- [x] `tests/unit/` builds and runs one trivial passing test.
- [x] A corpus-driven test helper that discovers `.star` files rather than listing them, so adding a fixture needs no build-file edit.

### B4 · Compiler hygiene
**Size:** S · **Depends on:** B1

- [x] `-Wall -Wextra -Werror` / `/W4 /WX`.
- [x] ASan + UBSan in Debug on Linux and macOS.
- [x] `.clang-format` and `.clang-tidy`, with a format check in CI.

### B5 · CI matrix
**Size:** M · **Depends on:** B1–B4

- [x] GitHub Actions: {Windows, macOS, Linux} × {Debug, Release}.
- [x] Runs `ctest`, the format check, and the sanitiser build.
- [x] Build + test under 10 minutes, or the loop stops being used.

### B6 · Wire the Python checker into CI — **can land today, before any C++ exists**
**Size:** S · **Depends on:** nothing

- [x] `python3 tests/check_stardata.py --check-docs --self-test --strict` runs on every push.
- [x] `--check-docs` parses every fenced example in `docs/*.md`; it has already caught two real spec bugs that the corpus could not.
- [x] Gives the spec-versus-corpus guarantee immediately rather than at the end of Phase 0.

---

## C · Diagnostics

Build this **first** of the `stardata` work. Everything downstream reports through it, and retrofitting spans into a parser that was written without them is miserable.

### C1 · Source manager and spans
**Size:** M · **Depends on:** B3

- [x] `SourceId`, `Span { SourceId, byte offset, length }`.
- [x] Registry mapping `SourceId` → path and contents.
- [x] Byte offset → line/column, with a lazily-built line table.
- [x] Correct columns for tabs and for non-ASCII (offset in bytes, column in characters).

### C2 · Diagnostic model
**Size:** M · **Depends on:** C1

Spec §15 lists the required diagnostics; `tests/check_stardata.py` already defines stable codes worth reusing.

- [x] `Diagnostic { code, severity, primary span, notes[], fix-its[] }`.
- [x] Code enum covering every row of spec §15.
- [x] Multi-span diagnostics — the duplicate-key case (§5.3) must cite *both* spans, so this is required, not optional.
- [x] Sink interface: collecting, counting, limiting.

### C3 · Diagnostic rendering
**Size:** S · **Depends on:** C2

- [x] Human renderer: source line, caret, underline, note, suggestion.
- [x] Machine renderer: one line per diagnostic, stable and greppable, for CI.
- [x] Colour when a TTY, never when piped.

### C4 · Diagnostic snapshot tests
**Size:** M · **Depends on:** C3, B3

- [x] Each file in `tests/corpus/invalid/` has an expected-output snapshot.
- [x] `--update-snapshots` regenerates; CI fails on any diff.
- [x] This is what stops error messages silently degrading, which is otherwise invisible until an author complains.

A fixture whose declared code no pass produces gets a snapshot too, recording
that the lexer and parser say nothing about it. That is a record of which
pass owns which fixture, not a hole in the test — and the two reasons a
snapshot reads "(none)" are worth keeping apart. Some fixtures wait on a
workstream that does not exist yet, and those snapshots move when it lands.
Others are checked elsewhere: everything the schema layer owns is asserted
from the same fixtures in `tests/unit/schema/corpus_test.cpp`, which loads
each one on top of the core-owned set the way a library is loaded.

### C5 · Point diagnostics at the author manual — **blocked until the manual exists**
**Size:** S · **Depends on:** the author manual of proposal §2.1

Every `with_note()` in `libs/` currently ends by citing a section of
`docs/stardata-spec.md`, because that is the only document there is. The
specification is written for implementers: an author sent to §3.4 to find out
why their decimal was rejected lands in a paragraph about scaled 64-bit
integers, which answers a question they did not ask.

- [ ] Repoint each note at the author manual, keeping the specification only where the note is genuinely about conformance rather than about how to write the file.
- [ ] `git grep -n 'spec §' -- libs/` finds the full set; there is no need for a marker comment.
- [ ] Decide once whether the rendered form should carry a link or a section number, and apply it uniformly.

Worth doing in one pass rather than opportunistically: the value is that the
diagnostics speak with one voice about where to go next, and a half-converted
set is worse than either end state.

---

## D · Lexer

### D1 · Token and trivia model
**Size:** S · **Depends on:** C1

- [x] Token kinds for spec §3: identifier, integer, decimal, string, lockey, annotation, operator, punctuation, angle.
- [x] Trivia kinds: whitespace, comment. Trivia is **retained**, not discarded (spec §14.2).
- [x] Tokens carry spans, not copied text.

Two additions beyond the list, both forced by requirements elsewhere: an
`error` token kind, so that the spans still tile the whole input when a byte
begins no token (E4 needs a tree covering every byte, error text included),
and a `bom` trivia kind, because §2.2 requires a leading U+FEFF to survive a
round-trip while explicitly not being content.

Annotation arguments are ordinary tokens rather than part of the annotation
token, since trivia may appear between them and §14.2 requires it survive.

### D2 · Lexer core
**Size:** M · **Depends on:** D1, C2

- [x] Implements spec §3.1–§3.9.
- [x] `<` and `>` emitted as an ambiguous `angle` kind, resolved by the parser (spec §4.2) — the lexer must **not** guess, and must not use whitespace to decide.
- [x] Multi-character operators matched before single-character punctuation (`>=` is not `>` then `=`).
- [x] UTF-8 validation with a useful error, not a crash.

`<=` and `>=` lex as operators, not as an angle plus an equals: neither can
open a type argument list, so neither is ambiguous.

The §15 reserved operators `*=`, `/=`, `::` and `->` are rejected with a note
naming §15. `=>` is not, and cannot be: it lexes as `=` followed by `>`, and
telling it from `= >` would mean reading the spacing that §3.1 makes
insignificant. The grammar rejects it instead, in E4.

### D3 · Lexical diagnostics
**Size:** M · **Depends on:** D2, C2

Every one of these has a fixture in `tests/corpus/invalid/`:

- [x] `E-STR-MULTILINE`, `E-STR-ESCAPE`, `E-STR-UNTERMINATED`
- [x] `E-DEC-PRECISION`, `E-DEC-LEADING-DOT`, `E-NUM-TRAILING-DOT`
- [x] `E-BRACKET-OUTSIDE`, `E-RESERVED-WORD`, `E-UNICODE-WS`, `E-BAD-CHAR`
- [x] Recovery: one bad token does not abandon the file.

Two codes beyond the list, each stating a §3 or §2 MUST that had no code:
`E-INT-RANGE` (§3.4's signed-64-bit range, also added to
`tests/check_stardata.py` so the two implementations still agree) and
`E-UTF8-INVALID` (§2.1). `E-UTF8-INVALID` is the one lexical code with no
corpus fixture: the corpus is shared with the Python checker, which reads
every file as UTF-8 text, so a fixture of malformed bytes would break that
checker rather than exercise it. It is covered by unit tests built from
bytes, and the "every lexical code has a fixture" test names the exemption
so it cannot quietly grow.

Neither code appears in spec §14.3's table, which begins at §5.2 and lists
none of §3's diagnostics. **Worth deciding separately:** whether that table
should grow the §3 rows, or whether its scope should be stated as semantic
diagnostics only.

### D4 · Adjacent string concatenation
**Size:** S · **Depends on:** D2

- [x] Spec §3.5.1 — adjacent literals form one scalar.
- [x] **Split points preserved in the CST**, so E6's round-trip reproduces the author's line breaks.

The lexer emits one token per literal and reports the run
(`TokenStream::string_run_at`) rather than merging, which is what leaves the
split points for the CST to keep. `decode_string_escapes` turns a run into
the scalar's value.

### D5 · Lexer conformance tests
**Size:** M · **Depends on:** D3, D4

- [x] Token-stream golden test over `tour.star`.
- [x] A table-driven test per spec §3 rule.
- [x] Fuzz the lexer on random bytes: no crash, no hang, no unbounded memory.

The golden runs over every file in `tests/corpus/`, not `tour.star` alone,
so the CRLF and LF fixtures of A4 are pinned too; `--update-snapshots`
regenerates. The fuzzer is in-process and deterministic so that it runs in
the ordinary `ctest` invocation under the Debug sanitisers, and it is
exhaustive over all one- and two-byte inputs on top of the random ones.

---

## E · Lossless CST

**The hard part of Phase 0, and the reason the estimate is 8 weeks rather than 6.** Proposal §13.1 makes this the property that lets a graphical editor and a hand-edited text format coexist; getting it wrong is not recoverable later.

Prototype E1–E2 in a scratch branch before committing to the design.

### E1 · Green tree
**Size:** L · **Depends on:** D1

Immutable, shareable, parent-free nodes — the rust-analyzer `rowan` / Roslyn green-red model.

- [x] `GreenNode { kind, text_length, children[] }`, `GreenToken { kind, text }`.
- [x] Interning so identical subtrees share storage.
- [x] No parent pointers and no absolute offsets — that is what makes subtrees shareable across edits.
- [x] Reference-counted, thread-safe to share.

Interning is bottom-up and identity-based, so structurally identical subtrees
built through one cache are literally one object. Tokens are always interned;
nodes only up to three children, following rowan's heuristic — a `File` node
with two thousand children is unique by construction and hashing it would
cost more than the sharing could save.

### E2 · Red tree / cursor API
**Size:** M · **Depends on:** E1

- [x] Cursor carrying parent and absolute offset, computed on demand.
- [x] Navigation: parent, children, siblings, ancestors, descendants.
- [x] `SyntaxNode::text()` reconstructs source by concatenating leaves.

### E3 · Trivia attachment policy
**Size:** M · **Depends on:** E2

Unglamorous and worth deciding once, in writing, because ad-hoc rules here are what make comments drift during editing.

- [x] Written rule for leading vs trailing trivia (suggested: trailing runs to and including the newline; everything else is leading on the next token).
- [x] A comment on its own line above a statement attaches to that statement, so moving the statement moves the comment.
- [x] Documented in `CONTRIBUTING.md` with examples.

The written rule adds one clause to the suggested policy: **a blank line
detaches**. Trivia up to and including the last blank line belongs to the
enclosing File or Block rather than to the next statement. Without it a
file's header banner attaches to whichever statement happens to be first, and
moving that statement takes the banner along.

A second convention falls out and is applied everywhere: a node's range
begins at its own first token, so trivia before a node belongs to the node's
parent. `Statement` is the sole exception, which is what makes the three
rules work. The worked example in `CONTRIBUTING.md` is pinned by a test, so
the prose cannot drift from the behaviour.

### E4 · Parser
**Size:** L · **Depends on:** E2, D5, C2

- [x] Implements the grammar of spec §4 exactly.
- [x] `<` disambiguation per §4.2 — comparison in operator position, type arguments in value position, **never by whitespace**.
- [x] `Call` in value position (§4.3).
- [x] Block shape: list vs record, `E-BLOCK-MIXED` on mixing (§5.2).
- [x] **Error recovery**: a malformed block produces an error node and parsing continues, because an editor must have a tree even for broken input.
- [x] The tree covers every byte of input, including trivia and error text.

One known limitation, from the lexer rather than the parser: `list<int>= 1`
cannot close its type argument list, because §3.6 matches `>=` as one
operator and §3.1 forbids the lexer from using the spacing to know better.
The grammar reports it rather than mis-parsing. It takes a type expression
immediately followed by `=` with no space, which no real file contains.

### E5 · Writer
**Size:** S · **Depends on:** E4

- [x] Serialise any tree back to text.
- [x] Byte-exact for an unmodified tree, including BOM and line-ending style.

### E6 · Round-trip conformance — **the phase's headline test**
**Size:** S · **Depends on:** E5, A4

- [x] For every `.star` under `tests/corpus/`, parse → write → compare bytes.
- [x] Runs on all three platforms.
- [x] Includes the CRLF and LF fixtures from A4.

Holds for the whole corpus, `tour.star` included — 63 KB exercising every
construct in the specification, zero diagnostics, byte-identical. It also
holds for every file in `tests/corpus/invalid/`, which matters more than it
sounds: recovery has to be lossless too, or an editor would corrupt a file
the moment it opened one with a typo in it.

### E7 · Edit API
**Size:** M · **Depends on:** E6

- [x] Replace a node or token, returning a new tree sharing unchanged subtrees.
- [x] Re-print touches only the affected span; text outside it is byte-identical.
- [x] Insert and delete a statement within a block, preserving surrounding trivia.

### E8 · Round-trip fuzzing
**Size:** M · **Depends on:** E7

Proposal §16.1 names round-trip degradation as a high risk; this is the mitigation.

- [x] Property test: apply N random edits, assert unaffected regions are byte-identical and the result re-parses.
- [x] Structure-aware fuzzer over the corpus, run in CI on a time budget.
- [x] A corpus of any crashers found, kept as regression fixtures.

---

## F · Schema layer and validation

### F1 · Typed AST view
**Size:** M · **Depends on:** E4

- [x] Typed accessors over the CST (`Statement::key()`, `Value::as_block()`), no separate tree.
- [x] Tolerates missing and malformed children, returning optionals rather than asserting.

### F2 · The core-owned schema set and root class
**Size:** M · **Depends on:** F1

Replaces the earlier "minimal schema-of-schemas, everything else in stdlib" plan. Some schemas describe `starcore`'s own data and cannot be a library's to define; spec §7.2.2 and §8.1.1 say which and why.

- [x] Core definitions written as Stardata in `libs/starcore/builtin/*.star`: the forms of spec §7.2.4, plus `starcore.object`, `starcore.room` and the `starcore.actor` trait.
- [x] Loaded from disk in Phase 0 so the validator can be developed against them. **Phase 1 embeds them into the binary** via a CMake-generated string literal, so they are one source of truth, diffable, and impossible to ship without.
- [x] A minimal hard-coded schema-of-schemas, sufficient only to validate the builtin files themselves.
- [x] `stdlib/stdlib/*.star` declares everything else — `thing`, `person`, `container`, actions, messages — with **no privileged status**. A test asserts `stdlib` uses only mechanisms available to any library.

The requirement list is data too, in `builtin/requirements.star`, declared
through the ordinary `schema` mechanism as a `core_requirement` form. That
keeps every requirement visible, named and diffable rather than buried in
C++ — and it keeps `libs/stardata` free of IF concepts, which the layering
of proposal §2.1 requires. Spec §7.2.4's table does not yet mention the
form; whether it should is a spec question, not a code one.

### F2a · Sealing and assertions — **the anti-wart task**
**Size:** S · **Depends on:** F2

The point of F2 is not that core owns some schemas; it is that core *checks* rather than hopes. ADRIFT 5 needs the library to create its location properties, and Inform 7 attaches special handling to the eighth action declared. Both fail bewilderingly and invisibly when the expectation is not met.

- [x] `sealed` on a schema: redefinition is an error naming the owner.
- [x] `class_extension` may add to a core class; retyping or removing a core property, or changing `of_class`, is an error.
- [x] Every core requirement is checked at load and reported by name — no requirement is left implicit.
- [x] A fixture per assertion in `tests/corpus/invalid/`.

F2 landed the part of F3's key validation the bootstrap needed — unknown key
in a closed schema, and a required key that is absent — because the builtin
files cannot be validated without it. Arity, duplicate keys, exclusive
groups, `+=`/`-=`/`?=` and the registry proper remain F3's; `arity` and
`combine` are already parsed into the key declaration and simply not acted
on yet.

### F2b · Markers — **owner: split**
**Size:** S · **Depends on:** F2

Where core needs to know *which* property means something, the library declares it (spec §7.2.3) rather than core hard-coding a name.

- [x] `prop_def` accepts a block as well as a bare type.
- [x] `affects_scope`, `always_resident`, `save_exclude` parsed and exposed to `starcore`.
- [x] An unknown marker is an error, not ignored.

The marker vocabulary is **declared**, as the `prop_marker` form in
`libs/starcore/builtin/schema.star`, rather than being a list in C++ — and
that has to hold for the *reader* as well as the validator. `PropMarkers` is
a name-to-flag map, not named fields, and the reader takes whatever the form
declares as a `bool`. Adding a marker is an edit to the schema and a line in
`starcore` that acts on it; nothing in `libs/stardata` changes, which is what
`a marker added to the schema is read with no code change` asserts by
extending `prop_marker` with a marker the repository has never heard of.

### F2c · Placement sugar — **owner: `starcore`**
**Size:** S · **Depends on:** F2

- [x] `in` / `on` / `under` / `behind` / `carried` / `worn` / `part_of` desugar to `holder` + `relation` (spec §8.5).
- [x] Using a relation keyword together with `holder` or `relation` in one block is an error (`E-PLACEMENT-CONFLICT`), citing both spans.
- [x] Round-trip (E5) preserves whichever spelling the author used — the sugar is expanded in the semantic view, never in the CST.

`Placement` keeps `from_sugar`, because an editor writing a file back needs
to know which spelling it started as. Two relation keywords in one block are
the same conflict wearing one spelling, and are reported too. On a conflict
**neither** spelling wins: §8.5 says the precedence is not resolvable, and
guessing would put the object somewhere the author did not ask for.

The keywords are **the values of the enum `starcore.object.relation` is typed
by**, read from the data rather than listed in the code — so `libs/stardata`
holds no piece of the object model. Which enum that is, is named by whoever
owns the object model (`starcore` in Phase 1, the test harness today) through
`SchemaSet::set_relation_enum`. **[OPEN]** Whether the desugaring belongs in
`stardata` at all, or moves up into `starcore` in Phase 1, is worth deciding
before F3 — and whether a library may amend the relation set needs a spec
mechanism, since there is no `enum_extension`.

### F2d · `schema_extension`, replacement, and the no-duplicates rule
**Size:** M · **Depends on:** F2a

Three gaps the implementation surfaced. Spec §7.5 and §7.6.

- [x] `schema_extension` adds keys to an existing form, **including a sealed one** — sealing prevents redefinition, not extension. Redeclaring a key identically warns; any difference errors.
- [x] No declaration may be duplicated, for any form with a `unique_in` key. Error cites both spans.
- [x] `@replaces(lib)` supersedes a declaration from a named library. Total replacement, no merge. Naming a source that declared no such thing is an error — that check is the point of naming it.
- [x] `@replaces` on a sealed declaration is an error.
- [x] `provides_schema` demoted to a checked manifest (§13.3); a mismatch warns. It was never a mechanism, and §7.2.2 previously said it was.

The uniqueness rule and `@replaces` are enforced in one place — `SchemaSet::offer`
— for every top-level declaration, rather than per kind. The previous shape
enforced uniqueness only on the two kinds the loader happened to track
structurally, which is to say on the two an author is least likely to
duplicate by accident. The namespace comes from the schema's `unique_in`, so
`rule` and `loc` are exempt without being named in the code.

An owner is a **library id** (`stdlib`, `starscape`), because that is the
name `@replaces(lib)` uses. The built-in set's owner is `starcore`, which is
not a library and so is a name no `@replaces` can successfully claim.

`core_requirement` is a reserved internal form (§7.2.5.1): declaring one
outside `starcore` is `E-CORE-RESERVED`. The loader is *told* which files are
core's, through `LoadOptions::is_core`, rather than recognising a name —
`libs/stardata` does not know that a thing called `starcore` exists and
should not learn. A negative fixture says which side it is on with
`# LOAD-AS core` in its header, since the same declaration is a requirement
when core writes it and an overstep when anything else does.

### F3 · Schema registry and key validation
**Size:** M · **Depends on:** F2

- [x] Registry keyed by form id; libraries may contribute (spec §13.3).
- [x] Unknown key in a closed schema → error; `open = yes` permits and retains.
- [x] Arity: duplicate under `arity = one` cites both spans; `arity = many` preserves order.
- [x] `+=` / `-=` do not count as binding occurrences (spec §5.3). **`?=` does** — see below.
- [x] Exclusive groups (§7.2.1): two or more members in a block is an error naming the group's members; zero is an error when a member is `required`.

**`?=` binds, and this bullet used to say otherwise.** Spec §5.3 has read
"arity counts binding occurrences only — those using `=` or `?=`" since the
first commit; F1 implemented `is_binding()` as `=` alone, and this line said
the same. The spec is right and both were wrong: a block whose only mention of
a key is `x ?= 1` has given `x` a value, and one whose only mention is
`x += { a }` has not. Counting `?=` as a non-binding makes those two
indistinguishable. Corrected in `ast::Statement::is_binding` and its test.

**[OPEN] `?=` may not be worth keeping.** Whether it binds is genuinely hard
to answer, because whether it *does* anything depends on what else is
declared, at any inheritance level and in any file — so the static question
("is this a binding occurrence?") and the dynamic one ("did this bind?") have
different answers and both are reasonable readings of the operator. It is a
plausible candidate for removal in favour of `@replaces` (§7.6), which says
the same thing with an owner named and a load-order-independent meaning.
Recorded now because everything downstream of §5.3 — arity, combination modes
(F5), and the save-state layout — inherits the ambiguity.

The **registry** is a hash index kept beside the declaration-order vector
rather than instead of it: order is load order (§13.2), which is what a reader
and a diagnostic both expect, and no hash map's iteration order is anybody's.
Building it surfaced two lookup bugs a linear scan had been hiding — `class`
and `trait` are separate `unique_in` namespaces (§7.2.4) and a single scan
returned whichever was declared first, and §7.4's instantiation rule names a
*class*, so a top-level statement naming a trait was being accepted as an
object of a kind that cannot exist. Hence `find_class`, `find_trait` and
`find_class_or_trait` rather than one function guessing.

`arity` is checked only for keys the schema declares. §5.3 states arity as
something "declared by the schema", and the open forms are open precisely
because their other keys are property defaults — whose shape is F11's
question, not this pass's.

Still parsed into `KeyDecl` and still acted on by nobody: `default` (F5's),
`editor` (the inspector's), and **`deprecated`**, which §7.2 says "produces a
warning carrying this message" and §14.3's table has no row for. That last one
is a five-line check and a new code, and is left out only because it is not on
this task's list — say the word and it goes in with a §14.3 row.

**[OPEN]** §7.2.1 allows a group to declare a `fix_hint` "so the error can
point at the right construct rather than merely refusing", and nothing says
where a group declares anything: a group is not a declaration, only a name
repeated across the keys that belong to it. Left unimplemented pending a
spelling.

### F4 · Type checking
**Size:** M · **Depends on:** F3

- [x] Every type of spec §6.2, including `TypeExpr` parsing and the bare-enum shorthand (§4.2).
- [x] Coercion from lexical kind to declared type, with a precise error when it fails.
- [x] `dice`, `clock_time`, `duration` sub-grammars validated at compile time. `resource` is checked as a string; **its existence is workstream G's**, since there is no VFS to ask yet.

Three parts, and only the first is §6.2's table read back.

**The declared-type check paid for itself immediately.** Asking whether a type
*expression* means anything — as opposed to whether a value fits it — found
five keys whose declared type nothing declared: `advances_turn_enum` on
`action`, `block<project_defaults>` and `block<project_simulation>` on
`project`, `block<version_constraints>` on `library`, and `block<exits>` on
stdlib's `room`. Every one of them looked checked and was not. All five are
now declared, taking their content from where the documents already give it:
§13.1's project manifest, §13.3's library manifest, proposal §7.2 for
`advances_turn_enum`, and §6.6.1 for `exits`, which the spec writes as
`map<direction, ref<room>>` and which needed a `direction` enum in stdlib to
be that. It is reported at the key, once, rather than at every value written
against it.

Two things it turned up that are the spec's to settle rather than the code's:

- **`ref<C>` names a form as often as a class.** §6.2 defines it as "a
  reference to an object of class `C`", and the built-in set writes
  `ref<action>` and `ref<sector>`, which are references to declared *forms*.
  Both resolve here. §6.2 should probably say so.
- **`offstage_default` cannot be an enum**, because one of its four values
  (proposal §5.3) is `none`, which §3.9 reserves. It is typed `identifier`
  with the four listed in its `doc`, which is the wrong shape for a closed
  set and the only one available.

**`clock_time` is checked for shape and not for range.** §6.2 resolves it
"against the sector's calendar" and §11.6 lets a sector declare a
`local_clock`, so whether hour 30 exists is not this pass's question —
rejecting it would leave an author with a thirty-hour day no way to say so.

**Instantiations (§7.4) are type-checked against the class's property set**,
walking `of_class` for inherited properties, which is what makes §14.3's
`exits.nrth` case actually fire. A key naming no property is left alone:
which keys are *permitted* inside an instantiation needs the object-local
`prop_def` of F11. **Traits are not in that walk**, because `read_class` does
not read the `traits` key into `ClassDecl` at all — a property arriving only
through a trait is not yet type-checked on an instantiation.

### F5 · Combination modes
**Size:** M · **Depends on:** F3

- [ ] Annotations of spec §5.4.1, and the `combine` defaults of §5.4.2 including `smart`.
- [ ] `@debug` and `@platform` remove statements entirely, including for arity checks.
- [ ] Unknown annotation → error (never silently ignored).

### F6 · Suggestions
**Size:** S · **Depends on:** F3

- [ ] Edit-distance "did you mean …?" for unknown keys, form names, and enum values.
- [ ] The `outdoors_room` / `outdoor_room` case from proposal §4.9 as a test.

### F7 · Template parsing and validation
**Size:** M · **Depends on:** F4

Templates are parsed here; the text VM that *evaluates* them is Phase 1.

- [ ] Template grammar of spec §9.1 — interpolation, style directives, conditionals.
- [ ] Expressions of §9.2, including juxtaposition (§9.2.1) and capitalisation (§9.2.2).
- [ ] `E-TEMPLATE-BRACKETS`, `E-STYLE-UNDECLARED`.
- [ ] Localisation keys: `E-LOC-UNDEFINED`, `E-LOC-DUPLICATE`, `W-LOC-UNUSED`.

### F8 · `failureMsg` placement — **owner: `starcore`**
**Size:** S · **Depends on:** F3

Spec §10.5.1 — the rule is subtle and entirely mechanical, so it is cheap to enforce and expensive to leave out.

- [ ] `E-FAILMSG-SILENT`, `E-FAILMSG-UNREACHABLE`, `W-FAILMSG-MISSING`.
- [ ] Reachability computed through conjunction edges only; barriers carry their own message.

### F10 · Globals, constants and flags
**Size:** S · **Depends on:** F4

- [ ] `global` and `const` declared, typed, and registered (spec §6.4).
- [ ] `set_flag` / `clear_flag` / `flag_set` resolve to a declared `bool` global; `E-FLAG-UNDECLARED` and `E-FLAG-NOT-BOOL` otherwise (§6.4.1).
- [ ] `W-GLOBAL-UNUSED` for a declared global never read.

### F11 · Object-local `prop_def`
**Size:** S · **Depends on:** F3

- [ ] `prop_def` inside an object instantiation declares a property on that object only (spec §8.7).
- [ ] Resolution order puts object-local declarations first (§8.4).
- [ ] Redeclaring an inherited name: error on type mismatch, warning on redundancy.

### F12 · Property access and narrowing — **owner: split**
**Size:** M · **Depends on:** F11, F9

The one genuinely novel piece of static analysis in Phase 0. Spec §8.8.

- [ ] Static type for each slot, taken from the action's grammar token where present.
- [ ] Three-way classification: definitely present / definitely absent / possibly present.
- [ ] Narrowing through `of_class`, `has_trait`, `is` in a conjunction.
- [ ] Narrowing flows forward through pipeline stages: `when` → `conditions` → `restrictions` → `effects` and messages. The last hop is what lets a broad grammar token keep its static knowledge (spec §8.8.1).
- [ ] **The stage sequence is read from the schema's `stage_order`, not hard-coded.** `stardata` implements the dataflow over whatever sequence it finds and knows none of the stage names; `starcore` declares the order. This is the task that most threatened the layering, and parameterising it is what saves it (proposal §2.1.1).
- [ ] Narrowing does not escape `OR` or `NOT`.
- [ ] `has_prop` and `prop_or` as the explicit escapes.
- [ ] Error carries the slot's static type, the property, and the classes that do declare it.

### F9 · Reference resolution — **phase boundary, keep small**
**Size:** M · **Depends on:** F4

`ref<C>` validation needs the class hierarchy, which is Phase 1's object model. Do the part that does not:

- [ ] Two-pass load: collect all declared ids, then resolve.
- [ ] Forward and cross-file references legal (spec §13.2).
- [ ] Unresolvable id → error with a suggestion.
- [ ] **Deferred to Phase 1:** checking that a target's *class* satisfies `ref<C>`, trait conflicts, containment cycles. Record the gap rather than growing this task.

---

## G · `starvfs`

Independent of `stardata` — good parallel work, or a change of scene when the CST gets tiring.

### G1 · VFS API
**Size:** M · **Depends on:** B3

Proposal §12.5: **async-capable from day one**, even though the desktop implementation is synchronous. Retrofitting this after the browser target exists is the expensive path.

- [ ] `read(path) -> future<bytes>` with a synchronous fast path for resident layers.
- [ ] `exists`, `list`, `stat`, `open_stream`.
- [ ] Path normalisation; escaping the mount root is impossible (spec/proposal §8.2 sandbox requirement).

### G2 · Directory layer
**Size:** S · **Depends on:** G1

- [ ] Read and write against a real directory.
- [ ] Case-sensitivity behaviour defined and tested — a real cross-platform trap.

### G3 · Archive layer
**Size:** M · **Depends on:** G1

- [ ] Decide libzip vs miniz and record why. Weigh dependency size against the WASM target.
- [ ] Read-only zip layer with entry index.
- [ ] Rejects path traversal in entry names.

### G4 · Mount stack
**Size:** M · **Depends on:** G2, G3

- [ ] Ordered layers, highest-priority-first resolution (proposal §14.1).
- [ ] Writes go to the topmost writable layer.
- [ ] Query which layer supplied a path — needed by the debugger and by mod diagnostics.

### G5 · VFS tests
**Size:** S · **Depends on:** G4

- [ ] Layer shadowing, write-through, missing files, traversal attempts.
- [ ] Same results on all three platforms, including a case-collision fixture.

---

## H · Corpus, tooling and exit

### H1 · Complete the negative fixtures
**Size:** M · **Depends on:** C2

- [ ] A fixture per row of spec §15 — currently 14 of roughly 20.
- [ ] Each declares its `# EXPECT` code.
- [ ] CI asserts every code in the table has at least one fixture, so the gap cannot reopen.

### H2 · Port checker expectations to C++
**Size:** M · **Depends on:** H1, C4, F8

- [ ] The C++ implementation reproduces every diagnostic the Python checker reports on the corpus.
- [ ] Both run in CI during the overlap, and disagreement fails the build.

### H3 · Stress-corpus generator
**Size:** M · **Depends on:** nothing

Proposal §17 item 2: build it **before** engine code, so the Phase 2 go/no-go gate is measurable from the first commit. A Python script — no C++ needed.

- [ ] Generates a parameterised world: 20,000 rooms, 60,000 objects, 500 NPCs, 40 sectors.
- [ ] Variants for `catch_up` and `simulate` offstage models.
- [ ] Deterministic from a seed.
- [ ] Phase 0 use: a parse-throughput baseline, recorded so later regressions are visible.

### H4 · Retire `check_stardata.py`
**Size:** S · **Depends on:** H2

- [ ] Once C++ covers everything, **delete it** — its own docstring says so, and two validators that disagree are worse than one.
- [ ] Keep the fixtures and the `# EXPECT` convention; they are the durable part.

### H5 · Phase 0 exit gate
**Size:** S · **Depends on:** E6, E8, F7, G5, H2

- [ ] `tour.star` parses with zero diagnostics on all three platforms.
- [ ] Round-trips byte-identically, CRLF and LF.
- [ ] Every `invalid/` fixture produces its expected diagnostic with a correct span.
- [ ] Random-edit fuzzing runs clean for a fixed budget.
- [ ] Parse throughput recorded for the stress corpus.
- [ ] A short `docs/phase-0-report.md` noting what slipped and what Phase 1 inherits.

---

## Critical path

```
A4 ──────────────────────────────┐
B1 → B3 → C1 → C2 → C3 → C4      │
                 ↓               │
              D1 → D2 → D3 → D5  │
                             ↓   ↓
                        E1 → E2 → E4 → E5 → E6 → E7 → E8
                                   ↓                    ↓
                              F1 → F3 → F4 → F7 → F8    │
                                                   ↓    ↓
                                                  H2 → H5
```

`G` (VFS) and `H3` (stress corpus) are off the critical path entirely and can be done whenever.

**Front-load, in this order:** A4 (the line-ending trap costs days if discovered late), B6 (free CI value immediately), C1–C3 (everything reports through diagnostics), then a throwaway prototype of E1–E2 before committing to the CST design.

## Rough size

| Workstream | Days |
|---|---|
| A · Governance | 3 |
| B · Build and CI | 6 |
| C · Diagnostics | 7 |
| D · Lexer | 8 |
| E · CST | 14 |
| F · Schema | 23 |
| G · VFS | 7 |
| H · Corpus and exit | 7 |
| **Total** | **≈ 75 days ≈ 15 weeks** |

The proposal estimated 6–8 weeks. **That was optimistic** — the CST and the schema layer are each about a fortnight on their own, and the estimate did not account for diagnostics being real infrastructure rather than a printf.

Two honest ways to land closer to the original number:

- **Defer F5, F6, F9 and F12 to Phase 1.** Combination modes, suggestions and reference resolution are all things Phase 1 needs anyway, and none is required by the phase exit criterion as written. Saves ~13 days.
- **Defer G entirely.** The VFS is not needed until Phase 2 loads a sector. Saves 7 days. The one thing that must not be deferred is the *shape* of its API (§G1's async requirement), so write the header and stub it.

Either or both gets Phase 0 to 9–11 weeks. I would take both and treat Phase 0 as strictly "the format is real and tooled".

## Not in Phase 0

Listed because each will be tempting, and each belongs to a later phase where its exit criterion lives.

| Not now | Where it belongs |
|---|---|
| Compile-time replication, `count = 10` (spec §8.9) | Phase 4, but its parser constraint is Phase 1 |
| Dynamic object creation | Never — spec §1.4.1 |
| World store, containment, traits, classes at runtime | Phase 1 |
| The text VM that *evaluates* templates (Phase 0 only parses them) | Phase 1 |
| Lua embedding and the sandbox | Phase 1 |
| The turn sequence and actor loop | Phase 1 |
| `starlang` and the English rule table | Phase 1 |
| Sectors, streaming, save deltas | Phase 2 |
| Any Qt code at all | Phase 3 |
| Starforge and `.spak` | Phase 4 |

The one thing worth doing early despite belonging later is **H3**, because a performance premise you cannot measure is a performance premise you are not testing.
