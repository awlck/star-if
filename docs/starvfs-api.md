# starvfs — the layered virtual filesystem's API

**Companion to:** `docs/phase-0-backlog.md` workstream G, `docs/proposal.md` §14.1

This is the rationale for the API surface under `libs/starvfs/include/starvfs/`.
The backlog's own recommendation for workstream G, in its "Rough size" section, is
to defer the *implementation* — the VFS isn't needed until Phase 2 loads a
sector — but not its *shape*:

> The one thing that must not be deferred is the shape of its API (§G1's async
> requirement), so write the header and stub it.

That is what landed alongside this document: headers that compile, link, and are
covered by a smoke test, with the layers and the mount stack's resolution logic
stubbed to fail with `Error::NotImplemented`. `Path`, `Result`, `Future`/`Promise`,
and `Layer::exists` are implemented for real, because they are pure computation —
nothing a reviewer can judge from a signature alone — and small enough that
stubbing them would just mean writing them twice.

## Why the shape matters now

Two decisions get more expensive the longer they wait, both called out in
`proposal.md`:

- **§12.5:** "the sector-streaming design's async loads are natural in a browser
  and awkward if the core assumes synchronous VFS reads — so the VFS API must be
  async-capable from day one, even though the desktop implementation is
  synchronous. Retrofitting this later is expensive."
- The host-I/O boundary has the same property: once code above it calls
  `std::filesystem` directly even once, every frontend built afterwards inherits
  that assumption, and untangling it later means auditing the whole library rather
  than reviewing one interface.

Both are addressed by the design below before any layer has real behaviour.

## Files

