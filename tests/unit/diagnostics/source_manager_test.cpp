// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include <catch2/catch_test_macros.hpp>

#include "stardata/diag/source_manager.hpp"

using stardata::diag::LineCol;
using stardata::diag::SourceManager;
using stardata::diag::Span;

TEST_CASE("SourceId is invalid until returned by add_file", "[diag][source-manager]") {
    stardata::diag::SourceId id;
    REQUIRE_FALSE(id.valid());
}

TEST_CASE("line_col finds line and column for ASCII text", "[diag][source-manager]") {
    SourceManager sources;
    const auto id = sources.add_file("x.star", "abc\ndef\nghi");

    CHECK(sources.line_col(id, 0).line == 1);
    CHECK(sources.line_col(id, 0).column == 1);

    // Offset 4 is the 'd' at the start of line 2.
    const LineCol at_d = sources.line_col(id, 4);
    CHECK(at_d.line == 2);
    CHECK(at_d.column == 1);

    // Offset 6 is the 'f' -- two characters into line 2.
    const LineCol at_f = sources.line_col(id, 6);
    CHECK(at_f.line == 2);
    CHECK(at_f.column == 3);
}

TEST_CASE("column counts UTF-8 code points, not bytes", "[diag][source-manager]") {
    SourceManager sources;
    // "café" is 4 code points but 5 bytes: 'é' is 0xC3 0xA9 in UTF-8.
    const auto id = sources.add_file("x.star", "caf\xC3\xA9\nx");

    // The newline sits after 4 code points, so it reads as column 5 --
    // never column 6, which is what a byte-offset diff would report.
    const LineCol at_newline = sources.line_col(id, 5);
    CHECK(at_newline.line == 1);
    CHECK(at_newline.column == 5);
}

TEST_CASE("a tab counts as exactly one column, never expanded to a tab stop",
          "[diag][source-manager]") {
    SourceManager sources;
    const auto id = sources.add_file("x.star", "a\tb");

    const LineCol at_b = sources.line_col(id, 2);
    CHECK(at_b.line == 1);
    CHECK(at_b.column == 3);
}

TEST_CASE("column_width counts code points across a span", "[diag][source-manager]") {
    SourceManager sources;
    const auto id = sources.add_file("x.star", "caf\xC3\xA9 done");

    // The span covers all 5 bytes of "café", which is 4 code points.
    CHECK(sources.column_width(id, 0, 5) == 4);
}

TEST_CASE("line_text trims a CRLF fixture's trailing carriage return", "[diag][source-manager]") {
    SourceManager sources;
    const auto id = sources.add_file("x.star", "a\r\nb\r\n");

    CHECK(sources.line_text(id, 1) == "a");
    CHECK(sources.line_text(id, 2) == "b");
}

TEST_CASE("line_text returns the last line when the file has no trailing newline",
          "[diag][source-manager]") {
    SourceManager sources;
    const auto id = sources.add_file("x.star", "one\ntwo");

    CHECK(sources.line_text(id, 1) == "one");
    CHECK(sources.line_text(id, 2) == "two");
    CHECK(sources.line_count(id) == 2);
}

TEST_CASE("line_text returns empty past the end of the file", "[diag][source-manager]") {
    SourceManager sources;
    const auto id = sources.add_file("x.star", "one\ntwo");

    CHECK(sources.line_text(id, 3).empty());
}

TEST_CASE("multiple registered sources keep independent line tables", "[diag][source-manager]") {
    SourceManager sources;
    const auto a = sources.add_file("a.star", "aaa\nbbb");
    const auto b = sources.add_file("b.star", "one two three");

    CHECK(sources.line_count(a) == 2);
    CHECK(sources.line_count(b) == 1);
    CHECK(sources.path(a) == "a.star");
    CHECK(sources.path(b) == "b.star");
    CHECK(sources.contents(a) == "aaa\nbbb");
}
