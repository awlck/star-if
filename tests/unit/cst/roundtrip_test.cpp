// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog E6, the phase's headline test: for every .star under
// tests/corpus/, parse -> write -> compare bytes. Plus E5's writer and the
// golden tree dumps that pin the shape the parser produces.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "stardata/cst/parser.hpp"
#include "stardata/cst/writer.hpp"
#include "stardata/diag/codes.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/diag/source_manager.hpp"

#include "support/corpus.hpp"
#include "support/snapshot.hpp"
#include "support/tree_dump.hpp"

using namespace stardata;

namespace {

std::filesystem::path corpus_dir() {
    return std::filesystem::path(STARIF_CORPUS_DIR);
}

std::filesystem::path snapshot_dir() {
    return std::filesystem::path(STARIF_UNIT_TEST_DIR) / "cst" / "snapshots";
}

// Binary, never text mode: the corpus is deliberately not line-ending
// normalised (backlog A4), and reading it as text on Windows would quietly
// turn the CRLF fixture into the LF one and make this test pass for the
// wrong reason.
std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

struct ParsedFile {
    diag::SourceManager sources;
    std::string source;
    diag::SourceId id;
    diag::DiagnosticSink sink;
    cst::GreenCache cache;
    cst::GreenNodePtr green;

    explicit ParsedFile(const std::filesystem::path& path)
        : source(read_bytes(path)), id(sources.add_file(path, source)),
          green(cst::parse(sources, id, cache, sink)) {}
};

} // namespace

TEST_CASE("every corpus file round-trips byte for byte", "[cst][roundtrip]") {
    const auto files = test::corpus_files(corpus_dir());
    REQUIRE_FALSE(files.empty());

    for (const auto& path : files) {
        INFO("corpus file: " << path.string());
        ParsedFile parsed(path);
        const std::string written = cst::to_text(*parsed.green);

        CHECK(written.size() == parsed.source.size());
        CHECK(written == parsed.source);
    }
}

TEST_CASE("every corpus file parses with no diagnostic at all", "[cst][roundtrip]") {
    for (const auto& path : test::corpus_files(corpus_dir())) {
        INFO("corpus file: " << path.string());
        ParsedFile parsed(path);
        for (const auto& diagnostic : parsed.sink.diagnostics()) {
            INFO("unexpected " << diag::code_string(diagnostic.code()) << ": "
                               << diagnostic.message());
            CHECK(false);
        }
        CHECK(parsed.sink.error_count() == 0);
    }
}

TEST_CASE("the CRLF and LF fixtures both survive unchanged", "[cst][roundtrip]") {
    // Backlog A4 and E6's third bullet, spelled out rather than left to the
    // sweep above: these two files are the same scenario in two line-ending
    // styles, and normalising either would be a silent corruption.
    const std::string crlf = read_bytes(corpus_dir() / "crlf.star");
    const std::string lf = read_bytes(corpus_dir() / "lf.star");
    REQUIRE(crlf.find("\r\n") != std::string::npos);
    REQUIRE(lf.find("\r\n") == std::string::npos);

    for (const char* name : {"crlf.star", "lf.star"}) {
        INFO("fixture: " << name);
        ParsedFile parsed(corpus_dir() / name);
        CHECK(cst::to_text(*parsed.green) == parsed.source);
    }
}

TEST_CASE("a broken file round-trips too", "[cst][roundtrip]") {
    // Recovery has to be lossless as well, or an editor would corrupt a file
    // the moment it opened one with a typo in it.
    const auto files = test::corpus_files(corpus_dir() / "invalid");
    REQUIRE_FALSE(files.empty());

    for (const auto& path : files) {
        INFO("invalid fixture: " << path.string());
        ParsedFile parsed(path);
        CHECK(cst::to_text(*parsed.green) == parsed.source);
    }
}

TEST_CASE("the parse tree of the line-ending fixtures matches its golden", "[cst][roundtrip]") {
    // Pins the shape the parser produces, including where trivia attached.
    // tour.star is deliberately absent: its tree runs to tens of thousands
    // of lines, and the properties worth asserting about it -- byte-exact
    // round-trip, zero diagnostics -- are asserted directly above rather
    // than through a golden nobody could read.
    for (const char* name : {"lf.star", "crlf.star"}) {
        INFO("fixture: " << name);
        ParsedFile parsed(corpus_dir() / name);
        const cst::SyntaxNode root = cst::SyntaxNode::root(parsed.green);
        const std::string golden =
            std::string(name).substr(0, std::string(name).find('.')) + ".tree.txt";
        CHECK(test::check_snapshot(snapshot_dir() / golden, test::dump_tree(root)));
    }
}

TEST_CASE("tour.star has the structure the corpus claims", "[cst][roundtrip]") {
    // Standing in for a golden on the big file: the counts move only when
    // the corpus or the parser changes, and either is worth a look.
    ParsedFile parsed(corpus_dir() / "tour.star");
    const cst::SyntaxNode root = cst::SyntaxNode::root(parsed.green);

    std::size_t top_level_statements = 0;
    for (const cst::SyntaxNode& child : root.child_nodes()) {
        top_level_statements += child.kind() == cst::SyntaxKind::Statement ? 1 : 0;
    }
    // tests/check_stardata.py reports 149 top-level statements for this file.
    CHECK(top_level_statements == 149);

    std::size_t errors = 0;
    for (const cst::SyntaxNode& node : root.descendants()) {
        errors += node.kind() == cst::SyntaxKind::Error ? 1 : 0;
    }
    CHECK(errors == 0);
}
