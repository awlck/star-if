# Phase 0 — Foundations: backlog

**Derived from:** `docs/proposal.md` §15 (Phase 0) and §17 · **Spec:** `docs/stardata-spec.md`

**Phase exit criterion (from the proposal):** `tests/corpus/tour.star` parses, round-trips byte-identically, and reports good errors on a corpus of broken files — on Windows, macOS and Linux.

---

## How to read this

Tasks are grouped into workstreams. Within a workstream they are roughly sequential; across workstreams they are mostly independent, and §Critical path says which order actually matters.

Sizes are **S** (≤ 1 day), **M** (2–4 days), **L** (1–2 weeks), at full-time pace. Multiply for evenings-and-weekends.

**Definition of done, applying to every task:**

- Tests exist and pass under `ctest`.
- CI is green on all three desktop platforms.
- No new compiler warnings (warnings are errors — see B4).
- Anything that changes the format is reflected in `docs/stardata-spec.md`, `tests/corpus/tour.star`, and a fixture in `tests/corpus/invalid/`.

---

## A · Repository and governance

Cheap, unblocks everything, and gets much more expensive after the first outside contribution.

### A1 · Licence files
**Size:** S · **Depends on:** nothing

Per proposal §14.5.

- [ ] `LICENSE` at root — Apache 2.0.
- [ ] `stdlib/LICENSE` — MIT-0.
- [ ] `docs/LICENSE` — CC-BY-4.0.
- [ ] SPDX headers in a source file template; a script or clang-format hook to keep them.
- [ ] `NOTICE` file, as Apache 2.0 expects.

### A2 · `TRADEMARKS.md`
**Size:** S · **Depends on:** nothing

Apache 2.0 §6 withholds trademark rights, so the names need a usage guideline rather than a licence term.

- [ ] Covers STAR IF, Starbase, Starforge, Starhelm, Stardata, Starpak, Starscape.
- [ ] States the "Built on STAR IF technology" convention for third-party games.
- [ ] Explicitly a courtesy guideline, not a licence obligation.

### A3 · Repository scaffolding
**Size:** S · **Depends on:** nothing

- [ ] `README.md` — what the project is, what works today, how to build.
- [ ] `CONTRIBUTING.md` — build, test, style, and the "spec + corpus + fixture" rule from the Definition of Done.
- [ ] `.gitignore`, `.editorconfig`.
- [ ] `docs/` index linking proposal, spec, backlog.

### A4 · Line-ending policy — **do this before any `.star` file is committed by a Windows contributor**
**Size:** S · **Depends on:** nothing

Spec §2 requires that CRLF and LF both round-trip **unchanged**. Git's `core.autocrlf` will silently rewrite line endings on checkout, which means the round-trip test (E6) passes on Linux and fails on Windows for reasons that have nothing to do with the parser. This has cost other projects days.

- [ ] `.gitattributes` marks `*.star` as `-text` so Git never normalises them.
- [ ] Corpus contains at least one deliberately CRLF file and one LF file, and CI asserts both survive.
- [ ] Documented in `CONTRIBUTING.md` so nobody "helpfully" fixes it.

---

## B · Build and continuous integration

### B1 · CMake skeleton and presets
**Size:** M · **Depends on:** A3

- [ ] Top-level `CMakeLists.txt` with the `libs/` and `apps/` layout of proposal §2.1.
- [ ] `CMakePresets.json` for `windows-msvc`, `macos-clang`, `linux-gcc`, `linux-clang`, each in Debug and Release.
- [ ] One-command build from a clean checkout on each platform.
- [ ] C++20 enforced; the build fails clearly on an inadequate compiler.

### B2 · Dependency manifest
**Size:** S · **Depends on:** B1

- [ ] `vcpkg.json` pinning Catch2 and the archive library chosen in G3.
- [ ] Dependencies resolve offline after one warm cache, so CI is not hostage to a registry.
- [ ] No Qt dependency anywhere in `libs/` — that boundary is load-bearing (proposal §2.1) and should be asserted, not assumed.

### B3 · Test harness
**Size:** S · **Depends on:** B1, B2

- [ ] Catch2 wired to `ctest`.
- [ ] `tests/unit/` builds and runs one trivial passing test.
- [ ] A corpus-driven test helper that discovers `.star` files rather than listing them, so adding a fixture needs no build-file edit.

### B4 · Compiler hygiene
**Size:** S · **Depends on:** B1

- [ ] `-Wall -Wextra -Werror` / `/W4 /WX`.
- [ ] ASan + UBSan in Debug on Linux and macOS.
- [ ] `.clang-format` and `.clang-tidy`, with a format check in CI.

### B5 · CI matrix
**Size:** M · **Depends on:** B1–B4

- [ ] GitHub Actions: {Windows, macOS, Linux} × {Debug, Release}.
- [ ] Runs `ctest`, the format check, and the sanitiser build.
- [ ] Build + test under 10 minutes, or the loop stops being used.

