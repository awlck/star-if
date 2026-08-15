// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog D3: every lexical diagnostic of spec §3, its span, and the
// recovery that follows it. The rule the whole workstream turns on is the
// last section here -- one bad token must not abandon the file, because an
// editor needs a tree for input the author is halfway through typing.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "stardata/diag/codes.hpp"

#include "support/lex_harness.hpp"

using stardata::diag::Code;
using stardata::lex::TokenKind;
using stardata::test::Lexed;

TEST_CASE("spec 3.5: a string literal may not span a line terminator", "[lex][diag]") {
    Lexed lexed{"description = \"opens here\n  and closes there\"\n"};

    REQUIRE(lexed.reported(Code::StringMultiline));
    const auto* diagnostic = lexed.find(Code::StringMultiline);
    REQUIRE(diagnostic != nullptr);

    // The span stops at the line break rather than running to the end of the
    // file (Appendix A1: an unterminated quote is a one-line error).
    CHECK(lexed.sources().text(diagnostic->primary_span()) == "\"opens here");
    CHECK_FALSE(diagnostic->notes().empty());

    // Recovery: the literal still becomes a token, and lexing continues on
    // the next line.
    CHECK(lexed.stream()[2].kind == TokenKind::String);
    CHECK(lexed.stream()[3].kind == TokenKind::Identifier);
}

TEST_CASE("spec 3.5: an unterminated string is reported once, at the end of the file",
          "[lex][diag]") {
    Lexed lexed{"a = \"no closing quote"};

    CHECK(lexed.codes() == std::vector<std::string>{"E-STR-UNTERMINATED"});
    const auto* diagnostic = lexed.find(Code::StringUnterminated);
    REQUIRE(diagnostic != nullptr);
    REQUIRE(diagnostic->fix_its().size() == 1);
    CHECK(diagnostic->fix_its()[0].replacement == "\"");
    CHECK(lexed.stream()[2].kind == TokenKind::String);
}

TEST_CASE("spec 3.5: escape sequences outside the defined set are rejected", "[lex][diag]") {
    SECTION("an unknown letter") {
        Lexed lexed{"a = \"bad \\q escape\""};
        CHECK(lexed.codes() == std::vector<std::string>{"E-STR-ESCAPE"});
        const auto* diagnostic = lexed.find(Code::StringEscape);
        REQUIRE(diagnostic != nullptr);
        CHECK(lexed.sources().text(diagnostic->primary_span()) == "\\q");
        // Recovery: the literal is still one token, closed where it closes.
        CHECK(lexed.stream()[2].kind == TokenKind::String);
        CHECK(lexed.text_of(2) == "\"bad \\q escape\"");
    }

    SECTION("every escape §3.5 defines is accepted") {
        Lexed lexed{"a = \"\\\" \\\\ \\n \\t \\[ \\] \\$ \\@ \\u00e9\""};
        CHECK(lexed.sink().error_count() == 0);
    }

    SECTION("a short hex quad") {
        Lexed lexed{"a = \"\\u00\""};
        CHECK(lexed.reported(Code::StringEscape));
    }

    SECTION("a surrogate half, which §3.5 requires be rejected") {
        Lexed lexed{"a = \"\\ud800\""};
        REQUIRE(lexed.reported(Code::StringEscape));
        const auto* diagnostic = lexed.find(Code::StringEscape);
        REQUIRE(diagnostic != nullptr);
        CHECK(lexed.sources().text(diagnostic->primary_span()) == "\\ud800");
    }
}

