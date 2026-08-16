// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog F2a, the anti-wart task. Spec §7.2.2:
//
//   > If `starcore` requires something to be defined a particular way, it
//   > MUST assert that requirement rather than assume it will be met.
//
// Each test below is a library attempting something only core may do, and
// each asserts two things: that it was refused, and that the refusal named
// the owner. The second half matters as much as the first. ADRIFT 5 and
// Inform 7 both fail here not by permitting the wrong thing but by failing
// bewilderingly, and a rejection that does not say whose rule was broken is
// only a slightly faster kind of bewilderment.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "stardata/diag/codes.hpp"

#include "support/schema_harness.hpp"

using namespace stardata;

namespace {

// True when some diagnostic of this code, or one of its notes, says `text`.
// The messages are the deliverable here, so the tests read them.
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

TEST_CASE("a library cannot redefine a sealed core form", "[schema][sealing]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("schema = {\n"
                     "    id = sector\n"
                     "    top_level = yes\n"
                     "    key = { name = id  type = identifier  required = yes }\n"
                     "}\n");

    CHECK(loaded.reported(diag::Code::SchemaSealed));
    CHECK(mentions(loaded, diag::Code::SchemaSealed, "starcore"));
    CHECK(mentions(loaded, diag::Code::SchemaSealed, "provides_schema"));

    // And the original survived: a rejected declaration changes nothing.
    const schema::Schema* sector = loaded.set.find("sector");
    REQUIRE(sector != nullptr);
    CHECK(sector->owner == "starcore");
    CHECK(sector->find_key("always_resident") != nullptr);
}

TEST_CASE("a library cannot redefine a sealed core class", "[schema][sealing]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("class = {\n"
                     "    id = starcore.object\n"
                     "    prop_def = { holder = int }\n"
                     "}\n");

    CHECK(loaded.reported(diag::Code::SchemaSealed));
    CHECK(mentions(loaded, diag::Code::SchemaSealed, "starcore"));
    CHECK(mentions(loaded, diag::Code::SchemaSealed, "class_extension"));

    // The real `holder` is untouched.
    const schema::ClassDecl* object = loaded.set.find_class("starcore.object");
    REQUIRE(object != nullptr);
    REQUIRE(object->find_property("holder") != nullptr);
    CHECK(object->find_property("holder")->type.to_string() == "ref<starcore.object>");
}

TEST_CASE("a library cannot redefine a sealed core trait", "[schema][sealing]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("trait = { id = starcore.actor  prop_def = { busy_until = text } }\n");

    CHECK(loaded.reported(diag::Code::SchemaSealed));
    const schema::ClassDecl* actor = loaded.set.find_class("starcore.actor");
    REQUIRE(actor != nullptr);
    CHECK(actor->find_property("busy_until")->type.to_string() == "int");
}

TEST_CASE("a class_extension may add a property to a core class", "[schema][sealing]") {
    // The permitted half, and the more important one: sealing is not a wall.
    // §7.2.2's last line is "Adding is always permitted."
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("class_extension = {\n"
                     "    of_class = starcore.object\n"
                     "    prop_def = { smells_of = text }\n"
                     "}\n");

    for (const diag::Diagnostic& diagnostic : loaded.sink.diagnostics()) {
        INFO("unexpected " << diag::code_string(diagnostic.code()) << ": " << diagnostic.message());
        CHECK(false);
    }

    const schema::ClassDecl* object = loaded.set.find_class("starcore.object");
    REQUIRE(object != nullptr);
    REQUIRE(object->find_property("smells_of") != nullptr);
    CHECK(object->find_property("smells_of")->type.to_string() == "text");
    CHECK(object->find_property("holder") != nullptr); // and nothing was lost
}

TEST_CASE("a class_extension may not retype a core property", "[schema][sealing]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("class_extension = {\n"
                     "    of_class = starcore.object\n"
                     "    prop_def = { holder = int }\n"
                     "}\n");

    CHECK(loaded.reported(diag::Code::PropDefTypeMismatch));
    CHECK(mentions(loaded, diag::Code::PropDefTypeMismatch, "ref<starcore.object>"));
    CHECK(mentions(loaded, diag::Code::PropDefTypeMismatch, "sealed"));

    // The type is the one core reads, not the one the library asked for.
    const schema::ClassDecl* object = loaded.set.find_class("starcore.object");
    CHECK(object->find_property("holder")->type.to_string() == "ref<starcore.object>");
}

TEST_CASE("re-declaring a property at the same type is not an error", "[schema][sealing]") {
    // Two libraries may both want a property and both declare it. That is a
    // no-op, not a conflict -- §7.2.2 forbids retyping, not agreeing.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("class_extension = {\n"
                     "    of_class = starcore.object\n"
                     "    prop_def = { holder = ref<starcore.object> }\n"
                     "}\n");

    CHECK_FALSE(loaded.reported(diag::Code::PropDefTypeMismatch));
    CHECK(loaded.sink.error_count() == 0);
}

TEST_CASE("a class_extension may not change what a class inherits from", "[schema][sealing]") {
    // §8.2: an extension names its target with `of_class`, so changing the
    // parent is spelled with a second one.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("class_extension = {\n"
                     "    of_class = starcore.room\n"
                     "    of_class = starcore.actor\n"
                     "}\n");

    CHECK(loaded.reported(diag::Code::CoreReparent));
    CHECK(mentions(loaded, diag::Code::CoreReparent, "starcore.room"));

    const schema::ClassDecl* room = loaded.set.find_class("starcore.room");
    REQUIRE(room != nullptr);
    CHECK(room->of_class == "starcore.object");
}

