// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <string_view>

namespace stardata {

// Placeholder until libs/stardata has real content (backlog workstream D
// onward: lexer, CST, parser, writer). This exists so the build skeleton
// (backlog B1) has an actual library to compile, link and test against,
// rather than an empty target that proves nothing.
[[nodiscard]] std::string_view version() noexcept;

} // namespace stardata
