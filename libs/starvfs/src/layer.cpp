// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "starvfs/layer.hpp"

#include <utility>

namespace starvfs {

Future<bool> Layer::exists(const Path& path) {
    Promise<bool> promise;
    Future<bool> future = promise.future();
    stat(path).then([promise = std::move(promise)](Result<Stat> result) mutable {
        promise.resolve(result.is_ok());
    });
    return future;
}

} // namespace starvfs
