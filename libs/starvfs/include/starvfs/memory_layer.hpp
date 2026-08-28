// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <string>

#include "starvfs/layer.hpp"

namespace starvfs {

// A writable, in-RAM layer with no host behind it at all. Not named in
// backlog G4's bullet list, but earns its place in Phase 0 for two
// reasons recorded in docs/starvfs-api.md: proposal §14.1 describes the
// save layer as "in-memory + on-disk", and a mount-stack test (backlog G5:
// "layer shadowing, write-through, missing files") wants a layer whose
// behaviour isn't entangled with a real filesystem's quirks.
//
// Stubbed for this change (backlog G1) like the other two layers, even
// though nothing here actually needs a host to implement for real -- kept
// consistent with DirectoryLayer and ZipLayer so all three layers land
// together at G4 rather than this one arriving early and the mount stack
// having to special-case it.
class MemoryLayer final : public Layer {
public:
    explicit MemoryLayer(std::string layer_name);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] bool writable() const noexcept override;

    Future<Bytes> read(const Path& path) override;
    Future<Stat> stat(const Path& path) override;
    Future<std::vector<Entry>> list(const Path& directory) override;
    Future<void> write(const Path& path, Bytes contents) override;
    Future<void> remove(const Path& path) override;
    std::unique_ptr<Stream> open_stream(const Path& path) override;

private:
    std::string name_;
};

} // namespace starvfs
