// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/text/template.hpp"

#include <cctype>
#include <cstddef>
#include <string>
#include <utility>

#include "stardata/diag/codes.hpp"
#include "stardata/diag/diagnostic.hpp"
#include "stardata/lex/lexer.hpp"

namespace stardata::text {
namespace {

using diag::Code;
using diag::Diagnostic;
using diag::Span;

[[nodiscard]] bool is_identifier_start(char c) noexcept {
    return (std::isalpha(static_cast<unsigned char>(c)) != 0) || c == '_';
}

[[nodiscard]] bool is_identifier_char(char c) noexcept {
    return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_';
}

[[nodiscard]] bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// The template's text, with every byte's source offset beside it.
//
// One buffer across every literal of a §3.5.1 run, because one template may
// be written across several: tour.star's `conditional_demo` opens `[if` in
// one literal and closes `[end]` in a later one. The offset table is what
// keeps the spans honest across the gap -- byte i of the buffer knows the
// file offset it came from, so a diagnostic points at the author's bracket
// and not at an index into a string that exists only here.
struct Body {
    std::string text;
    std::vector<std::uint32_t> offsets;
    diag::SourceId source;

    // Where to point when there is no interior at all: an empty string is an
    // empty template, and a span of offset 0 would underline the top of the
    // file rather than the value.
    std::uint32_t fallback = 0;
    bool has_fallback = false;

    void append(std::string_view literal, std::uint32_t at) {
        if (!has_fallback) {
            fallback = at;
            has_fallback = true;
        }
        // `literal` includes its quotes; the interior is what a template is.
        if (literal.size() < 2) {
            return;
        }
        for (std::size_t i = 1; i + 1 < literal.size(); ++i) {
            text.push_back(literal[i]);
            offsets.push_back(at + static_cast<std::uint32_t>(i));
        }
    }

    [[nodiscard]] std::uint32_t offset_at(std::size_t index) const noexcept {
        if (offsets.empty()) {
            return fallback;
        }
        if (index >= offsets.size()) {
            return offsets.back() + 1;
        }
        return offsets[index];
    }

    // [from, to) as a source span. Across a literal boundary this covers the
    // quotes and whitespace between them, which is the range the author would
    // underline by hand.
    [[nodiscard]] Span span(std::size_t from, std::size_t to) const noexcept {
        const std::uint32_t start = offset_at(from);
        const std::uint32_t end = offset_at(to);
        return Span{source, start, end > start ? end - start : 0};
    }
};

// One escape is one character, whatever it is (§3.5). The template layer
// never asks which: the whole point of reading the source rather than the
// decoded value is that `\[` must not be a bracket, and skipping the pair
// is what makes that true. `\uXXXX` is six.
[[nodiscard]] std::size_t escape_length(const Body& body, std::size_t at) noexcept {
    if (at + 1 >= body.text.size()) {
        return 1;
    }
    if (body.text[at + 1] == 'u') {
        return at + 5 < body.text.size() ? 6 : body.text.size() - at;
    }
    return 2;
}

// --- expressions (§9.2) ------------------------------------------------

// A recursive-descent parser over one interpolation's contents.
//
// It is its own lexer rather than a reuse of `lex::lex`, because §9.2's
// grammar is not §3's. The clearest case is the dot: §3.3 makes
// `self.range` a single Identifier token and says the dot "has no built-in
// meaning at the lexical level", while §9.2's `Path ::= Slot ('.'
// Identifier)+` gives it one here. Reading identifiers without dots and
// handling '.' in the path rule follows §9.2 literally.
class ExprParser {
public:
    ExprParser(const Body& body, std::size_t from, std::size_t to) noexcept
        : body_(body), pos_(from), end_(to) {}

    // The whole range as one expression, or nothing when anything is left
    // over -- a half-written expression is not a smaller expression.
    [[nodiscard]] std::optional<Expr> parse_all() {
        std::optional<Expr> expr = parse_expr();
        skip_space();
        if (!expr || pos_ != end_) {
            return std::nullopt;
        }
        return expr;
    }

private:
    void skip_space() {
        while (pos_ < end_ && is_space(body_.text[pos_])) {
            ++pos_;
        }
    }

    [[nodiscard]] bool at(char c) const noexcept { return pos_ < end_ && body_.text[pos_] == c; }

