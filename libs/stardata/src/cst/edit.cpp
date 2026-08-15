// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/cst/edit.hpp"

#include <string>
#include <utility>
#include <vector>

#include "stardata/cst/parser.hpp"

namespace stardata::cst {

namespace {

// Rebuilds the spine from `node`'s parent to the root, with `node` replaced
// by `replacement`. Every sibling and every subtree off the spine is carried
// over by pointer, so the cost is the depth of the tree rather than its size.
[[nodiscard]] GreenNodePtr rebuild_to_root(const SyntaxNode& node, GreenElement replacement,
                                           GreenCache& cache) {
    std::optional<SyntaxNode> parent = node.parent();
    if (!parent) {
        // The root itself was replaced; a token cannot be a root, so this is
        // always a node.
        return replacement.node_ptr();
    }

    std::vector<GreenElement> children = parent->green()->children();
    children[node.index_in_parent()] = std::move(replacement);
    GreenNodePtr rebuilt = cache.node(parent->kind(), std::move(children));
    return rebuild_to_root(*parent, GreenElement(std::move(rebuilt)), cache);
}

// The same, for a node whose child list has already been rewritten.
[[nodiscard]] GreenNodePtr rebuild_with_children(const SyntaxNode& node,
                                                 std::vector<GreenElement> children,
                                                 GreenCache& cache) {
    GreenNodePtr rebuilt = cache.node(node.kind(), std::move(children));
    return rebuild_to_root(node, GreenElement(std::move(rebuilt)), cache);
}

} // namespace

GreenNodePtr replace_node(const SyntaxNode& target, GreenNodePtr replacement, GreenCache& cache) {
    return rebuild_to_root(target, GreenElement(std::move(replacement)), cache);
}

GreenNodePtr replace_token(const SyntaxToken& target, GreenTokenPtr replacement,
                           GreenCache& cache) {
    const std::optional<SyntaxNode> parent = target.parent();
    if (!parent) {
        return nullptr; // a token with no parent is not part of any tree
    }
    std::vector<GreenElement> children = parent->green()->children();
    children[target.index_in_parent()] = GreenElement(std::move(replacement));
    return rebuild_with_children(*parent, std::move(children), cache);
}

GreenNodePtr insert_child(const SyntaxNode& parent, std::size_t index, GreenElement child,
                          GreenCache& cache) {
    std::vector<GreenElement> children = parent.green()->children();
    if (index > children.size()) {
        index = children.size();
    }
    children.insert(children.begin() + static_cast<std::ptrdiff_t>(index), std::move(child));
    return rebuild_with_children(parent, std::move(children), cache);
}

GreenNodePtr remove_child(const SyntaxNode& parent, std::size_t index, GreenCache& cache) {
    std::vector<GreenElement> children = parent.green()->children();
    if (index >= children.size()) {
        return parent.green(); // nothing to remove; the tree is unchanged
    }
    children.erase(children.begin() + static_cast<std::ptrdiff_t>(index));
    return rebuild_with_children(parent, std::move(children), cache);
}

GreenNodePtr remove_statement(const SyntaxNode& statement, GreenCache& cache) {
    const std::optional<SyntaxNode> parent = statement.parent();
    if (!parent) {
        return statement.green();
    }
    return remove_child(*parent, statement.index_in_parent(), cache);
}

GreenNodePtr insert_statement_after(const SyntaxNode& anchor, GreenNodePtr statement,
                                    GreenCache& cache) {
    const std::optional<SyntaxNode> parent = anchor.parent();
    if (!parent) {
        return anchor.green();
    }
    return insert_child(*parent, anchor.index_in_parent() + 1, GreenElement(std::move(statement)),
                        cache);
}

std::optional<GreenNodePtr> statement_from_text(std::string_view text, GreenCache& cache) {
    // A scratch source and sink: the fragment is not part of any file, and
    // any diagnostic it provokes belongs to the caller's bad input rather
    // than to a real source, so neither outlives this call.
    diag::SourceManager scratch;
    const diag::SourceId id = scratch.add_file("<fragment>", std::string(text));
    diag::DiagnosticSink sink;

    const GreenNodePtr file = parse(scratch, id, cache, sink);
    if (sink.has_errors()) {
        return std::nullopt;
    }

    std::optional<GreenNodePtr> found;
    for (const GreenElement& child : file->children()) {
        if (child.kind() != SyntaxKind::Statement) {
            // Only trivia may sit alongside; anything else means the text was
            // not one statement.
            if (!is_trivia_kind(child.kind())) {
                return std::nullopt;
            }
            continue;
        }
        if (found) {
            return std::nullopt; // more than one statement
        }
        found = child.node_ptr();
    }
    return found;
}

} // namespace stardata::cst
