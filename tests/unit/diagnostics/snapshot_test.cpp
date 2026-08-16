// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Exercises the snapshot harness against a hand-built diagnostic (backlog
// C4). corpus_snapshot_test.cpp is the corpus-wide half of C4; this file
// stays because the fixture it renders cannot be produced from a .star file
// at all yet: E-DUP-KEY is the schema layer's, and it is the running example
// of spec §5.3's "cite both spans" requirement, so the multi-span rendering
// has a golden before workstream F exists to provoke it.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <sstream>

#include "stardata/diag/diagnostic.hpp"
#include "stardata/diag/render.hpp"
#include "stardata/diag/source_manager.hpp"

#include "support/snapshot.hpp"

using namespace stardata::diag;

namespace {

std::filesystem::path snapshot_dir() {
    return std::filesystem::path(STARIF_UNIT_TEST_DIR) / "diagnostics" / "snapshots";
}

// The fixture both snapshot tests below render: two colliding `title` keys,
// the running example for spec §5.3's "cite both spans" requirement.
struct DuplicateKeyFixture {
    SourceManager sources;
    SourceId id;
    Diagnostic diag;

    DuplicateKeyFixture()
        : id(sources.add_file("tests/corpus/invalid/duplicate-key.star", "title = \"A\"\n"
                                                                         "title = \"B\"\n")),
          diag(Code::DuplicateKey, Span{id, 12, 5}, "duplicate key 'title' (arity = one)") {
        diag.with_note("first occurrence here", Span{id, 0, 5});
        diag.with_fix_it(Span{id, 12, 5}, "", "remove one of the duplicate keys");
    }
};

} // namespace

TEST_CASE("a duplicate-key diagnostic's human rendering matches its checked-in snapshot",
          "[diag][snapshot]") {
    DuplicateKeyFixture fixture;

    std::ostringstream out;
    render_human(out, fixture.diag, fixture.sources, /*use_color=*/false);

    CHECK(stardata::test::check_snapshot(snapshot_dir() / "duplicate-key.human.txt", out.str()));
}

TEST_CASE("the same diagnostic's machine rendering matches its checked-in snapshot",
          "[diag][snapshot]") {
    DuplicateKeyFixture fixture;

    std::ostringstream out;
    render_machine(out, fixture.diag, fixture.sources);

    CHECK(stardata::test::check_snapshot(snapshot_dir() / "duplicate-key.machine.txt", out.str()));
}
