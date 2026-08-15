// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <iosfwd>
#include <string>

#include "stardata/cst/green.hpp"
#include "stardata/cst/syntax.hpp"

namespace stardata::cst {

// Serialises a tree back to text (backlog E5).
//
// For a tree straight from the parser this reproduces the input byte for
// byte -- byte-order mark, line-ending style, trailing whitespace and all --
// because the parser puts every byte of the input into some leaf and this
// walks the leaves in order. That is §14.2's requirement, and E6 asserts it
// over the whole corpus.
//
// It is deliberately not a formatter. Nothing here decides how anything
// should look; the tree already knows, and an edit that wants different
// formatting supplies different leaves.
void write(const GreenNode& node, std::string& out);
void write(const GreenNode& node, std::ostream& out);

[[nodiscard]] std::string to_text(const GreenNode& node);
[[nodiscard]] std::string to_text(const SyntaxNode& node);

} // namespace stardata::cst
