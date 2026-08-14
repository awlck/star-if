// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "stardata/diag/source_manager.hpp"

namespace stardata::lex {

// The token kinds of spec §3. Deliberately coarse: the parser distinguishes
// `==` from `+=` by looking at the token's text (SourceManager::text), and
// the CST node kinds of workstream E carry the finer classification. A
// lexer that pre-judges more than §3 does is a lexer that has to be undone
// by the parser.
enum class TokenKind : std::uint8_t {
    Identifier,  // §3.3  [A-Za-z_][A-Za-z0-9_.]*, including the reserved words
    Integer,     // §3.4  '-'? [0-9]+
    Decimal,     // §3.4  '-'? [0-9]+ '.' [0-9]+
    String,      // §3.5  a single quoted literal, quotes included in the span
    LocKey,      // §3.5  '$' Identifier
    Annotation,  // §3.8  '@' Identifier -- see the note on arguments below
    Operator,    // §3.6  '=' '==' '!=' '<=' '>=' '+=' '-=' '?='
    Punctuation, // §3.7  '{' '}' '(' ')' ','
    Angle,       // §3.7  a bare '<' or '>' -- see the note on ambiguity below
    Error,       // a run of bytes that begins no token; retained, see below
    EndOfFile,   // always the last token; zero length, at the end of input
};

// Annotation arguments (§3.8's `'(' AnnotationArgs ')'`) are NOT part of the
// Annotation token. `@platform(glk, cli)` lexes as Annotation, Punctuation,
// Identifier, Punctuation, Identifier, Punctuation -- five tokens after the
// annotation -- because trivia may appear between them (`@platform( glk,
// # why\n cli )` is legal) and §14.2 requires that trivia survive into the
// tree. Folding the arguments into one token would make that impossible.

// `Angle` exists because §4.2's disambiguation is the parser's job: a `<` is
// a comparison in operator position and a type-argument opener in value
// position, and the lexer has no way to know which it is looking at. §3.1
// makes whitespace insignificant, so guessing from spacing is forbidden as
// well as unreliable. `<=` and `>=` are unambiguous -- neither can open a
// type argument list -- so they lex as Operator.

// `Error` covers bytes the lexer could not turn into a token (a stray `[`,
// an unexpected character, malformed UTF-8). It is emitted rather than
// dropped so that the token and trivia spans still tile the whole input,
// which is what lets E4's tree cover every byte including error text.

[[nodiscard]] std::string_view to_string(TokenKind kind) noexcept;

// Trivia carries no meaning but MUST be preserved for the byte-exact
// round-trip of §14.2, so the lexer retains it rather than discarding it.
enum class TriviaKind : std::uint8_t {
    Whitespace,   // §3.1  a run of tab, LF, CR or space
    Comment,      // §3.2  '#' to just before the next line terminator
    ByteOrderMark // §2.2  a leading U+FEFF: accepted, preserved, not content
};

// ByteOrderMark is a third kind beyond the two the backlog names because §2.2
// requires a leading BOM to survive a round-trip while explicitly not being
// content. Calling it whitespace would work but would lie to every consumer
// that asks what it is looking at.

[[nodiscard]] std::string_view to_string(TriviaKind kind) noexcept;

struct Trivia {
    TriviaKind kind = TriviaKind::Whitespace;
    diag::Span span;
};

// A token carries its span, never a copy of its text (backlog D1). Text is
// recovered with SourceManager::text(token.span) at the moment it is needed,
// so a token stream over a megabyte of source costs the tokens and nothing
// else, and no token can disagree with the source it came from.
struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    diag::Span span;

    // The trivia lying between the previous token and this one, as a range
    // into TokenStream::trivia(). See TokenStream::preceding_trivia.
    std::uint32_t trivia_first = 0;
    std::uint32_t trivia_count = 0;
};

// A half-open range of token indices, `[first, last)`.
struct TokenRange {
    std::size_t first = 0;
    std::size_t last = 0;

    [[nodiscard]] constexpr std::size_t size() const noexcept { return last - first; }

    friend constexpr bool operator==(TokenRange lhs, TokenRange rhs) noexcept {
        return lhs.first == rhs.first && lhs.last == rhs.last;
    }
};

// The result of lexing one source: every token in order, plus every piece of
// trivia between them. Together their spans tile the source exactly, with no
// gaps and no overlaps -- `covers_source` asserts it.
class TokenStream {
public:
    TokenStream() = default;
    explicit TokenStream(diag::SourceId source) noexcept : source_(source) {}

    [[nodiscard]] diag::SourceId source() const noexcept { return source_; }
    [[nodiscard]] const std::vector<Token>& tokens() const noexcept { return tokens_; }
    [[nodiscard]] const std::vector<Trivia>& trivia() const noexcept { return trivia_; }
    [[nodiscard]] std::size_t size() const noexcept { return tokens_.size(); }
    [[nodiscard]] const Token& operator[](std::size_t index) const { return tokens_.at(index); }

    // The trivia between tokens()[index - 1] and tokens()[index], in source
    // order. Deliberately not called *leading* trivia: whether a given
    // comment belongs to the token before it or the token after it is the
    // attachment policy of backlog E3, and the lexer does not decide it. All
    // trailing trivia in the file precedes the EndOfFile token.
    [[nodiscard]] std::span<const Trivia> preceding_trivia(std::size_t index) const;

    // Spec §3.5.1 / §4.1: two or more string literals separated only by
    // trivia form a single scalar. The lexer does not merge them -- §3.5.1
    // requires the split points to survive into the CST so that a round-trip
    // reproduces the author's line breaks -- so it reports the run instead
    // and leaves the grouping to the parser.
    //
    // Returns the run containing `index`, or `{index, index + 1}` when that
    // token is not a String. Because trivia lives outside the token vector,
    // "separated only by trivia" is exactly "adjacent in tokens()".
    [[nodiscard]] TokenRange string_run_at(std::size_t index) const;

    // True when tokens()[index] is a String that no String immediately
    // precedes -- the first literal of a run, and the one whose span a
    // diagnostic about the whole scalar should point at.
    [[nodiscard]] bool starts_string_run(std::size_t index) const;

    // True when the token and trivia spans, concatenated in source order,
    // reproduce the source byte for byte. An invariant of every stream the
    // lexer produces, and the property E5's writer will inherit; exposed so
    // tests can assert it on real corpus files rather than trusting it.
    [[nodiscard]] bool covers_source(const diag::SourceManager& sources) const;

private:
    friend class Lexer;

    diag::SourceId source_;
    std::vector<Token> tokens_;
    std::vector<Trivia> trivia_;
};

} // namespace stardata::lex