`libs/starvfs/include/starvfs/` is flat — the whole library is estimated at
~1.5k lines (`proposal §2`'s tech-stack table), well under the size at which
`stardata`'s `diag/`/`lex/`/`cst/` subdirectory split earns itself.

| Header | Contents |
|---|---|
| `bytes.hpp` | `Bytes`, the one payload type |
| `error.hpp` | `Error`, `Failure`, `Result<T>` |
| `future.hpp` | `Future<T>`, `Promise<T>` |
| `path.hpp` | `Path` — the canonical VFS path |
| `host.hpp` | `HostIo`, `HostStat`, `HostEntry`, `CaseSensitivity` |
| `native_host.hpp` | `NativeHostIo` — the one Phase 0 `HostIo` |
| `layer.hpp` | `Layer`, `Stat`, `Entry`, `Stream` |
| `directory_layer.hpp`, `zip_layer.hpp`, `memory_layer.hpp` | the three concrete layers |
| `vfs.hpp` | `Vfs` — the mount stack façade, `LayerId` |
| `version.hpp` | mirrors `stardata::version()`, so the smoke test has a real symbol |

Style follows `libs/stardata/include/stardata/diag/source_manager.hpp`: SPDX
header, `#pragma once`, `[[nodiscard]]` throughout, opaque handles with a private
`kInvalid` sentinel (`LayerId` mirrors `stardata::diag::SourceId` exactly), and
comments that explain *why*, citing the backlog task or proposal section a design
choice answers.

## Path: escape made impossible, not merely rejected

`Path::parse` is the sandbox boundary `proposal §8.2` asks for — "File access is
exclusively through the VFS API, which is scoped to the game's own mount points
and cannot escape them" — expressed as a type rather than a check:

- `/` is the only separator. A `\` is **rejected, never translated**: it is a
  legal byte in a POSIX filename and in a zip entry name, and translating it
  would make two distinct entries alias each other.
- A leading `/` is stripped — every `Path` is root-relative, and there is no
  absolute form.
- `.` segments and empty segments (`a//b`, a trailing `/`) are dropped.
- `..` pops the preceding segment. Resolving one with nothing to pop — `..` at
  the root, or enough of them to walk past it — **fails the parse** rather than
  clamping to the root.
- An embedded NUL or invalid UTF-8 fails the parse.

`Layer` and `Vfs` take `const Path&`, never a raw string, so there is no call
site where an unnormalised name can reach a layer and no traversal check to
remember at each of them — backlog G3's "rejects path traversal in entry names"
falls out of `ZipLayer` using `Path` for its index rather than needing a check of
its own.

Windows reserved names (`CON`, `NUL`, `COM1`, …), trailing dots, and trailing
spaces are **deliberately not rejected** by `Path`: a `.spak` may legitimately
contain `res/con.png`, and the zip layer must be able to serve it. Refusing to
*write* such a name to a real directory is `NativeHostIo`'s problem, surfaced on
Windows — not this type's.

## Errors: a bespoke enum, no exceptions

Nothing in this codebase throws (`grep -rn "throw " libs/` is empty), so errors
are values here too. A plain `enum class Error` rather than `std::error_code`:
`error_code`'s entire purpose is errno/`WinError` interop, and the point of the
`HostIo` boundary is that errno lives on the far side of it. An enum is
exhaustively switchable, which `-Werror` (backlog B4) turns into a build failure
the moment a caller forgets a case — see `error.cpp`'s `to_string`, which has no
`default:` for exactly that reason.

A bare `Error` is nearly useless in a mod diagnostic — `NotFound` doesn't say
which layer looked, or for what — so every failure is a `Failure{Error, detail}`,
and every layer builds its own naming itself, the same way
`stardata::schema::SchemaSet::offer`'s rejections name the declaration's owner.

`Result<T>` is a minimal `expected<T, Failure>` (C++20 has no `std::expected`) at
roughly 80 lines, with a `Result<void>` specialisation for a write's "did it
work" answer.

## Future: async without threads

`Future<T>`/`Promise<T>` meet `proposal §12.5`'s async requirement with no
thread, mutex, or condition variable anywhere:

- `Future<T>::ready()`/`failed()` build an **already-complete** future with no
  allocation and no shared state — a resident layer's `read()` is a plain
  return, exactly as cheap as returning `T` would have been.
- A pending future holds refcounted state a `Promise<T>` completes later.
  Nothing spawns a thread: on desktop nothing is ever actually pending; on the
  web target (Emscripten/WASM, single-threaded) a `HostIo` completes its
  promises from the same event-loop turn that receives the browser's I/O
  callback, and `Vfs::pump()`/`HostIo::pump()` is what lets a frontend drive
  that without `starvfs` owning an event loop of its own.
- `then()`'s continuation runs **inline**, on the caller's thread, the instant
  the future is (or becomes) ready. Code written against `then()` is therefore
  correct on the synchronous desktop path with no `pump()` call at all, and
  unchanged when a real async host is plugged in later.

**Cancellation is deliberately absent.** Nothing in the engine's read pattern
has a cancel story yet, and a token nobody honours is worse than none — this is
a recorded gap, not an oversight.

**A C++20 coroutine `co_await` adaptor is future work, not a redesign.**
`await_ready`/`await_suspend`/`await_resume` are a non-breaking addition to
`Future<T>` once a scheduler exists to drive them. Coroutines were considered
for this task and set aside: they are viral (every caller up to the sector
loader would become a coroutine) and need a scheduler this project does not
have yet.

`Future<void>`/`Promise<void>` are full explicit specialisations rather than
instantiations of the primary template, because `ready(T value)`/`resolve(T
value)` cannot be instantiated at `T = void` — the same reason
`std::promise<void>` gets its own specialisation in the standard library.

## The HostIo boundary

`HostIo` is the one interface between `starvfs` and the real world: raw byte
I/O — `read`, `read_range`, `write`, `remove`, `make_directory`, `stat`,
`list`, `pump()` — against **host paths**, opaque strings a `Layer` builds by
joining its own mount-time configuration with a `starvfs::Path`. `starvfs`
never interprets these strings itself, which is the entire point: a host may
read one as a `std::filesystem` path, an IndexedDB key, or a `fetch()` URL, and
every frontend gets `starvfs`'s layering, path normalisation, and zip decoding
for free by implementing only this one interface.

`NativeHostIo` is the sole Phase 0 implementation and **the only file in this
library allowed to include `<filesystem>`, `<fstream>`, or `<cstdio>`**, enforced
by `scripts/check_starvfs_host_boundary.py` — modelled directly on
`scripts/check_no_qt_in_libs.py`, the same repo's existing "assert the boundary,
don't assume it" gate.

## Layers

Three concrete `Layer`s:

- **`DirectoryLayer`** — reads and writes against a real directory, through a
  `HostIo&` rather than `std::filesystem` directly (backlog G2).
- **`ZipLayer`** — read-only; an entry index built at mount from the archive's
  central directory, backed by **miniz** — confirming, not revisiting, the
  provisional call `CONTRIBUTING.md` already records (`vcpkg.json` pins it).
  Entry names are indexed through `Path`, so "rejects path traversal in entry
  names" (backlog G3) is `Path::parse`'s guarantee, not a check `ZipLayer` has to
  remember to make.
- **`MemoryLayer`** — writable, in RAM, no host behind it at all. Not named in
  backlog G4's bullet list, but earns its place twice over: `proposal §14.1`
  describes the save layer as "in-memory + on-disk", and a mount-stack test
  (backlog G5: "layer shadowing, write-through, missing files") wants a layer
  whose behaviour isn't entangled with a real filesystem's quirks.

`exists()` is **not virtual** on `Layer` — it is defined once, as
`stat().is_ok()`. A second virtual would let some layer answer the two
questions inconsistently, which is exactly the kind of drift `Path` is designed
to make structurally impossible elsewhere in this API.

This change stubs every layer's I/O to fail with `Error::NotImplemented`
(`write`/`remove` on `ZipLayer` are the one exception — a read-only layer's
write must fail unconditionally, and that needs no index to be correct, so it
is implemented for real now). `pump()` forwards to each layer's `HostIo` where
one exists.

## The mount stack

`Vfs` is the ordered stack of `proposal §14.1`:

```
  save layer (writable, in-memory + on-disk)     <- highest
  mod layers (ordered by user's load order)
  DLC / expansion layers
  patch layers (shipped updates)
  base game layer (.spak)                        <- lowest
```

`mount()` pushes onto the top; a read walks down until a layer answers; a write
always goes to the top. This one mechanism is what makes an update a patch
layer, a mod a layer, and a save a layer (`proposal §14.1`, `§14.3`).

This change implements `mount`/`unmount`/`layer_name` for real — pure
bookkeeping, no I/O — and stubs `read`/`stat`/`exists`/`list`/`write`/`remove`/
`resolve` to fail with `NotImplemented`; their resolution logic is backlog G4's
job.

### Write policy: a deliberate correction to G4's wording

**Writes target the topmost layer unconditionally.** If it is not writable, the
write fails with `Error::ReadOnlyLayer` — it does **not** fall through to the
next writable layer underneath. This corrects backlog G4's original wording,
"writes go to the topmost writable layer": a write must never silently land in
a layer the caller didn't expect — a mod layer sitting under a read-only save
layer, say — because the destination would then depend on the whole stack's
flags rather than on one known layer. `docs/phase-0-backlog.md` has been updated
to match.

### `list` semantics: merge, not first-match

`Vfs::list()` merges every mounted layer's `list()` of one directory, the
topmost layer's entry winning a name collision, rather than stopping at the
first layer that answers. Stopping early would be wrong: a mod that adds one
room file must not hide the rest of the base game's directory.

**Whiteouts (deletion markers) are deliberately out of scope.** Nothing in
`proposal §14.1`/`§14.3` needs deletion-shadowing — saves are deltas that *add*
overrides, never subtract a base-game file — and a marker convention is a
format decision better made once a real need names it, rather than spun up
speculatively here.

### Sandbox scoping needs nothing extra

Because a `Path` cannot represent an escape from its root, a `Vfs` handle *is*
the capability `proposal §8.2`'s Lua sandbox needs — there is no separate
scoping mechanism to design. A narrower view for a script that should only see
its own `res/` (say) is a thin `SubTree` wrapper over a `Vfs` and a prefix,
which is Phase 2's problem once the Lua binding exists, not this one's.

## Two smaller calls

**`Bytes` is `std::vector<std::byte>`, not `std::string`.** `.spak` payloads
(`proposal §14.2`: `manifest.cbor`, `objects/<sector>.cbor`, `res/` assets) are
binary, and `std::string` invites `c_str()`/NUL-termination assumptions a CBOR
blob or a compressed zip entry does not satisfy. A caller that wants
`stardata::diag::SourceManager::add_file`'s `std::string` does one explicit copy
at that boundary — exactly where the conversion should be visible. Shared or
refcounted buffers are not yet worth the complexity: a `ZipLayer` decompressing
an entry produces a fresh buffer regardless, and sharing only pays off for
*resident* entries read repeatedly — a caching optimisation that fits behind
this signature unchanged, whenever it's needed.

**`starvfs` stays independent of `stardata`**, as workstream G's own preamble
requires ("Independent of `stardata` — good parallel work"). It links neither
`stardata` nor `starcore`; failures are `starvfs::Failure`, never a
`stardata::diag::Diagnostic`, and `starvfs` has no notion of a source span.
Phase 2's loader is where a `starvfs::Failure` becomes a `stardata::diag::Diagnostic`
— that is where the span lives anyway.

## Case sensitivity (backlog G2's "real cross-platform trap")

**VFS paths are case-sensitive on every platform, unconditionally.** It is the
only rule a zip layer can honour at all — entry names are bytes, and a `.spak`
may legitimately hold both `Lamp.star` and `lamp.star` — and the only one under
which a game behaves identically on Windows, macOS, and Linux.

`DirectoryLayer` therefore has to *defend* that rule against a host whose real
filesystem disagrees with it (Windows, and macOS by default): when
`HostIo::case_sensitivity()` reports `Insensitive`, a lookup must compare the
name the host actually returned against the name that was asked for,
byte-for-byte, and report `Error::NotFound` on a mismatch rather than accepting
whatever the case-insensitive filesystem handed back. That turns "works on my
Mac, 404s in CI" into a failure the author sees locally. (This is G2's job; the
stub `NativeHostIo` in this change reports `Sensitive` unconditionally, which
means the defence simply never fires yet — it does not mask a real mismatch.)

