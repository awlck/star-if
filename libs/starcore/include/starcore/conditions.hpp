// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace starcore {

// The combinators of spec §10.3, named once.
//
// They were file-local constants in narrowing.cpp until F8 needed the same
// four words. Two passes each holding their own copy of "COUNT_AT_LEAST" is
// how the two eventually come to disagree about it -- and the vocabulary is
// the thing this library exists to own, so it should be stated here rather
// than twice in its implementation.
namespace combinator {

inline constexpr std::string_view kOr = "OR";
inline constexpr std::string_view kNot = "NOT";
inline constexpr std::string_view kAnd = "AND";
inline constexpr std::string_view kCountAtLeast = "COUNT_AT_LEAST";

} // namespace combinator

// §10.5.1's "fails as a whole" set: `NOT`, `OR` and `COUNT_AT_LEAST`.
//
// No individual child's failure is the reason such a block failed, so no
// child can explain it -- which is what makes a `failureMsg` below one
// unreachable (§10.5.1) and a narrowing established inside one unusable
// outside it (§8.8.3). One set, two consequences.
[[nodiscard]] bool is_barrier(std::string_view key) noexcept;

// The same set as a list, for a diagnostic that has to name them.
[[nodiscard]] const std::vector<std::string>& barrier_names();

} // namespace starcore
