// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "starvfs/native_host.hpp"

namespace starvfs {

namespace {

[[nodiscard]] Failure not_implemented(std::string_view what) {
    return make_failure(Error::NotImplemented,
                        "NativeHostIo::" + std::string(what) + " (backlog G2)");
}

} // namespace

NativeHostIo::NativeHostIo(std::string root_directory)
    : root_directory_(std::move(root_directory)) {}

Future<Bytes> NativeHostIo::read(std::string_view /*host_path*/) {
    return Future<Bytes>::failed(not_implemented("read"));
}

Future<Bytes> NativeHostIo::read_range(std::string_view /*host_path*/, std::uint64_t /*offset*/,
                                       std::uint64_t /*length*/) {
    return Future<Bytes>::failed(not_implemented("read_range"));
}

Future<void> NativeHostIo::write(std::string_view /*host_path*/, Bytes /*contents*/) {
    return Future<void>::failed(not_implemented("write"));
}

Future<void> NativeHostIo::remove(std::string_view /*host_path*/) {
    return Future<void>::failed(not_implemented("remove"));
}

Future<void> NativeHostIo::make_directory(std::string_view /*host_path*/) {
    return Future<void>::failed(not_implemented("make_directory"));
}

Future<HostStat> NativeHostIo::stat(std::string_view /*host_path*/) {
    return Future<HostStat>::failed(not_implemented("stat"));
}

Future<std::vector<HostEntry>> NativeHostIo::list(std::string_view /*host_path*/) {
    return Future<std::vector<HostEntry>>::failed(not_implemented("list"));
}

CaseSensitivity NativeHostIo::case_sensitivity() const noexcept {
    // Conservative placeholder: reporting Sensitive means DirectoryLayer's
    // defence against an insensitive host (docs/starvfs-api.md) never
    // fires, so this stub never masks a real mismatch -- it just doesn't
    // yet detect the host's actual behaviour. G2 replaces this with a
    // runtime probe.
    return CaseSensitivity::Sensitive;
}

} // namespace starvfs
