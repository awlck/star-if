// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "stardata/cst/green.hpp"
#include "stardata/cst/kind.hpp"

namespace stardata::cst {

// The red tree: a cursor over a green tree that supplies the two things the
// green tree deliberately omits -- who the parent is, and where in the file
// the node sits (backlog E2).
//
// Both are computed on the way down rather than stored in the green tree,
// which is what keeps a green subtree shareable: the same `Statement` can be
// at offset 40 in one tree and offset 900 in the next, and neither tree has
// to know about the other.
//
// A SyntaxNode is a cheap value type -- one shared_ptr and two integers --
// so it is passed and returned by value throughout. The chain of parents it
// holds keeps its ancestors alive, so a node handed out of a function
// remains valid even if the caller dropped the root.

class SyntaxNode;
class SyntaxToken;

// A child position: either a node or a token, in source order.
class SyntaxElement {
public:
    SyntaxElement(SyntaxNode node);
    SyntaxElement(SyntaxToken token);

    [[nodiscard]] bool is_node() const noexcept;
    [[nodiscard]] bool is_token() const noexcept;
    [[nodiscard]] SyntaxKind kind() const noexcept;
    [[nodiscard]] TextRange text_range() const noexcept;

    // Empty unless the element is of that shape.
    [[nodiscard]] std::optional<SyntaxNode> as_node() const;
    [[nodiscard]] std::optional<SyntaxToken> as_token() const;

private:
    // Held by pointer so the class can be declared before SyntaxNode and
    // SyntaxToken are complete; both are small, so this costs one allocation
    // per element handed out and keeps the header order sane.
    std::shared_ptr<const std::variant<SyntaxNode, SyntaxToken>> repr_;
};

namespace detail {

// One link in the parent chain. Refcounted, so a node keeps its ancestors
// alive without the tree owning its cursors.
struct SyntaxData {
    GreenNodePtr green;
    std::shared_ptr<const SyntaxData> parent;
    std::uint32_t offset = 0;          // absolute, in the file
    std::uint32_t index_in_parent = 0; // position among the parent's children
};

using SyntaxDataPtr = std::shared_ptr<const SyntaxData>;

} // namespace detail

class SyntaxToken {
public:
    SyntaxToken(detail::SyntaxDataPtr parent, GreenTokenPtr green, std::uint32_t offset,
                std::uint32_t index_in_parent) noexcept
        : parent_(std::move(parent)), green_(std::move(green)), offset_(offset),
          index_in_parent_(index_in_parent) {}

    [[nodiscard]] SyntaxKind kind() const noexcept { return green_->kind(); }
    [[nodiscard]] std::string_view text() const noexcept { return green_->text(); }
    [[nodiscard]] bool is_trivia() const noexcept { return green_->is_trivia(); }
    [[nodiscard]] TextRange text_range() const noexcept {
        return TextRange{offset_, green_->text_length()};
    }
    [[nodiscard]] const GreenTokenPtr& green() const noexcept { return green_; }
    [[nodiscard]] std::uint32_t index_in_parent() const noexcept { return index_in_parent_; }

    [[nodiscard]] std::optional<SyntaxNode> parent() const;
    [[nodiscard]] std::optional<SyntaxElement> next_sibling() const;
    [[nodiscard]] std::optional<SyntaxElement> prev_sibling() const;

    // The next or previous token in the file, crossing node boundaries.
    [[nodiscard]] std::optional<SyntaxToken> next_token() const;
    [[nodiscard]] std::optional<SyntaxToken> prev_token() const;

private:
    detail::SyntaxDataPtr parent_;
    GreenTokenPtr green_;
    std::uint32_t offset_;
    std::uint32_t index_in_parent_;
};

class SyntaxNode {
public:
    // The root of a tree. Offset 0, no parent.
    [[nodiscard]] static SyntaxNode root(GreenNodePtr green);

    // Wraps an already-built cursor link. Public only because the cursor
    // machinery in syntax.cpp needs it; callers outside the CST should use
    // `root` and navigate.
    [[nodiscard]] static SyntaxNode root_from_data(detail::SyntaxDataPtr data);

    [[nodiscard]] SyntaxKind kind() const noexcept { return data_->green->kind(); }
    [[nodiscard]] std::uint32_t text_length() const noexcept { return data_->green->text_length(); }
    [[nodiscard]] TextRange text_range() const noexcept {
        return TextRange{data_->offset, data_->green->text_length()};
    }
    [[nodiscard]] const GreenNodePtr& green() const noexcept { return data_->green; }
    [[nodiscard]] std::uint32_t index_in_parent() const noexcept { return data_->index_in_parent; }

    // --- navigation ---
    [[nodiscard]] std::optional<SyntaxNode> parent() const;
    [[nodiscard]] std::vector<SyntaxElement> children() const;
    [[nodiscard]] std::vector<SyntaxNode> child_nodes() const;
    [[nodiscard]] std::vector<SyntaxToken> child_tokens() const;
    [[nodiscard]] std::optional<SyntaxElement> child_at(std::size_t index) const;

    [[nodiscard]] std::optional<SyntaxElement> next_sibling() const;
    [[nodiscard]] std::optional<SyntaxElement> prev_sibling() const;
    [[nodiscard]] std::optional<SyntaxNode> next_sibling_node() const;

    // Ancestors from the parent upward; descendants in document order,
    // this node first. Materialised rather than lazy: Phase 0 walks trees
    // in tests and in the writer, neither of which is hot enough to justify
    // the iterator machinery.
    [[nodiscard]] std::vector<SyntaxNode> ancestors() const;
    [[nodiscard]] std::vector<SyntaxNode> descendants() const;
    [[nodiscard]] std::vector<SyntaxToken> tokens() const; // every leaf, in order

    // The first child node of the given kind, which is how the typed
    // accessors of backlog F1 will be built.
    [[nodiscard]] std::optional<SyntaxNode> first_child_of_kind(SyntaxKind kind) const;
    [[nodiscard]] std::optional<SyntaxToken> first_token_of_kind(SyntaxKind kind) const;

    // The deepest node whose range contains `offset`, and the token there.
    [[nodiscard]] std::optional<SyntaxToken> token_at_offset(std::uint32_t offset) const;

    // Reconstructs the source this node covers by concatenating its leaves
    // (backlog E2). For an unmodified parse this is byte-identical to the
    // input over `text_range()`.
    [[nodiscard]] std::string text() const;

    // Identity: two cursors are equal when they denote the same green node
    // at the same absolute position.
    friend bool operator==(const SyntaxNode& lhs, const SyntaxNode& rhs) noexcept {
        return lhs.data_->green == rhs.data_->green && lhs.data_->offset == rhs.data_->offset;
    }

private:
    explicit SyntaxNode(detail::SyntaxDataPtr data) noexcept : data_(std::move(data)) {}
    friend class SyntaxToken;

    detail::SyntaxDataPtr data_;
};

} // namespace stardata::cst
