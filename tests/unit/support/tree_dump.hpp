// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <string>

#include "stardata/cst/syntax.hpp"

namespace stardata::test {

// Renders a tree as indented text, one line per node and leaf:
//
//   File @0..12
//     Statement @0..12
//       Key @0..5
//         identifier @0..5 "title"
//
// Nodes show their absolute range, leaves their text with control characters
// escaped. This is what the golden tests compare, so a change in tree shape
// or in trivia attachment shows up as a file diff rather than as a silently
// different tree.
[[nodiscard]] std::string dump_tree(const cst::SyntaxNode& root);

} // namespace stardata::test