A mount-time collision scan — walking a directory tree and warning about names
that collide case-insensitively — is worth having later as a **warning
surface for Starforge, not a runtime error**: on by default when packaging, off
at runtime, so an author packaging on Linux learns about a `Lamp.star` /
`lamp.star` collision before shipping rather than after a player's
case-insensitive install silently drops one of them.

## Staging onto G1–G5

| Step | Contents |
|---|---|
| **This change (G1)** | Every header above, fully commented. `Path`, `Result`/`Failure`, `Future`/`Promise`, and `Layer::exists` implemented for real. Every layer and `Vfs`'s resolution logic stubbed to `Error::NotImplemented` (`ZipLayer::write`/`remove` implemented for real, being unconditional). `NativeHostIo` stubbed. Build wiring, the host-boundary CI gate, and smoke/unit tests for `Path` and `Future`. |
| G2 | `DirectoryLayer` and `NativeHostIo` for real, including the case-sensitivity defence above. |
| G3 | `ZipLayer` over miniz: the entry index, `read`/`stat`/`list`/`open_stream`. |
| G4 | `Vfs`'s resolution logic: `read`/`stat`/`exists`/`list`/`write`/`remove`/`resolve`. |
| G5 | The cross-platform test matrix: layer shadowing, write-through, missing files, traversal attempts, the case-collision fixture. |

