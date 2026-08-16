// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog C4: every fixture in tests/corpus/invalid/ has a checked-in
// snapshot of what the front end actually says about it, rendered exactly as
// an author would read it.
//
// This is the test that stops error messages silently degrading. Nothing
// else notices when a message loses the detail that made it useful: the
// codes still match, the counts still match, and the only evidence is an
// author eventually complaining. A golden turns that into a file diff.
//
// A fixture whose declared code no pass here produces gets a snapshot too,
// recording that the lexer and parser say nothing about it. That is not a
// gap in the test; it is a record of which pass owns which fixture.
//
// Two different reasons a snapshot reads "(none)", and the difference
// matters. Some fixtures are waiting on a workstream that does not exist
// yet -- the schema layer's type checking, `failureMsg` placement -- and
// those snapshots will move when it lands. Others are checked, just not
// here: everything the schema layer owns is asserted from these same
// fixtures in tests/unit/schema/corpus_test.cpp, which loads each one on
// top of the core-owned set the way a library is loaded. This file is the
// front end's account of the corpus, not the whole compiler's.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <set>
#include <sstream>
#include <string>

#include "stardata/cst/green.hpp"
#include "stardata/cst/parser.hpp"
#include "stardata/diag/codes.hpp"
#include "stardata/diag/render.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/diag/source_manager.hpp"

#include "support/corpus.hpp"
#include "support/fixture.hpp"
#include "support/snapshot.hpp"

using namespace stardata;

namespace {

std::filesystem::path invalid_dir() {
    return std::filesystem::path(STARIF_CORPUS_DIR) / "invalid";
}

std::filesystem::path snapshot_dir() {
    return std::filesystem::path(STARIF_UNIT_TEST_DIR) / "diagnostics" / "snapshots";
}

// One fixture, parsed. Parsing subsumes lexing, so a single pass collects
// both the lexical diagnostics of workstream D and the syntactic ones of E
// in the order an author would meet them.
struct ParsedFixture {
    std::string text;
    diag::SourceManager sources;
    diag::SourceId id;
    diag::DiagnosticSink sink;
    cst::GreenCache cache;
    cst::GreenNodePtr green;

    explicit ParsedFixture(const std::filesystem::path& path)
        : text(test::read_bytes(path)),
          id(sources.add_file(std::filesystem::path(test::corpus_name(path)), text)),
          green(cst::parse(sources, id, cache, sink)) {}

    [[nodiscard]] std::set<std::string> reported() const {
        std::set<std::string> codes;
        for (const diag::Diagnostic& diagnostic : sink.diagnostics()) {
            codes.emplace(diag::code_string(diagnostic.code()));
        }
        return codes;
    }
};

std::string join(const std::set<std::string>& codes) {
    if (codes.empty()) {
        return "(none)";
    }
    std::string joined;
    for (const std::string& code : codes) {
        if (!joined.empty()) {
            joined += ' ';
        }
        joined += code;
    }
    return joined;
}

// The snapshot: a three-line header naming the fixture and the gap between
// what it declares and what the front end currently reports, then the human
// rendering of each diagnostic in report order.
//
// The header is derived from the fixture rather than maintained by hand, so
// it cannot drift from it -- and it puts the interesting part first for a
// reader skimming the snapshot directory, which is where "we do not check
// this yet" is otherwise invisible.
std::string report(const ParsedFixture& fixture, const std::filesystem::path& path) {
    std::ostringstream out;
    out << "# " << test::corpus_name(path) << '\n'
        << "# declared " << join(test::expected_codes(fixture.text)) << '\n'
        << "# lexer and parser report " << join(fixture.reported()) << "\n\n";

    if (fixture.sink.diagnostics().empty()) {
        out << "(no diagnostics)\n";
        return out.str();
    }

    bool first = true;
    for (const diag::Diagnostic& diagnostic : fixture.sink.diagnostics()) {
        if (!first) {
            out << '\n';
        }
        first = false;
        diag::render_human(out, diagnostic, fixture.sources, /*use_color=*/false);
    }
    return out.str();
}

} // namespace

TEST_CASE("each invalid fixture's diagnostics match its checked-in snapshot",
          "[diag][corpus][snapshot]") {
    const auto files = test::corpus_files(invalid_dir());
    REQUIRE_FALSE(files.empty());

    for (const auto& path : files) {
        INFO("invalid fixture: " << path.string());
        const ParsedFixture fixture(path);
        const std::string name = path.stem().string() + ".txt";
        CHECK(test::check_snapshot(snapshot_dir() / "invalid" / name, report(fixture, path)));
    }
}

TEST_CASE("the machine rendering of the whole invalid corpus matches its snapshot",
          "[diag][corpus][snapshot]") {
    // The same diagnostics in the form CI prints them: one greppable line
    // each, the whole corpus on one screen. Worth pinning separately because
    // this format is a contract with editors and log scrapers, and a change
    // to it is invisible in the human goldens.
    std::ostringstream out;
    for (const auto& path : test::corpus_files(invalid_dir())) {
        const ParsedFixture fixture(path);
        for (const diag::Diagnostic& diagnostic : fixture.sink.diagnostics()) {
            diag::render_machine(out, diagnostic, fixture.sources);
        }
    }

    CHECK(test::check_snapshot(snapshot_dir() / "invalid.machine.txt", out.str()));
}

TEST_CASE("every diagnostic over the invalid corpus points inside the file it came from",
          "[diag][corpus]") {
    // Asserted outright rather than left to the goldens, because this is the
    // one failure a golden would happily record: a span past the end of the
    // file renders as a garbled snippet or an empty one, and blessing the
    // result would make the corruption permanent.
    for (const auto& path : test::corpus_files(invalid_dir())) {
        INFO("invalid fixture: " << path.string());
        const ParsedFixture fixture(path);
        const auto size = static_cast<std::uint32_t>(fixture.text.size());

        for (const diag::Diagnostic& diagnostic : fixture.sink.diagnostics()) {
            INFO("code: " << diag::code_string(diagnostic.code()));
            CHECK(diagnostic.primary_span().source == fixture.id);
            CHECK(diagnostic.primary_span().end() <= size);
            CHECK_FALSE(diagnostic.message().empty());

            for (const diag::Note& note : diagnostic.notes()) {
                CHECK_FALSE(note.message.empty());
                if (note.span) {
                    CHECK(note.span->end() <= size);
                }
            }
            for (const diag::FixIt& fix : diagnostic.fix_its()) {
                CHECK(fix.span.end() <= size);
            }
        }
    }
}
