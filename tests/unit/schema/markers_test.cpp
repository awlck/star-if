// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog F2b: markers, spec §7.2.3.
//
// A marker lets core act on a property without knowing its name: there is a
// surface the author writes and a fact the engine reads, and the job is to
// keep those two from drifting apart without the engine ever memorising a
// name.
//
// F2c's placement tests used to live here too, since the two are the same
// idea from opposite ends. They moved to tests/unit/starcore/placement_test.cpp
// when proposal §2.1.1 drew the mechanism/vocabulary line: the marker
// *mechanism* is `stardata`'s and containment is not. What is left here is
// the half that belongs to the format library, and the split is why the
// remaining tests never name a marker the schema does not declare.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "stardata/diag/codes.hpp"

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
    const schema::ClassDecl* trait = loaded.set.find_trait("shuttered");
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
    const schema::ClassDecl* trait = loaded.set.find_trait("shuttered");
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

// --- the vocabulary is data, and this proves it -------------------------

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
    const schema::ClassDecl* trait = loaded.set.find_trait("psychic");
    REQUIRE(trait != nullptr);
    REQUIRE(trait->find_property("thoughts") != nullptr);
    CHECK(trait->find_property("thoughts")->markers.is_set("telepathic"));

    // And it is the only one set: reading is by name, not by position.
    REQUIRE(trait->find_property("thoughts")->markers.all().size() == 1);
    CHECK(trait->find_property("thoughts")->markers.all().front().first == "telepathic");
}
