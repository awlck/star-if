// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "starvfs/zip_layer.hpp"

#include <utility>

namespace starvfs {

namespace {

[[nodiscard]] Failure not_implemented(std::string_view what) {
    return make_failure(Error::NotImplemented, "ZipLayer::" + std::string(what) + " (backlog G3)");
}

[[nodiscard]] Failure read_only(std::string_view layer_name) {
    // Not a stub: a ZipLayer is read-only unconditionally, and every write
    // path must say so from day one, independent of whether the entry
    // index that would answer a read exists yet (backlog G3).
    return make_failure(Error::ReadOnlyLayer, std::string(layer_name));
}

} // namespace

ZipLayer::ZipLayer(HostIo& host, std::string archive_path, std::string layer_name)
    : host_(host), archive_path_(std::move(archive_path)), name_(std::move(layer_name)) {}

std::string_view ZipLayer::name() const noexcept {
    return name_;
}

bool ZipLayer::writable() const noexcept {
    return false;
}

Future<Bytes> ZipLayer::read(const Path& /*path*/) {
    return Future<Bytes>::failed(not_implemented("read"));
}

Future<Stat> ZipLayer::stat(const Path& /*path*/) {
    return Future<Stat>::failed(not_implemented("stat"));
}

Future<std::vector<Entry>> ZipLayer::list(const Path& /*directory*/) {
    return Future<std::vector<Entry>>::failed(not_implemented("list"));
}

Future<void> ZipLayer::write(const Path& /*path*/, Bytes /*contents*/) {
    return Future<void>::failed(read_only(name_));
}

Future<void> ZipLayer::remove(const Path& /*path*/) {
    return Future<void>::failed(read_only(name_));
}

std::unique_ptr<Stream> ZipLayer::open_stream(const Path& /*path*/) {
    return nullptr; // G3: a stream over a STORED (uncompressed) entry.
}

void ZipLayer::pump() {
    host_.pump();
}

} // namespace starvfs
