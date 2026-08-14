// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include <catch2/catch_test_macros.hpp>

#include <sstream>

#include "stardata/diag/diagnostic.hpp"
#include "stardata/diag/render.hpp"
#include "stardata/diag/source_manager.hpp"

using namespace stardata::diag;

TEST_CASE("render_machine prints one greppable line with path, line, column", "[diag][render]") {
    SourceManager sources;
    const auto id = sources.add_file("tests/corpus/invalid/dup.star", "title = \"A\"\n"
                                                                      "title = \"B\"\n");
    Diagnostic diag(Code::DuplicateKey, Span{id, 12, 5}, "duplicate key 'title'");

    std::ostringstream out;
    render_machine(out, diag, sources);

    CHECK(out.str() == "tests/corpus/invalid/dup.star:2:1: error: E-DUP-KEY: "
                       "duplicate key 'title'\n");
}

TEST_CASE("render_machine never emits colour, regardless of use_color elsewhere",
          "[diag][render]") {
    SourceManager sources;
    const auto id = sources.add_file("x.star", "x");
    Diagnostic diag(Code::UnknownKey, Span{id, 0, 1}, "unknown key");

    std::ostringstream out;
    render_machine(out, diag, sources);

    CHECK(out.str().find('\x1b') == std::string::npos);
}

TEST_CASE("render_human without colour prints a plain source snippet with a caret underline",
          "[diag][render]") {
    SourceManager sources;
    const auto id = sources.add_file("dup.star", "title = \"A\"\n"
                                                 "title = \"B\"\n");
    Diagnostic diag(Code::DuplicateKey, Span{id, 12, 5}, "duplicate key 'title' (arity = one)");
    diag.with_note("first occurrence here", Span{id, 0, 5});
    diag.with_fix_it(Span{id, 12, 5}, "", "remove one of the duplicate keys");

    std::ostringstream out;
    render_human(out, diag, sources, /*use_color=*/false);
    const std::string text = out.str();

    CHECK(text.find("error: E-DUP-KEY: duplicate key 'title' (arity = one)") != std::string::npos);
    CHECK(text.find("--> dup.star:2:1") != std::string::npos);
    CHECK(text.find("title = \"B\"") != std::string::npos);
    CHECK(text.find("^^^^^") != std::string::npos);
    CHECK(text.find("note: first occurrence here") != std::string::npos);
    CHECK(text.find("--> dup.star:1:1") != std::string::npos);
    CHECK(text.find("fix-it: remove one of the duplicate keys") != std::string::npos);
    CHECK(text.find('\x1b') == std::string::npos);
}

TEST_CASE("render_human with colour wraps the severity in ANSI escapes", "[diag][render]") {
    SourceManager sources;
    const auto id = sources.add_file("x.star", "x");
    Diagnostic diag(Code::UnknownKey, Span{id, 0, 1}, "unknown key 'foo'");

    std::ostringstream out;
    render_human(out, diag, sources, /*use_color=*/true);

    CHECK(out.str().find('\x1b') != std::string::npos);
}

TEST_CASE("the caret underline width matches code points, not bytes", "[diag][render]") {
    SourceManager sources;
    // "café" is 4 code points across 5 bytes.
    const auto id = sources.add_file("x.star", "caf\xC3\xA9 broken");
    Diagnostic diag(Code::ReservedWord, Span{id, 0, 5}, "bad value");

    std::ostringstream out;
    render_human(out, diag, sources, /*use_color=*/false);

    CHECK(out.str().find("^^^^\n") != std::string::npos);
    CHECK(out.str().find("^^^^^") == std::string::npos);
}

TEST_CASE("stdout_is_tty and stderr_is_tty are callable and return a bool", "[diag][render]") {
    // The value is environment-dependent (a test runner's stdout is rarely a
    // TTY), so this only proves the platform-specific detection links and
    // runs without crashing -- not a specific answer.
    [[maybe_unused]] const bool out_tty = stdout_is_tty();
    [[maybe_unused]] const bool err_tty = stderr_is_tty();
    SUCCEED();
}
