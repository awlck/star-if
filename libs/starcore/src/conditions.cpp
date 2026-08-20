// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "starcore/conditions.hpp"

namespace starcore {

bool is_barrier(std::string_view key) noexcept {
    return key == combinator::kOr || key == combinator::kNot || key == combinator::kCountAtLeast;
}

const std::vector<std::string>& barrier_names() {
    static const std::vector<std::string> names = {
        std::string(combinator::kNot),
        std::string(combinator::kOr),
        std::string(combinator::kCountAtLeast),
    };
    return names;
}

} // namespace starcore
