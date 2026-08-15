// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog D2 and D5: a table-driven test per rule of spec §3. Each case
// names the section it comes from, so a failure points at the paragraph
// rather than at a line number in this file.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "stardata/lex/lexer.hpp"

#include "support/lex_harness.hpp"

using stardata::lex::TokenKind;
using stardata::lex::TriviaKind;
using stardata::test::Lexed;

namespace {

struct KindCase {
    std::string_view section;
    std::string_view source;
    std::vector<TokenKind> kinds;
    // `true` and `false` are the one input here that is well-formed
    // lexically and still an error (§3.9). Everything else must be silent.
    bool diagnosed = false;
};

// Every row is "this input produces exactly these token kinds, in this
// order", with trivia excluded because trivia is not a token.
const std::vector<KindCase>& kind_cases() {
    static const std::vector<KindCase> cases = {
        // §3.3 identifiers
        {"§3.3", "room", {TokenKind::Identifier}},
        {"§3.3", "_private", {TokenKind::Identifier}},
        {"§3.3", "starscape.combat.melee", {TokenKind::Identifier}},
        {"§3.3", "a1_b2.c3", {TokenKind::Identifier}},
        // §3.3: the dot is an identifier character, so a namespaced name is
        // one token and not three.
        {"§3.3", "a.b", {TokenKind::Identifier}},

        // §3.4 numbers
        {"§3.4", "42", {TokenKind::Integer}},
        {"§3.4", "-42", {TokenKind::Integer}},
        {"§3.4", "0", {TokenKind::Integer}},
        {"§3.4", "9223372036854775807", {TokenKind::Integer}},
        {"§3.4", "1.500", {TokenKind::Decimal}},
        {"§3.4", "-3.750", {TokenKind::Decimal}},

        // §3.5 strings and localisation keys
        {"§3.5", "\"text\"", {TokenKind::String}},
        {"§3.5", "\"\"", {TokenKind::String}},
        {"§3.5", "\"a \\\" b\"", {TokenKind::String}},
        {"§3.5", "\"\\u00e9\"", {TokenKind::String}},
        {"§3.5", "$loc_key", {TokenKind::LocKey}},
        {"§3.5", "$a.b", {TokenKind::LocKey}},

        // §3.6 operators. Multi-character forms are matched first, so none
        // of these splits into two tokens.
        {"§3.6", "=", {TokenKind::Operator}},
        {"§3.6", "==", {TokenKind::Operator}},
        {"§3.6", "!=", {TokenKind::Operator}},
        {"§3.6", "<=", {TokenKind::Operator}},
        {"§3.6", ">=", {TokenKind::Operator}},
        {"§3.6", "+=", {TokenKind::Operator}},
        {"§3.6", "-=", {TokenKind::Operator}},
        {"§3.6", "?=", {TokenKind::Operator}},

        // §3.7 punctuation, and §4.2's ambiguous angle
        {"§3.7", "{}", {TokenKind::Punctuation, TokenKind::Punctuation}},
        {"§3.7", "(),", {TokenKind::Punctuation, TokenKind::Punctuation, TokenKind::Punctuation}},
        {"§3.7", "<", {TokenKind::Angle}},
        {"§3.7", ">", {TokenKind::Angle}},

        // §3.8 annotations. The arguments are ordinary tokens, so that
        // trivia inside the parentheses survives into the tree.
        {"§3.8", "@debug", {TokenKind::Annotation}},
        {"§3.8",
         "@platform(glk, cli)",
         {TokenKind::Annotation, TokenKind::Punctuation, TokenKind::Identifier,
          TokenKind::Punctuation, TokenKind::Identifier, TokenKind::Punctuation}},
        {"§3.8",
         "@priority(3)",
         {TokenKind::Annotation, TokenKind::Punctuation, TokenKind::Integer,
          TokenKind::Punctuation}},

        // §3.9: the reserved words are ordinary identifiers to the lexer.
        // Only `true` and `false` are additionally diagnosed, and even they
        // still produce a token so that the statement around them parses.
        {"§3.9", "yes", {TokenKind::Identifier}},
        {"§3.9", "inherit", {TokenKind::Identifier}},
        {"§3.9", "none", {TokenKind::Identifier}},
        {"§3.9", "true", {TokenKind::Identifier}, true},

        // §4.2: `list<int>` and `strength < 14` differ only in position, and
        // the lexer must hand the parser the same `<` in both.
        {"§4.2",
         "type = list<int>",
         {TokenKind::Identifier, TokenKind::Operator, TokenKind::Identifier, TokenKind::Angle,
          TokenKind::Identifier, TokenKind::Angle}},
        {"§4.2", "strength < 14", {TokenKind::Identifier, TokenKind::Angle, TokenKind::Integer}},
        // The same type expression with no spaces, and with spaces
        // everywhere, must lex identically: §3.1 makes whitespace
        // insignificant and §4.2 forbids depending on it.
        {"§4.2",
         "map < identifier , int >",
         {TokenKind::Identifier, TokenKind::Angle, TokenKind::Identifier, TokenKind::Punctuation,
          TokenKind::Identifier, TokenKind::Angle}},

        // §2.5: an empty file is conforming, and yields only EndOfFile.
        {"§2.5", "", {}},
    };
    return cases;
}

struct TriviaCase {
    std::string_view section;
    std::string_view source;
    std::vector<TriviaKind> kinds;
};

const std::vector<TriviaCase>& trivia_cases() {
    static const std::vector<TriviaCase> cases = {
        {"§3.1", "  \t\n\r ", {TriviaKind::Whitespace}},
        {"§3.2", "# a comment", {TriviaKind::Comment}},
        {"§3.2",
         "# one\n# two\n",
         {TriviaKind::Comment, TriviaKind::Whitespace, TriviaKind::Comment,
          TriviaKind::Whitespace}},
        // §3.2: a comment stops at the line terminator, and the CR of a CRLF
        // pair is part of that terminator, not of the comment.
        {"§3.2", "# one\r\n", {TriviaKind::Comment, TriviaKind::Whitespace}},
        // §2.2: a leading BOM is its own trivia -- accepted, preserved, and
        // not content.
        {"§2.2", "\xEF\xBB\xBF# c", {TriviaKind::ByteOrderMark, TriviaKind::Comment}},
    };
    return cases;
}

} // namespace

