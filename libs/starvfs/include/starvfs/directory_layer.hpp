// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <string>

#include "starvfs/host.hpp"
#include "starvfs/layer.hpp"

namespace starvfs {

// backlog G2: reads and writes against a real directory, through a HostIo
// rather than std::filesystem directly (host.hpp).
//
// Stubbed for this change (backlog G1); every method fails with
// Error::NotImplemented. G2 fills these in for real, including the
// case-sensitivity defence docs/starvfs-api.md describes: on a host whose
// HostIo::case_sensitivity() reports Insensitive, a lookup must compare
// the name the host actually returned against the name asked for, so a
// mismatch that a case-insensitive filesystem would silently accept is
// reported as Error::NotFound instead.
class DirectoryLayer final : public Layer {
public:
    // `host` is not owned -- the frontend owns and outlives every HostIo it
    // hands to a Vfs (the same boundary host.hpp documents). `host_prefix`
    // is joined with each Path to build the host path this layer asks
    // `host` for. `layer_name` is this layer's diagnostic name
    // (Layer::name()).
    DirectoryLayer(HostIo& host, std::string host_prefix, std::string layer_name, bool writable);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] bool writable() const noexcept override;

    Future<Bytes> read(const Path& path) override;
    Future<Stat> stat(const Path& path) override;
    Future<std::vector<Entry>> list(const Path& directory) override;
    Future<void> write(const Path& path, Bytes contents) override;
    Future<void> remove(const Path& path) override;
    std::unique_ptr<Stream> open_stream(const Path& path) override;
    void pump() override;

private:
    // host_prefix_ joined with a VFS Path, e.g. "saves/slot1" + "objects/hold.cbor"
    // -> "saves/slot1/objects/hold.cbor". G2's real read/write/stat/list
    // call this to build the string they hand to host_; this stage's
    // stubs use it only to name the file a failure was about.
    [[nodiscard]] std::string host_path(const Path& path) const;

    HostIo& host_;
    std::string host_prefix_;
    std::string name_;
    bool writable_;
};

} // namespace starvfs
