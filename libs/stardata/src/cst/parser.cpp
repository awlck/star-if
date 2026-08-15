// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/cst/parser.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "stardata/cst/builder.hpp"
#include "stardata/diag/codes.hpp"
#include "stardata/diag/diagnostic.hpp"
#include "stardata/lex/lexer.hpp"

namespace stardata::cst {

namespace {

using diag::Code;

// The same reporting helper the lexer uses: build a diagnostic, chain notes
// and fix-its onto it, and hand it to the sink when it goes out of scope.
class Reporter {
public:
    Reporter(diag::DiagnosticSink& sink, diag::Diagnostic diagnostic)
        : sink_(sink), diagnostic_(std::move(diagnostic)) {}
    Reporter(const Reporter&) = delete;
    Reporter& operator=(const Reporter&) = delete;
    ~Reporter() { sink_.report(std::move(diagnostic_)); }

    Reporter& with_note(std::string message, std::optional<diag::Span> span = std::nullopt) {
        diagnostic_.with_note(std::move(message), span);
        return *this;
    }
    Reporter& with_fix_it(diag::Span at, std::string replacement, std::string message) {
        diagnostic_.with_fix_it(at, std::move(replacement), std::move(message));
        return *this;
    }

private:
    diag::DiagnosticSink& sink_;
    diag::Diagnostic diagnostic_;
};

[[nodiscard]] std::string quoted_text(std::string_view text) {
    return "'" + std::string(text) + "'";
}

} // namespace

// ---------------------------------------------------------------------------
// Trivia attachment (backlog E3)
// ---------------------------------------------------------------------------
//
// The policy, decided once and written down here and in CONTRIBUTING.md,
// because ad-hoc rules in this corner are what make comments drift when a
// tool moves a statement.
//
//   1. TRAILING. A statement's trailing trivia runs from its last token to
//      and including the first line terminator. So `id = key  # why` keeps
//      its end-of-line comment, and the newline goes with it.
//
//   2. LEADING. What remains attaches to the statement that follows, so a
//      comment on its own line above a statement belongs to that statement
//      and travels with it.
//
//   3. DETACHED. Except that a blank line breaks the association: trivia up
//      to and including the last blank line belongs to the enclosing File or
//      Block, not to the next statement. Without this rule a file's header
//      banner would attach to whatever statement happened to come first, and
//      moving that statement would take the banner with it.
//
// Rule 3 is the only addition to the backlog's suggested policy, and it is
// what makes the policy match what an author sees: a blank line is how
// people already separate "this belongs to that" from "this stands alone".
//
// One convention follows from all three, and every parse function below
// obeys it: A NODE'S RANGE BEGINS AT ITS OWN FIRST TOKEN. Trivia sitting
// before a node belongs to the node's parent, never to the node itself. The
// single exception is `Statement`, which owns its leading and trailing
// trivia precisely so that it carries its comments when it moves.
//
// Without that rule `Block` would swallow the space in `exits = { ... }`,
// and replacing the block would delete a space nobody asked it to touch.

class Parser {
public:
    Parser(const diag::SourceManager& sources, const lex::TokenStream& tokens, GreenCache& cache,
           diag::DiagnosticSink& sink)
        : sources_(sources), tokens_(tokens), sink_(sink), builder_(cache),
          text_(sources.contents(tokens.source())) {}

    GreenNodePtr run() {
        builder_.start_node(SyntaxKind::File);
        parse_items(/*inside_block=*/false);
        flush_trivia(); // whatever precedes EndOfFile
        builder_.finish_node();
        return builder_.finish();
    }

private:
    // -----------------------------------------------------------------------
    // Token access
    // -----------------------------------------------------------------------