TEST_CASE("token kinds follow spec 3", "[lex]") {
    for (const KindCase& test_case : kind_cases()) {
        INFO(test_case.section << "  source: " << test_case.source);
        Lexed lexed{std::string(test_case.source)};
        CHECK(lexed.kinds() == test_case.kinds);
        CHECK((lexed.sink().error_count() != 0) == test_case.diagnosed);
        CHECK(lexed.stream().covers_source(lexed.sources()));
    }
}

TEST_CASE("trivia kinds follow spec 3.1 and 3.2", "[lex]") {
    for (const TriviaCase& test_case : trivia_cases()) {
        INFO(test_case.section << "  source: " << test_case.source);
        Lexed lexed{std::string(test_case.source)};
        std::vector<TriviaKind> kinds;
        for (const auto& piece : lexed.stream().trivia()) {
            kinds.push_back(piece.kind);
        }
        CHECK(kinds == test_case.kinds);
        CHECK(lexed.stream().covers_source(lexed.sources()));
    }
}

TEST_CASE("spec 3.1: newlines have no syntactic significance", "[lex]") {
    // "a block written across ten lines and the same block written on one
    // line are identical" -- identical, here, means the same token kinds in
    // the same order.
    Lexed spread{"exits = {\n    north = corridor\n}\n"};
    Lexed inline_form{"exits = { north = corridor }"};
    CHECK(spread.kinds() == inline_form.kinds());
}

