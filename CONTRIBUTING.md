# Contributing

## Where the project is

Phase 0 (Foundations) is in progress — see
[`docs/phase-0-backlog.md`](docs/phase-0-backlog.md) for the task list and
[`docs/README.md`](docs/README.md) for the rest of the documentation. The
build skeleton (workstream B) exists: CMake, presets, a dependency manifest,
compiler warnings and sanitisers, and `libs/stardata` as a placeholder
library. The actual lexer/CST/parser (workstreams D-F) haven't started —
`libs/stardata` today only exists to give the build something real to
compile, link and test.

## Build

You need a C++20 compiler (GCC ≥ 12, Clang ≥ 14, or MSVC ≥ 19.29), CMake
≥ 3.25, Ninja, and [vcpkg](https://github.com/microsoft/vcpkg):

```sh
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh   # or bootstrap-vcpkg.bat on Windows
export VCPKG_ROOT="$(pwd)/vcpkg"
```

Then, from a clean checkout, one command builds and tests everything for a
given platform and configuration (`CMakePresets.json`; backlog B1):

```sh
cmake --workflow --preset linux-gcc-debug     # or linux-clang-*, macos-clang-*, windows-msvc-*, each in -debug/-release
```

That runs configure, build and `ctest` in one shot. `vcpkg.json` pins
Catch2 and miniz; vcpkg resolves both from its own registry cache after the
first (network-requiring) run, so CI is not hostage to a registry on every
build (backlog B2). On Windows, run this from a Developer Command Prompt (or
after `vcvarsall.bat`) so `cl` and Ninja can find each other.

## Test

Once configured, `ctest` is the primary test runner (backlog B3):

```sh
ctest --test-dir build/linux-gcc-debug --output-on-failure
```

Separately, the Stardata format itself still has its own conformance
checker, standing in for the real parser's own conformance tests until
workstream D/E lands:

```sh
python3 tests/check_stardata.py --check-docs --self-test --strict
```

Both run in CI (backlog B5, B6). See [`tests/README.md`](tests/README.md)
for what the Python checker checks and doesn't, and for the suppression
syntax.

## The spec + corpus + fixture rule

This is the **Definition of Done** for every task in the backlog, and it's
worth internalising before your first change:

> Anything that changes the format is reflected in
> `docs/stardata-spec.md`, `tests/corpus/tour.star`, and a fixture in
> `tests/corpus/invalid/`.

Concretely, adding or changing a rule in the format means, in order:

1. Write (or edit) the rule in `docs/stardata-spec.md`. This is the
   normative source — code and tests follow it, not the other way round.
2. Add a positive example to `tests/corpus/tour.star`, the reference corpus
   that's meant to exercise every construct in the spec.
3. Add a negative fixture to `tests/corpus/invalid/` that declares the
   diagnostic code the rule should provoke (`# EXPECT ...`).
4. Implement the check — in `tests/check_stardata.py` if it's checkable
   without the schema layer, otherwise in the real parser once it exists.

Step 3 is the one that gets skipped, and it's the one that matters most: a
rule with no negative fixture is a rule nobody has confirmed can actually
fail. `tests/check_stardata.py --self-test` runs every fixture in
`tests/corpus/invalid/` and checks it produces its expected code.

The same discipline applies in reverse: don't add a fixture or a corpus
example for behaviour the spec doesn't describe. The spec, the corpus and
the fixtures are meant to stay in lock-step, and `check_stardata.py
--check-docs` exists specifically to catch the spec drifting from its own
worked examples.

## Line endings — read this before committing a `.star` file

Spec §2 requires that both LF and CRLF line endings round-trip through the
format **unchanged** — an author's line endings are theirs to keep, not
something the tooling normalises. Git's `core.autocrlf` will happily
rewrite line endings on checkout unless told not to, which is exactly the
kind of thing that passes on Linux and silently fails the round-trip test
on Windows for reasons that have nothing to do with the parser.

`.gitattributes` marks `*.star` as `-text` for this reason: it tells Git to
leave those files alone on checkout and checkin, byte for byte, regardless
of platform or of any user-level `core.autocrlf` setting. **Do not
"helpfully" change this**, and do not run a line-ending normalisation tool
(`dos2unix`, an editor's "fix line endings" action, etc.) over
`tests/corpus/`. The corpus deliberately contains both a CRLF fixture and
an LF fixture so the round-trip property is actually tested on both, not
assumed.

If your editor is configured to normalise line endings on save, either
disable that for `tests/corpus/` or double-check `git diff` before
committing — a diff that touches every line of a `.star` file you didn't
mean to edit is almost always this.

## Style

- C++20. `-Wall -Wextra -Werror` / `/W4 /WX` — warnings are errors
  (backlog B4, `cmake/CompilerWarnings.cmake`). ASan + UBSan run in Debug
  builds on Linux and macOS (`cmake/Sanitizers.cmake`). `.clang-format` and
  `.clang-tidy` at the repo root define the formatting and lint rules; run
  `clang-format -i` before committing, and expect CI to check it (backlog
  B5).
- Python (`tests/check_stardata.py`, `scripts/`) is dependency-free by
  design — don't add a `requirements.txt` for tooling that doesn't need
  one.
- No Qt in `libs/` — that boundary is load-bearing (proposal §2.1): it's
  what keeps the compiler and CLI runtime buildable and testable without
  Qt, and keeps the WASM target tractable.

## Dependencies

`vcpkg.json` (backlog B2) currently pins two things:

- **Catch2** — the test framework (backlog B3).
- **miniz** — the archive library for `starvfs`'s zip layer (backlog G3).
  Chosen over libzip for now: it's a small, dependency-free, permissively
  licensed single-purpose library with no transitive dependencies (libzip
  pulls in zlib and optionally bzip2/OpenSSL/zstd), which matters more than
  usual here because the core is also an Emscripten/WASM target (proposal
  §2) where every linked dependency has an outsized cost. This is a
  provisional call made to give `vcpkg.json` something real to pin — G3 is
  where it gets revisited properly, with the read-only zip layer that
  would actually test the choice.

## SPDX headers

Every new source file (C++, CMake, Python; not `.star` or prose) should
start with an SPDX header:

```sh
python3 scripts/spdx_header.py --template path/to/new_file.cpp
```

prints the two lines to paste in, with the licence identifier appropriate
to where the file lives (see [Licensing](#licensing) below — most of the
tree is Apache-2.0, `stdlib/core/` is MIT-0). Run once, after cloning:

```sh
./scripts/install-hooks.sh
```

to install a pre-commit hook that checks staged files for the right
header and blocks the commit if one's missing or wrong.

## Licensing

The tree is not uniformly licensed. `NOTICE` has the authoritative table;
summarised:

| Path | Licence | File |
|---|---|---|
| `libs/`, `apps/`, `cmake/`, `scripts/`, `tests/` | Apache License 2.0 | [`LICENSE`](LICENSE) |
| `stdlib/core/` | MIT No Attribution (MIT-0) | [`stdlib/LICENSE`](stdlib/LICENSE) |
| `stdlib/starscape/` (implementation) | Apache License 2.0 | [`LICENSE`](LICENSE) |
| `docs/` | Creative Commons Attribution 4.0 (CC-BY-4.0) | [`docs/LICENSE`](docs/LICENSE) |

The Starscape *SRD* — the RPG rules as publishable text, as opposed to
their code implementation — will carry its own licence (the ORC License;
see `docs/proposal.md` §14.5) once it exists as a separate artefact. That's
a licence for *republishing the rules text*, distinct from the Apache 2.0
licence on the code that implements them.

By contributing, you agree your contribution is licensed under the licence
that already applies to the location you're contributing to.

The names STAR IF, Starbase, Starforge, Starhelm, Stardata, Starpak and
Starscape are governed by [`TRADEMARKS.md`](TRADEMARKS.md), which is a
usage guideline, not a licence term (Apache 2.0 §6 withholds trademark
rights already, so nothing here narrows what the code licence grants).

## Trivia attachment, error recovery, and other subtle rules

Some backlog tasks (E3 in particular — trivia attachment policy) ask for a
rule to be **written down**, not just implemented, precisely because an
unwritten convention is one every contributor reinvents slightly
differently. When you land one of these, add it here rather than leaving
it to be inferred from the code.
