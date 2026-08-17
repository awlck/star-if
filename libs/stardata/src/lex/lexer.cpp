// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/lex/lexer.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "stardata/diag/codes.hpp"
#include "stardata/diag/diagnostic.hpp"

namespace stardata::lex {

namespace {

using diag::Code;

// -------------------------------------------------------------------------
// Character classes (spec §3). All are ASCII-only by construction: §3.3
// admits no non-ASCII identifier character, and §3.1 admits no non-ASCII
// whitespace, so a byte >= 0x80 outside a string or comment is always an
// error of one kind or another.
// -------------------------------------------------------------------------

[[nodiscard]] bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

[[nodiscard]] bool is_hex_digit(char c) noexcept {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// A `\uXXXX` escape's four digits (§3.5's HexQuad), all present and all hex.
[[nodiscard]] bool is_hex_quad(std::string_view text) noexcept {
    if (text.size() != 4) {
        return false;
    }
    for (char c : text) {
        if (!is_hex_digit(c)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_ident_start(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

[[nodiscard]] bool is_ident_continue(char c) noexcept {
    return is_ident_start(c) || is_digit(c) || c == '.';
}

// §3.1 exactly: tab, LF, CR, space. Nothing else, and in particular no
// non-ASCII whitespace -- that is a diagnostic, not a token separator.
[[nodiscard]] bool is_ascii_whitespace(char c) noexcept {
    return c == '\t' || c == '\n' || c == '\r' || c == ' ';
}

// §3.9. `true` and `false` are here too: reserved, and additionally
// diagnosed on sight because unlike the rest they are valid nowhere.
constexpr std::array<std::string_view, 13> kReservedWords{"yes", "no",   "inherit", "none", "all",
                                                          "and", "or",   "not",     "if",   "else",
                                                          "end", "true", "false"};

// -------------------------------------------------------------------------
// UTF-8
// -------------------------------------------------------------------------

struct Utf8Decoded {
    char32_t code_point = 0;
    std::uint32_t length = 1; // bytes consumed; 1 for an invalid sequence
    bool valid = false;
};

// Strict decode: rejects continuation bytes in lead position, truncated
// sequences, overlong encodings, surrogate halves and anything above
// U+10FFFF. An invalid sequence reports length 1 so a caller advancing by
// `length` always makes progress.
[[nodiscard]] Utf8Decoded decode_utf8(std::string_view text, std::size_t index) noexcept {
    const auto lead = static_cast<unsigned char>(text[index]);
    if (lead < 0x80) {
        return {lead, 1, true};
    }

    std::uint32_t needed = 0;
    char32_t code_point = 0;
    char32_t smallest = 0;
    if ((lead & 0xE0) == 0xC0) {
        needed = 2;
        code_point = lead & 0x1FU;
        smallest = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
        needed = 3;
        code_point = lead & 0x0FU;
        smallest = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
        needed = 4;
        code_point = lead & 0x07U;
        smallest = 0x10000;
    } else {
        return {};
    }

    if (text.size() - index < needed) {
        return {};
    }
    for (std::uint32_t k = 1; k < needed; ++k) {
        const auto continuation = static_cast<unsigned char>(text[index + k]);
        if ((continuation & 0xC0) != 0x80) {
            return {};
        }
        code_point = (code_point << 6) | (continuation & 0x3FU);
    }
    if (code_point < smallest || code_point > 0x10FFFF ||
        (code_point >= 0xD800 && code_point <= 0xDFFF)) {
        return {};
    }
    return {code_point, needed, true};
}

// §3.1: "Other Unicode whitespace (U+00A0, U+2028, …) MUST be rejected with
// a diagnostic suggesting the ASCII equivalent." Returns that equivalent, or
// nullptr when the code point is not whitespace-like at all. The zero-width
// characters map to the empty string -- the suggestion is to delete them,
// since they are invisible padding rather than a mistyped space.
[[nodiscard]] const char* ascii_equivalent(char32_t code_point) noexcept {
    switch (code_point) {
    case 0x0085: // NEXT LINE
    case 0x00A0: // NO-BREAK SPACE
    case 0x1680: // OGHAM SPACE MARK
    case 0x202F: // NARROW NO-BREAK SPACE
    case 0x205F: // MEDIUM MATHEMATICAL SPACE
    case 0x3000: // IDEOGRAPHIC SPACE
        return " ";
    case 0x2028: // LINE SEPARATOR
    case 0x2029: // PARAGRAPH SEPARATOR
        return "\n";
    case 0x200B: // ZERO WIDTH SPACE
    case 0x200C: // ZERO WIDTH NON-JOINER
    case 0x200D: // ZERO WIDTH JOINER
    case 0x2060: // WORD JOINER
    case 0xFEFF: // ZERO WIDTH NO-BREAK SPACE (a BOM anywhere but the start)
        return "";
    default:
        // U+2000..U+200A: EN QUAD through HAIR SPACE.
        return (code_point >= 0x2000 && code_point <= 0x200A) ? " " : nullptr;
    }
}

[[nodiscard]] std::string formatted(const char* format, unsigned value) {
    std::array<char, 16> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), format, value);
    return written > 0 ? std::string(buffer.data(), static_cast<std::size_t>(written))
                       : std::string("?");
}

[[nodiscard]] std::string code_point_name(char32_t code_point) {
    return formatted("U+%04X", static_cast<unsigned>(code_point));
}

// A raw byte, which is what a malformed sequence has rather than a code
// point. Spelling it "U+00FF" would claim the file decoded when it did not.
[[nodiscard]] std::string byte_name(unsigned char value) {
    return formatted("0x%02X", value);
}

// The §15 sequences that are reserved rather than merely unknown. Reported
// with a note, so that an author who writes `hp *= 2` learns the operator is
// coming rather than that `*` is gibberish.
//
// `=>` is absent on purpose: it lexes as `=` followed by a `>`, and since
// §3.1 makes whitespace insignificant, `a => b` and `a = > b` are the same
// token stream. Distinguishing them would mean deciding on spacing, which
// §4.2 explicitly forbids for `>`. The grammar rejects it instead, when the
// parser finds an angle where a value should be.
[[nodiscard]] std::string_view reserved_operator(std::string_view text, std::size_t index) {
    const auto pair = text.substr(index, 2);
    if (pair.size() < 2) {
        return {};
    }
    if (pair == "*=" || pair == "/=" || pair == "::" || pair == "->") {
        return pair;
    }
    return {};
}

[[nodiscard]] std::string quoted_text(std::string_view text) {
    return "'" + std::string(text) + "'";
}

// House style for diagnostic prose, so that the hundreds still to be written
// in workstreams E and F read as one voice rather than as whoever wrote them:
//
//   message  What happened, in the compiler's own voice, addressed to the
//            author. First person is deliberate -- "I don't recognise the
//            escape '\q'" is a colleague reporting a difficulty; "invalid
//            escape sequence" is a machine filing a defect. One line: it has
//            to fit the single-line machine rendering too.
//   note     Why the rule exists, or what the rule actually is, ending in
//            the section that says so. This is where an author who wants to
//            understand rather than just comply is served.
//   fix-it   The mechanical edit, imperative and boring. Never a joke: it is
//            the one part a tool applies without a human reading it.
//
// Warmth is not the same as whimsy. A line an author reads once is allowed
// to be funny; a line they read on every build had better be useful first.
// Where the two are available at once -- 'true' being reserved for no reason
// except to give this very message -- take both.
//
// Numbers inside prose read badly as digits ("has 1 fractional digits"), so
// small counts are spelled.
[[nodiscard]] std::string spelled(std::size_t count) {
    static constexpr std::string_view kWords[] = {"no",   "one", "two",   "three", "four",
                                                  "five", "six", "seven", "eight", "nine"};
    return count < std::size(kWords) ? std::string(kWords[count]) : std::to_string(count);
}

// Encodes one scalar value as UTF-8. Used when decoding a `\uXXXX` escape;
// surrogate halves have already been diagnosed by then and are encoded
// as-written rather than replaced, since the token text is the truth.
void append_utf8(char32_t code_point, std::string& out) {
    if (code_point < 0x80) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

// Builds a diagnostic and hands it to the sink when it goes out of scope, so
// that a call site can chain notes and fix-its straight onto report(...)
// without the lexer having to reach back into the sink for the diagnostic it
// just pushed -- which would be wrong anyway, since a sink at its limit
// stores nothing.
class Reporter {
public:
    Reporter(diag::DiagnosticSink& sink, diag::Diagnostic diagnostic)
        : sink_(sink), diagnostic_(std::move(diagnostic)) {}
    Reporter(const Reporter&) = delete;
    Reporter& operator=(const Reporter&) = delete;
    ~Reporter() { sink_.report(std::move(diagnostic_)); }

    Reporter& with_note(std::string message) {
        diagnostic_.with_note(std::move(message));
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

} // namespace

// -------------------------------------------------------------------------
// The lexer proper
// -------------------------------------------------------------------------

class Lexer {
public:
    Lexer(const diag::SourceManager& sources, diag::SourceId source, diag::DiagnosticSink& sink)
        : sink_(sink), text_(sources.contents(source)), stream_(source) {}

    TokenStream run() {
        // One pass up front, so that malformed bytes are reported once with
        // an accurate span rather than once per token that trips over them,
        // and so the token loop below can decode leniently and never crash.
        validate_utf8();

        // §2.2: a leading BOM is accepted, preserved and not treated as
        // content. Anywhere else U+FEFF is zero-width padding, handled as
        // non-ASCII whitespace with the rest.
        if (text_.starts_with("\xEF\xBB\xBF")) {
            push_trivia(TriviaKind::ByteOrderMark, 0, 3);
            pos_ = 3;
        }

        while (pos_ < text_.size()) {
            if (lex_trivia()) {
                continue;
            }
            lex_token();
        }
        push_token(TokenKind::EndOfFile, pos_, 0);
        return std::move(stream_);
    }

private:
    // ---------------------------------------------------------------------
    // Emission
    // ---------------------------------------------------------------------

    [[nodiscard]] diag::Span span(std::size_t offset, std::size_t length) const noexcept {
        return diag::Span{stream_.source(), static_cast<std::uint32_t>(offset),
                          static_cast<std::uint32_t>(length)};
    }

    // std::string_view::substr throws when the offset is past the end, which
    // a lexer looking ahead at the last byte of a file does constantly.
    [[nodiscard]] std::string_view slice(std::size_t offset, std::size_t length) const noexcept {
        if (offset >= text_.size()) {
            return {};
        }
        return text_.substr(offset, length);
    }

    void push_trivia(TriviaKind kind, std::size_t offset, std::size_t length) {
        stream_.trivia_.push_back(Trivia{kind, span(offset, length)});
    }

    void push_token(TokenKind kind, std::size_t offset, std::size_t length) {
        Token token;
        token.kind = kind;
        token.span = span(offset, length);
        token.trivia_first = trivia_consumed_;
        token.trivia_count = static_cast<std::uint32_t>(stream_.trivia_.size()) - trivia_consumed_;
        trivia_consumed_ = static_cast<std::uint32_t>(stream_.trivia_.size());
        stream_.tokens_.push_back(token);
    }

    // Not [[nodiscard]]: reporting is the point, and most call sites want
    // neither a note nor a fix-it.
    Reporter report(Code code, diag::Span at, std::string message) {
        return Reporter(sink_, diag::Diagnostic(code, at, std::move(message)));
    }

    // ---------------------------------------------------------------------
    // Whole-file UTF-8 validation (spec §2.1)
    // ---------------------------------------------------------------------

    // Runs before tokenizing, so its diagnostics precede the rest regardless
    // of where in the file they occur. A driver that presents diagnostics in
    // source order sorts them; nothing here depends on the order.
    void validate_utf8() {
        std::size_t i = 0;
        while (i < text_.size()) {
            if (static_cast<unsigned char>(text_[i]) < 0x80) {
                ++i;
                continue;
            }
            const Utf8Decoded decoded = decode_utf8(text_, i);
            if (decoded.valid) {
                i += decoded.length;
                continue;
            }
            // Coalesce the whole malformed run into one diagnostic. A file
            // that is actually a JPEG should produce one complaint, not one
            // per byte.
            const std::size_t start = i;
            do {
                ++i;
            } while (i < text_.size() && static_cast<unsigned char>(text_[i]) >= 0x80 &&
                     !decode_utf8(text_, i).valid);
            report(Code::Utf8Invalid, span(start, i - start),
                   "I can't read this as UTF-8: " +
                       (i - start == 1
                            ? "the byte " + byte_name(static_cast<unsigned char>(text_[start])) +
                                  " here begins no character"
                            : spelled(i - start) + " bytes here, starting with " +
                                  byte_name(static_cast<unsigned char>(text_[start])) +
                                  ", spell no character"))
                .with_note("every source file is UTF-8; if this one came from another editor, "
                           "check which encoding it was saved in (spec §2.1)");
        }
    }

    // ---------------------------------------------------------------------
    // Trivia (spec §3.1, §3.2)
    // ---------------------------------------------------------------------

    // Consumes one piece of trivia at pos_ and returns true, or returns
    // false with pos_ unchanged when a token starts here.
    bool lex_trivia() {
        const char c = text_[pos_];

        if (is_ascii_whitespace(c)) {
            const std::size_t start = pos_;
            while (pos_ < text_.size() && is_ascii_whitespace(text_[pos_])) {
                ++pos_;
            }
            // One piece per contiguous run. Splitting a run at line
            // boundaries is E3's decision, not the lexer's -- the spans are
            // all here either way.
            push_trivia(TriviaKind::Whitespace, start, pos_ - start);
            return true;
        }

        if (c == '#') {
            const std::size_t start = pos_;
            // §3.2: up to but excluding the next line terminator. Stopping
            // at CR as well as LF keeps the CR of a CRLF pair out of the
            // comment, so a CRLF file's comment trivia and an LF file's are
            // the same text.
            while (pos_ < text_.size() && text_[pos_] != '\n' && text_[pos_] != '\r') {
                ++pos_;
            }
            push_trivia(TriviaKind::Comment, start, pos_ - start);
            return true;
        }

        if (static_cast<unsigned char>(c) >= 0x80) {
            const Utf8Decoded decoded = decode_utf8(text_, pos_);
            if (decoded.valid) {
                if (const char* replacement = ascii_equivalent(decoded.code_point)) {
                    lex_unicode_whitespace(decoded, replacement);
                    return true;
                }
            }
        }
        return false;
    }

    void lex_unicode_whitespace(const Utf8Decoded& decoded, std::string_view replacement) {
        const std::size_t start = pos_;
        pos_ += decoded.length;
        const diag::Span at = span(start, decoded.length);
        const std::string name = code_point_name(decoded.code_point);
        // The complaint is always the same one: it passes for whitespace
        // without being any. Which way it deceives depends on the character.
        std::string message;
        if (replacement.empty()) {
            message = name + " is invisible here, and it is not a space";
        } else if (replacement == "\n") {
            message = name + " looks like a line break, but it is not one";
        } else {
            message = name + " looks like a space, but it is not one";
        }
        Reporter diagnostic = report(Code::UnicodeWhitespace, at, std::move(message));
        diagnostic.with_note("only tab, newline, carriage return and the plain space separate "
                             "tokens; a lookalike almost always arrives by copy-and-paste "
                             "(spec §3.1)");
        if (replacement.empty()) {
            diagnostic.with_fix_it(at, "", "delete the zero-width character");
        } else {
            diagnostic.with_fix_it(at, std::string(replacement),
                                   replacement == "\n" ? "use a plain newline"
                                                       : "use a plain space");
        }
        // Recovered as whitespace: it is what the author meant, and it keeps
        // the rest of the file lexing as they expect.
        push_trivia(TriviaKind::Whitespace, start, decoded.length);
    }

    // ---------------------------------------------------------------------
    // Tokens
    // ---------------------------------------------------------------------

    void lex_token() {
        const char c = text_[pos_];

        if (c == '"') {
            lex_string();
            return;
        }
        if (c == '$') {
            lex_sigil(TokenKind::LocKey,
                      "a localisation key is '$' followed straight away by a name, as in "
                      "'$cell_description' (spec §3.5)");
            return;
        }
        if (c == '@') {
            lex_sigil(TokenKind::Annotation,
                      "an annotation is '@' followed straight away by a name, as in '@debug' "
                      "(spec §3.8)");
            return;
        }
        if (lex_operator()) {
            return;
        }
        if (c == '{' || c == '}' || c == '(' || c == ')' || c == ',') {
            push_token(TokenKind::Punctuation, pos_, 1);
            ++pos_;
            return;
        }
        if (c == '<' || c == '>') {
            // §4.2: ambiguous by design. The parser resolves it by position.
            push_token(TokenKind::Angle, pos_, 1);
            ++pos_;
            return;
        }
        if (c == '[' || c == ']') {
            lex_bracket();
            return;
        }
        if (is_digit(c) || (c == '-' && pos_ + 1 < text_.size() && is_digit(text_[pos_ + 1]))) {
            lex_number();
            return;
        }
        if (c == '.' && pos_ + 1 < text_.size() && is_digit(text_[pos_ + 1])) {
            lex_leading_dot_decimal();
            return;
        }
        if (is_ident_start(c)) {
            lex_identifier();
            return;
        }
        lex_bad_character();
    }

    // §3.5. The span includes both quotes; escape validation happens here so
    // that a later pass decoding the literal never has to re-diagnose.
    void lex_string() {
        const std::size_t start = pos_;
        ++pos_; // opening quote
        bool closed = false;
        bool cut_short = false;

        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == '"') {
                ++pos_;
                closed = true;
                break;
            }
            if (c == '\n' || c == '\r') {
                // §3.5 / Appendix A1: end the token at the line break rather
                // than swallowing the rest of the file. The terminator
                // itself stays out of the token and becomes whitespace.
                report(Code::StringMultiline, span(start, pos_ - start),
                       "this string opens here, but the line ends before it closes")
                    .with_note("no literal spans a line. Write long text as one literal per "
                               "line: literals that sit next to each other join into a single "
                               "string, with nothing inserted between them (spec §3.5.1)");
                cut_short = true;
                break;
            }
            if (c == '\\') {
                lex_escape();
                continue;
            }
            ++pos_;
        }

        if (!closed && !cut_short) {
            report(Code::StringUnterminated, span(start, pos_ - start),
                   "this string opens here, and then the file simply ends")
                .with_fix_it(span(pos_, 0), "\"", "add the closing quote");
        }
        push_token(TokenKind::String, start, pos_ - start);
    }

    // At a backslash inside a string literal. Advances past the escape,
    // reporting E-STR-ESCAPE for anything §3.5 does not define.
    void lex_escape() {
        const std::size_t start = pos_;
        if (pos_ + 1 >= text_.size()) {
            report(Code::StringEscape, span(start, 1),
                   "this backslash is the last thing in the file, so there is nothing left for "
                   "it to escape");
            pos_ = text_.size();
            return;
        }

        const char escaped = text_[pos_ + 1];
        if (escaped == '\n' || escaped == '\r') {
            // Consume only the backslash: the line terminator still belongs
            // to the multiline check, which gives the better diagnostic.
            report(Code::StringEscape, span(start, 1),
                   "this backslash sits at the end of the line, so there is nothing for it to "
                   "escape");
            ++pos_;
            return;
        }

        switch (escaped) {
        case '"':
        case '\\':
        case 'n':
        case 't':
        case '[':
        case ']':
        case '$':
        case '@':
            pos_ += 2;
            return;
        case 'u':
            lex_unicode_escape();
            return;
        default:
            break;
        }

        const std::size_t length =
            1 + static_cast<std::size_t>(decode_utf8(text_, pos_ + 1).length);
        report(Code::StringEscape, span(start, length),
               "I don't recognise the escape " + quoted_text(slice(start, length)))
            .with_note(
                "the whole set is \\\" \\\\ \\n \\t \\[ \\] \\$ \\@ and \\uXXXX. It is closed on "
                "purpose, so that a new escape can be added later without changing what "
                "files written today mean (spec §3.5)")
            .with_fix_it(span(start, 1), "\\\\", "if you meant a backslash of your own, double it");
        pos_ += length;
    }

    // At the backslash of a `\uXXXX` escape.
    void lex_unicode_escape() {
        const std::size_t start = pos_;
        const std::string_view digits = slice(start + 2, 4);
        if (!is_hex_quad(digits)) {
            report(Code::StringEscape, span(start, 2),
                   "'\\u' wants exactly four hexadecimal digits after it");
            pos_ += 2;
            return;
        }

        std::uint32_t value = 0;
        std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
        if (value >= 0xD800 && value <= 0xDFFF) {
            report(Code::StringEscape, span(start, 6),
                   "'\\u" + std::string(digits) + "' is half of a character, not a character")
                .with_note("write the character itself instead -- the file is UTF-8, so it can "
                           "hold it directly, and it will be far easier to read next year "
                           "(spec §3.5)");
        }
        pos_ += 6;
    }

    // `$name` and `@name`: one sigil, one identifier, no space between.
    // `expectation` becomes the note describing the form that was wanted.
    void lex_sigil(TokenKind kind, std::string_view expectation) {
        const std::size_t start = pos_;
        if (pos_ + 1 < text_.size() && is_ident_start(text_[pos_ + 1])) {
            pos_ += 2;
            while (pos_ < text_.size() && is_ident_continue(text_[pos_])) {
                ++pos_;
            }
            push_token(kind, start, pos_ - start);
            return;
        }
        report(Code::BadChar, span(start, 1),
               quoted_text(slice(start, 1)) + " on its own does not name anything")
            .with_note(std::string(expectation));
        ++pos_;
        push_token(TokenKind::Error, start, 1);
    }

    // §3.6, longest match first: `>=` is one operator, never `>` then `=`.
    // A bare `<` or `>` is not handled here -- it is an Angle (§4.2).
    bool lex_operator() {
        static constexpr std::array<std::string_view, 6> kTwoCharOperators{
            "==", "!=", "<=", ">=", "+=", "-="};
        const std::string_view pair = slice(pos_, 2);

        // `?=` is still matched here, as one token, although §3.6 no longer
        // lists it. That is §15's requirement rather than an oversight: the
        // operator appeared in published drafts, so a file written against
        // one is owed "this was removed, write `=`" instead of "`?` begins no
        // token", which is what falling through to lex_bad_char would give.
        //
        // An Operator token is pushed despite the error, so the statement
        // still parses as `Key Op Value` and the author gets this one
        // diagnostic rather than it plus a cascade of structural ones.
        if (pair == "?=") {
            report(Code::OpRemoved, span(pos_, 2), "'?=' was removed from the format")
                .with_note("it meant \"bind only if this key is unset\", which could not be read "
                           "locally: whether it bound depended on every other declaration of the "
                           "key, at any inheritance level, in any file (spec §6.3.1)")
                .with_note("the case it existed for is already covered -- a project loads after "
                           "every library, so a library's plain `=` is always pre-emptable "
                           "(spec §13.2)")
                .with_fix_it(span(pos_, 2), "=", "bind it plainly with '='");
            push_token(TokenKind::Operator, pos_, 2);
            pos_ += 2;
            return true;
        }

        for (std::string_view op : kTwoCharOperators) {
            if (pair == op) {
                push_token(TokenKind::Operator, pos_, 2);
                pos_ += 2;
                return true;
            }
        }
        if (text_[pos_] == '=') {
            push_token(TokenKind::Operator, pos_, 1);
            ++pos_;
            return true;
        }
        return false;
    }

    void lex_bracket() {
        const std::size_t start = pos_;
        ++pos_;
        report(Code::BracketOutside, span(start, 1),
               quoted_text(slice(start, 1)) + " belongs inside a string literal, not out here")
            .with_note("'[' and ']' are held permanently for the template language and for "
                       "grammar lines, so they never come to mean anything on their own "
                       "(spec §3.7, §15)");
        push_token(TokenKind::Error, start, 1);
    }

    // §3.4. Both malformed forms still emit a token: an author who wrote
    // `1.5` wants the rest of the statement parsed, not discarded.
    void lex_number() {
        const std::size_t start = pos_;
        if (text_[pos_] == '-') {
            ++pos_;
        }
        while (pos_ < text_.size() && is_digit(text_[pos_])) {
            ++pos_;
        }
        const std::size_t integer_end = pos_;

        if (pos_ < text_.size() && text_[pos_] == '.') {
            if (pos_ + 1 < text_.size() && is_digit(text_[pos_ + 1])) {
                ++pos_;
                const std::size_t fraction_start = pos_;
                while (pos_ < text_.size() && is_digit(text_[pos_])) {
                    ++pos_;
                }
                check_decimal_precision(start, pos_ - fraction_start);
                push_token(TokenKind::Decimal, start, pos_ - start);
                return;
            }
            ++pos_; // the trailing '.'
            const std::string_view literal = slice(start, pos_ - start);
            const std::string mantissa(literal.substr(0, literal.size() - 1));
            report(Code::NumberTrailingDot, span(start, pos_ - start),
                   quoted_text(literal) + " trails off after the dot -- did you mean " +
                       quoted_text(mantissa) + " or " + quoted_text(mantissa + ".000") + "?")
                .with_fix_it(span(start, pos_ - start), mantissa, "drop the trailing '.'");
            push_token(TokenKind::Decimal, start, pos_ - start);
            return;
        }

        check_integer_range(start, integer_end);
        push_token(TokenKind::Integer, start, integer_end - start);
    }

    // §3.4: exactly three fractional digits, rejected rather than rounded.
    // A fix-it is offered only when digits are missing -- padding `1.5` to
    // `1.500` changes nothing, whereas truncating `1.5005` would silently
    // discard precision, which is the failure this rule exists to prevent.
    void check_decimal_precision(std::size_t start, std::size_t fraction_digits) {
        if (fraction_digits == 3) {
            return;
        }
        const std::string_view literal = slice(start, pos_ - start);
        Reporter diagnostic =
            report(Code::DecimalPrecision, span(start, pos_ - start),
                   quoted_text(literal) + " has " + spelled(fraction_digits) + " fractional digit" +
                       (fraction_digits == 1 ? "" : "s") + ", and a decimal wants exactly three");
        if (fraction_digits < 3) {
            diagnostic.with_fix_it(span(start, pos_ - start),
                                   std::string(literal) + std::string(3 - fraction_digits, '0'),
                                   "pad to three fractional digits");
        } else {
            diagnostic.with_note("I will not round it down for you: quietly losing precision "
                                 "in a damage formula is the very accident fixed-point "
                                 "arithmetic is here to prevent (spec §3.4)");
        }
    }

    // §3.4: "Integer is a signed 64-bit value. Out-of-range literals MUST be
    // rejected." Range is the only thing that can go wrong here; the digits
    // were matched by the loop above.
    void check_integer_range(std::size_t start, std::size_t end) {
        const std::string_view literal = slice(start, end - start);
        std::int64_t value = 0;
        const auto result = std::from_chars(literal.data(), literal.data() + literal.size(), value);
        if (result.ec == std::errc::result_out_of_range) {
            report(Code::IntegerRange, span(start, end - start),
                   "I can't hold " + quoted_text(literal) + " in an integer")
                .with_note("integers run from -9223372036854775808 to 9223372036854775807, "
                           "which is as much room as a signed 64-bit value has (spec §3.4)");
        }
    }

    // §3.4: `.5` must be rejected. Recovered as a decimal spanning `.5`, so
    // the statement around it still parses.
    void lex_leading_dot_decimal() {
        const std::size_t start = pos_;
        ++pos_; // the '.'
        const std::size_t fraction_start = pos_;
        while (pos_ < text_.size() && is_digit(text_[pos_])) {
            ++pos_;
        }
        const std::size_t fraction_digits = pos_ - fraction_start;
        const std::string_view literal = slice(start, pos_ - start);
        const diag::Span at = span(start, pos_ - start);
        Reporter diagnostic = report(Code::DecimalLeadingDot, at,
                                     quoted_text(literal) + " is missing its leading digit");

        if (fraction_digits <= 3) {
            // Both rules are in play here, so offer the number the author
            // almost certainly meant rather than half of it: '.5' wants to
            // become '0.500', not '0.5', which would only fail the next rule
            // along and cost them a second round trip.
            const std::string whole = "0." + std::string(slice(fraction_start, fraction_digits)) +
                                      std::string(3 - fraction_digits, '0');
            diagnostic
                .with_note("a decimal is a digit, a dot, and exactly three more digits, so the "
                           "number you want here is " +
                           quoted_text(whole) + " (spec §3.4)")
                .with_fix_it(at, whole, "write it as " + quoted_text(whole));
        } else {
            diagnostic
                .with_note("a decimal also wants exactly three fractional digits, which this "
                           "one does not have either (spec §3.4)")
                .with_fix_it(span(start, 0), "0", "add the leading digit");
        }
        push_token(TokenKind::Decimal, start, pos_ - start);
    }

    // §3.3, plus §3.9's one lexical reservation.
    void lex_identifier() {
        const std::size_t start = pos_;
        while (pos_ < text_.size() && is_ident_continue(text_[pos_])) {
            ++pos_;
        }
        const std::string_view identifier = slice(start, pos_ - start);
        if (identifier == "true" || identifier == "false") {
            const std::string_view replacement = identifier == "true" ? "yes" : "no";
            report(Code::ReservedWord, span(start, pos_ - start),
                   quoted_text(identifier) + " is a word I know, but not a value I take")
                .with_note("the booleans here are 'yes' and 'no'. 'true' and 'false' are "
                           "reserved for no other purpose than to make this message possible, "
                           "rather than leave you with an unknown identifier (spec §3.9)")
                .with_fix_it(span(start, pos_ - start), std::string(replacement),
                             "use " + quoted_text(replacement));
        }
        push_token(TokenKind::Identifier, start, pos_ - start);
    }

    void lex_bad_character() {
        const std::size_t start = pos_;
        const auto lead = static_cast<unsigned char>(text_[pos_]);

        if (lead < 0x80) {
            if (const std::string_view reserved = reserved_operator(text_, pos_);
                !reserved.empty()) {
                pos_ += 2;
                report(Code::BadChar, span(start, 2),
                       quoted_text(reserved) + " is spoken for: it is held back for a later "
                                               "version of the format")
                    .with_note("reserving it now is what lets it be added later without "
                               "changing the meaning of a file written today (spec §15)");
                push_token(TokenKind::Error, start, 2);
                return;
            }
            ++pos_;
            report(Code::BadChar, span(start, 1),
                   "I don't know what to do with " + quoted_text(slice(start, 1)) + " here");
            push_token(TokenKind::Error, start, 1);
            return;
        }

        const Utf8Decoded decoded = decode_utf8(text_, pos_);
        if (decoded.valid) {
            pos_ += decoded.length;
            report(Code::BadChar, span(start, decoded.length),
                   "I don't know what to do with " + code_point_name(decoded.code_point) + " here")
                .with_note("a name is built from ASCII letters, digits, '_' and '.'; an "
                           "accented letter or a symbol is perfectly welcome inside a string "
                           "literal, just not out here (spec §3.3)");
            push_token(TokenKind::Error, start, decoded.length);
            return;
        }

        // Malformed UTF-8; validate_utf8() has already reported the run.
        // Consume all of it as one error token so the stream still tiles the
        // input and so binary input costs one token, not one per byte.
        do {
            ++pos_;
        } while (pos_ < text_.size() && static_cast<unsigned char>(text_[pos_]) >= 0x80 &&
                 !decode_utf8(text_, pos_).valid);
        push_token(TokenKind::Error, start, pos_ - start);
    }

    diag::DiagnosticSink& sink_;
    std::string_view text_;
    TokenStream stream_;
    std::size_t pos_ = 0;
    std::uint32_t trivia_consumed_ = 0;
};

TokenStream lex(const diag::SourceManager& sources, diag::SourceId source,
                diag::DiagnosticSink& sink) {
    return Lexer(sources, source, sink).run();
}

bool is_reserved_word(std::string_view identifier) noexcept {
    for (std::string_view word : kReservedWords) {
        if (identifier == word) {
            return true;
        }
    }
    return false;
}

void decode_string_escapes(std::string_view literal, std::string& out) {
    // Strip the quotes the token span includes. An unterminated literal has
    // only the opening one, so each end is trimmed only if it is there.
    if (literal.starts_with('"')) {
        literal.remove_prefix(1);
    }
    if (literal.ends_with('"') && !literal.empty()) {
        literal.remove_suffix(1);
    }

    for (std::size_t i = 0; i < literal.size();) {
        if (literal[i] != '\\' || i + 1 >= literal.size()) {
            out.push_back(literal[i]);
            ++i;
            continue;
        }
        const char escaped = literal[i + 1];
        switch (escaped) {
        case 'n':
            out.push_back('\n');
            i += 2;
            continue;
        case 't':
            out.push_back('\t');
            i += 2;
            continue;
        case '"':
        case '\\':
        case '[':
        case ']':
        case '$':
        case '@':
            out.push_back(escaped);
            i += 2;
            continue;
        case 'u': {
            const std::string_view digits = literal.substr(i + 2, 4);
            std::uint32_t value = 0;
            if (is_hex_quad(digits)) {
                std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
                append_utf8(value, out);
                i += 6;
                continue;
            }
            break;
        }
        default:
            break;
        }
        // Not a defined escape: lex() already reported it, so reproduce the
        // bytes rather than inventing a replacement.
        out.push_back(literal[i]);
        ++i;
    }
}

std::string decode_string_escapes(std::string_view literal) {
    std::string out;
    decode_string_escapes(literal, out);
    return out;
}

} // namespace stardata::lex