TEST_CASE("spec 3.6: multi-character operators win over single-character ones", "[lex]") {
    // The rule that makes `>=` one token rather than an angle and an equals.
    Lexed lexed{">= <= == != += -= ?="};
    for (std::size_t index = 0; index < 7; ++index) {
        INFO("operator index " << index);
        CHECK(lexed.stream()[index].kind == TokenKind::Operator);
        CHECK(lexed.stream()[index].span.length == 2);
    }
    CHECK(lexed.sink().error_count() == 0);
}

TEST_CASE("spec 3.7: a bare angle is never merged with a following equals", "[lex]") {
    // `< =` is two tokens; `<=` is one. Whitespace decides nothing else in
    // this format, but it does decide which characters are adjacent.
    Lexed split{"< ="};
    CHECK(split.kinds() == std::vector{TokenKind::Angle, TokenKind::Operator});

    Lexed joined{"<="};
    CHECK(joined.kinds() == std::vector{TokenKind::Operator});
}

TEST_CASE("spec 3.2: a '#' inside a string literal is an ordinary character", "[lex]") {
    Lexed lexed{"hash = \"Cell block #4\"\n"};
    CHECK(lexed.kinds() ==
          std::vector{TokenKind::Identifier, TokenKind::Operator, TokenKind::String});
    CHECK(lexed.text_of(2) == "\"Cell block #4\"");
    CHECK(lexed.sink().error_count() == 0);
}

TEST_CASE("spec 3.4: a decimal is not two tokens around a dot", "[lex]") {
    Lexed lexed{"weight = 1.500"};
    CHECK(lexed.stream()[2].kind == TokenKind::Decimal);
    CHECK(lexed.text_of(2) == "1.500");
}

TEST_CASE("spec 3.9: is_reserved_word covers exactly the reserved list", "[lex]") {
    for (std::string_view word : {"yes", "no", "inherit", "none", "all", "and", "or", "not", "if",
                                  "else", "end", "true", "false"}) {
        INFO("reserved word: " << word);
        CHECK(stardata::lex::is_reserved_word(word));
    }
    // Upper-case combinators are keys, not reserved words (§3.9's last
    // paragraph), and neither are ordinary names.
    for (std::string_view word : {"AND", "OR", "NOT", "COUNT_AT_LEAST", "yes_man", "endgame"}) {
        INFO("not reserved: " << word);
        CHECK_FALSE(stardata::lex::is_reserved_word(word));
    }
}

TEST_CASE("every token carries a span into its own source, never copied text", "[lex]") {
    Lexed lexed{"id = brass_key\n"};
    for (const auto& token : lexed.stream().tokens()) {
        CHECK(token.span.source == lexed.id());
    }
    CHECK(lexed.text_of(2) == "brass_key");
}

TEST_CASE("the stream always ends with exactly one zero-length EndOfFile", "[lex]") {
    for (std::string_view source : {"", "a", "a = b\n", "# just a comment", "   "}) {
        INFO("source: " << source);
        Lexed lexed{std::string(source)};
        const auto& tokens = lexed.stream().tokens();
        REQUIRE_FALSE(tokens.empty());
        CHECK(tokens.back().kind == TokenKind::EndOfFile);
        CHECK(tokens.back().span.length == 0);
        CHECK(tokens.back().span.offset == source.size());
        for (std::size_t index = 0; index + 1 < tokens.size(); ++index) {
            CHECK(tokens[index].kind != TokenKind::EndOfFile);
        }
    }
}

TEST_CASE("trailing trivia is carried by the EndOfFile token", "[lex]") {
    // Nothing else could carry it, and dropping it would lose the last
    // comment of every file that ends in one.
    Lexed lexed{"a = b\n# trailing\n"};
    const auto& stream = lexed.stream();
    const std::size_t eof = stream.size() - 1;
    CHECK(stream[eof].kind == TokenKind::EndOfFile);
    CHECK(stream.preceding_trivia(eof).size() == 3); // newline, comment, newline
    CHECK(stream.covers_source(lexed.sources()));
}
