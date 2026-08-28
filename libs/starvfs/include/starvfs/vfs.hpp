// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "starvfs/layer.hpp"

namespace starvfs {

// Opaque handle to a layer mounted in a Vfs. Meaningless outside the Vfs
// that issued it; default-constructed is invalid. Mirrors
// stardata::diag::SourceId.
//
// Carries its issuing Vfs's owner_ tag alongside the layer's index, not just
// the index alone. Multiple simultaneously live Vfs instances are a
// supported, expected pattern (docs/starvfs-api.md, "Multiple independent
// stacks") -- one for the runtime's own state, others handed to scripts for
// ancillary files -- so a LayerId obtained from one Vfs being passed to a
// DIFFERENT Vfs's unmount()/layer_name() is a realistic mistake, not a
// theoretical one. Without the tag that call would either silently touch
// whatever the wrong Vfs happens to have at the same index, or fail a bounds
// check for the wrong reason; with it, Vfs::mount/unmount/layer_name reject
// a mismatched owner the same way they reject an invalid id.
class LayerId {
public:
    constexpr LayerId() noexcept = default;

    [[nodiscard]] constexpr bool valid() const noexcept { return index_ != kInvalid; }

    friend constexpr bool operator==(LayerId lhs, LayerId rhs) noexcept {
        return lhs.owner_ == rhs.owner_ && lhs.index_ == rhs.index_;
    }
    friend constexpr bool operator!=(LayerId lhs, LayerId rhs) noexcept { return !(lhs == rhs); }

private:
    friend class Vfs;
    static constexpr std::uint32_t kInvalid = static_cast<std::uint32_t>(-1);

    explicit constexpr LayerId(std::uint32_t owner, std::uint32_t index) noexcept
        : owner_(owner), index_(index) {}

    std::uint32_t owner_ = kInvalid; // which Vfs issued this id
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
//
// MULTIPLE INDEPENDENT STACKS (docs/starvfs-api.md, "Multiple independent
// stacks"). A Vfs is a plain, freely-instantiable object: no singleton, no
// static registry, no assumption anywhere in this class that only one
// exists. Constructing more than one is the intended way to run several
// mount stacks side by side -- one for the runtime's own game state
// (proposal §14.1's base/patch/mod/save stack), plus zero or more separate
// Vfs instances a frontend hands to a running game's scripts for ancillary
// files unrelated to that state (proposal §8.2's sandbox already scopes a
// script to *a* VFS handle; which Vfs it gets is just "whichever object the
// frontend constructs for it").
//
// A layer belongs to exactly one Vfs -- mount() takes std::unique_ptr<Layer>,
// not a shared_ptr, and there is deliberately no way to mount the same Layer
// object into two stacks. If two stacks need the same underlying files, the
// frontend constructs two Layer objects pointed at the same HostIo/directory;
// a layer is a thin wrapper, so this costs little, and it avoids one layer's
// mutable state (a ZipLayer's index, a MemoryLayer's contents) being shared,
// and so implicitly synchronised, across stacks that otherwise have nothing
// to do with each other.
//
// Nothing here pumps more than one Vfs: each instance's pump() drains only
// its own layers, and a frontend running several stacks calls pump() on
// each. There is no central registry that does this centrally -- that would
// be the one piece of shared state this design otherwise avoids entirely,
// for a convenience the frontend's own event loop already provides for free.
class Vfs {
public:
    // `name` is this stack's own diagnostic label -- "game", "mod-resources"
    // -- distinct from any individual Layer::name(): once more than one Vfs
    // is alive at once, a failure or log line naming only the layer doesn't
    // say which stack it came from. Optional and empty by default; nothing
    // here reads it except name() itself.
    explicit Vfs(std::string name = {});

    [[nodiscard]] std::string_view name() const noexcept;

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

    // Empty for an id this Vfs never issued, one issued by a different Vfs
    // (see LayerId's comment), or one that has since been unmount()ed --
    // "no longer mounted" is deliberately indistinguishable from "never
    // was", the same way a second unmount() call on the same id fails
    // rather than reporting the id as already gone but once valid.
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

    std::string name_;
    std::uint32_t owner_id_; // stamped into every LayerId this Vfs issues; see LayerId's comment
    std::vector<MountedLayer> layers_;
};

} // namespace starvfs