    // Whether what is here could start an expression at all. `)` and `,` are
    // the two that matter: they end one.
    [[nodiscard]] bool begins_expr() const noexcept {
        if (pos_ >= end_) {
            return false;
        }
        const char c = body_.text[pos_];
        return is_identifier_start(c) || c == '-' || c == '\\' ||
               (std::isdigit(static_cast<unsigned char>(c)) != 0);
    }

    [[nodiscard]] std::optional<std::string> take_identifier(std::size_t& start) {
        if (pos_ >= end_ || !is_identifier_start(body_.text[pos_])) {
            return std::nullopt;
        }
        start = pos_;
        while (pos_ < end_ && is_identifier_char(body_.text[pos_])) {
            ++pos_;
        }
        return body_.text.substr(start, pos_ - start);
    }

    [[nodiscard]] std::optional<Expr> parse_number() {
        const std::size_t start = pos_;
        if (at('-')) {
            ++pos_;
        }
        std::size_t digits = 0;
        while (pos_ < end_ && (std::isdigit(static_cast<unsigned char>(body_.text[pos_])) != 0)) {
            ++pos_;
            ++digits;
        }
        if (digits == 0) {
            pos_ = start;
            return std::nullopt;
        }
        if (at('.') && pos_ + 1 < end_ &&
            (std::isdigit(static_cast<unsigned char>(body_.text[pos_ + 1])) != 0)) {
            ++pos_;
            while (pos_ < end_ &&
                   (std::isdigit(static_cast<unsigned char>(body_.text[pos_])) != 0)) {
                ++pos_;
            }
        }
        Expr expr;
        expr.kind = Expr::Kind::Number;
        expr.literal = body_.text.substr(start, pos_ - start);
        expr.span = body_.span(start, pos_);
        expr.name_span = expr.span;
        return expr;
    }

    // A quoted literal inside a template. Its own quotes are escaped in the
    // source (the template is itself inside a string), so what arrives here
    // is `\"...\"` and the escapes have not been decoded.
    [[nodiscard]] std::optional<Expr> parse_string() {
        const std::size_t start = pos_;
        if (!at('\\') || pos_ + 1 >= end_ || body_.text[pos_ + 1] != '"') {
            return std::nullopt;
        }
        pos_ += 2;
        while (pos_ < end_) {
            if (body_.text[pos_] == '\\' && pos_ + 1 < end_ && body_.text[pos_ + 1] == '"') {
                pos_ += 2;
                Expr expr;
                expr.kind = Expr::Kind::String;
                expr.literal = body_.text.substr(start, pos_ - start);
                expr.span = body_.span(start, pos_);
                expr.name_span = expr.span;
                return expr;
            }
            ++pos_;
        }
        pos_ = start;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Expr> parse_expr() {
        skip_space();
        if (pos_ >= end_) {
            return std::nullopt;
        }
        if (body_.text[pos_] == '-' ||
            (std::isdigit(static_cast<unsigned char>(body_.text[pos_])) != 0)) {
            return parse_number();
        }
        if (body_.text[pos_] == '\\') {
            return parse_string();
        }

        std::size_t start = 0;
        const std::optional<std::string> name = take_identifier(start);
        if (!name) {
            return std::nullopt;
        }

        Expr expr;
        expr.name = *name;
        expr.name_span = body_.span(start, pos_);

        // `Call ::= Identifier '(' ( Expr ( ',' Expr )* )? ')'`
        if (at('(')) {
            ++pos_;
            expr.kind = Expr::Kind::Call;
            skip_space();
            if (!at(')')) {
                while (true) {
                    std::optional<Expr> argument = parse_expr();
                    if (!argument) {
                        return std::nullopt;
                    }
                    expr.args.push_back(std::move(*argument));
                    skip_space();
                    if (at(',')) {
                        ++pos_;
                        continue;
                    }
                    break;
                }
            }
            if (!at(')')) {
                return std::nullopt;
            }
            ++pos_;
            expr.span = body_.span(start, pos_);
            return expr;
        }

        // `Path ::= Slot ( '.' Identifier )+`
        if (at('.')) {
            expr.kind = Expr::Kind::Path;
            while (at('.')) {
                ++pos_;
                std::size_t segment_start = 0;
                const std::optional<std::string> segment = take_identifier(segment_start);
                if (!segment) {
                    return std::nullopt;
                }
                expr.segments.push_back(*segment);
            }
            expr.span = body_.span(start, pos_);
            return expr;
        }

        // `Apply ::= Identifier Expr` (§9.2.1). One argument, never more:
        // "a function taking more or fewer than one argument MUST use the
        // parenthesised form", which is what keeps the sugar unambiguous
        // with one token of lookahead -- and one token is exactly what
        // `begins_expr` looks at. Without it, `the(noun)`'s inner `noun`
        // would try to apply itself to the ')' that follows it.
        const std::size_t after_name = pos_;
        skip_space();
        if (begins_expr()) {
            std::optional<Expr> argument = parse_expr();
            if (!argument) {
                return std::nullopt;
            }
            expr.kind = Expr::Kind::Apply;
            expr.args.push_back(std::move(*argument));
            expr.span = body_.span(start, pos_);
            return expr;
        }

        pos_ = after_name;
        expr.kind = Expr::Kind::Name;
        expr.span = expr.name_span;
        return expr;
    }

    const Body& body_;
    std::size_t pos_;
    std::size_t end_;
};

// --- fragments (§9.1) --------------------------------------------------

class Scanner {
public:
    Scanner(const Body& body, diag::DiagnosticSink& sink) noexcept : body_(body), sink_(sink) {}

