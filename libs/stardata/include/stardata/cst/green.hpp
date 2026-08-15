// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "stardata/cst/kind.hpp"

namespace stardata::cst {

// The green tree: immutable, parent-free, position-free nodes, after the
// rust-analyzer `rowan` and Roslyn green-red design (backlog E1).
//
// The three omissions are the whole point, and each is load-bearing:
//
//   No parent pointer, so one subtree can appear in any number of trees.
//   No absolute offset, so a subtree is unchanged by anything that happens
//     before it -- which is what lets an edit at the top of a file share
//     every untouched node with the previous tree (E7).
//   No mutation, so sharing needs no copying and no locking.
//
// Position and parentage are supplied on demand by the red tree
// (`SyntaxNode`, cst/syntax.hpp), computed as a cursor walks down from a
// root. A green node knows only its kind, its total text length, and its
// children.

class GreenNode;
class GreenToken;

using GreenNodePtr = std::shared_ptr<const GreenNode>;
using GreenTokenPtr = std::shared_ptr<const GreenToken>;

// A leaf: a kind and the exact bytes it covers. The text is owned rather
// than referenced, so a tree outlives the buffer it was parsed from and an
// edit can introduce text that was never in any source file. Interning is
// what keeps that from being expensive -- a file's thousands of `=` tokens
// are one object.
class GreenToken {
public:
    GreenToken(SyntaxKind kind, std::string text) noexcept : text_(std::move(text)), kind_(kind) {}

    [[nodiscard]] SyntaxKind kind() const noexcept { return kind_; }
    [[nodiscard]] std::string_view text() const noexcept { return text_; }
    [[nodiscard]] std::uint32_t text_length() const noexcept {
        return static_cast<std::uint32_t>(text_.size());
    }
    [[nodiscard]] bool is_trivia() const noexcept { return is_trivia_kind(kind_); }

private:
    std::string text_;
    SyntaxKind kind_;
};

// A child of a green node: either another node or a token. Held by
// shared_ptr, whose refcount is atomic, so a tree may be shared across
// threads without further synchronisation (backlog E1's last bullet).
class GreenElement {
public:
    GreenElement(GreenNodePtr node) noexcept : repr_(std::move(node)) {}
    GreenElement(GreenTokenPtr token) noexcept : repr_(std::move(token)) {}

    [[nodiscard]] bool is_node() const noexcept {
        return std::holds_alternative<GreenNodePtr>(repr_);
    }
    [[nodiscard]] bool is_token() const noexcept { return !is_node(); }

    // Null unless the element is of that shape; check with is_node/is_token.
    [[nodiscard]] const GreenNode* node() const noexcept {
        const auto* held = std::get_if<GreenNodePtr>(&repr_);
        return held ? held->get() : nullptr;
    }
    [[nodiscard]] const GreenToken* token() const noexcept {
        const auto* held = std::get_if<GreenTokenPtr>(&repr_);
        return held ? held->get() : nullptr;
    }
    [[nodiscard]] GreenNodePtr node_ptr() const noexcept {
        const auto* held = std::get_if<GreenNodePtr>(&repr_);
        return held ? *held : GreenNodePtr{};
    }
    [[nodiscard]] GreenTokenPtr token_ptr() const noexcept {
        const auto* held = std::get_if<GreenTokenPtr>(&repr_);
        return held ? *held : GreenTokenPtr{};
    }

    [[nodiscard]] SyntaxKind kind() const noexcept;
    [[nodiscard]] std::uint32_t text_length() const noexcept;

    // Identity, not structure: two elements are the same when they point at
    // the same object. Because the cache interns bottom-up, structurally
    // identical subtrees built through one cache *are* the same object, so
    // this is also a structural comparison in practice.
    friend bool operator==(const GreenElement& lhs, const GreenElement& rhs) noexcept {
        return lhs.repr_ == rhs.repr_;
    }

    [[nodiscard]] const void* identity() const noexcept;

private:
    std::variant<GreenNodePtr, GreenTokenPtr> repr_;
};

class GreenNode {
public:
    GreenNode(SyntaxKind kind, std::vector<GreenElement> children) noexcept;

    [[nodiscard]] SyntaxKind kind() const noexcept { return kind_; }
    [[nodiscard]] std::uint32_t text_length() const noexcept { return text_length_; }
    [[nodiscard]] const std::vector<GreenElement>& children() const noexcept { return children_; }

    // Concatenates every leaf beneath this node. This is the tree's text,
    // and for an unmodified parse it is byte-identical to the source range
    // the node came from (backlog E5).
    void write_text(std::string& out) const;
    [[nodiscard]] std::string text() const;

private:
    std::vector<GreenElement> children_;
    std::uint32_t text_length_;
    SyntaxKind kind_;
};

// Hands out green nodes and tokens, returning the same object whenever the
// same one is asked for twice (backlog E1's "interning so identical subtrees
// share storage").
//
// Tokens are always interned: a real file is mostly punctuation, indentation
// and a small vocabulary of keys, so the hit rate is enormous. Nodes are
// interned only up to `kMaxInternedChildren` children, following rowan's
// heuristic -- a `File` node with two thousand children is unique by
// construction, and hashing it on every lookup would cost more than the
// storage it could ever save.
//
// Thread-safe: the parser may build on one thread while another walks a tree
// the cache already handed out.
class GreenCache {
public:
    static constexpr std::size_t kMaxInternedChildren = 3;

    [[nodiscard]] GreenTokenPtr token(SyntaxKind kind, std::string_view text);
    [[nodiscard]] GreenNodePtr node(SyntaxKind kind, std::vector<GreenElement> children);

    // How many distinct tokens and interned nodes are held. Tests use these
    // to show that interning is doing something rather than trusting it.
    [[nodiscard]] std::size_t token_count() const;
    [[nodiscard]] std::size_t node_count() const;

private:
    struct TokenKey {
        SyntaxKind kind;
        std::string text;
        friend bool operator==(const TokenKey&, const TokenKey&) noexcept = default;
    };
    struct TokenKeyHash {
        [[nodiscard]] std::size_t operator()(const TokenKey& key) const noexcept;
    };
    struct NodeKey {
        SyntaxKind kind;
        std::vector<const void*> children; // identities, interned bottom-up
        friend bool operator==(const NodeKey&, const NodeKey&) noexcept = default;
    };
    struct NodeKeyHash {
        [[nodiscard]] std::size_t operator()(const NodeKey& key) const noexcept;
    };

    mutable std::mutex mutex_;
    std::unordered_map<TokenKey, GreenTokenPtr, TokenKeyHash> tokens_;
    std::unordered_map<NodeKey, GreenNodePtr, NodeKeyHash> nodes_;
};

} // namespace stardata::cst