## Known traps, recorded ahead of time

- **`proposal §14.4`** excludes the zip archive's own structure from a
  `.spak`'s signed payload. `ZipLayer` will eventually need to expose the byte
  ranges it actually read from the archive, which is a constraint on how its
  index is built, not an afterthought to bolt on at G3.
- **Ranged reads into a *deflated* zip entry are not seekable.** `read_range`
  on a compressed entry means decompressing from the start of that entry every
  time. Stored (uncompressed) entries are the fast case — worth Starforge
  storing large CBOR blobs uncompressed, once that trade-off matters.
- **Symlinks in a directory layer are the one traversal route `Path` does not
  close.** `Path`'s guarantee is about *names*; a symlink can point outside the
  mount root regardless of how well-formed the name that reaches it is.
  `NativeHostIo` needs to refuse to follow one out of the root — this is a G2
  concern, not something `Path` can solve.
- **Windows specifics** — `MAX_PATH`, reserved names, trailing dots/spaces, and
  UTF-8→UTF-16 conversion at the host boundary — are all `NativeHostIo`'s
  problem (G2), worth naming here before someone hits them blind.
- **`libs/stardata/src/schema/loader.cpp`'s `read_bytes`/`load_directory`** are
  where the VFS eventually displaces direct disk access in the schema loader.
  Out of scope for this change; recorded so the future seam is on record rather
  than rediscovered.

## Verification

This change was verified by:

1. `cmake -S . -B build && cmake --build build` — the whole project, including
   the pre-existing `stardata`/`starcore` targets, builds clean under both GCC
   13 and Clang 18 with `-Wall -Wextra -Wshadow -Werror` (backlog B4).
2. `ctest` — all pre-existing tests plus `starvfs_unit_tests`'s 21 cases (55
   assertions) pass, including under a Debug build with ASan+UBSan enabled
   (backlog B4's sanitiser policy).
3. `python3 scripts/check_starvfs_host_boundary.py` — passes; verified to
   actually catch a violation by temporarily adding `#include <filesystem>` to
   `vfs.cpp`.
4. `python3 scripts/check_spec_refs.py`, `scripts/check_no_qt_in_libs.py`,
   `scripts/check_layering.py`, `scripts/check_format_forms.py` — all still
   pass; `check_spec_refs.py` in particular is what confirms every `proposal
   §N` citation above resolves against `docs/proposal.md` rather than being
   silently checked against the wrong document.
5. `clang-format --dry-run -Werror` and `scripts/spdx_header.py --check` — both
   clean over every new file.
