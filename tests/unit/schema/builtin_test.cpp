// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog F2: the core-owned schema set loads, validates against the
// hard-coded schema-of-schemas, and satisfies every requirement it states
// about itself.
//
// That last part is F2a's point and worth being explicit about: the
// requirements in libs/starcore/builtin/requirements.star guard the builtin
// set against its own edits as much as against a library's. Delete a slot
// from starcore.object and this test fails by name, here, rather than in
// Phase 1 when something tries to read it.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "stardata/diag/codes.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/diag/source_manager.hpp"
#include "stardata/schema/loader.hpp"

#include "support/schema_harness.hpp"

using namespace stardata;

TEST_CASE("the built-in schema set loads with no diagnostic at all", "[schema][builtin]") {
    test::LoadedSet loaded;
    loaded.load_builtin();

    for (const diag::Diagnostic& diagnostic : loaded.sink.diagnostics()) {
        INFO("unexpected " << diag::code_string(diagnostic.code()) << ": " << diagnostic.message());
        CHECK(false);
    }
    CHECK(loaded.sink.error_count() == 0);
    CHECK(loaded.files.size() >= 3);
}

TEST_CASE("every core-owned form of spec 7.2.4 is declared and sealed", "[schema][builtin]") {
    test::LoadedSet loaded;
    loaded.load_builtin();

    // The table in 7.2.4, first half. requirements.star asserts the same
    // list; this asserts it a second time, in C++, so that deleting a
    // requirement cannot quietly delete the check along with it.
    for (const char* form : {"class", "class_extension", "trait", "enum", "global", "const",
                             "action", "rule", "turn_hook", "sector", "project", "library"}) {
        INFO("core-owned form: " << form);
        const schema::Schema* declared = loaded.set.find(form);
        REQUIRE(declared != nullptr);
        CHECK(declared->sealed);
        CHECK(declared->owner == "starcore");
    }

    // And the hard-coded one, which is not in any file.
    CHECK(schema::schema_of_schemas().id == "schema");
    CHECK(schema::schema_of_schemas().sealed);
}

TEST_CASE("the root class carries exactly the slots of spec 8.1.1", "[schema][builtin]") {
    test::LoadedSet loaded;
    loaded.load_builtin();

    const schema::ClassDecl* object = loaded.set.find_class("starcore.object");
    REQUIRE(object != nullptr);
    CHECK(object->sealed);
    CHECK(object->of_class.empty()); // the root derives from nothing

    for (const auto& [name, type] :
         {std::pair<const char*, const char*>{"holder", "ref<starcore.object>"},
          {"relation", "enum<relation_enum>"},
          {"sector", "ref<sector>"},
          {"present_in", "set<ref<starcore.room>>"},
          {"name", "text"},
          {"synonyms", "list<identifier>"}}) {
        INFO("slot: " << name);
        const schema::PropDecl* property = object->find_property(name);
        REQUIRE(property != nullptr);
        CHECK(property->type.to_string() == type);
    }
}

TEST_CASE("every core requirement the built-in set states is met", "[schema][builtin]") {
    test::LoadedSet loaded;
    loaded.load_builtin();

    diag::DiagnosticSink sink;
    schema::check_requirements(loaded.set, sink);

    for (const diag::Diagnostic& diagnostic : sink.diagnostics()) {
        INFO("unmet: " << diagnostic.message());
        CHECK(false);
    }
    CHECK(sink.error_count() == 0);

    // Not a fixed number -- the list is meant to grow -- but a floor, so
    // that "all requirements met" cannot become true by there being none.
    CHECK(loaded.set.requirements().size() >= 20);
}

TEST_CASE("every requirement has an id, a kind and a reason", "[schema][builtin]") {
    // "Every core requirement is checked at load and reported by name -- no
    // requirement is left implicit." A requirement with no doc is one whose
    // failure would tell an author what is wrong but not why it matters.
    test::LoadedSet loaded;
    loaded.load_builtin();
    REQUIRE_FALSE(loaded.set.requirements().empty());

    for (const schema::CoreRequirement& requirement : loaded.set.requirements()) {
        INFO("requirement: " << requirement.id);
        CHECK_FALSE(requirement.id.empty());
        CHECK_FALSE(requirement.subject.empty());
        CHECK_FALSE(requirement.doc.empty());
        CHECK((requirement.kind == "form" || requirement.kind == "class" ||
               requirement.kind == "trait" || requirement.kind == "property" ||
               requirement.kind == "parent"));
        if (requirement.kind == "property" || requirement.kind == "parent") {
            CHECK_FALSE(requirement.member.empty());
        }
    }
}
