// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog F2b (markers, spec §7.2.3) and F2c (placement sugar, spec §8.5).
//
// Both are small, and both are the same idea from opposite ends. A marker
// lets core act on a property without knowing its name; the placement sugar
// lets an author write the common case without spelling out the two slots it
// sets. In each case there is a surface the author writes and a fact the
// engine reads, and the job is to keep those two things from drifting apart
// -- without the engine ever memorising a name, and without the tree ever
// losing the author's spelling.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "stardata/diag/codes.hpp"

#include "support/cst_harness.hpp"
#include "support/schema_harness.hpp"

using namespace stardata;

namespace {

[[nodiscard]] bool mentions(const test::LoadedSet& loaded, diag::Code code, std::string_view text) {
    for (const diag::Diagnostic& diagnostic : loaded.sink.diagnostics()) {
        if (diagnostic.code() != code) {
            continue;
        }
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

// The placement a block declares.
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

    [[nodiscard]] std::optional<schema::Placement> placement() {
        const ast::Block block = *parsed.ast().statements()[0].value()->as_block();
        return schema::read_placement(block, loaded.set.relation_values(), sink);
    }
};

} // namespace

// --- markers (spec 7.2.3) ----------------------------------------------

TEST_CASE("a property declares its markers, and they reach the engine", "[schema][markers]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("trait = {\n"
                     "    id = shuttered\n"
                     "    prop_def = {\n"
                     "        shuttered = { type = bool  affects_scope = yes }\n"
                     "        panes     = { type = int   save_exclude = yes }\n"
                     "        frame     = int\n"
                     "    }\n"
                     "}\n");

    CHECK(loaded.sink.error_count() == 0);
    const schema::ClassDecl* trait = loaded.set.find_class("shuttered");
    REQUIRE(trait != nullptr);

    // The marker form and the bare form declare the same thing about the
    // type; they differ only in what else they say.
    REQUIRE(trait->find_property("shuttered") != nullptr);
    CHECK(trait->find_property("shuttered")->type.to_string() == "bool");
    CHECK(trait->find_property("shuttered")->markers.is_set("affects_scope"));
    CHECK_FALSE(trait->find_property("shuttered")->markers.is_set("save_exclude"));

    CHECK(trait->find_property("panes")->markers.is_set("save_exclude"));
    CHECK_FALSE(trait->find_property("panes")->markers.is_set("affects_scope"));

    // A bare type carries no markers at all, which is the common case --
    // not three markers that happen to be false.
    REQUIRE(trait->find_property("frame") != nullptr);
    CHECK(trait->find_property("frame")->markers.empty());
    CHECK(trait->find_property("frame")->type.to_string() == "int");
    CHECK_FALSE(trait->find_property("frame")->markers.is_set("affects_scope"));
    CHECK_FALSE(trait->find_property("frame")->markers.is_set("always_resident"));
    CHECK_FALSE(trait->find_property("frame")->markers.is_set("save_exclude"));
}

TEST_CASE("the engine learns which properties affect scope, not their names", "[schema][markers]") {
    // §7.2.3's whole argument, as a test. Two libraries, two different
    // property names, the same fact reaching core. Nothing here knows the
    // word `open`.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    loaded.load_text("trait = {\n"
                     "    id = shuttered\n"
                     "    prop_def = { shuttered = { type = bool  affects_scope = yes } }\n"
                     "}\n",
                     "a_ruleset");

    std::vector<std::string> scope_properties;
    for (const schema::ClassDecl& declared : loaded.set.classes()) {
        for (const schema::PropDecl& property : declared.properties) {
            if (property.markers.is_set("affects_scope")) {
                scope_properties.push_back(declared.id + "." + property.name);
            }
        }
    }

    // stdlib's `open`, `locked` and `lit_radius`, plus the ruleset's own.
    CHECK(scope_properties.size() >= 4);
    bool found_library = false;
    bool found_ruleset = false;
    for (const std::string& name : scope_properties) {
        found_library = found_library || name == "openable.open";
        found_ruleset = found_ruleset || name == "shuttered.shuttered";
    }
    CHECK(found_library);
    CHECK(found_ruleset);
}

