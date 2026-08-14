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

Not yet started: the C++ engine (`libs/`, `apps/`), the build, and CI.
Nothing here builds or runs a game yet.

## Building

There is no C++ code yet, so there is nothing to build. Once `libs/` and
`apps/` exist (backlog workstream B), this section will cover the CMake
presets and the one-command build from a clean checkout on Windows, macOS
and Linux.

In the meantime, the Stardata conformance checker needs only Python 3 and no
dependencies:

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