### B6 · Wire the Python checker into CI — **can land today, before any C++ exists**
**Size:** S · **Depends on:** nothing

- [ ] `python3 tests/check_stardata.py --check-docs --self-test --strict` runs on every push.
- [ ] `--check-docs` parses every fenced example in `docs/*.md`; it has already caught two real spec bugs that the corpus could not.
- [ ] Gives the spec-versus-corpus guarantee immediately rather than at the end of Phase 0.

---

## C · Diagnostics

Build this **first** of the `stardata` work. Everything downstream reports through it, and retrofitting spans into a parser that was written without them is miserable.

### C1 · Source manager and spans
**Size:** M · **Depends on:** B3

- [ ] `SourceId`, `Span { SourceId, byte offset, length }`.
- [ ] Registry mapping `SourceId` → path and contents.
- [ ] Byte offset → line/column, with a lazily-built line table.
- [ ] Correct columns for tabs and for non-ASCII (offset in bytes, column in characters).

### C2 · Diagnostic model
**Size:** M · **Depends on:** C1

Spec §15 lists the required diagnostics; `tests/check_stardata.py` already defines stable codes worth reusing.

- [ ] `Diagnostic { code, severity, primary span, notes[], fix-its[] }`.
- [ ] Code enum covering every row of spec §15.
- [ ] Multi-span diagnostics — the duplicate-key case (§5.3) must cite *both* spans, so this is required, not optional.
- [ ] Sink interface: collecting, counting, limiting.

### C3 · Diagnostic rendering
**Size:** S · **Depends on:** C2

- [ ] Human renderer: source line, caret, underline, note, suggestion.
- [ ] Machine renderer: one line per diagnostic, stable and greppable, for CI.
- [ ] Colour when a TTY, never when piped.

### C4 · Diagnostic snapshot tests
**Size:** M · **Depends on:** C3, B3

- [ ] Each file in `tests/corpus/invalid/` has an expected-output snapshot.
- [ ] `--update-snapshots` regenerates; CI fails on any diff.
- [ ] This is what stops error messages silently degrading, which is otherwise invisible until an author complains.

---

## D · Lexer

### D1 · Token and trivia model
**Size:** S · **Depends on:** C1

- [ ] Token kinds for spec §3: identifier, integer, decimal, string, lockey, annotation, operator, punctuation, angle.
- [ ] Trivia kinds: whitespace, comment. Trivia is **retained**, not discarded (spec §14.2).
- [ ] Tokens carry spans, not copied text.

### D2 · Lexer core
**Size:** M · **Depends on:** D1, C2

- [ ] Implements spec §3.1–§3.9.
- [ ] `<` and `>` emitted as an ambiguous `angle` kind, resolved by the parser (spec §4.2) — the lexer must **not** guess, and must not use whitespace to decide.
- [ ] Multi-character operators matched before single-character punctuation (`>=` is not `>` then `=`).
- [ ] UTF-8 validation with a useful error, not a crash.

### D3 · Lexical diagnostics
**Size:** M · **Depends on:** D2, C2

Every one of these has a fixture in `tests/corpus/invalid/`:

- [ ] `E-STR-MULTILINE`, `E-STR-ESCAPE`, `E-STR-UNTERMINATED`
- [ ] `E-DEC-PRECISION`, `E-DEC-LEADING-DOT`, `E-NUM-TRAILING-DOT`
- [ ] `E-BRACKET-OUTSIDE`, `E-RESERVED-WORD`, `E-UNICODE-WS`, `E-BAD-CHAR`
- [ ] Recovery: one bad token does not abandon the file.

### D4 · Adjacent string concatenation
**Size:** S · **Depends on:** D2

- [ ] Spec §3.5.1 — adjacent literals form one scalar.
- [ ] **Split points preserved in the CST**, so E6's round-trip reproduces the author's line breaks.

### D5 · Lexer conformance tests
**Size:** M · **Depends on:** D3, D4

- [ ] Token-stream golden test over `tour.star`.
- [ ] A table-driven test per spec §3 rule.
- [ ] Fuzz the lexer on random bytes: no crash, no hang, no unbounded memory.

---

## E · Lossless CST

**The hard part of Phase 0, and the reason the estimate is 8 weeks rather than 6.** Proposal §13.1 makes this the property that lets a graphical editor and a hand-edited text format coexist; getting it wrong is not recoverable later.

Prototype E1–E2 in a scratch branch before committing to the design.

### E1 · Green tree
**Size:** L · **Depends on:** D1

Immutable, shareable, parent-free nodes — the rust-analyzer `rowan` / Roslyn green-red model.

- [ ] `GreenNode { kind, text_length, children[] }`, `GreenToken { kind, text }`.
- [ ] Interning so identical subtrees share storage.
- [ ] No parent pointers and no absolute offsets — that is what makes subtrees shareable across edits.
- [ ] Reference-counted, thread-safe to share.

