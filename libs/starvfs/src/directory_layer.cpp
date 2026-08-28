// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "starvfs/directory_layer.hpp"

#include <utility>

namespace starvfs {

DirectoryLayer::DirectoryLayer(HostIo& host, std::string host_prefix, std::string layer_name,
                               bool writable)
    : host_(host), host_prefix_(std::move(host_prefix)), name_(std::move(layer_name)),
      writable_(writable) {}

std::string_view DirectoryLayer::name() const noexcept {
    return name_;
}

bool DirectoryLayer::writable() const noexcept {
    return writable_;
}

std::string DirectoryLayer::host_path(const Path& path) const {
    if (host_prefix_.empty()) {
        return std::string(path.str());
    }
    std::string joined = host_prefix_;
    if (!path.empty()) {
        joined.push_back('/');
        joined.append(path.str());
    }
    return joined;
}

namespace {

[[nodiscard]] Failure not_implemented(std::string_view what, const std::string& host_path) {
    return make_failure(Error::NotImplemented, "DirectoryLayer::" + std::string(what) + ": " +
                                                   host_path + " (backlog G2)");
}

} // namespace

Future<Bytes> DirectoryLayer::read(const Path& path) {
    return Future<Bytes>::failed(not_implemented("read", host_path(path)));
}

Future<Stat> DirectoryLayer::stat(const Path& path) {
    return Future<Stat>::failed(not_implemented("stat", host_path(path)));
}

Future<std::vector<Entry>> DirectoryLayer::list(const Path& directory) {
    return Future<std::vector<Entry>>::failed(not_implemented("list", host_path(directory)));
}

Future<void> DirectoryLayer::write(const Path& path, Bytes /*contents*/) {
    if (!writable_) {
        return Future<void>::failed(make_failure(Error::ReadOnlyLayer, name_));
    }
    return Future<void>::failed(not_implemented("write", host_path(path)));
}

Future<void> DirectoryLayer::remove(const Path& path) {
    if (!writable_) {
        return Future<void>::failed(make_failure(Error::ReadOnlyLayer, name_));
    }
    return Future<void>::failed(not_implemented("remove", host_path(path)));
}

std::unique_ptr<Stream> DirectoryLayer::open_stream(const Path& /*path*/) {
    return nullptr; // G2: a stream backed by an open host handle.
}

void DirectoryLayer::pump() {
    host_.pump();
}

} // namespace starvfs