    [[nodiscard]] const lex::Token& token(std::size_t index) const {
        const std::size_t last = tokens_.size() - 1; // EndOfFile
        return tokens_[index < last ? index : last];
    }
    [[nodiscard]] lex::TokenKind kind_at(std::size_t offset = 0) const {
        return token(pos_ + offset).kind;
    }
    [[nodiscard]] std::string_view text_at(std::size_t offset = 0) const {
        return sources_.text(token(pos_ + offset).span);
    }
    [[nodiscard]] bool at_end() const { return kind_at() == lex::TokenKind::EndOfFile; }
    [[nodiscard]] bool at_punct(std::string_view what, std::size_t offset = 0) const {
        return kind_at(offset) == lex::TokenKind::Punctuation && text_at(offset) == what;
    }
    [[nodiscard]] bool at_angle(std::string_view what, std::size_t offset = 0) const {
        return kind_at(offset) == lex::TokenKind::Angle && text_at(offset) == what;
    }
    [[nodiscard]] diag::Span span_at(std::size_t offset = 0) const {
        return token(pos_ + offset).span;
    }

    // -----------------------------------------------------------------------
    // Trivia
    // -----------------------------------------------------------------------

    // The byte range of the trivia sitting before the current token.
    [[nodiscard]] std::uint32_t trivia_end() const { return token(pos_).span.offset; }

    [[nodiscard]] std::uint32_t trivia_start() const {
        const auto run = tokens_.preceding_trivia(pos_);
        return run.empty() ? trivia_end() : run.front().span.offset;
    }

    // Emits trivia covering [cursor_, limit), splitting a piece where the
    // limit falls inside one. Every byte is emitted exactly once, which is
    // what keeps the tree total.
    void emit_trivia_upto(std::uint32_t limit) {
        if (cursor_ == 0) {
            cursor_ = trivia_start();
        }
        for (const lex::Trivia& piece : tokens_.preceding_trivia(pos_)) {
            const std::uint32_t from = std::max(cursor_, piece.span.offset);
            const std::uint32_t to = std::min(limit, piece.span.end());
            if (to <= from) {
                continue;
            }
            builder_.token(from_trivia_kind(piece.kind),
                           text_.substr(from, static_cast<std::size_t>(to - from)));
            cursor_ = to;
        }
        cursor_ = std::max(cursor_, limit);
    }

    void flush_trivia() { emit_trivia_upto(trivia_end()); }

    // Rule 1: to and including the first line terminator.
    [[nodiscard]] std::uint32_t trailing_limit() const {
        const std::uint32_t start = std::max(cursor_, trivia_start());
        const std::uint32_t end = trivia_end();
        for (std::uint32_t at = start; at < end; ++at) {
            if (text_[at] == '\n') {
                return at + 1;
            }
        }
        return end;
    }

    // Rule 3: to and including the last blank line, if there is one.
    [[nodiscard]] std::uint32_t detached_limit() const {
        const std::uint32_t start = std::max(cursor_, trivia_start());
        const std::uint32_t end = trivia_end();
        std::uint32_t result = start;
        std::uint32_t line_start = start;
        for (std::uint32_t at = start; at < end; ++at) {
            if (text_[at] != '\n') {
                continue;
            }
            const std::string_view line =
                text_.substr(line_start, static_cast<std::size_t>(at - line_start));
            if (line.find_first_not_of(" \t\r") == std::string_view::npos) {
                result = at + 1; // a blank line; everything through it detaches
            }
            line_start = at + 1;
        }
        return result;
    }

    void emit_trailing_trivia() { emit_trivia_upto(trailing_limit()); }
    void emit_detached_trivia() { emit_trivia_upto(detached_limit()); }

    // -----------------------------------------------------------------------
    // Emission
    // -----------------------------------------------------------------------

    void emit_token() {
        flush_trivia();
        builder_.token(from_token_kind(kind_at()), text_at());
        ++pos_;
        cursor_ = 0; // recomputed lazily for the new token's run
    }

    void emit_token_as(SyntaxKind kind) {
        flush_trivia();
        builder_.token(kind, text_at());
        ++pos_;
        cursor_ = 0;
    }

    Reporter report(Code code, diag::Span at, std::string message) {
        return Reporter(sink_, diag::Diagnostic(code, at, std::move(message)));
    }

