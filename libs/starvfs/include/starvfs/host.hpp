// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "starvfs/bytes.hpp"
#include "starvfs/future.hpp"

namespace starvfs {

// Whether the host's underlying storage compares names case-sensitively.
// The VFS's own rule is that paths are always case-sensitive
// (docs/starvfs-api.md, "Case sensitivity") -- the only rule a zip layer
// can honour and the only one under which a game behaves identically on
// all three desktop platforms. DirectoryLayer uses this to tell whether it
// has to defend that rule itself against a host that disagrees with it
// (Windows, and macOS by default).
enum class CaseSensitivity { Sensitive, Insensitive };

struct HostStat {
    std::uint64_t size = 0;
    bool is_directory = false;
};

struct HostEntry {
    std::string name; // one path segment, not a full host path
    bool is_directory = false;
};

// The one interface between starvfs and the real world: raw byte I/O
// against HOST paths, which are opaque strings to starvfs -- whatever a
// Layer built by joining its own mount-time configuration with a
// starvfs::Path. starvfs never interprets these strings itself, which is
// the entire point of this boundary (backlog G1; proposal §12.5's "the VFS
// API must be async-capable from day one" is the same requirement this
// interface's Future-returning methods satisfy): a host may read a path as
// a std::filesystem path, an IndexedDB key, or a fetch() URL, and every
// frontend gets starvfs's layering, path normalisation and zip decoding
// for free by implementing only this.
//
// NativeHostIo (native_host.hpp) is the one Phase 0 implementation, and the
// only file in this library allowed to touch <filesystem>, <fstream> or
// <cstdio> -- enforced by scripts/check_starvfs_host_boundary.py, the same
// way scripts/check_no_qt_in_libs.py enforces libs/'s Qt boundary.
class HostIo {
public:
    virtual ~HostIo() = default;

    virtual Future<Bytes> read(std::string_view host_path) = 0;
    // Reads [offset, offset + length) of the file at `host_path`. Note for
    // an archive layer built on this: the range is meaningful for a STORED
    // zip entry but not a DEFLATEd one, which is not seekable -- see
    // docs/starvfs-api.md, "Known traps".
    virtual Future<Bytes> read_range(std::string_view host_path, std::uint64_t offset,
                                     std::uint64_t length) = 0;
    virtual Future<void> write(std::string_view host_path, Bytes contents) = 0;
    virtual Future<void> remove(std::string_view host_path) = 0;
    virtual Future<void> make_directory(std::string_view host_path) = 0;
    virtual Future<HostStat> stat(std::string_view host_path) = 0;
    // Entries directly inside the directory at `host_path`, non-recursive.
    virtual Future<std::vector<HostEntry>> list(std::string_view host_path) = 0;

    // Drains completions queued by an asynchronous host (the browser
    // target of proposal §12.5). A synchronous host -- NativeHostIo
    // included -- has nothing to drain and leaves this as a no-op.
    virtual void pump() {}

    [[nodiscard]] virtual CaseSensitivity case_sensitivity() const noexcept = 0;
};

} // namespace starvfs