TEST_CASE("an unknown marker is refused, not ignored", "[schema][markers]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("trait = {\n"
                     "    id = shuttered\n"
                     "    prop_def = { shuttered = { type = bool  affect_scope = yes } }\n"
                     "}\n");

    CHECK(loaded.reported(diag::Code::UnknownKey));
    CHECK(mentions(loaded, diag::Code::UnknownKey, "affect_scope"));
    // The message lists the vocabulary, since the mistake is nearly always a
    // near-miss and the set is small enough to print.
    CHECK(mentions(loaded, diag::Code::UnknownKey, "affects_scope"));
    CHECK(mentions(loaded, diag::Code::UnknownKey, "save_exclude"));

    // The property still exists, at its declared type: one bad marker is not
    // a reason to lose the declaration.
    const schema::ClassDecl* trait = loaded.set.find_class("shuttered");
    REQUIRE(trait != nullptr);
    REQUIRE(trait->find_property("shuttered") != nullptr);
    CHECK(trait->find_property("shuttered")->type.to_string() == "bool");
}

TEST_CASE("the marker vocabulary is data, not a list in the code", "[schema][markers]") {
    // §7.2.3 names three markers. They are declared as the `prop_marker`
    // form, so adding one is an edit to libs/starcore/builtin/ and a line in
    // starcore that acts on it -- not a C++ list nobody can see.
    test::LoadedSet loaded;
    loaded.load_builtin();

    const schema::Schema* markers = loaded.set.find("prop_marker");
    REQUIRE(markers != nullptr);
    CHECK(markers->sealed);
    CHECK_FALSE(markers->top_level); // it describes the inside of a prop_def
    for (const char* marker : {"type", "affects_scope", "always_resident", "save_exclude"}) {
        INFO("marker: " << marker);
        CHECK(markers->find_key(marker) != nullptr);
    }
}

// --- placement sugar (spec 8.5) ----------------------------------------

TEST_CASE("a relation keyword desugars to holder and relation", "[schema][placement]") {
    for (const char* keyword : {"in", "on", "under", "behind", "carried", "worn", "part_of"}) {
        INFO("relation: " << keyword);
        Placed placed(std::string("    ") + keyword + " = ornate_box\n");
        const std::optional<schema::Placement> placement = placed.placement();

        REQUIRE(placement);
        CHECK(placement->holder == "ornate_box");
        CHECK(placement->relation == keyword);
        CHECK(placement->from_sugar);
        CHECK(placed.sink.error_count() == 0);
    }
}

TEST_CASE("both spellings produce identical data", "[schema][placement]") {
    // §8.5: "Both spellings are legal and produce identical data."
    Placed sugar("    in = ornate_box\n");
    Placed longhand("    holder = ornate_box\n    relation = in\n");

    const std::optional<schema::Placement> from_sugar = sugar.placement();
    const std::optional<schema::Placement> from_longhand = longhand.placement();

    REQUIRE(from_sugar);
    REQUIRE(from_longhand);
    CHECK(from_sugar->holder == from_longhand->holder);
    CHECK(from_sugar->relation == from_longhand->relation);

    // The one thing that differs is which spelling was used, which is what
    // an editor writing the file back needs to know.
    CHECK(from_sugar->from_sugar);
    CHECK_FALSE(from_longhand->from_sugar);
}

TEST_CASE("an object with no placement has none", "[schema][placement]") {
    // A root object, or one placed at run time. Not an error.
    Placed placed("    name = \"a brass key\"\n");
    CHECK_FALSE(placed.placement());
    CHECK(placed.sink.error_count() == 0);
}