    [[nodiscard]] Template run() {
        Template result;
        result.span = body_.span(0, body_.text.size());
        while (pos_ < body_.text.size()) {
            const char c = body_.text[pos_];
            if (c == '\\') {
                const std::size_t length = escape_length(body_, pos_);
                literal_.append(body_.text, pos_, length);
                pos_ += length;
                continue;
            }
            if (c == '[') {
                flush(result);
                scan_interpolation(result);
                continue;
            }
            if (c == ']') {
                flush(result);
                report(pos_, pos_ + 1, "this ']' closes nothing",
                       "brackets delimit a template's interpolations, so every ']' needs a '[' "
                       "before it; write \\] for a literal one (spec §9.1)");
                ++pos_;
                continue;
            }
            if (c == '@' && scan_style(result)) {
                continue;
            }
            literal_.push_back(c);
            ++pos_;
        }
        flush(result);
        return result;
    }

private:
    void report(std::size_t from, std::size_t to, std::string message, std::string note) {
        Diagnostic diagnostic(Code::TemplateBrackets, body_.span(from, to), std::move(message));
        diagnostic.with_note(std::move(note));
        sink_.report(std::move(diagnostic));
    }

    // Accumulated literal text becomes a fragment, escapes decoded. Decoding
    // happens here rather than byte by byte above so that the one decoder of
    // §3.5 is used -- adding a second would be how the two quietly come to
    // disagree about `\u`.
    void flush(Template& result) {
        if (!literal_.empty()) {
            Fragment fragment;
            fragment.kind = Fragment::Kind::Literal;
            fragment.text = lex::decode_string_escapes("\"" + literal_ + "\"");
            fragment.span = body_.span(literal_start_, pos_);
            result.fragments.push_back(std::move(fragment));
            literal_.clear();
        }
        // Set even when nothing was flushed, so the next run of literal text
        // starts where it actually starts rather than at whatever the last
        // non-empty flush left behind.
        literal_start_ = pos_;
    }

    // At '['. Everything up to the first unescaped ']' is the contents; a
    // nested '[' is left inside them, where the expression parser will
    // decline it rather than the scanner guessing which bracket was meant.
    void scan_interpolation(Template& result) {
        const std::size_t open = pos_;
        std::size_t at = pos_ + 1;
        while (at < body_.text.size() && body_.text[at] != ']') {
            at += body_.text[at] == '\\' ? escape_length(body_, at) : 1;
        }
        if (at >= body_.text.size()) {
            report(open, open + 1, "this '[' is never closed",
                   "an interpolation runs from '[' to the next ']'; write \\[ for a literal "
                   "bracket (spec §9.1)");
            // Consumed as literal text, so one unclosed bracket produces one
            // diagnostic rather than one per bracket after it.
            literal_start_ = open;
            literal_.append(body_.text, open, body_.text.size() - open);
            pos_ = body_.text.size();
            return;
        }

        Fragment fragment;
        fragment.span = body_.span(open, at + 1);

        const std::size_t from = open + 1;
        std::size_t start = from;
        std::size_t stop = at;
        while (start < stop && is_space(body_.text[start])) {
            ++start;
        }
        while (stop > start && is_space(body_.text[stop - 1])) {
            --stop;
        }
        const std::string_view word(body_.text.data() + start, stop - start);

        // §9.2.1: "`if`, `else` and `end` are reserved (§3.9) and are never
        // read as applications", so these three are decided before the
        // expression parser ever sees the contents.
        if (word == "else") {
            fragment.kind = Fragment::Kind::Else;
        } else if (word == "end") {
            fragment.kind = Fragment::Kind::End;
        } else if (word.size() > 2 && word.compare(0, 2, "if") == 0 && is_space(word[2])) {
            fragment.kind = Fragment::Kind::If;
            fragment.expr = ExprParser(body_, start + 2, stop).parse_all();
        } else {
            fragment.kind = Fragment::Kind::Interpolation;
            fragment.expr = ExprParser(body_, start, stop).parse_all();
        }

        result.fragments.push_back(std::move(fragment));
        pos_ = at + 1;
        literal_start_ = pos_;
    }

