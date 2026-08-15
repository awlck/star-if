// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/cst/syntax.hpp"

#include <utility>

namespace stardata::cst {

namespace {

// Builds the cursor for child `index` of `parent`, whose absolute offset is
// `offset`. The one place a red node is created, so the offset arithmetic
// lives in exactly one function.
[[nodiscard]] SyntaxElement make_child(const detail::SyntaxDataPtr& parent,
                                       const GreenElement& child, std::uint32_t offset,
                                       std::uint32_t index) {
    if (GreenNodePtr as_node = child.node_ptr()) {
        auto data = std::make_shared<const detail::SyntaxData>(
            detail::SyntaxData{std::move(as_node), parent, offset, index});
        return SyntaxElement(SyntaxNode::root_from_data(std::move(data)));
    }
    return SyntaxElement(SyntaxToken(parent, child.token_ptr(), offset, index));
}

void collect_descendants(const SyntaxNode& node, std::vector<SyntaxNode>& out) {
    out.push_back(node);
    for (const SyntaxNode& child : node.child_nodes()) {
        collect_descendants(child, out);
    }
}

} // namespace

// --------------------------------------------------------------------------
// SyntaxElement
// --------------------------------------------------------------------------

SyntaxElement::SyntaxElement(SyntaxNode node)
    : repr_(std::make_shared<const std::variant<SyntaxNode, SyntaxToken>>(std::move(node))) {}

SyntaxElement::SyntaxElement(SyntaxToken token)
    : repr_(std::make_shared<const std::variant<SyntaxNode, SyntaxToken>>(std::move(token))) {}

bool SyntaxElement::is_node() const noexcept {
    return std::holds_alternative<SyntaxNode>(*repr_);
}

bool SyntaxElement::is_token() const noexcept {
    return std::holds_alternative<SyntaxToken>(*repr_);
}

SyntaxKind SyntaxElement::kind() const noexcept {
    if (const auto* node = std::get_if<SyntaxNode>(repr_.get())) {
        return node->kind();
    }
    return std::get<SyntaxToken>(*repr_).kind();
}

TextRange SyntaxElement::text_range() const noexcept {
    if (const auto* node = std::get_if<SyntaxNode>(repr_.get())) {
        return node->text_range();
    }
    return std::get<SyntaxToken>(*repr_).text_range();
}

std::optional<SyntaxNode> SyntaxElement::as_node() const {
    if (const auto* node = std::get_if<SyntaxNode>(repr_.get())) {
        return *node;
    }
    return std::nullopt;
}

std::optional<SyntaxToken> SyntaxElement::as_token() const {
    if (const auto* token = std::get_if<SyntaxToken>(repr_.get())) {
        return *token;
    }
    return std::nullopt;
}

// --------------------------------------------------------------------------
// SyntaxNode
// --------------------------------------------------------------------------

SyntaxNode SyntaxNode::root(GreenNodePtr green) {
    auto data = std::make_shared<const detail::SyntaxData>(
        detail::SyntaxData{std::move(green), nullptr, 0, 0});
    return SyntaxNode(std::move(data));
}

SyntaxNode SyntaxNode::root_from_data(detail::SyntaxDataPtr data) {
    return SyntaxNode(std::move(data));
}

std::optional<SyntaxNode> SyntaxNode::parent() const {
    if (!data_->parent) {
        return std::nullopt;
    }
    return SyntaxNode(data_->parent);
}

std::vector<SyntaxElement> SyntaxNode::children() const {
    std::vector<SyntaxElement> result;
    const auto& green_children = data_->green->children();
    result.reserve(green_children.size());

    std::uint32_t offset = data_->offset;
    for (std::size_t index = 0; index < green_children.size(); ++index) {
        result.push_back(
            make_child(data_, green_children[index], offset, static_cast<std::uint32_t>(index)));
        offset += green_children[index].text_length();
    }
    return result;
}

std::optional<SyntaxElement> SyntaxNode::child_at(std::size_t index) const {
    const auto& green_children = data_->green->children();
    if (index >= green_children.size()) {
        return std::nullopt;
    }
    std::uint32_t offset = data_->offset;
    for (std::size_t n = 0; n < index; ++n) {
        offset += green_children[n].text_length();
    }
    return make_child(data_, green_children[index], offset, static_cast<std::uint32_t>(index));
}

std::vector<SyntaxNode> SyntaxNode::child_nodes() const {
    std::vector<SyntaxNode> result;
    for (const SyntaxElement& child : children()) {
        if (auto node = child.as_node()) {
            result.push_back(*node);
        }
    }
    return result;
}

std::vector<SyntaxToken> SyntaxNode::child_tokens() const {
    std::vector<SyntaxToken> result;
    for (const SyntaxElement& child : children()) {
        if (auto token = child.as_token()) {
            result.push_back(*token);
        }
    }
    return result;
}

std::optional<SyntaxElement> SyntaxNode::next_sibling() const {
    if (!data_->parent) {
        return std::nullopt;
    }
    return SyntaxNode(data_->parent).child_at(data_->index_in_parent + 1);
}

std::optional<SyntaxElement> SyntaxNode::prev_sibling() const {
    if (!data_->parent || data_->index_in_parent == 0) {
        return std::nullopt;
    }
    return SyntaxNode(data_->parent).child_at(data_->index_in_parent - 1);
}

std::optional<SyntaxNode> SyntaxNode::next_sibling_node() const {
    if (!data_->parent) {
        return std::nullopt;
    }
    const SyntaxNode owner(data_->parent);
    for (std::size_t index = data_->index_in_parent + 1;; ++index) {
        auto sibling = owner.child_at(index);
        if (!sibling) {
            return std::nullopt;
        }
        if (auto node = sibling->as_node()) {
            return node;
        }
    }
}

std::vector<SyntaxNode> SyntaxNode::ancestors() const {
    std::vector<SyntaxNode> result;
    for (auto current = parent(); current; current = current->parent()) {
        result.push_back(*current);
    }
    return result;
}

std::vector<SyntaxNode> SyntaxNode::descendants() const {
    // Pre-order is document order, and appending as it recurses keeps this
    // linear. Nesting depth is bounded by block nesting, so the recursion is
    // shallow even on a large file.
    std::vector<SyntaxNode> result;
    collect_descendants(*this, result);
    return result;
}

std::vector<SyntaxToken> SyntaxNode::tokens() const {
    std::vector<SyntaxToken> result;
    for (const SyntaxElement& child : children()) {
        if (auto token = child.as_token()) {
            result.push_back(*token);
        } else {
            for (SyntaxToken nested : child.as_node()->tokens()) {
                result.push_back(std::move(nested));
            }
        }
    }
    return result;
}

std::optional<SyntaxNode> SyntaxNode::first_child_of_kind(SyntaxKind kind) const {
    for (const SyntaxNode& child : child_nodes()) {
        if (child.kind() == kind) {
            return child;
        }
    }
    return std::nullopt;
}

std::optional<SyntaxToken> SyntaxNode::first_token_of_kind(SyntaxKind kind) const {
    for (const SyntaxToken& token : child_tokens()) {
        if (token.kind() == kind) {
            return token;
        }
    }
    return std::nullopt;
}

std::optional<SyntaxToken> SyntaxNode::token_at_offset(std::uint32_t offset) const {
    if (!text_range().contains(offset)) {
        return std::nullopt;
    }
    for (const SyntaxElement& child : children()) {
        if (!child.text_range().contains(offset)) {
            continue;
        }
        if (auto token = child.as_token()) {
            return token;
        }
        return child.as_node()->token_at_offset(offset);
    }
    return std::nullopt;
}

std::string SyntaxNode::text() const {
    return data_->green->text();
}

// --------------------------------------------------------------------------
// SyntaxToken
// --------------------------------------------------------------------------

std::optional<SyntaxNode> SyntaxToken::parent() const {
    if (!parent_) {
        return std::nullopt;
    }
    return SyntaxNode::root_from_data(parent_);
}

std::optional<SyntaxElement> SyntaxToken::next_sibling() const {
    if (!parent_) {
        return std::nullopt;
    }
    return SyntaxNode::root_from_data(parent_).child_at(index_in_parent_ + 1);
}

std::optional<SyntaxElement> SyntaxToken::prev_sibling() const {
    if (!parent_ || index_in_parent_ == 0) {
        return std::nullopt;
    }
    return SyntaxNode::root_from_data(parent_).child_at(index_in_parent_ - 1);
}

std::optional<SyntaxToken> SyntaxToken::next_token() const {
    // Walk right through the tree: try the next sibling, and failing that
    // climb until an ancestor has one, then descend to its first leaf.
    std::optional<SyntaxElement> sibling = next_sibling();
    std::optional<SyntaxNode> current = parent();
    while (!sibling) {
        if (!current) {
            return std::nullopt;
        }
        sibling = current->next_sibling();
        current = current->parent();
    }
    while (true) {
        if (auto token = sibling->as_token()) {
            return token;
        }
        const SyntaxNode node = *sibling->as_node();
        const std::vector<SyntaxToken> leaves = node.tokens();
        if (!leaves.empty()) {
            return leaves.front();
        }
        // An empty node: keep going right from it.
        std::optional<SyntaxElement> next = node.next_sibling();
        std::optional<SyntaxNode> above = node.parent();
        while (!next) {
            if (!above) {
                return std::nullopt;
            }
            next = above->next_sibling();
            above = above->parent();
        }
        sibling = next;
    }
}

std::optional<SyntaxToken> SyntaxToken::prev_token() const {
    std::optional<SyntaxElement> sibling = prev_sibling();
    std::optional<SyntaxNode> current = parent();
    while (!sibling) {
        if (!current) {
            return std::nullopt;
        }
        sibling = current->prev_sibling();
        current = current->parent();
    }
    while (true) {
        if (auto token = sibling->as_token()) {
            return token;
        }
        const SyntaxNode node = *sibling->as_node();
        const std::vector<SyntaxToken> leaves = node.tokens();
        if (!leaves.empty()) {
            return leaves.back();
        }
        std::optional<SyntaxElement> previous = node.prev_sibling();
        std::optional<SyntaxNode> above = node.parent();
        while (!previous) {
            if (!above) {
                return std::nullopt;
            }
            previous = above->prev_sibling();
            above = above->parent();
        }
        sibling = previous;
    }
}

} // namespace stardata::cst