TEST_CASE("extending a class nothing declares is refused", "[schema][sealing]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("class_extension = { of_class = starcore.hovercraft }\n");

    CHECK(loaded.reported(diag::Code::SchemaInvalid));
    CHECK(mentions(loaded, diag::Code::SchemaInvalid, "starcore.hovercraft"));
}

TEST_CASE("an absent core requirement is reported by name", "[schema][sealing]") {
    // The third bullet of §7.2.2, and the one that is easiest to leave out:
    // "the absence of anything core requires -- reported at load, naming the
    // requirement, rather than surfacing as a failure later."
    //
    // Loading only requirements.star, with nothing to satisfy it, is the
    // sharpest form of the test: every requirement fails at once, and each
    // failure has to name itself.
    test::LoadedSet loaded;
    const diag::SourceId id = loaded.sources.add_file(
        "requirements.star", "core_requirement = { id = object_holder  requires = property\n"
                             "                     subject = starcore.object  member = holder\n"
                             "                     type = ref<starcore.object>\n"
                             "                     doc = \"The containment parent.\" }\n");
    // The `core_requirement` form is not declared here, so the top-level
    // check will complain about that too -- which is correct, and not what
    // this test is about.
    schema::load_source(id, schema::LoadOptions{"starcore"}, loaded.sources, loaded.cache,
                        loaded.set, loaded.sink);

    diag::DiagnosticSink requirements;
    schema::check_requirements(loaded.set, requirements);

    REQUIRE(requirements.diagnostics().size() == 1);
    const diag::Diagnostic& failure = requirements.diagnostics().front();
    CHECK(failure.code() == diag::Code::CoreRequirement);
    CHECK(failure.message().find("starcore.object") != std::string::npos);

    // Named, and with the reason attached: an author who has never heard of
    // a containment slot is told what it is for, not merely that it is gone.
    bool named = false;
    bool explained = false;
    for (const diag::Note& note : failure.notes()) {
        named = named || note.message.find("object_holder") != std::string::npos;
        explained = explained || note.message.find("containment parent") != std::string::npos;
    }
    CHECK(named);
    CHECK(explained);
}

TEST_CASE("a core requirement catches a property that changed type", "[schema][sealing]") {
    // The requirement guards the built-in set against its own edits, which
    // is what stops the anti-wart machinery from being a rule that only
    // applies to other people.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("class = { id = probe.object  prop_def = { holder = int } }\n"
                     "core_requirement = { id = probe_holder  requires = property\n"
                     "                     subject = probe.object  member = holder\n"
                     "                     type = ref<starcore.object>\n"
                     "                     doc = \"A stand-in for the real slot.\" }\n",
                     "starcore");

    diag::DiagnosticSink requirements;
    schema::check_requirements(loaded.set, requirements);

    REQUIRE(requirements.diagnostics().size() == 1);
    const std::string& message = requirements.diagnostics().front().message();
    CHECK(message.find("probe.object.holder") != std::string::npos);
    CHECK(message.find("ref<starcore.object>") != std::string::npos);
    CHECK(message.find("int") != std::string::npos);
}

// --- ordinary key validation, which the bootstrap needs -----------------

TEST_CASE("an unknown key in a closed form is refused", "[schema][sealing]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("sector = { id = station_alpha  alwyas_resident = yes }\n");

    CHECK(loaded.reported(diag::Code::UnknownKey));
    CHECK(mentions(loaded, diag::Code::UnknownKey, "alwyas_resident"));
}

TEST_CASE("an unknown key in an open form is permitted", "[schema][sealing]") {
    // §7.3, and §8.1's reason for it: any key on a class that is not one of
    // the declared few sets a default for a property of that name.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("class = { id = crate  anything_at_all = 3 }\n");

    CHECK_FALSE(loaded.reported(diag::Code::UnknownKey));
}

TEST_CASE("a required key that is absent is refused", "[schema][sealing]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("enum = { id = mood_enum }\n"); // no `values`

    CHECK(loaded.reported(diag::Code::KeyMissing));
    CHECK(mentions(loaded, diag::Code::KeyMissing, "values"));
}

TEST_CASE("a top-level statement naming nothing at all is refused", "[schema][sealing]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("hovercraft = { id = eels }\n");

    CHECK(loaded.reported(diag::Code::UnknownKey));
    CHECK(mentions(loaded, diag::Code::UnknownKey, "hovercraft"));
}

TEST_CASE("a nested block shape cannot be written at the top level", "[schema][sealing]") {
    // `prop_def` describes the inside of a class, not a thing a file holds.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("prop_def = { open = bool }\n");

    CHECK(loaded.reported(diag::Code::UnknownKey));
    CHECK(mentions(loaded, diag::Code::UnknownKey, "top_level"));
}

TEST_CASE("instantiating a declared class is not an unknown key", "[schema][sealing]") {
    // §7.4: a statement whose key is a class id creates one of that class.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib_core();
    loaded.load_text("thing = { id = brass_key  name = \"brass key\" }\n");

    CHECK_FALSE(loaded.reported(diag::Code::UnknownKey));
    CHECK(loaded.sink.error_count() == 0);
}

TEST_CASE("a trait may not declare a parent", "[schema][sealing]") {
    // §8.3: a trait MUST NOT participate in the class hierarchy.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("trait = { id = shiny  of_class = thing }\n");

    CHECK(loaded.reported(diag::Code::SchemaInvalid));
    CHECK(mentions(loaded, diag::Code::SchemaInvalid, "cut across the class tree"));
}