    // Consumes the current token inside an Error node. Always makes
    // progress, which is what keeps recovery from looping.
    void emit_error_token() {
        builder_.start_node(SyntaxKind::Error);
        emit_token();
        builder_.finish_node();
    }

    // -----------------------------------------------------------------------
    // Grammar (spec §4)
    // -----------------------------------------------------------------------

    [[nodiscard]] bool starts_scalar(std::size_t offset = 0) const {
        switch (kind_at(offset)) {
        case lex::TokenKind::Identifier:
        case lex::TokenKind::Integer:
        case lex::TokenKind::Decimal:
        case lex::TokenKind::String:
        case lex::TokenKind::LocKey:
            return true;
        default:
            return false;
        }
    }

    // A statement is a Key followed by an operator. `<` and `>` count: in
    // operator position they are comparisons (§4.2), which is exactly what
    // this position is.
    [[nodiscard]] bool looks_like_statement() const {
        const bool key =
            kind_at() == lex::TokenKind::Identifier || kind_at() == lex::TokenKind::String;
        if (!key) {
            return false;
        }
        return kind_at(1) == lex::TokenKind::Operator || kind_at(1) == lex::TokenKind::Angle;
    }

    // Parses statements and, inside a block, bare scalars. Records where each
    // shape was first seen so §5.2's mixed-block check can cite both.
    struct BlockShape {
        std::optional<diag::Span> first_statement;
        std::optional<diag::Span> first_scalar;
        std::size_t statements = 0;
        std::size_t scalars = 0;
    };

    void parse_items(bool inside_block, BlockShape* shape = nullptr) {
        while (!at_end()) {
            if (at_punct("}")) {
                if (inside_block) {
                    return; // the caller consumes it
                }
                emit_detached_trivia();
                report(Code::BraceUnbalanced, span_at(),
                       "this '}' closes a block that was never opened");
                emit_error_token();
                continue;
            }

            emit_detached_trivia();

            if (looks_like_statement()) {
                if (shape != nullptr) {
                    ++shape->statements;
                    if (!shape->first_statement) {
                        shape->first_statement = span_at();
                    }
                }
                parse_statement();
                continue;
            }

            if (inside_block && starts_scalar()) {
                if (shape != nullptr) {
                    ++shape->scalars;
                    if (!shape->first_scalar) {
                        shape->first_scalar = span_at();
                    }
                }
                parse_scalar();
                continue;
            }

            // Nothing the grammar allows here.
            report(Code::StrayToken, span_at(),
                   "I don't know what " + quoted_text(text_at()) + " is doing here")
                .with_note(inside_block
                               ? "a block holds either statements or bare values, and this is "
                                 "neither (spec §4, §5.2)"
                               : "a file is a sequence of statements, each a key, an operator "
                                 "and a value (spec §4)");
            emit_error_token();
        }
    }

    void parse_statement() {
        builder_.start_node(SyntaxKind::Statement);
        flush_trivia(); // the leading trivia belongs inside the statement

        parse_key();
        parse_operator();
        parse_value();

        emit_trailing_trivia();
        builder_.finish_node();
    }

    void parse_key() {
        flush_trivia();
        builder_.start_node(SyntaxKind::Key);
        emit_token(); // Identifier or String; looks_like_statement checked it
        builder_.finish_node();
    }

    void parse_operator() {
        if (kind_at() == lex::TokenKind::Operator || kind_at() == lex::TokenKind::Angle) {
            emit_token_as(SyntaxKind::Operator);
            return;
        }
        report(Code::StrayToken, span_at(), "I expected an operator after this key")
            .with_note("a statement is a key, an operator and a value (spec §4)");
    }

