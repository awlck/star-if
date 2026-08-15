// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

#include "stardata/cst/green.hpp"
#include "stardata/cst/syntax.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/diag/source_manager.hpp"

namespace stardata::cst {

// Editing a tree (backlog E7).
//
// Green nodes are immutable, so an edit does not change a tree -- it builds
// a new one. Only the spine from the edited node up to the root is rebuilt;
// every other subtree is carried over by pointer, so an edit deep in a large
// file allocates a handful of nodes rather than copying the file.
//
// That sharing is what makes §14.2's second paragraph hold: "an edit to one
// node MUST re-print only the affected span. Formatting and comments outside
// that span MUST be preserved exactly." Text outside the edit is not
// reprinted so much as never touched -- it is the same objects.
//
// Every function returns the new ROOT, because an edit anywhere changes the
// root. The original tree remains valid and unchanged; callers holding it
// see nothing.

// Replaces one node with another. `target` must belong to a tree reachable
// from its root -- that is, it must have come from navigation, not from
// `SyntaxNode::root` on a detached subtree.
[[nodiscard]] GreenNodePtr replace_node(const SyntaxNode& target, GreenNodePtr replacement,
                                        GreenCache& cache);

[[nodiscard]] GreenNodePtr replace_token(const SyntaxToken& target, GreenTokenPtr replacement,
                                         GreenCache& cache);

// Structural edits on a node's child list. `index` is a position among
// `parent.children()`, counting nodes and tokens alike.
[[nodiscard]] GreenNodePtr insert_child(const SyntaxNode& parent, std::size_t index,
                                        GreenElement child, GreenCache& cache);

[[nodiscard]] GreenNodePtr remove_child(const SyntaxNode& parent, std::size_t index,
                                        GreenCache& cache);

// Removes a statement from the block or file that holds it.
//
// The statement's own leading and trailing trivia go with it, which is the
// payoff of E3's attachment policy: a statement carries its own comment and
// its own line ending, so deleting it leaves no orphaned comment and no
// blank line where it used to be, and every other byte is untouched.
[[nodiscard]] GreenNodePtr remove_statement(const SyntaxNode& statement, GreenCache& cache);

// Inserts `statement` immediately after `anchor`, which must itself be a
// Statement. The new statement supplies its own indentation and line ending,
// exactly as `statement_from_text` produces them.
[[nodiscard]] GreenNodePtr insert_statement_after(const SyntaxNode& anchor, GreenNodePtr statement,
                                                  GreenCache& cache);

// Builds a Statement node by parsing `text`, which must be one complete
// statement and should carry whatever indentation and line ending it is
// meant to have in the file. Returns nothing if `text` does not parse as a
// single statement.
//
// This is the general way to construct a node to insert: rather than a
// builder API that could produce a tree the parser never would, the text
// goes through the same parser as everything else.
[[nodiscard]] std::optional<GreenNodePtr> statement_from_text(std::string_view text,
                                                              GreenCache& cache);

} // namespace stardata::cst
