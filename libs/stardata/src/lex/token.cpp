// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/lex/token.hpp"

#include <cassert>

namespace stardata::lex {

std::string_view to_string(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::Identifier:
        return "identifier";
    case TokenKind::Integer:
        return "integer";
    case TokenKind::Decimal:
        return "decimal";
    case TokenKind::String:
        return "string";
    case TokenKind::LocKey:
        return "lockey";
    case TokenKind::Annotation:
        return "annotation";
    case TokenKind::Operator:
        return "operator";
    case TokenKind::Punctuation:
        return "punctuation";
    case TokenKind::Angle:
        return "angle";
    case TokenKind::Error:
        return "error";
    case TokenKind::EndOfFile:
        return "eof";
    }
    assert(false && "unhandled TokenKind");
    return "error";
}

std::string_view to_string(TriviaKind kind) noexcept {
    switch (kind) {
    case TriviaKind::Whitespace:
        return "whitespace";
    case TriviaKind::Comment:
        return "comment";
    case TriviaKind::ByteOrderMark:
        return "bom";
    }
    assert(false && "unhandled TriviaKind");
    return "whitespace";
}

std::span<const Trivia> TokenStream::preceding_trivia(std::size_t index) const {
    const Token& token = tokens_.at(index);
    return std::span<const Trivia>(trivia_).subspan(token.trivia_first, token.trivia_count);
}

bool TokenStream::starts_string_run(std::size_t index) const {
    if (index >= tokens_.size() || tokens_[index].kind != TokenKind::String) {
        return false;
    }
    return index == 0 || tokens_[index - 1].kind != TokenKind::String;
}

TokenRange TokenStream::string_run_at(std::size_t index) const {
    if (index >= tokens_.size() || tokens_[index].kind != TokenKind::String) {
        return TokenRange{index, index + 1};
    }
    std::size_t first = index;
    while (first > 0 && tokens_[first - 1].kind == TokenKind::String) {
        --first;
    }
    std::size_t last = index + 1;
    while (last < tokens_.size() && tokens_[last].kind == TokenKind::String) {
        ++last;
    }
    return TokenRange{first, last};
}

bool TokenStream::covers_source(const diag::SourceManager& sources) const {
    const std::size_t length = sources.contents(source_).size();

    // Walk tokens and trivia together in source order. Every piece must
    // start exactly where the previous one ended.
    std::uint32_t cursor = 0;
    std::size_t trivia_index = 0;
    for (const Token& token : tokens_) {
        if (token.trivia_first != trivia_index) {
            return false;
        }
        for (std::uint32_t n = 0; n < token.trivia_count; ++n) {
            const Trivia& piece = trivia_.at(trivia_index++);
            if (piece.span.source != source_ || piece.span.offset != cursor ||
                piece.span.length == 0) {
                return false;
            }
            cursor = piece.span.end();
        }
        if (token.span.source != source_ || token.span.offset != cursor) {
            return false;
        }
        cursor = token.span.end();
    }
    return trivia_index == trivia_.size() && cursor == length;
}

} // namespace stardata::lex
