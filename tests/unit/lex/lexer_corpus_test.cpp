// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog D5's golden test, run over every file in tests/corpus/ rather than
// over tour.star alone, and the D3 requirement that each lexical diagnostic
// have a fixture under tests/corpus/invalid/ -- asserted here from the
// fixtures' own `# EXPECT` lines, so the two cannot drift apart.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "stardata/diag/codes.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/diag/source_manager.hpp"
#include "stardata/lex/lexer.hpp"

#include "support/corpus.hpp"
#include "support/snapshot.hpp"
#include "support/token_dump.hpp"

using namespace stardata;

namespace {

std::filesystem::path corpus_dir() {
    return std::filesystem::path(STARIF_CORPUS_DIR);
}

std::filesystem::path snapshot_dir() {
    return std::filesystem::path(STARIF_UNIT_TEST_DIR) / "lex" / "snapshots";
}

// Reads a file as bytes. The corpus is deliberately not line-ending
// normalised (backlog A4, .gitattributes marks *.star as -text), so reading
// in text mode would defeat the CRLF fixture on Windows.
std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

// The codes a fixture declares with `# EXPECT <CODE>` in its header, using
// the same convention as tests/check_stardata.py --self-test.
std::set<std::string> expected_codes(const std::string& contents) {
    std::set<std::string> codes;
    std::istringstream lines(contents);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t hash = line.find_first_not_of(" \t");
        if (hash == std::string::npos) {
            continue;
        }
        if (line[hash] != '#') {
            break; // the header ends at the first non-comment line
        }
        const std::size_t marker = line.find("EXPECT");
        if (marker == std::string::npos) {
            continue;
        }
        std::istringstream rest(line.substr(marker + 6));
        std::string code;
        if (rest >> code) {
            codes.insert(code);
        }
    }
    return codes;
}

// The codes this workstream is responsible for. A fixture may declare codes
// belonging to later workstreams -- E-BLOCK-MIXED is the parser's, and
// W-FAILMSG-MISSING is F8's -- and those are not the lexer's to report yet.
const std::set<std::string>& lexical_codes() {
    static const std::set<std::string> codes = {
        "E-STR-MULTILINE",   "E-STR-ESCAPE",       "E-STR-UNTERMINATED", "E-DEC-PRECISION",
        "E-DEC-LEADING-DOT", "E-NUM-TRAILING-DOT", "E-INT-RANGE",        "E-BRACKET-OUTSIDE",
        "E-RESERVED-WORD",   "E-UNICODE-WS",       "E-BAD-CHAR",         "E-UTF8-INVALID"};
    return codes;
}

struct LexedFile {
    diag::SourceManager sources;
    diag::SourceId id;
    diag::DiagnosticSink sink;
    lex::TokenStream stream;

    explicit LexedFile(const std::filesystem::path& path)
        : id(sources.add_file(path, read_bytes(path))), stream(lex::lex(sources, id, sink)) {}

    [[nodiscard]] std::set<std::string> codes() const {
        std::set<std::string> result;
        for (const diag::Diagnostic& diagnostic : sink.diagnostics()) {
            result.emplace(diag::code_string(diagnostic.code()));
        }
        return result;
    }
};

} // namespace

TEST_CASE("every corpus file lexes without a diagnostic", "[lex][corpus]") {
    const auto files = test::corpus_files(corpus_dir());
    REQUIRE_FALSE(files.empty());

    for (const auto& path : files) {
        INFO("corpus file: " << path.string());
        LexedFile lexed(path);
        for (const auto& diagnostic : lexed.sink.diagnostics()) {
            INFO("unexpected " << diag::code_string(diagnostic.code()) << ": "
                               << diagnostic.message());
            CHECK(false);
        }
        CHECK(lexed.sink.error_count() == 0);
    }
}

TEST_CASE("the tokens and trivia of a corpus file tile it exactly", "[lex][corpus]") {
    // The property E5's writer inherits: concatenating every span in order
    // reproduces the file byte for byte, CRLF and LF fixtures alike.
    for (const auto& path : test::corpus_files(corpus_dir())) {
        INFO("corpus file: " << path.string());
        LexedFile lexed(path);
        CHECK(lexed.stream.covers_source(lexed.sources));
    }
    for (const auto& path : test::corpus_files(corpus_dir() / "invalid")) {
        INFO("invalid fixture: " << path.string());
        LexedFile lexed(path);
        CHECK(lexed.stream.covers_source(lexed.sources));
    }
}

TEST_CASE("the token stream of each corpus file matches its golden dump", "[lex][corpus]") {
    for (const auto& path : test::corpus_files(corpus_dir())) {
        INFO("corpus file: " << path.string());
        LexedFile lexed(path);
        const std::string name = path.stem().string() + ".tokens.txt";
        CHECK(test::check_snapshot(snapshot_dir() / name,
                                   test::dump_tokens(lexed.stream, lexed.sources)));
    }
}

TEST_CASE("each invalid fixture reports the lexical codes it declares", "[lex][corpus]") {
    const auto files = test::corpus_files(corpus_dir() / "invalid");
    REQUIRE_FALSE(files.empty());

    for (const auto& path : files) {
        INFO("invalid fixture: " << path.string());
        LexedFile lexed(path);
        const std::set<std::string> reported = lexed.codes();

        for (const std::string& expected : expected_codes(read_bytes(path))) {
            if (!lexical_codes().contains(expected)) {
                continue; // a later workstream's diagnostic
            }
            INFO("expected code: " << expected);
            CHECK(reported.contains(expected));
        }
    }
}

TEST_CASE("every lexical code has a fixture that provokes it", "[lex][corpus]") {
    // The other direction, and the one that actually stops the gap
    // reopening: adding a code without a fixture fails here.
    std::set<std::string> covered;
    for (const auto& path : test::corpus_files(corpus_dir() / "invalid")) {
        for (const std::string& code : expected_codes(read_bytes(path))) {
            covered.insert(code);
        }
    }

    for (const std::string& code : lexical_codes()) {
        if (code == "E-UTF8-INVALID") {
            // Deliberately fixture-less: the corpus is shared with
            // tests/check_stardata.py, which reads every file as UTF-8 text,
            // so a fixture of invalid bytes would break the Python checker
            // instead of exercising it. Covered by a unit test built from
            // bytes instead -- see lexer_diagnostics_test.cpp.
            continue;
        }
        INFO("lexical code without a fixture: " << code);
        CHECK(covered.contains(code));
    }
}
