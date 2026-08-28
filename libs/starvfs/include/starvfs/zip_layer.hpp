// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <string>

#include "starvfs/host.hpp"
#include "starvfs/layer.hpp"

namespace starvfs {

// backlog G3: a read-only layer over a zip archive (a `.spak`, proposal
// §14.2, or a plain `.zip` mod package) -- read through `host` rather than
// opened directly, so even the archive layer stays on the HostIo side of
// the boundary host.hpp documents. Entry names are indexed through
// starvfs::Path, which is what makes "rejects path traversal in entry
// names" (backlog G3) a consequence of Path::parse rather than a check
// ZipLayer has to remember to make itself.
//
// Backed by miniz (CONTRIBUTING.md's provisional call, backlog G3: "decide
// libzip vs miniz and record why"), confirmed rather than revisited by this
// change -- see docs/starvfs-api.md.
//
// Stubbed for this change (backlog G1); read/stat/list/open_stream fail
// with Error::NotImplemented until G3 builds the entry index. write/remove
// are implemented for real here, because "always fails, unconditionally"
// needs no index to be correct.
class ZipLayer final : public Layer {
public:
    // `archive_path` is a host path to the archive itself, read and
    // indexed through `host` at mount time (once G3 builds that index --
    // this stub does not open the archive at all). `layer_name` is this
    // layer's diagnostic name (Layer::name()).
    ZipLayer(HostIo& host, std::string archive_path, std::string layer_name);

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
    // archive_path_ is unused until G3 builds the entry index; declared
    // now because it is part of this layer's design (docs/starvfs-api.md),
    // not an afterthought bolted on once the implementation lands.
    HostIo& host_;
    [[maybe_unused]] std::string archive_path_;
    std::string name_;
};

} // namespace starvfs
