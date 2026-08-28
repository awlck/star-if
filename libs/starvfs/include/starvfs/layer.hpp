// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "starvfs/bytes.hpp"
#include "starvfs/future.hpp"
#include "starvfs/path.hpp"

namespace starvfs {

struct Stat {
    std::uint64_t size = 0;
    bool is_directory = false;
};

struct Entry {
    std::string name;
    bool is_directory = false;
};

// A byte-range view onto one file, for a caller that wants to read
// incrementally rather than materialise the whole thing -- backlog G1's
// `open_stream`, meant for large res/ assets and long .spak reads.
//
// Synchronous by design even though Layer::read is async: a Stream is only
// handed out once a layer has already resolved the file (mount, index
// lookup, and for ZipLayer the entry's offset in the archive are all
// settled by the time open_stream returns one), so what remains is bytes a
// resident buffer or an already-open host handle can serve immediately. An
// async stream is future work if a layer ever needs one, not a Phase 0
// requirement.
class Stream {
public:
    virtual ~Stream() = default;

    // Reads up to `length` bytes into `buffer`, returns the number
    // actually read (0 at end of stream).
    virtual std::size_t read(std::byte* buffer, std::size_t length) = 0;
    virtual void seek(std::uint64_t offset) = 0;
    [[nodiscard]] virtual std::uint64_t size() const = 0;
};

// One filesystem-shaped source of files: a directory, a zip archive, or an
// in-memory store (directory_layer.hpp, zip_layer.hpp, memory_layer.hpp). A
// Vfs is a stack of these, resolved highest-priority-first (proposal
// §14.1).
class Layer {
public:
    virtual ~Layer() = default;

    // Named for diagnostics -- "mods/lantern-fix", "game.spak" -- so a
    // failure, or a Vfs::resolve() query (backlog G4: "query which layer
    // supplied a path"), can say which layer, not just which path.
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual bool writable() const noexcept = 0;

    virtual Future<Bytes> read(const Path& path) = 0;
    virtual Future<Stat> stat(const Path& path) = 0;
    // Entries directly inside `directory`, non-recursive; Vfs::list merges
    // this across every mounted layer.
    virtual Future<std::vector<Entry>> list(const Path& directory) = 0;

    // A read-only layer's write/remove must fail with Error::ReadOnlyLayer
    // rather than assert or silently no-op: Vfs::write relies on that to
    // report the right error when a caller writes through a layer directly
    // (tests, tooling) rather than through the mount stack's own
    // topmost-layer check (docs/starvfs-api.md, "Write policy").
    virtual Future<void> write(const Path& path, Bytes contents) = 0;
    virtual Future<void> remove(const Path& path) = 0;

    virtual std::unique_ptr<Stream> open_stream(const Path& path) = 0;

    // Forwards to this layer's host, if it has one -- the default no-op
    // suits MemoryLayer, which has none. Vfs::pump() calls this for every
    // mounted layer to drain an asynchronous host's queued completions
    // (proposal §12.5's browser target).
    virtual void pump() {}

    // Not virtual: exists() is defined once, here, as stat().is_ok(). A
    // second virtual would let a layer answer the two questions
    // inconsistently, which is exactly the kind of drift Path::parse (see
    // path.hpp) is designed to make impossible for traversal -- the same
    // reasoning applies to any invariant this interface can hold by
    // construction instead of by every implementation remembering it.
    Future<bool> exists(const Path& path);
};

} // namespace starvfs