    void parse_value() {
        flush_trivia(); // belongs to the Statement, not to the Value
        builder_.start_node(SyntaxKind::Value);

        while (kind_at() == lex::TokenKind::Annotation) {
            parse_annotation();
        }

        if (at_punct("{")) {
            parse_block();
        } else if (kind_at() == lex::TokenKind::Identifier) {
            parse_identifier_value();
        } else if (starts_scalar()) {
            parse_scalar();
        } else {
            // The operator is there and the value is not. Point at the gap
            // rather than at whatever token happens to follow.
            report(Code::ValueMissing, diag::Span{tokens_.source(), trivia_start(), 0},
                   "this statement has an operator but no value after it")
                .with_note("a statement is a key, an operator and a value (spec §4)");
        }

        builder_.finish_node();
    }

    // An identifier in value position is a Scalar, unless a `<` follows
    // (a TypeExpr, §4.2) or a `(` does (a Call, §4.3). §4.2 forbids deciding
    // this by whitespace, and nothing here looks at any: the decision is the
    // kind of the very next token, spaced or not.
    void parse_identifier_value() {
        flush_trivia();
        const std::size_t start = builder_.checkpoint();
        emit_token();

        if (at_angle("<")) {
            builder_.start_node_at(start, SyntaxKind::TypeExpr);
            parse_type_arguments();
            builder_.finish_node();
        } else if (at_punct("(")) {
            builder_.start_node_at(start, SyntaxKind::Call);
            parse_call_arguments();
            builder_.finish_node();
        } else {
            builder_.start_node_at(start, SyntaxKind::Scalar);
            builder_.finish_node();
        }
    }

    // At the '<'. Consumes through the matching '>'.
    void parse_type_arguments() {
        emit_token(); // '<'
        while (true) {
            if (kind_at() != lex::TokenKind::Identifier) {
                break;
            }
            flush_trivia();
            const std::size_t start = builder_.checkpoint();
            emit_token();
            if (at_angle("<")) {
                builder_.start_node_at(start, SyntaxKind::TypeExpr);
                parse_type_arguments();
                builder_.finish_node();
            }
            if (at_punct(",")) {
                emit_token();
                continue;
            }
            break;
        }
        if (at_angle(">")) {
            emit_token();
            return;
        }
        // A `>=` here would have lexed as one operator token (§3.6 matches
        // the longer form first), so `list<int>= 1` cannot close its type
        // argument list. Whitespace is insignificant (§3.1), so the lexer
        // could not have known; the grammar reports it instead.
        report(Code::StrayToken, span_at(), "I expected '>' to close this type argument list")
            .with_note("spec §4.2: a type expression is a name, '<', its arguments, and '>'");
    }

    // At the '('. Consumes through the matching ')'.
    void parse_call_arguments() {
        emit_token(); // '('
        while (!at_punct(")") && !at_end()) {
            if (kind_at() == lex::TokenKind::Identifier) {
                flush_trivia();
                const std::size_t start = builder_.checkpoint();
                emit_token();
                if (at_punct("(")) {
                    builder_.start_node_at(start, SyntaxKind::Call);
                    parse_call_arguments();
                    builder_.finish_node();
                } else {
                    builder_.start_node_at(start, SyntaxKind::Scalar);
                    builder_.finish_node();
                }
            } else if (starts_scalar()) {
                parse_scalar();
            } else {
                report(Code::StrayToken, span_at(),
                       quoted_text(text_at()) + " is not something a call can take as an argument")
                    .with_note("spec §4.3: an argument is another call or a scalar");
                emit_error_token();
                continue;
            }

            if (at_punct(",")) {
                emit_token();
                continue;
            }
            break;
        }
        if (at_punct(")")) {
            emit_token();
            return;
        }
        report(Code::StrayToken, span_at(), "I expected ')' to close this call")
            .with_note("spec §4.3: a call is a name, '(', its arguments, and ')'");
    }

    // One scalar. Adjacent string literals form a single Scalar node holding
    // every literal, so §3.5.1's split points survive into the tree and E5
    // reproduces the author's line breaks.
    void parse_scalar() {
        flush_trivia();
        builder_.start_node(SyntaxKind::Scalar);
        if (kind_at() == lex::TokenKind::String) {
            while (kind_at() == lex::TokenKind::String) {
                emit_token();
            }
        } else {
            emit_token();
        }
        builder_.finish_node();
    }

