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

// A one-line-per-kind census of a tree: how many nodes and leaves of each
// kind it holds, and how many bytes it covers.
//
// This is the golden for a file whose full tree dump would be too large to
// read -- tour.star's runs to tens of thousands of lines. It still moves when
// the parser's shape changes or the corpus grows, which is the point, and it
// blesses through the same --update-snapshots path as every other snapshot
// rather than being a number somebody has to edit by hand.
[[nodiscard]] std::string summarise_tree(const cst::SyntaxNode& root);

} // namespace stardata::test
