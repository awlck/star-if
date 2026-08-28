// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "starvfs/vfs.hpp"

#include <atomic>
#include <utility>

namespace starvfs {

namespace {

// One tag per Vfs instance, stamped into every LayerId it issues (see
// LayerId's comment in vfs.hpp for why). Atomic because nothing rules out
// constructing a Vfs from more than one thread; this is one fetch-add per
// Vfs construction, not a hot path, so a relaxed increment costs nothing
// worth measuring.
std::atomic<std::uint32_t> g_next_owner_id{0};

} // namespace

Vfs::Vfs(std::string name)
    : name_(std::move(name)), owner_id_(g_next_owner_id.fetch_add(1, std::memory_order_relaxed)) {}

std::string_view Vfs::name() const noexcept {
    return name_;
}

LayerId Vfs::mount(std::unique_ptr<Layer> layer) {
    const auto index = static_cast<std::uint32_t>(layers_.size());
    layers_.push_back(MountedLayer{std::move(layer), /*active=*/true});
    return LayerId(owner_id_, index);
}

bool Vfs::unmount(LayerId id) {
    if (!id.valid() || id.owner_ != owner_id_ || id.index_ >= layers_.size() ||
        !layers_[id.index_].active) {
        return false;
    }
    layers_[id.index_].active = false;
    return true;
}

std::string_view Vfs::layer_name(LayerId id) const {
    if (!id.valid() || id.owner_ != owner_id_ || id.index_ >= layers_.size() ||
        !layers_[id.index_].active) {
        return {};
    }
    return layers_[id.index_].layer->name();
}

void Vfs::pump() {
    for (const MountedLayer& mounted : layers_) {
        if (mounted.active) {
            mounted.layer->pump();
        }
    }
}

namespace {

// backlog G4's resolution/write-policy/list-merge logic. Fully specified
// (vfs.hpp, docs/starvfs-api.md) but not yet implemented -- this stub
// stage covers the API's shape (backlog G1), not its behaviour.
[[nodiscard]] Failure not_implemented(std::string_view what) {
    return make_failure(Error::NotImplemented, "Vfs::" + std::string(what) + " (backlog G4)");
}

} // namespace

Future<Bytes> Vfs::read(const Path& /*path*/) {
    return Future<Bytes>::failed(not_implemented("read"));
}

Future<Stat> Vfs::stat(const Path& /*path*/) {
    return Future<Stat>::failed(not_implemented("stat"));
}

Future<bool> Vfs::exists(const Path& /*path*/) {
    return Future<bool>::failed(not_implemented("exists"));
}

Future<std::vector<Entry>> Vfs::list(const Path& /*directory*/) {
    return Future<std::vector<Entry>>::failed(not_implemented("list"));
}

Future<void> Vfs::write(const Path& /*path*/, Bytes /*contents*/) {
    return Future<void>::failed(not_implemented("write"));
}

Future<void> Vfs::remove(const Path& /*path*/) {
    return Future<void>::failed(not_implemented("remove"));
}

Future<std::optional<LayerId>> Vfs::resolve(const Path& /*path*/) {
    return Future<std::optional<LayerId>>::failed(not_implemented("resolve"));
}

} // namespace starvfs