    void parse_annotation() {
        flush_trivia();
        builder_.start_node(SyntaxKind::Annotation);
        emit_token(); // '@name'
        if (at_punct("(")) {
            emit_token();
            while (!at_punct(")") && !at_end()) {
                if (kind_at() == lex::TokenKind::Identifier ||
                    kind_at() == lex::TokenKind::Integer) {
                    emit_token();
                } else if (at_punct(",")) {
                    emit_token();
                } else {
                    report(Code::StrayToken, span_at(),
                           quoted_text(text_at()) + " is not a valid annotation argument")
                        .with_note("spec §3.8: an annotation argument is a name or an integer");
                    emit_error_token();
                }
            }
            if (at_punct(")")) {
                emit_token();
            } else {
                report(Code::StrayToken, span_at(),
                       "I expected ')' to close this annotation's arguments");
            }
        }
        builder_.finish_node();
    }

    void parse_block() {
        flush_trivia();
        const diag::Span open = span_at();
        builder_.start_node(SyntaxKind::Block);
        emit_token(); // '{'

        BlockShape shape;
        parse_items(/*inside_block=*/true, &shape);

        // §5.2: a block is statements or bare values, never both. Reported
        // once for the whole block, naming the minority form, because the
        // usual cause is a single missed '='.
        if (shape.statements > 0 && shape.scalars > 0) {
            report_mixed_block(shape);
        }

        if (at_punct("}")) {
            emit_trivia_upto(trivia_end());
            emit_token();
        } else {
            report(Code::BraceUnbalanced, open, "this block is never closed")
                .with_note("the file ends with the block still open (spec §4)")
                .with_fix_it(diag::Span{tokens_.source(), trivia_end(), 0}, "}",
                             "add the closing '}'");
            flush_trivia();
        }
        builder_.finish_node();
    }

    void report_mixed_block(const BlockShape& shape) {
        const bool scalars_are_minority = shape.scalars <= shape.statements;
        const diag::Span minority =
            scalars_are_minority ? *shape.first_scalar : *shape.first_statement;
        const diag::Span majority =
            scalars_are_minority ? *shape.first_statement : *shape.first_scalar;

        Reporter reporter =
            report(Code::BlockMixed, minority,
                   scalars_are_minority
                       ? "this block holds statements, and " +
                             quoted_text(sources_.text(minority)) + " is a bare value among them"
                       : "this block holds bare values, and " +
                             quoted_text(sources_.text(minority)) + " is a statement among them");
        reporter.with_note("a block is either a list of values or a set of statements, never "
                           "both (spec §5.2)",
                           majority);
        if (scalars_are_minority) {
            reporter.with_fix_it(diag::Span{minority.source, minority.end(), 0}, " = ",
                                 "if a '=' went missing, this is where it belongs");
        }
    }

    const diag::SourceManager& sources_;
    const lex::TokenStream& tokens_;
    diag::DiagnosticSink& sink_;
    GreenBuilder builder_;
    std::string_view text_;
    std::size_t pos_ = 0;
    std::uint32_t cursor_ = 0; // bytes of the current trivia run already emitted
};

GreenNodePtr parse(const diag::SourceManager& sources, const lex::TokenStream& tokens,
                   GreenCache& cache, diag::DiagnosticSink& sink) {
    return Parser(sources, tokens, cache, sink).run();
}

GreenNodePtr parse(const diag::SourceManager& sources, diag::SourceId source, GreenCache& cache,
                   diag::DiagnosticSink& sink) {
    const lex::TokenStream tokens = lex::lex(sources, source, sink);
    return parse(sources, tokens, cache, sink);
}

SyntaxNode parse_to_syntax(const diag::SourceManager& sources, diag::SourceId source,
                           GreenCache& cache, diag::DiagnosticSink& sink) {
    return SyntaxNode::root(parse(sources, source, cache, sink));
}

} // namespace stardata::cst
