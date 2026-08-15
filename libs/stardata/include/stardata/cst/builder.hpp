// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "stardata/cst/green.hpp"
#include "stardata/cst/kind.hpp"

namespace stardata::cst {

// Assembles a green tree from a linear sequence of `start_node`, `token` and
// `finish_node` calls -- the shape a recursive-descent parser produces
// naturally.
//
// The checkpoint mechanism is what makes that possible for this grammar.
// `Value` cannot be classified until after its first token has been read: an
// identifier is a `Scalar` unless a `<` follows (making it a `TypeExpr`,
// §4.2) or a `(` does (a `Call`, §4.3). Rather than look ahead and duplicate
// the classification, the parser takes a checkpoint, consumes the
// identifier, discovers what it has, and wraps retroactively.
class GreenBuilder {
public:
    explicit GreenBuilder(GreenCache& cache) noexcept : cache_(cache) {}

    void start_node(SyntaxKind kind);

    // Wraps everything built since `checkpoint` in a new node. The
    // checkpoint must be at or after the start of the node currently open.
    void start_node_at(std::size_t checkpoint, SyntaxKind kind);

    void token(SyntaxKind kind, std::string_view text);
    void finish_node();

    [[nodiscard]] std::size_t checkpoint() const noexcept { return children_.size(); }

    // The finished root. Every started node must have been finished.
    [[nodiscard]] GreenNodePtr finish();

    [[nodiscard]] std::size_t open_nodes() const noexcept { return open_.size(); }

private:
    struct Open {
        SyntaxKind kind;
        std::size_t first_child; // index into children_
    };

    GreenCache& cache_;
    std::vector<GreenElement> children_;
    std::vector<Open> open_;
};

} // namespace stardata::cst