TEST_CASE("spec 3.4: a decimal has exactly three fractional digits", "[lex][diag]") {
    SECTION("too few, which is fixable by padding") {
        Lexed lexed{"weight = 1.5"};
        REQUIRE(lexed.reported(Code::DecimalPrecision));
        const auto* diagnostic = lexed.find(Code::DecimalPrecision);
        REQUIRE(diagnostic != nullptr);
        REQUIRE(diagnostic->fix_its().size() == 1);
        CHECK(diagnostic->fix_its()[0].replacement == "1.500");
    }

    SECTION("too many, which is deliberately not fixable") {
        // §3.4 rejects rather than rounds, so suggesting `1.500` here would
        // be suggesting exactly the silent precision loss the rule prevents.
        Lexed lexed{"weight = 1.5005"};
        REQUIRE(lexed.reported(Code::DecimalPrecision));
        const auto* diagnostic = lexed.find(Code::DecimalPrecision);
        REQUIRE(diagnostic != nullptr);
        CHECK(diagnostic->fix_its().empty());
        CHECK_FALSE(diagnostic->notes().empty());
    }

    SECTION("exactly three is silent") {
        Lexed lexed{"weight = 1.500"};
        CHECK(lexed.sink().error_count() == 0);
    }
}

TEST_CASE("spec 3.4: a decimal may not start with a dot", "[lex][diag]") {
    Lexed lexed{"chance = .5"};

    REQUIRE(lexed.reported(Code::DecimalLeadingDot));
    const auto* diagnostic = lexed.find(Code::DecimalLeadingDot);
    REQUIRE(diagnostic != nullptr);
    REQUIRE(diagnostic->fix_its().size() == 1);
    // The suggestion corrects both rules at once. Offering `0.5` would fix
    // the leading dot and leave the author to discover the three-digit rule
    // on the next build, which is one round trip too many.
    CHECK(diagnostic->fix_its()[0].replacement == "0.500");
    // `.5` is still one error, not one for the dot and another for the
    // precision: the note carries the second rule.
    CHECK(lexed.codes() == std::vector<std::string>{"E-DEC-LEADING-DOT"});
    CHECK_FALSE(diagnostic->notes().empty());
    CHECK(lexed.stream()[2].kind == TokenKind::Decimal);
}

TEST_CASE("spec 3.4: a number may not end with a dot", "[lex][diag]") {
    Lexed lexed{"reach = 2."};

    REQUIRE(lexed.reported(Code::NumberTrailingDot));
    const auto* diagnostic = lexed.find(Code::NumberTrailingDot);
    REQUIRE(diagnostic != nullptr);
    REQUIRE(diagnostic->fix_its().size() == 1);
    CHECK(diagnostic->fix_its()[0].replacement == "2");
}

TEST_CASE("spec 3.4: an integer literal must fit in a signed 64-bit value", "[lex][diag]") {
    SECTION("out of range") {
        Lexed lexed{"n = 99999999999999999999"};
        CHECK(lexed.codes() == std::vector<std::string>{"E-INT-RANGE"});
        CHECK(lexed.stream()[2].kind == TokenKind::Integer);
    }

    SECTION("the boundaries themselves are in range") {
        Lexed lexed{"lo = -9223372036854775808  hi = 9223372036854775807"};
        CHECK(lexed.sink().error_count() == 0);
    }
}

TEST_CASE("spec 3.7: brackets may not appear outside a string literal", "[lex][diag]") {
    Lexed lexed{"stages = [ one ]\n"};

    CHECK(lexed.codes() == std::vector<std::string>{"E-BRACKET-OUTSIDE", "E-BRACKET-OUTSIDE"});
    // Recovery keeps the bytes in the stream as error tokens, so the tree
    // E4 builds still covers them.
    CHECK(lexed.kinds() == std::vector{TokenKind::Identifier, TokenKind::Operator, TokenKind::Error,
                                       TokenKind::Identifier, TokenKind::Error});
    CHECK(lexed.stream().covers_source(lexed.sources()));
}

TEST_CASE("spec 3.7: brackets inside a string literal are ordinary characters", "[lex][diag]") {
    // They belong to the template language there (§9), which is exactly why
    // §3.7 reserves them outside one.
    Lexed lexed{"msg = \"[if is_dark]dark[end]\"\n"};
    CHECK(lexed.sink().error_count() == 0);
    CHECK(lexed.stream()[2].kind == TokenKind::String);
}