### E2 · Red tree / cursor API
**Size:** M · **Depends on:** E1

- [ ] Cursor carrying parent and absolute offset, computed on demand.
- [ ] Navigation: parent, children, siblings, ancestors, descendants.
- [ ] `SyntaxNode::text()` reconstructs source by concatenating leaves.

### E3 · Trivia attachment policy
**Size:** M · **Depends on:** E2

Unglamorous and worth deciding once, in writing, because ad-hoc rules here are what make comments drift during editing.

- [ ] Written rule for leading vs trailing trivia (suggested: trailing runs to and including the newline; everything else is leading on the next token).
- [ ] A comment on its own line above a statement attaches to that statement, so moving the statement moves the comment.
- [ ] Documented in `CONTRIBUTING.md` with examples.

### E4 · Parser
**Size:** L · **Depends on:** E2, D5, C2

- [ ] Implements the grammar of spec §4 exactly.
- [ ] `<` disambiguation per §4.2 — comparison in operator position, type arguments in value position, **never by whitespace**.
- [ ] `Call` in value position (§4.3).
- [ ] Block shape: list vs record, `E-BLOCK-MIXED` on mixing (§5.2).
- [ ] **Error recovery**: a malformed block produces an error node and parsing continues, because an editor must have a tree even for broken input.
- [ ] The tree covers every byte of input, including trivia and error text.

### E5 · Writer
**Size:** S · **Depends on:** E4

- [ ] Serialise any tree back to text.
- [ ] Byte-exact for an unmodified tree, including BOM and line-ending style.

### E6 · Round-trip conformance — **the phase's headline test**
**Size:** S · **Depends on:** E5, A4

- [ ] For every `.star` under `tests/corpus/`, parse → write → compare bytes.
- [ ] Runs on all three platforms.
- [ ] Includes the CRLF and LF fixtures from A4.

### E7 · Edit API
**Size:** M · **Depends on:** E6

- [ ] Replace a node or token, returning a new tree sharing unchanged subtrees.
- [ ] Re-print touches only the affected span; text outside it is byte-identical.
- [ ] Insert and delete a statement within a block, preserving surrounding trivia.

### E8 · Round-trip fuzzing
**Size:** M · **Depends on:** E7

Proposal §16.1 names round-trip degradation as a high risk; this is the mitigation.

- [ ] Property test: apply N random edits, assert unaffected regions are byte-identical and the result re-parses.
- [ ] Structure-aware fuzzer over the corpus, run in CI on a time budget.
- [ ] A corpus of any crashers found, kept as regression fixtures.

---

## F · Schema layer and validation

### F1 · Typed AST view
**Size:** M · **Depends on:** E4

- [ ] Typed accessors over the CST (`Statement::key()`, `Value::as_block()`), no separate tree.
- [ ] Tolerates missing and malformed children, returning optionals rather than asserting.

### F2 · Schema bootstrap
**Size:** M · **Depends on:** F1

Schemas are written in Stardata (spec §7.2), so this is mildly circular and needs care.

- [ ] A minimal hard-coded schema-of-schemas sufficient to validate `schema.star` itself.
- [ ] `stdlib/core/schema.star` declaring the standard forms of Appendix C.
- [ ] A test asserting `schema.star` validates against the bootstrap.

### F3 · Schema registry and key validation
**Size:** M · **Depends on:** F2

- [ ] Registry keyed by form id; libraries may contribute (spec §13.3).
- [ ] Unknown key in a closed schema → error; `open = yes` permits and retains.
- [ ] Arity: duplicate under `arity = one` cites both spans; `arity = many` preserves order.
- [ ] `+=` / `-=` / `?=` do not count as binding occurrences (spec §5.3).

### F4 · Type checking
**Size:** M · **Depends on:** F3

- [ ] Every type of spec §6.2, including `TypeExpr` parsing and the bare-enum shorthand (§4.2).
- [ ] Coercion from lexical kind to declared type, with a precise error when it fails.
- [ ] `dice`, `clock_time`, `resource`, `duration` sub-grammars validated at compile time.

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

### F8 · `failureMsg` placement
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

### F12 · Property access and narrowing
**Size:** M · **Depends on:** F11, F9

The one genuinely novel piece of static analysis in Phase 0. Spec §8.8.

- [ ] Static type for each slot, taken from the action's grammar token where present.
- [ ] Three-way classification: definitely present / definitely absent / possibly present.
- [ ] Narrowing through `of_class`, `has_trait`, `is` in a conjunction, and from `when` into later stages.
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

### H4 · Retire or keep `check_stardata.py`
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
| F · Schema | 17 |
| G · VFS | 7 |
| H · Corpus and exit | 7 |
| **Total** | **≈ 69 days ≈ 14 weeks** |

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
