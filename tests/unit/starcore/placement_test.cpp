// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog F2c: placement sugar, spec §8.5. Owned by `libs/starcore`.
//
// These tests moved here from tests/unit/schema/markers_test.cpp when
// proposal §2.1.1 drew the mechanism/vocabulary line, and the move is the
// point rather than a side effect. `in`, `on` and `carried` are containment,
// containment is interactive fiction, and no amount of passing the keywords
// in as a parameter made the pass generic -- it made it a semantic pass
// wearing a parameter, which is worse, because it looked like a mechanism.
//
// What survived the move unchanged is the insistence that the vocabulary be
// **data**. `starcore` naming `relation_enum` outright is right; `starcore`
// listing its seven values in C++ would not be, and the test at the bottom is
// what keeps them apart: supersede the enum from a file (§7.6) and the sugar
// follows, with no code change anywhere.
//
// This binary links `starcore`. `stardata_unit_tests` does not, which is what
// makes the boundary something the build enforces rather than something a
// comment asserts.
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <sstream>
#include <string>

#include "stardata/diag/codes.hpp"
#include "stardata/diag/render.hpp"

#include "starcore/globals.hpp"
#include "starcore/messages.hpp"
#include "starcore/narrowing.hpp"
#include "starcore/placement.hpp"
#include "starcore/text.hpp"
#include "support/corpus.hpp"
#include "support/cst_harness.hpp"
#include "support/fixture.hpp"
#include "support/schema_harness.hpp"
#include "support/snapshot.hpp"

using namespace stardata;

namespace {

// One object instantiation, read for its placement.
//
// The built-in set is loaded because the relation keywords come from it --
// they are the values of the enum `starcore.object`'s `relation` property is
// typed by, not a list in the code. A test that passed its own list would be
// testing a list it had just written.
struct Placed {
    test::LoadedSet loaded;
    test::Parsed parsed;
    diag::DiagnosticSink sink;

    explicit Placed(std::string body)
        : parsed("thing = {\n    id = subject\n" + std::move(body) + "}\n") {
        loaded.load_builtin();
    }