TEST_CASE("spec 3.9: 'true' and 'false' are rejected with a pointer at yes / no", "[lex][diag]") {
    Lexed lexed{"open = true\nshut = false\n"};

    CHECK(lexed.codes() == std::vector<std::string>{"E-RESERVED-WORD", "E-RESERVED-WORD"});
    const auto* diagnostic = lexed.find(Code::ReservedWord);
    REQUIRE(diagnostic != nullptr);
    REQUIRE(diagnostic->fix_its().size() == 1);
    CHECK(diagnostic->fix_its()[0].replacement == "yes");
    // Still tokens: the statement around them parses, so the author gets one
    // error rather than a cascade.
    CHECK(lexed.stream()[2].kind == TokenKind::Identifier);
}

TEST_CASE("spec 3.1: non-ASCII whitespace is rejected with an ASCII suggestion", "[lex][diag]") {
    SECTION("a no-break space") {
        Lexed lexed{"a\xC2\xA0= b"}; // U+00A0 between the key and the operator
        REQUIRE(lexed.reported(Code::UnicodeWhitespace));
        const auto* diagnostic = lexed.find(Code::UnicodeWhitespace);
        REQUIRE(diagnostic != nullptr);
        CHECK(diagnostic->message().find("U+00A0") != std::string::npos);
        REQUIRE(diagnostic->fix_its().size() == 1);
        CHECK(diagnostic->fix_its()[0].replacement == " ");
        // Recovered as whitespace, which is what the author meant, so the
        // statement still lexes as a statement.
        CHECK(lexed.kinds() ==
              std::vector{TokenKind::Identifier, TokenKind::Operator, TokenKind::Identifier});
    }

    SECTION("a zero-width space, whose fix is deletion") {
        Lexed lexed{"a\xE2\x80\x8B = b"}; // U+200B
        const auto* diagnostic = lexed.find(Code::UnicodeWhitespace);
        REQUIRE(diagnostic != nullptr);
        REQUIRE(diagnostic->fix_its().size() == 1);
        CHECK(diagnostic->fix_its()[0].replacement.empty());
    }

    SECTION("U+FEFF is a BOM only at the start of the file") {
        Lexed leading{"\xEF\xBB\xBF"
                      "a = b"};
        CHECK(leading.sink().error_count() == 0);

        Lexed elsewhere{"a = \xEF\xBB\xBF b"};
        CHECK(elsewhere.reported(Code::UnicodeWhitespace));
    }
}

TEST_CASE("spec 3: a character that begins no token is reported and skipped", "[lex][diag]") {
    SECTION("an ASCII stray") {
        Lexed lexed{"a % b"};
        CHECK(lexed.codes() == std::vector<std::string>{"E-BAD-CHAR"});
        CHECK(lexed.kinds() ==
              std::vector{TokenKind::Identifier, TokenKind::Error, TokenKind::Identifier});
    }

    SECTION("a non-ASCII letter, which §3.3 does not admit in an identifier") {
        Lexed lexed{"caf\xC3\xA9 = 1"}; // é
        REQUIRE(lexed.reported(Code::BadChar));
        const auto* diagnostic = lexed.find(Code::BadChar);
        REQUIRE(diagnostic != nullptr);
        CHECK(diagnostic->message().find("U+00E9") != std::string::npos);
    }

    SECTION("a sigil with nothing after it") {
        Lexed lockey{"a = $"};
        CHECK(lockey.codes() == std::vector<std::string>{"E-BAD-CHAR"});

        Lexed annotation{"a = @ b"};
        CHECK(annotation.codes() == std::vector<std::string>{"E-BAD-CHAR"});
    }

    SECTION("a §15 reserved operator, which gets a note rather than a shrug") {
        for (std::string_view reserved : {"*=", "/=", "::", "->"}) {
            INFO("reserved operator: " << reserved);
            Lexed lexed{"hp " + std::string(reserved) + " 2"};
            REQUIRE(lexed.reported(Code::BadChar));
            const auto* diagnostic = lexed.find(Code::BadChar);
            REQUIRE(diagnostic != nullptr);
            CHECK(lexed.sources().text(diagnostic->primary_span()) == reserved);
            CHECK_FALSE(diagnostic->notes().empty());
        }
    }
}

