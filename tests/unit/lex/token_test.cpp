// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog D1 (the token and trivia model) and D4 (adjacent-literal
// concatenation, with the split points preserved).
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "stardata/lex/lexer.hpp"
#include "stardata/lex/token.hpp"

#include "support/lex_harness.hpp"

using stardata::lex::TokenKind;
using stardata::lex::TokenRange;
using stardata::lex::TriviaKind;
using stardata::test::Lexed;

TEST_CASE("trivia is retained, not discarded", "[lex][trivia]") {
    // Spec §14.2. Everything downstream of the lexer depends on this: a
    // comment the lexer drops is a comment no round-trip can put back.
    Lexed lexed{"# leading comment\nid = brass_key  # trailing comment\n"};

    std::vector<std::string_view> comments;
    for (const auto& piece : lexed.stream().trivia()) {
        if (piece.kind == TriviaKind::Comment) {
            comments.push_back(lexed.sources().text(piece.span));
        }
    }
    CHECK(comments == std::vector<std::string_view>{"# leading comment", "# trailing comment"});
}

TEST_CASE("preceding_trivia groups trivia onto the token that follows it", "[lex][trivia]") {
    Lexed lexed{"a  # note\n  = b"};
    const auto& stream = lexed.stream();

    // `a` has nothing before it.
    CHECK(stream.preceding_trivia(0).empty());
    // `=` is preceded by whitespace, the comment, and more whitespace. The
    // lexer takes no view on which of them will end up attached to `a` and
    // which to `=`: that is E3's policy, applied over exactly these spans.
    const auto before_operator = stream.preceding_trivia(1);
    REQUIRE(before_operator.size() == 3);
    CHECK(before_operator[0].kind == TriviaKind::Whitespace);
    CHECK(before_operator[1].kind == TriviaKind::Comment);
    CHECK(before_operator[2].kind == TriviaKind::Whitespace);
}

TEST_CASE("a token stream tiles its source with no gaps and no overlaps", "[lex][trivia]") {
    for (std::string_view source : {"", "a", "a = b", "  # c\n",
                                    "\xEF\xBB\xBF"
                                    "a = \"s\"\n",
                                    "a = [ ] % \xFF b"}) {
        INFO("source: " << source);
        Lexed lexed{std::string(source)};
        CHECK(lexed.stream().covers_source(lexed.sources()));

        // Spelled out independently of covers_source, so that a bug in the
        // invariant check cannot hide a bug in the lexer.
        std::string reassembled;
        for (std::size_t index = 0; index < lexed.stream().size(); ++index) {
            for (const auto& piece : lexed.stream().preceding_trivia(index)) {
                reassembled += lexed.sources().text(piece.span);
            }
            reassembled += lexed.text_of(index);
        }
        CHECK(reassembled == source);
    }
}

TEST_CASE("§3.5.1 adjacent string literals form one scalar", "[lex][strings]") {
    Lexed lexed{"long_description = \"The reactor housing is scorched black. \"\n"
                "                   \"Something went very wrong here, and recently.\"\n"};
    const auto& stream = lexed.stream();

    // The literals are NOT merged into one token: §3.5.1 requires the split
    // points to survive so that a round-trip reproduces the author's line
    // breaks. The run is reported instead.
    CHECK(lexed.kinds() == std::vector{TokenKind::Identifier, TokenKind::Operator,
                                       TokenKind::String, TokenKind::String});

    CHECK(stream.starts_string_run(2));
    CHECK_FALSE(stream.starts_string_run(3));
    CHECK(stream.string_run_at(2) == TokenRange{2, 4});
    CHECK(stream.string_run_at(3) == TokenRange{2, 4});

    // The scalar the run denotes is the concatenation, with no separator
    // inserted -- not even the newline and indentation between them.
    std::string value;
    const TokenRange run = stream.string_run_at(2);
    for (std::size_t index = run.first; index < run.last; ++index) {
        stardata::lex::decode_string_escapes(lexed.text_of(index), value);
    }
    CHECK(value == "The reactor housing is scorched black. "
                   "Something went very wrong here, and recently.");
}

TEST_CASE("a run of one, and a token that is not a string", "[lex][strings]") {
    Lexed lexed{"a = \"only\" b"};
    const auto& stream = lexed.stream();

    CHECK(stream.starts_string_run(2));
    CHECK(stream.string_run_at(2) == TokenRange{2, 3});
    // A non-string reports itself, so a caller can walk the stream by run
    // without special-casing.
    CHECK_FALSE(stream.starts_string_run(0));
    CHECK(stream.string_run_at(0) == TokenRange{0, 1});
}

TEST_CASE("a comment between two literals does not break the run", "[lex][strings]") {
    // §3.5.1 says "separated only by trivia", and a comment is trivia.
    Lexed lexed{"a = \"one\"  # why\n    \"two\"\n"};
    CHECK(lexed.stream().string_run_at(2) == TokenRange{2, 4});
}

TEST_CASE("anything that is not trivia does break the run", "[lex][strings]") {
    Lexed lexed{"a = \"one\" b = \"two\""};
    CHECK(lexed.stream().string_run_at(2) == TokenRange{2, 3});
}

TEST_CASE("decode_string_escapes implements §3.5's escape table", "[lex][strings]") {
    using stardata::lex::decode_string_escapes;

    CHECK(decode_string_escapes("\"plain\"") == "plain");
    CHECK(decode_string_escapes("\"a\\\"b\"") == "a\"b");
    CHECK(decode_string_escapes("\"a\\\\b\"") == "a\\b");
    CHECK(decode_string_escapes("\"a\\nb\"") == "a\nb");
    CHECK(decode_string_escapes("\"a\\tb\"") == "a\tb");
    // §3.5: these four produce the literal character, so that text which
    // would otherwise be read by the template language (§9) is not.
    CHECK(decode_string_escapes("\"\\[\\]\\$\\@\"") == "[]$@");
    // \uXXXX, encoded back to UTF-8.
    CHECK(decode_string_escapes("\"caf\\u00e9\"") == "caf\xC3\xA9");
    CHECK(decode_string_escapes("\"\\u0041\"") == "A");
    // An unterminated literal still decodes: the diagnostic is lex()'s job,
    // and a caller that has one anyway should get its content.
    CHECK(decode_string_escapes("\"unclosed") == "unclosed");
    // An escape §3.5 does not define is reproduced rather than invented
    // away; lex() has already reported it.
    CHECK(decode_string_escapes("\"a\\qb\"") == "a\\qb");
    CHECK(decode_string_escapes("\"trailing\\") == "trailing\\");
}

TEST_CASE("kind names are stable and distinct", "[lex]") {
    // The golden dumps and the diagnostics quote these, so a rename is a
    // visible diff rather than a silent one.
    using stardata::lex::to_string;
    CHECK(to_string(TokenKind::Identifier) == "identifier");
    CHECK(to_string(TokenKind::Angle) == "angle");
    CHECK(to_string(TokenKind::EndOfFile) == "eof");
    CHECK(to_string(TriviaKind::Comment) == "comment");
    CHECK(to_string(TriviaKind::ByteOrderMark) == "bom");
}