    [[nodiscard]] std::optional<starcore::Placement> placement() {
        const ast::Block block = *parsed.ast().statements()[0].value()->as_block();
        return starcore::read_placement(block, loaded.set, sink);
    }
};

[[nodiscard]] bool mentions(const diag::DiagnosticSink& sink, std::string_view text) {
    for (const diag::Diagnostic& diagnostic : sink.diagnostics()) {
        if (diagnostic.message().find(text) != std::string::npos) {
            return true;
        }
        for (const diag::Note& note : diagnostic.notes()) {
            if (note.message.find(text) != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

// --- desugaring (spec §8.5) --------------------------------------------

TEST_CASE("a relation keyword desugars to holder and relation", "[starcore][placement]") {
    for (const char* keyword : {"in", "on", "under", "behind", "carried", "worn", "part_of"}) {
        INFO("relation: " << keyword);
        Placed placed(std::string("    ") + keyword + " = ornate_box\n");
        const std::optional<starcore::Placement> placement = placed.placement();

        REQUIRE(placement);
        CHECK(placement->holder == "ornate_box");
        CHECK(placement->relation == keyword);
        CHECK(placement->from_sugar);
        CHECK(placed.sink.error_count() == 0);
    }
}

TEST_CASE("both spellings produce identical data", "[starcore][placement]") {
    // §8.5: "Both spellings are legal and produce identical data."
    Placed sugar("    in = ornate_box\n");
    Placed longhand("    holder = ornate_box\n    relation = in\n");

    const std::optional<starcore::Placement> from_sugar = sugar.placement();
    const std::optional<starcore::Placement> from_longhand = longhand.placement();

    REQUIRE(from_sugar);
    REQUIRE(from_longhand);
    CHECK(from_sugar->holder == from_longhand->holder);
    CHECK(from_sugar->relation == from_longhand->relation);

    // The one thing that differs is which spelling was used, which is what
    // an editor writing the file back needs to know.
    CHECK(from_sugar->from_sugar);
    CHECK_FALSE(from_longhand->from_sugar);
}

TEST_CASE("an object with no placement has none", "[starcore][placement]") {
    // A root object, or one placed at run time. Not an error.
    Placed placed("    name = \"a brass key\"\n");
    CHECK_FALSE(placed.placement());
    CHECK(placed.sink.error_count() == 0);
}

// --- the conflict ------------------------------------------------------

TEST_CASE("writing both spellings is refused", "[starcore][placement]") {
    Placed placed("    in = ornate_box\n    holder = mess_table\n    relation = on\n");
    const std::optional<starcore::Placement> placement = placed.placement();

    // Neither wins. §8.5 says the conflict is not resolvable by precedence,
    // and guessing would put the object somewhere the author did not ask for.
    CHECK_FALSE(placement);
    REQUIRE(placed.sink.diagnostics().size() == 1);
    const diag::Diagnostic& conflict = placed.sink.diagnostics().front();
    CHECK(conflict.code() == diag::Code::PlacementConflict);
    CHECK(conflict.message().find("in") != std::string::npos);
    CHECK(conflict.message().find("holder") != std::string::npos);

    // Both spans, so the author can see the pair rather than hunt for it.
    REQUIRE_FALSE(conflict.notes().empty());
    CHECK(conflict.notes().front().span.has_value());
}

TEST_CASE("two relation keywords are the same conflict", "[starcore][placement]") {
    Placed placed("    in = ornate_box\n    on = mess_table\n");
    CHECK_FALSE(placed.placement());
    REQUIRE(placed.sink.diagnostics().size() == 1);
    CHECK(placed.sink.diagnostics().front().code() == diag::Code::PlacementConflict);
}

TEST_CASE("the conflict is caught where an author writes it", "[starcore][placement]") {
    // Not just through the helper: an ordinary instantiation, in a file, read
    // the way a game's would be -- which is the only place it can be written.
    //
    // Two steps now rather than one, and that is the shape of the boundary.
    // `stardata` loads the schemas and validates against them; `starcore`
    // then walks the same tree asking its own question. Phase 1's `starforge`
    // sequences the two.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();

    test::Parsed parsed("thing = { id = confused_key  in = ornate_box  holder = mess_table }\n");
    diag::DiagnosticSink sink;
    starcore::check_placements(parsed.ast(), loaded.set, sink);

    REQUIRE(sink.diagnostics().size() == 1);
    CHECK(sink.diagnostics().front().code() == diag::Code::PlacementConflict);
    CHECK(mentions(sink, "same two slots"));
}

TEST_CASE("the conflict is caught in the long object spelling too", "[starcore][placement]") {
    // §7.4's two spellings are the same declaration, so every pass that walks
    // instantiations has to see both. This one asks the format layer rather
    // than testing `find_class` itself, which is what keeps the knowledge that
    // there ARE two spellings in `libs/stardata` where §7.2.4 puts `object`.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();

    test::Parsed parsed("object = { id = confused_key  of_class = thing\n"
                        "           in = ornate_box  holder = mess_table }\n");
    diag::DiagnosticSink sink;
    starcore::check_placements(parsed.ast(), loaded.set, sink);

    REQUIRE(sink.diagnostics().size() == 1);
    CHECK(sink.diagnostics().front().code() == diag::Code::PlacementConflict);
}

TEST_CASE("the sugar is expanded in the view and never in the tree", "[starcore][placement]") {
    // Backlog F2c's third bullet, and §14.2's requirement. `in = box` has to
    // still say `in = box` after a parse and a write -- the expansion is a
    // reading of the tree, not an edit to it.
    const std::string source = "thing = { id = brass_key  in = ornate_box }\n";
    test::Parsed parsed(source);
    test::LoadedSet loaded;
    loaded.load_builtin();

    CHECK(parsed.written() == source);

    const ast::Block block = *parsed.ast().statements()[0].value()->as_block();
    diag::DiagnosticSink sink;
    const std::optional<starcore::Placement> placement =
        starcore::read_placement(block, loaded.set, sink);
    REQUIRE(placement);
    CHECK(placement->relation == "in");

    // And after reading it, the bytes are still the author's.
    CHECK(parsed.written() == source);
    CHECK_FALSE(block.find("holder")); // nothing was inserted
    CHECK_FALSE(block.find("relation"));
}

TEST_CASE("stdlib and the corpus both keep the placement rule", "[starcore][placement]") {
    // The reference corpus places objects with the sugar throughout, so
    // running the pass over every valid fixture is the check that the rule
    // does not fire on correct files.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    REQUIRE(loaded.sink.error_count() == 0);

    // tests/corpus/ itself is the valid corpus -- tour.star and the two
    // line-ending fixtures. The invalid ones live one directory down.
    const auto files = test::corpus_files(test::corpus_dir());
    REQUIRE_FALSE(files.empty());
    for (const auto& path : files) {
        INFO("valid fixture: " << path.string());
        test::Parsed parsed(test::read_bytes(path), test::corpus_name(path));
        diag::DiagnosticSink sink;
        starcore::check_placements(parsed.ast(), loaded.set, sink);
        CHECK(sink.diagnostics().empty());
    }
}

// --- the vocabulary is data, and this proves it ------------------------

TEST_CASE("the relation keywords come from the enum, not from the code", "[starcore][placement]") {
    // `starcore` owning the vocabulary is not the same as hard-coding it.
    // Supersede `relation_enum` from a file with §7.6's `@replaces(starcore)`
    // and the sugar follows the new values: `magnetised` becomes placement and
    // `in` stops being it, with no change to any C++.
    //
    // This is also half an answer to a question F2c left open -- whether a
    // library may amend the relation set. It may replace it wholesale, which
    // is the mechanism §7.6 already provides; adding to one still has no
    // spelling, since there is no `enum_extension`.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("enum = @replaces(starcore) {\n"
                     "    id     = relation_enum\n"
                     "    values = { bolted welded magnetised }\n"
                     "}\n",
                     "a ruleset", "ruleset/mounting.star");
    REQUIRE(loaded.sink.error_count() == 0);

    const std::vector<std::string>& keywords = starcore::relation_keywords(loaded.set);
    REQUIRE(keywords.size() == 3);

    test::Parsed bolted("thing = { id = plate  bolted = bulkhead }\n");
    diag::DiagnosticSink sink;
    const std::optional<starcore::Placement> placement = starcore::read_placement(
        *bolted.ast().statements()[0].value()->as_block(), loaded.set, sink);

    REQUIRE(placement);
    CHECK(placement->relation == "bolted");
    CHECK(placement->holder == "bulkhead");

    // And `in` is now just a key like any other, because nothing in the code
    // ever knew it.
    test::Parsed inside("thing = { id = key  in = box }\n");
    diag::DiagnosticSink other;
    CHECK_FALSE(starcore::read_placement(*inside.ast().statements()[0].value()->as_block(),
                                         loaded.set, other));
    CHECK(other.diagnostics().empty());
}

TEST_CASE("relation_enum names the enum starcore.object is typed by", "[starcore][placement]") {
    // The link that makes `starcore::relation_enum_id` the right name rather
    // than a guess: `starcore.object.relation` is declared at
    // `enum<relation_enum>`, and a core_requirement asserts it.
    test::LoadedSet loaded;
    loaded.load_builtin();

    const schema::ClassDecl* object = loaded.set.find_class("starcore.object");
    REQUIRE(object != nullptr);
    REQUIRE(object->find_property("relation") != nullptr);
    CHECK(object->find_property("relation")->type.to_string() ==
          "enum<" + std::string(starcore::relation_enum_id) + ">");

    const schema::EnumDecl* relations = loaded.set.find_enum(starcore::relation_enum_id);
    REQUIRE(relations != nullptr);
    CHECK(relations->values.size() == 7);
    for (const char* keyword : {"in", "on", "under", "behind", "carried", "worn", "part_of"}) {
        INFO("relation: " << keyword);
        CHECK(relations->has_value(keyword));
    }
}

// --- the corpus --------------------------------------------------------

TEST_CASE("each invalid fixture reports the starcore codes it declares", "[starcore][corpus]") {
    // The other half of what moved. E-PLACEMENT-CONFLICT used to be asserted
    // from these fixtures in tests/unit/schema/corpus_test.cpp, because the
    // loader raised it; it is asserted here now, because `starcore` does.
    const auto files = test::corpus_files(test::corpus_dir() / "invalid");
    REQUIRE_FALSE(files.empty());

    bool any = false;
    for (const auto& path : files) {
        const std::string contents = test::read_bytes(path);
        const std::set<std::string> expected = test::expected_codes(contents);
        bool relevant = false;
        for (const std::string& code : expected) {
            relevant = relevant || test::starcore_codes().contains(code);
        }
        if (!relevant) {
            continue; // another library's fixture
        }
        any = true;

        INFO("invalid fixture: " << path.string());
        test::LoadedSet loaded;
        loaded.load_builtin();
        loaded.load_stdlib();
        // Loaded as well as parsed: §8.8's analysis asks the registry what
        // each class declares, so a fixture's own classes and actions have
        // to be in it. Placement needed only the relation enum and so never
        // did.
        const diag::SourceId id = loaded.load_text(contents, "a library", test::corpus_name(path));
        // Viewed out of the manager that loaded it rather than parsed into a
        // second one. `GlobalIndex::check` answers out of the registry now,
        // so the span on a diagnostic about a declaration belongs to the
        // loader's source manager -- and a span rendered against a manager it
        // did not come from is garbage at best.
        diag::DiagnosticSink parse_sink;
        const cst::GreenNodePtr green = cst::parse(loaded.sources, id, loaded.cache, parse_sink);
        const ast::File ast = ast::File::from(cst::SyntaxNode::root(green), id);
        diag::DiagnosticSink sink;
        starcore::check_placements(ast, loaded.set, sink);
        starcore::check_property_reads(ast, loaded.set, sink);
        starcore::check_failure_messages(ast, loaded.set, sink);
        starcore::GlobalIndex globals;
        globals.add_file(ast);
        globals.check(loaded.set, sink);
        // The text layer is two-phase (§13.2 lets a `$key` and the `loc`
        // that defines it sit in either order, in either file), so the
        // fixture is indexed and then the index is asked.
        starcore::TextIndex text;
        text.add_file(ast, sink);
        text.check(sink);

        std::set<std::string> reported;
        for (const diag::Diagnostic& diagnostic : sink.diagnostics()) {
            reported.emplace(diag::code_string(diagnostic.code()));
        }
        for (const std::string& code : expected) {
            if (!test::starcore_codes().contains(code)) {
                continue;
            }
            INFO("expected code: " << code);
            CHECK(reported.contains(code));
        }
    }
    CHECK(any); // a set of codes no fixture provokes is not covered at all
}

TEST_CASE("each fixture's starcore diagnostics match its checked-in snapshot",
          "[starcore][corpus][snapshot]") {
    for (const auto& path : test::corpus_files(test::corpus_dir() / "invalid")) {
        const std::string contents = test::read_bytes(path);
        bool relevant = false;
        for (const std::string& code : test::expected_codes(contents)) {
            relevant = relevant || test::starcore_codes().contains(code);
        }
        if (!relevant) {
            continue;
        }

        INFO("invalid fixture: " << path.string());
        test::LoadedSet loaded;
        loaded.load_builtin();
        loaded.load_stdlib();
        // Loaded as well as parsed: §8.8's analysis asks the registry what
        // each class declares, so a fixture's own classes and actions have
        // to be in it. Placement needed only the relation enum and so never
        // did.
        const diag::SourceId id = loaded.load_text(contents, "a library", test::corpus_name(path));
        // Viewed out of the manager that loaded it rather than parsed into a
        // second one. `GlobalIndex::check` answers out of the registry now,
        // so the span on a diagnostic about a declaration belongs to the
        // loader's source manager -- and a span rendered against a manager it
        // did not come from is garbage at best.
        diag::DiagnosticSink parse_sink;
        const cst::GreenNodePtr green = cst::parse(loaded.sources, id, loaded.cache, parse_sink);
        const ast::File ast = ast::File::from(cst::SyntaxNode::root(green), id);
        diag::DiagnosticSink sink;
        starcore::check_placements(ast, loaded.set, sink);
        starcore::check_property_reads(ast, loaded.set, sink);
        starcore::check_failure_messages(ast, loaded.set, sink);
        starcore::GlobalIndex globals;
        globals.add_file(ast);
        globals.check(loaded.set, sink);
        starcore::TextIndex text;
        text.add_file(ast, sink);
        text.check(sink);

        std::ostringstream out;
        out << "# " << test::corpus_name(path) << '\n'
            << "# starcore's passes -- placement, property reads, failureMsg\n"
            << "# placement, globals and the text layer -- over the core-owned set,\n"
            << "# stdlib, and the fixture as a library\n\n";
        bool first = true;
        for (const diag::Diagnostic& diagnostic : sink.diagnostics()) {
            if (!first) {
                out << '\n';
            }
            first = false;
            diag::render_human(out, diagnostic, loaded.sources, /*use_color=*/false);
        }
        if (first) {
            out << "(no diagnostics)\n";
        }

        const std::string name = path.stem().string() + ".txt";
        CHECK(test::check_snapshot(std::filesystem::path(STARIF_UNIT_TEST_DIR) / "starcore" /
                                       "snapshots" / name,
                                   out.str()));
    }
}

TEST_CASE("every starcore code has a fixture that provokes it", "[starcore][corpus]") {
    std::set<std::string> covered;
    for (const auto& path : test::corpus_files(test::corpus_dir() / "invalid")) {
        for (const std::string& code : test::expected_codes(test::read_bytes(path))) {
            covered.insert(code);
        }
    }

    for (const std::string& code : test::starcore_codes()) {
        INFO("starcore code without a fixture: " << code);
        CHECK(covered.contains(code));
    }
}
