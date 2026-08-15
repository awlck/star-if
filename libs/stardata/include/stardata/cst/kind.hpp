// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstdint>
#include <string_view>

#include "stardata/lex/token.hpp"

namespace stardata::cst {

// Every kind a node or a leaf of the concrete syntax tree can have.
//
// Leaves come first and mirror the lexer's token and trivia kinds one for
// one, because §14.2's byte-exact round-trip means the tree holds every
// lexeme the lexer produced -- trivia included. Nodes follow, and are the
// grammar of spec §4.
enum class SyntaxKind : std::uint8_t {
    // --- leaves: tokens (spec §3) ---
    Identifier,
    Integer,
    Decimal,
    String,
    LocKey,
    AnnotationName, // the '@name' token; its arguments are separate leaves
    Operator,
    Punctuation,
    Angle,
    ErrorToken, // bytes the lexer could not tokenise, kept so the tree is total

    // --- leaves: trivia (spec §3.1, §3.2, §2.2) ---
    Whitespace,
    Comment,
    ByteOrderMark,

    // --- nodes: the grammar of spec §4 ---
    File,       // Statement*
    Statement,  // Key Op Value
    Key,        // Identifier | String
    Value,      // Annotation* ( Block | TypeExpr | Call | Scalar )
    Scalar,     // one literal, or a run of adjacent String literals (§3.5.1)
    Block,      // '{' ( Statement* | Scalar* ) '}'
    TypeExpr,   // Identifier '<' TypeArg ( ',' TypeArg )* '>'   (§4.2)
    Call,       // Identifier '(' ( CallArg ( ',' CallArg )* )? ')'  (§4.3)
    Annotation, // '@' Identifier ( '(' AnnotationArgs ')' )?    (§3.8)
    Error,      // a run the parser could not fit the grammar, kept verbatim
};

[[nodiscard]] std::string_view to_string(SyntaxKind kind) noexcept;

// True for a kind that can only ever be a leaf. Nodes have children; leaves
// have text.
[[nodiscard]] bool is_leaf_kind(SyntaxKind kind) noexcept;

// True for whitespace, a comment or a byte-order mark -- the lexemes that
// carry no meaning and must nonetheless survive (§14.2).
[[nodiscard]] bool is_trivia_kind(SyntaxKind kind) noexcept;

// The tree kind for a lexer token or trivia kind. Total: every lexer kind
// has a leaf kind here, which is what lets the parser put the entire token
// stream into the tree without deciding what to drop.
[[nodiscard]] SyntaxKind from_token_kind(lex::TokenKind kind) noexcept;
[[nodiscard]] SyntaxKind from_trivia_kind(lex::TriviaKind kind) noexcept;

// A half-open byte range, `[offset, offset + length)`. Used for a red node's
// absolute position; green nodes know only their length, never where they
// sit, which is what makes them shareable between trees (backlog E1).
struct TextRange {
    std::uint32_t offset = 0;
    std::uint32_t length = 0;

    [[nodiscard]] constexpr std::uint32_t end() const noexcept { return offset + length; }
    [[nodiscard]] constexpr bool empty() const noexcept { return length == 0; }
    [[nodiscard]] constexpr bool contains(std::uint32_t position) const noexcept {
        return position >= offset && position < end();
    }

    friend constexpr bool operator==(TextRange lhs, TextRange rhs) noexcept {
        return lhs.offset == rhs.offset && lhs.length == rhs.length;
    }
};

} // namespace stardata::cst