TEST_CASE("spec 2.1: malformed UTF-8 is a diagnostic, not a crash", "[lex][diag]") {
    // No corpus fixture exists for this one: tests/check_stardata.py reads
    // the corpus as UTF-8 text, so a fixture containing invalid bytes would
    // break the Python checker rather than exercise it. The condition is
    // reachable only from a byte source, which is what this test builds.
    SECTION("a lone continuation byte") {
        Lexed lexed{std::string("a = \x80 b")};
        CHECK(lexed.reported(Code::Utf8Invalid));
        CHECK(lexed.stream().covers_source(lexed.sources()));
    }

    SECTION("a truncated sequence at the end of the file") {
        Lexed lexed{std::string("a = \xE2\x80")};
        CHECK(lexed.reported(Code::Utf8Invalid));
        CHECK(lexed.stream().covers_source(lexed.sources()));
    }

    SECTION("an overlong encoding of '/'") {
        Lexed lexed{std::string("\xC0\xAF")};
        CHECK(lexed.reported(Code::Utf8Invalid));
    }

    SECTION("a long malformed run costs one diagnostic, not one per byte") {
        Lexed lexed{std::string(256, '\xFF')};
        CHECK(lexed.codes() == std::vector<std::string>{"E-UTF8-INVALID"});
        // ...and one error token, not 256.
        CHECK(lexed.kinds() == std::vector{TokenKind::Error});
    }
}

TEST_CASE("recovery: one bad token does not abandon the file", "[lex][diag]") {
    // Backlog D3's last line. Every diagnostic above appears at once, and
    // the well-formed statements on either side still lex correctly.
    const std::string source = "before = 1\n"
                               "bad    = [ ]\n"
                               "worse  = .5\n"
                               "wrong  = \"unclosed\n"
                               "odd    = 2.\n"
                               "silly  = true\n"
                               "after  = 2\n";
    Lexed lexed{source};

    CHECK(lexed.sink().error_count() >= 6);
    CHECK(lexed.stream().covers_source(lexed.sources()));

    // The last statement is intact: three tokens, in order, with the right
    // text -- which is the whole point of recovering rather than bailing.
    const auto& tokens = lexed.stream().tokens();
    REQUIRE(tokens.size() >= 4);
    const std::size_t last = tokens.size() - 1;
    CHECK(tokens[last].kind == TokenKind::EndOfFile);
    CHECK(lexed.text_of(last - 3) == "after");
    CHECK(lexed.text_of(last - 2) == "=");
    CHECK(lexed.text_of(last - 1) == "2");
}

TEST_CASE("a sink at its limit still lexes the whole file", "[lex][diag]") {
    // The limit drops diagnostics, and the notes and fix-its that would have
    // hung off them have nowhere to go. Neither may cost a token.
    stardata::diag::SourceManager sources;
    const std::string source(2000, '%');
    const auto id = sources.add_file("flood.star", source);

    stardata::diag::DiagnosticSink unlimited;
    const auto full = stardata::lex::lex(sources, id, unlimited);

    stardata::diag::DiagnosticSink limited(4);
    const auto capped = stardata::lex::lex(sources, id, limited);

    CHECK(limited.diagnostics().size() == 4);
    CHECK(limited.error_count() == unlimited.error_count());
    CHECK(capped.size() == full.size());
    CHECK(capped.covers_source(sources));
}
