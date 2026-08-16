# STAR IF System

A parser-based interactive fiction authoring system, aimed at two things the
incumbents (Inform 7, TADS 3, ADRIFT 5, Dialog) don't do well together:

1. **Large worlds stay fast.** Turn cost scales with what's near the player,
   not with the size of the game — thousands of rooms and tens of thousands
   of objects should be a design choice, not a performance cliff.
2. **RPG mechanics are first-class but optional.** Stats, combat, equipment,
   quests and NPC schedules are supported by the core; a pure puzzle game
   pays nothing for them.

See [`docs/proposal.md`](docs/proposal.md) for the full design rationale, and
[`docs/README.md`](docs/README.md) for the rest of the documentation.

## Components

| Name | What it is |
|---|---|
| **Starbase** | The editor / IDE. |
| **Starforge** | The compiler and packager. |
| **Starhelm** | The runtime / interpreter. |
| **Stardata** | The source data format (`.star`) — see [`docs/stardata-spec.md`](docs/stardata-spec.md). |
| **Starpak** | The compiled distributable format (`.spak`). |

## Status

Early development, in Phase 0 (Foundations) of the [roadmap](docs/proposal.md#15-roadmap).
See [`docs/phase-0-backlog.md`](docs/phase-0-backlog.md) for the current task
list and [`docs/phase-0-backlog.md#critical-path`](docs/phase-0-backlog.md#critical-path)
for what's blocking what.

What exists today:

- The Stardata format specification ([`docs/stardata-spec.md`](docs/stardata-spec.md)).
- A conformance corpus (`tests/corpus/`) and a dependency-free Python checker
  (`tests/check_stardata.py`) that validates it against the spec — a
  stand-in until the real parser (`libs/stardata`) exists. See
  [`tests/README.md`](tests/README.md).
- The build skeleton: CMake, presets for all three desktop platforms, a
  vcpkg manifest, compiler warnings-as-errors, and ASan/UBSan in Debug.
- In `libs/stardata`: the diagnostics layer (source manager, spans,
  diagnostic model, human and machine renderers), the **lexer** — tokens and
  retained trivia for every rule of the specification's §3 — the
  **lossless CST** (a green/red syntax tree, a parser for the grammar of §4
  with error recovery, a writer, and an edit API that shares untouched
  subtrees), a **typed AST view** over that tree, and the beginnings of the
  **schema layer**.

  `tests/corpus/tour.star` — 63 KB exercising every construct in the spec —
  parses with zero diagnostics and writes back **byte for byte**, CRLF and
  LF fixtures alike. So does every deliberately broken file in
  `tests/corpus/invalid/`, because recovery has to be lossless too. That is
  the round-trip property of spec §14.2, and the Phase 0 exit criterion.

- In `libs/starcore/builtin`: the **core-owned schema set** (spec §7.2.2),
  written as Stardata — the forms `starcore` reads and writes itself,
  `starcore.object` and its six slots, and the list of what core depends on,
  stated as data so that a failure is a named diagnostic at load rather than
  a crash much later. All of it sealed — a library adds keys with
  `schema_extension` and properties with `class_extension`, and may never
  redefine or supersede.

- In `stdlib/core`: the kinds, traits and actions an author actually writes,
  with **no privileged status** — and a test that says so, by loading the
  directory as an ordinary library and requiring that nothing in it needed a
  privilege.

  Not yet: types, arity, combination modes, reference resolution. Nothing
  yet checks that a value matches its declared type.

Not yet started: the actual engine (the rest of `libs/`, all of `apps/`).
Nothing here plays a game yet.

## Building

```sh
cmake --workflow --preset linux-gcc-debug   # or macos-clang-*, windows-msvc-*, linux-clang-*, each -debug/-release
```

See [`CONTRIBUTING.md`](CONTRIBUTING.md#build) for the one-time vcpkg setup
this needs.

The Stardata conformance checker needs only Python 3 and no dependencies:

```sh
python3 tests/check_stardata.py --check-docs --self-test --strict
```

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md).

## Licence

Licensing is not uniform across the tree — see [`NOTICE`](NOTICE) for the
full table and [`CONTRIBUTING.md`](CONTRIBUTING.md#licensing) for how it
maps onto the directory layout. In short: engine and tooling code is
Apache 2.0, `stdlib/core` is MIT-0, and documentation is CC-BY-4.0.

The names STAR IF, Starbase, Starforge, Starhelm, Stardata, Starpak and
Starscape are governed by [`TRADEMARKS.md`](TRADEMARKS.md), not by the code
licence.
