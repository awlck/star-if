// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "starvfs/memory_layer.hpp"

#include <utility>

namespace starvfs {

namespace {

[[nodiscard]] Failure not_implemented(std::string_view what) {
    return make_failure(Error::NotImplemented,
                        "MemoryLayer::" + std::string(what) + " (backlog G4)");
}

} // namespace

MemoryLayer::MemoryLayer(std::string layer_name) : name_(std::move(layer_name)) {}

std::string_view MemoryLayer::name() const noexcept {
    return name_;
}

bool MemoryLayer::writable() const noexcept {
    return true;
}

Future<Bytes> MemoryLayer::read(const Path& /*path*/) {
    return Future<Bytes>::failed(not_implemented("read"));
}

Future<Stat> MemoryLayer::stat(const Path& /*path*/) {
    return Future<Stat>::failed(not_implemented("stat"));
}

Future<std::vector<Entry>> MemoryLayer::list(const Path& /*directory*/) {
    return Future<std::vector<Entry>>::failed(not_implemented("list"));
}

Future<void> MemoryLayer::write(const Path& /*path*/, Bytes /*contents*/) {
    return Future<void>::failed(not_implemented("write"));
}

Future<void> MemoryLayer::remove(const Path& /*path*/) {
    return Future<void>::failed(not_implemented("remove"));
}

std::unique_ptr<Stream> MemoryLayer::open_stream(const Path& /*path*/) {
    return nullptr; // G4: a stream over the stored buffer.
}

} // namespace starvfs