    // At '@'. `@style(id)` and `@endstyle` are the two directives §9.1
    // defines; anything else beginning with '@' is literal text, which is
    // what "literal text is everything not otherwise matched" means. No
    // diagnostic for a near miss: inventing one would make `@ 10 credits`
    // an error in a message.
    [[nodiscard]] bool scan_style(Template& result) {
        constexpr std::string_view kOpen = "@style";
        constexpr std::string_view kClose = "@endstyle";
        const std::string_view rest(body_.text.data() + pos_, body_.text.size() - pos_);

        if (rest.size() >= kClose.size() && rest.compare(0, kClose.size(), kClose) == 0 &&
            (rest.size() == kClose.size() || !is_identifier_char(rest[kClose.size()]))) {
            flush(result);
            Fragment fragment;
            fragment.kind = Fragment::Kind::StyleClose;
            fragment.span = body_.span(pos_, pos_ + kClose.size());
            result.fragments.push_back(std::move(fragment));
            pos_ += kClose.size();
            literal_start_ = pos_;
            return true;
        }

        if (rest.size() <= kOpen.size() || rest.compare(0, kOpen.size(), kOpen) != 0 ||
            rest[kOpen.size()] != '(') {
            return false;
        }
        std::size_t at = pos_ + kOpen.size() + 1;
        const std::size_t name_start = at;
        if (at >= body_.text.size() || !is_identifier_start(body_.text[at])) {
            return false;
        }
        while (at < body_.text.size() && is_identifier_char(body_.text[at])) {
            ++at;
        }
        if (at >= body_.text.size() || body_.text[at] != ')') {
            return false;
        }

        flush(result);
        Fragment fragment;
        fragment.kind = Fragment::Kind::StyleOpen;
        fragment.text = body_.text.substr(name_start, at - name_start);
        fragment.span = body_.span(pos_, at + 1);
        result.fragments.push_back(std::move(fragment));
        pos_ = at + 1;
        literal_start_ = pos_;
        return true;
    }

    const Body& body_;
    diag::DiagnosticSink& sink_;
    std::size_t pos_ = 0;
    std::size_t literal_start_ = 0;
    std::string literal_;
};

} // namespace

std::vector<const Fragment*> Template::style_directives() const {
    std::vector<const Fragment*> found;
    for (const Fragment& fragment : fragments) {
        if (fragment.kind == Fragment::Kind::StyleOpen) {
            found.push_back(&fragment);
        }
    }
    return found;
}

Template parse_template(const ast::Scalar& scalar, diag::DiagnosticSink& sink) {
    Body body;
    body.source = scalar.source();
    bool any = false;
    for (const cst::SyntaxToken& token : scalar.literals()) {
        if (token.kind() != cst::SyntaxKind::String) {
            continue;
        }
        any = true;
        body.append(token.text(), token.text_range().offset);
    }
    if (!any) {
        return Template{};
    }
    return Scanner(body, sink).run();
}

Template parse_template(std::string_view literal, Span at, diag::DiagnosticSink& sink) {
    Body body;
    body.source = at.source;
    body.append(literal, at.offset);
    return Scanner(body, sink).run();
}

NameLookup name_lookup(std::string_view written) {
    NameLookup lookup{std::string(written), false};
    if (!written.empty() && (std::isupper(static_cast<unsigned char>(written[0])) != 0)) {
        lookup.capitalises = true;
        lookup.name[0] =
            static_cast<char>(std::tolower(static_cast<unsigned char>(lookup.name[0])));
    }
    return lookup;
}

} // namespace stardata::text