TEST_CASE("writing both spellings is refused", "[schema][placement]") {
    Placed placed("    in = ornate_box\n    holder = mess_table\n    relation = on\n");
    const std::optional<schema::Placement> placement = placed.placement();

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

TEST_CASE("two relation keywords are the same conflict", "[schema][placement]") {
    Placed placed("    in = ornate_box\n    on = mess_table\n");
    CHECK_FALSE(placed.placement());
    REQUIRE(placed.sink.diagnostics().size() == 1);
    CHECK(placed.sink.diagnostics().front().code() == diag::Code::PlacementConflict);
}

TEST_CASE("the conflict is caught where an author writes it", "[schema][placement]") {
    // Not just through the helper: an ordinary instantiation, loaded the way
    // a game's file is, because that is the only place it can be written.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    loaded.load_text("thing = { id = confused_key  in = ornate_box  holder = mess_table }\n");

    CHECK(loaded.reported(diag::Code::PlacementConflict));
    CHECK(mentions(loaded, loaded.first(diag::Code::PlacementConflict)->code(), "same two slots"));
}

TEST_CASE("the sugar is expanded in the view and never in the tree", "[schema][placement]") {
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
    const std::optional<schema::Placement> placement =
        schema::read_placement(block, loaded.set.relation_values(), sink);
    REQUIRE(placement);
    CHECK(placement->relation == "in");

    // And after reading it, the bytes are still the author's.
    CHECK(parsed.written() == source);
    CHECK_FALSE(block.find("holder")); // nothing was inserted
    CHECK_FALSE(block.find("relation"));
}

TEST_CASE("stdlib and the corpus both keep the placement rule", "[schema][placement]") {
    // The reference corpus places objects with the sugar throughout; loading
    // stdlib is the check that the rule does not fire on correct files.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    CHECK_FALSE(loaded.reported(diag::Code::PlacementConflict));
}

// --- the vocabularies are data, and these prove it ---------------------

TEST_CASE("a marker added to the schema is read with no code change", "[schema][markers]") {
    // The sharpest form of §7.2.3's claim, and the one that would fail if
    // the markers were named fields in C++: extend `prop_marker` with a
    // marker nothing in this repository has ever heard of, and it is read.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("schema_extension = {\n"
                     "    of_schema = prop_marker\n"
                     "    key = { name = telepathic  type = bool }\n"
                     "}\n"
                     "trait = {\n"
                     "    id = psychic\n"
                     "    prop_def = { thoughts = { type = text  telepathic = yes } }\n"
                     "}\n");

    CHECK(loaded.sink.error_count() == 0);
    const schema::ClassDecl* trait = loaded.set.find_class("psychic");
    REQUIRE(trait != nullptr);
    REQUIRE(trait->find_property("thoughts") != nullptr);
    CHECK(trait->find_property("thoughts")->markers.is_set("telepathic"));

    // And it is the only one set: reading is by name, not by position.
    REQUIRE(trait->find_property("thoughts")->markers.all().size() == 1);
    CHECK(trait->find_property("thoughts")->markers.all().front().first == "telepathic");
}

TEST_CASE("the relation keywords come from the enum, not from the code", "[schema][placement]") {
    // Point the placement vocabulary at a different enum and the keywords
    // follow it. If the seven words of §8.5 were a list in the code this
    // could not work, and `in` would still be sugar here.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("enum = { id = mounting_enum  values = { bolted welded } }\n");
    loaded.set.set_relation_enum("mounting_enum");

    CHECK(loaded.set.relation_values().size() == 2);

    test::Parsed bolted("thing = { id = plate  bolted = bulkhead }\n");
    diag::DiagnosticSink sink;
    const std::optional<schema::Placement> placement = schema::read_placement(
        *bolted.ast().statements()[0].value()->as_block(), loaded.set.relation_values(), sink);

    REQUIRE(placement);
    CHECK(placement->relation == "bolted");
    CHECK(placement->holder == "bulkhead");

    // And `in` is now just a key like any other, because nothing in the code
    // ever knew it.
    test::Parsed inside("thing = { id = key  in = box }\n");
    diag::DiagnosticSink other;
    CHECK_FALSE(schema::read_placement(*inside.ast().statements()[0].value()->as_block(),
                                       loaded.set.relation_values(), other));
    CHECK(other.diagnostics().empty());
}

TEST_CASE("relation_enum names the enum starcore.object is typed by", "[schema][placement]") {
    // The link that makes the harness's `set_relation_enum` the right name
    // rather than a guess: `starcore.object.relation` is declared at
    // `enum<relation_enum>`, and a core_requirement asserts it.
    test::LoadedSet loaded;
    loaded.load_builtin();

    const schema::ClassDecl* object = loaded.set.find_class("starcore.object");
    REQUIRE(object != nullptr);
    REQUIRE(object->find_property("relation") != nullptr);
    CHECK(object->find_property("relation")->type.to_string() == "enum<relation_enum>");

    const schema::EnumDecl* relations = loaded.set.find_enum("relation_enum");
    REQUIRE(relations != nullptr);
    CHECK(relations->values.size() == 7);
    for (const char* keyword : {"in", "on", "under", "behind", "carried", "worn", "part_of"}) {
        INFO("relation: " << keyword);
        CHECK(relations->has_value(keyword));
    }
}
