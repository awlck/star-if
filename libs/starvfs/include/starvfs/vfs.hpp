// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "starvfs/layer.hpp"

namespace starvfs {

// Opaque handle to a layer mounted in a Vfs. Meaningless outside the Vfs
// that issued it; default-constructed is invalid. Mirrors
// stardata::diag::SourceId.
class LayerId {
public:
    constexpr LayerId() noexcept = default;

    [[nodiscard]] constexpr bool valid() const noexcept { return index_ != kInvalid; }

    friend constexpr bool operator==(LayerId lhs, LayerId rhs) noexcept {
        return lhs.index_ == rhs.index_;
    }
    friend constexpr bool operator!=(LayerId lhs, LayerId rhs) noexcept { return !(lhs == rhs); }

private:
    friend class Vfs;
    static constexpr std::uint32_t kInvalid = static_cast<std::uint32_t>(-1);

    explicit constexpr LayerId(std::uint32_t index) noexcept : index_(index) {}

    std::uint32_t index_ = kInvalid;
};

// The mount stack (proposal §14.1): an ordered set of Layers, resolved
// highest-priority-first --
//
//   save layer (writable, in-memory + on-disk)     <- highest
//   mod layers (ordered by user's load order)
//   DLC / expansion layers
//   patch layers (shipped updates)
//   base game layer (.spak)                        <- lowest
//
// A read walks down from the top until a layer answers; a write always
// goes to the top. This one mechanism is what makes an update a patch
// layer, a mod a layer, and a save a layer (proposal §14.1, §14.3).
//
// Bookkeeping (mount/unmount/layer_name) is implemented for real in this
// change; path resolution (read/stat/exists/list/write/remove/resolve) is
// stubbed, fully specified but not yet implemented -- backlog G4's job.
class Vfs {
public:
    // Pushes onto the TOP of the stack: the most recently mounted layer
    // wins both a read and a write. Callers assemble the stack bottom-up
    // (base game, then patches, then mods, then the save layer last) to
    // match the ordering above. Takes ownership of `layer`.
    LayerId mount(std::unique_ptr<Layer> layer);

    // False if `id` was never mounted or was already unmounted.
    bool unmount(LayerId id);

    Future<Bytes> read(const Path& path);
    Future<Stat> stat(const Path& path);
    Future<bool> exists(const Path& path);

    // Merges every mounted layer's list() of `directory`, the top layer's
    // entry winning a name collision, rather than stopping at the first
    // layer that answers -- a mod adding one room file must not hide the
    // rest of the base game's directory. See docs/starvfs-api.md, "list
    // semantics", for why this merges and why there is deliberately no
    // whiteout/deletion-marker support yet.
    Future<std::vector<Entry>> list(const Path& directory);

    // Always the topmost layer. Fails with Error::ReadOnlyLayer if it is
    // not writable, rather than falling through to the next writable layer
    // underneath it -- a write must never silently land somewhere the
    // caller didn't expect. This corrects backlog G4's original "writes go
    // to the topmost writable layer" wording; see docs/starvfs-api.md,
    // "Write policy", for the reasoning.
    Future<void> write(const Path& path, Bytes contents);
    Future<void> remove(const Path& path);

    // backlog G4: "query which layer supplied a path" -- the debugger and
    // mod diagnostics want to say "lamp.star came from mods/lantern-fix",
    // not just "lamp.star exists". nullopt if no mounted layer has it.
    Future<std::optional<LayerId>> resolve(const Path& path);
    [[nodiscard]] std::string_view layer_name(LayerId id) const;

    // Forwards to every mounted layer's HostIo::pump(), draining any
    // asynchronous host's queued completions (proposal §12.5's browser
    // target). A no-op on the synchronous desktop path.
    void pump();

private:
    struct MountedLayer {
        std::unique_ptr<Layer> layer;
        bool active = true; // false after unmount(); the slot stays so LayerIds stay stable
    };

    std::vector<MountedLayer> layers_;
};

} // namespace starvfs
