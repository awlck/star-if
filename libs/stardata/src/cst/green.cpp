// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/cst/green.hpp"

#include <functional>
#include <utility>

namespace stardata::cst {

namespace {

// Boost's hash_combine. Nothing here is adversarial -- the keys are a
// compiler's own syntax kinds and token text -- so a fast mixer is the right
// trade against a cryptographic one.
void hash_combine(std::size_t& seed, std::size_t value) noexcept {
    seed ^= value + 0x9E3779B97F4A7C15ULL + (seed << 6) + (seed >> 2);
}

} // namespace

SyntaxKind GreenElement::kind() const noexcept {
    if (const GreenNode* as_node = node()) {
        return as_node->kind();
    }
    return token()->kind();
}

std::uint32_t GreenElement::text_length() const noexcept {
    if (const GreenNode* as_node = node()) {
        return as_node->text_length();
    }
    return token()->text_length();
}

const void* GreenElement::identity() const noexcept {
    if (const GreenNode* as_node = node()) {
        return static_cast<const void*>(as_node);
    }
    return static_cast<const void*>(token());
}

GreenNode::GreenNode(SyntaxKind kind, std::vector<GreenElement> children) noexcept
    : children_(std::move(children)), text_length_(0), kind_(kind) {
    for (const GreenElement& child : children_) {
        text_length_ += child.text_length();
    }
}

void GreenNode::write_text(std::string& out) const {
    for (const GreenElement& child : children_) {
        if (const GreenNode* as_node = child.node()) {
            as_node->write_text(out);
        } else {
            out += child.token()->text();
        }
    }
}

std::string GreenNode::text() const {
    std::string out;
    out.reserve(text_length_);
    write_text(out);
    return out;
}

std::size_t GreenCache::TokenKeyHash::operator()(const TokenKey& key) const noexcept {
    std::size_t seed = std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(key.kind));
    hash_combine(seed, std::hash<std::string>{}(key.text));
    return seed;
}

std::size_t GreenCache::NodeKeyHash::operator()(const NodeKey& key) const noexcept {
    std::size_t seed = std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(key.kind));
    for (const void* child : key.children) {
        hash_combine(seed, std::hash<const void*>{}(child));
    }
    return seed;
}

GreenTokenPtr GreenCache::token(SyntaxKind kind, std::string_view text) {
    TokenKey key{kind, std::string(text)};
    const std::lock_guard<std::mutex> guard(mutex_);
    if (const auto found = tokens_.find(key); found != tokens_.end()) {
        return found->second;
    }
    auto created = std::make_shared<const GreenToken>(kind, std::string(text));
    tokens_.emplace(std::move(key), created);
    return created;
}

GreenNodePtr GreenCache::node(SyntaxKind kind, std::vector<GreenElement> children) {
    if (children.size() > kMaxInternedChildren) {
        // Too wide to be worth hashing: a node this size is almost certainly
        // unique, and the lookup would cost more than the sharing saves.
        return std::make_shared<const GreenNode>(kind, std::move(children));
    }

    NodeKey key{kind, {}};
    key.children.reserve(children.size());
    for (const GreenElement& child : children) {
        key.children.push_back(child.identity());
    }

    const std::lock_guard<std::mutex> guard(mutex_);
    if (const auto found = nodes_.find(key); found != nodes_.end()) {
        return found->second;
    }
    auto created = std::make_shared<const GreenNode>(kind, std::move(children));
    nodes_.emplace(std::move(key), created);
    return created;
}

std::size_t GreenCache::token_count() const {
    const std::lock_guard<std::mutex> guard(mutex_);
    return tokens_.size();
}

std::size_t GreenCache::node_count() const {
    const std::lock_guard<std::mutex> guard(mutex_);
    return nodes_.size();
}

} // namespace stardata::cst
