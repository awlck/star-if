// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/cst/kind.hpp"

#include <cassert>

namespace stardata::cst {

std::string_view to_string(SyntaxKind kind) noexcept {
    switch (kind) {
    case SyntaxKind::Identifier:
        return "identifier";
    case SyntaxKind::Integer:
        return "integer";
    case SyntaxKind::Decimal:
        return "decimal";
    case SyntaxKind::String:
        return "string";
    case SyntaxKind::LocKey:
        return "lockey";
    case SyntaxKind::AnnotationName:
        return "annotation_name";
    case SyntaxKind::Operator:
        return "operator";
    case SyntaxKind::Punctuation:
        return "punctuation";
    case SyntaxKind::Angle:
        return "angle";
    case SyntaxKind::ErrorToken:
        return "error_token";
    case SyntaxKind::Whitespace:
        return "whitespace";
    case SyntaxKind::Comment:
        return "comment";
    case SyntaxKind::ByteOrderMark:
        return "bom";
    case SyntaxKind::File:
        return "File";
    case SyntaxKind::Statement:
        return "Statement";
    case SyntaxKind::Key:
        return "Key";
    case SyntaxKind::Value:
        return "Value";
    case SyntaxKind::Scalar:
        return "Scalar";
    case SyntaxKind::Block:
        return "Block";
    case SyntaxKind::TypeExpr:
        return "TypeExpr";
    case SyntaxKind::Call:
        return "Call";
    case SyntaxKind::Annotation:
        return "Annotation";
    case SyntaxKind::Error:
        return "Error";
    }
    assert(false && "unhandled SyntaxKind");
    return "error_token";
}

bool is_leaf_kind(SyntaxKind kind) noexcept {
    return kind <= SyntaxKind::ByteOrderMark;
}

bool is_trivia_kind(SyntaxKind kind) noexcept {
    return kind == SyntaxKind::Whitespace || kind == SyntaxKind::Comment ||
           kind == SyntaxKind::ByteOrderMark;
}

SyntaxKind from_token_kind(lex::TokenKind kind) noexcept {
    switch (kind) {
    case lex::TokenKind::Identifier:
        return SyntaxKind::Identifier;
    case lex::TokenKind::Integer:
        return SyntaxKind::Integer;
    case lex::TokenKind::Decimal:
        return SyntaxKind::Decimal;
    case lex::TokenKind::String:
        return SyntaxKind::String;
    case lex::TokenKind::LocKey:
        return SyntaxKind::LocKey;
    case lex::TokenKind::Annotation:
        return SyntaxKind::AnnotationName;
    case lex::TokenKind::Operator:
        return SyntaxKind::Operator;
    case lex::TokenKind::Punctuation:
        return SyntaxKind::Punctuation;
    case lex::TokenKind::Angle:
        return SyntaxKind::Angle;
    case lex::TokenKind::Error:
        return SyntaxKind::ErrorToken;
    case lex::TokenKind::EndOfFile:
        // EndOfFile is zero-length and carries only its preceding trivia; the
        // parser never puts one in the tree, so mapping it is a courtesy for
        // exhaustiveness rather than something that happens.
        return SyntaxKind::ErrorToken;
    }
    assert(false && "unhandled lex::TokenKind");
    return SyntaxKind::ErrorToken;
}

SyntaxKind from_trivia_kind(lex::TriviaKind kind) noexcept {
    switch (kind) {
    case lex::TriviaKind::Whitespace:
        return SyntaxKind::Whitespace;
    case lex::TriviaKind::Comment:
        return SyntaxKind::Comment;
    case lex::TriviaKind::ByteOrderMark:
        return SyntaxKind::ByteOrderMark;
    }
    assert(false && "unhandled lex::TriviaKind");
    return SyntaxKind::Whitespace;
}

} // namespace stardata::cst
