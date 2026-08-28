// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// docs/starvfs-api.md, "Multiple independent stacks": a Vfs is freely
// instantiable more than once, and a LayerId from one Vfs must not be
// mistaken for one from another. Vfs's I/O methods (read/stat/list/...) are
// still stubs (backlog G4), so this covers only what this change implements
// for real: mount/unmount/name/layer_name bookkeeping.
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <string_view>

#include "starvfs/memory_layer.hpp"
#include "starvfs/vfs.hpp"

using starvfs::LayerId;
using starvfs::MemoryLayer;
using starvfs::Vfs;

TEST_CASE("a default-constructed Vfs has an empty name", "[vfs]") {
    Vfs vfs;
    CHECK(vfs.name().empty());
}

TEST_CASE("a Vfs reports the name it was constructed with", "[vfs]") {
    Vfs vfs{"game"};
    CHECK(vfs.name() == "game");
}

TEST_CASE("mount returns a valid LayerId and layer_name reports it", "[vfs]") {
    Vfs vfs;
    const LayerId id = vfs.mount(std::make_unique<MemoryLayer>("base"));
    CHECK(id.valid());
    CHECK(vfs.layer_name(id) == "base");
}

TEST_CASE("unmount succeeds once and fails on a second call", "[vfs]") {
    Vfs vfs;
    const LayerId id = vfs.mount(std::make_unique<MemoryLayer>("base"));
    CHECK(vfs.unmount(id));
    CHECK_FALSE(vfs.unmount(id));
    CHECK(vfs.layer_name(id).empty()); // no longer active
}

TEST_CASE("an invalid, default-constructed LayerId is rejected", "[vfs]") {
    Vfs vfs;
    vfs.mount(std::make_unique<MemoryLayer>("base"));
    CHECK_FALSE(vfs.unmount(LayerId{}));
    CHECK(vfs.layer_name(LayerId{}).empty());
}

TEST_CASE("two independent Vfs instances issue independently valid LayerIds", "[vfs]") {
    Vfs game_vfs{"game"};
    Vfs mod_resources_vfs{"mod-resources"};

    const LayerId game_id = game_vfs.mount(std::make_unique<MemoryLayer>("base"));
    const LayerId mod_id = mod_resources_vfs.mount(std::make_unique<MemoryLayer>("base"));

    CHECK(game_vfs.layer_name(game_id) == "base");
    CHECK(mod_resources_vfs.layer_name(mod_id) == "base");
}

TEST_CASE("a LayerId from one Vfs is rejected by a different Vfs", "[vfs]") {
    // The scenario docs/starvfs-api.md calls out: both stacks mount a layer
    // at the same numeric slot (index 0), so a guard that checked only the
    // index -- not which Vfs issued it -- would let this through silently.
    Vfs vfs_a{"a"};
    Vfs vfs_b{"b"};

    const LayerId id_from_a = vfs_a.mount(std::make_unique<MemoryLayer>("layer-a"));
    vfs_b.mount(std::make_unique<MemoryLayer>("layer-b"));

    CHECK_FALSE(vfs_b.unmount(id_from_a));
    CHECK(vfs_b.layer_name(id_from_a).empty());

    // The id is still good against the Vfs that actually issued it.
    CHECK(vfs_a.layer_name(id_from_a) == "layer-a");
    CHECK(vfs_a.unmount(id_from_a));
}

TEST_CASE("pump does not throw with no layers mounted", "[vfs]") {
    Vfs vfs;
    vfs.pump();
    SUCCEED();
}
