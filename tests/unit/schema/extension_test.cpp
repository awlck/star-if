// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog F2d: `schema_extension`, replacement, and the no-duplicates rule.
// Spec §7.5 and §7.6.
//
// The three of them are one idea seen from three sides. A declaration is
// either new, an addition to something that exists, or a deliberate
// replacement of it -- and the format should be able to tell which without
// guessing. What it must never do is accept two declarations of the same
// thing and pick one silently, which is what happened before §7.6, and which
// is the failure an author only notices when their `take` does not take.
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

void require_clean(const test::LoadedSet& loaded) {
    for (const diag::Diagnostic& diagnostic : loaded.sink.diagnostics()) {
        INFO("unexpected " << diag::code_string(diagnostic.code()) << ": " << diagnostic.message());
        CHECK(false);
    }
}

} // namespace

// --- schema_extension (spec 7.5) ---------------------------------------

TEST_CASE("a schema_extension adds a key to a sealed core form", "[schema][extension]") {
    // This is the whole reason the form exists, and the distinction §7.2.2
    // draws: sealing prevents redefinition, not extension. A ruleset may add
    // stamina_cost to the core `action`, and may not change what `id` means.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("schema_extension = {\n"
                     "    of_schema = action\n"
                     "    key = { name = stamina_cost  type = int  default = 0 }\n"
                     "}\n");

    require_clean(loaded);
    const schema::Schema* action = loaded.set.find("action");
    REQUIRE(action != nullptr);
    REQUIRE(action->find_key("stamina_cost") != nullptr);
    CHECK(action->find_key("stamina_cost")->type.to_string() == "int");
    CHECK(action->find_key("stamina_cost")->has_default);

    // The form is still sealed, and still core's. Extension is not a way in.
    CHECK(action->sealed);
    CHECK(action->owner == "starcore");
    CHECK(action->find_key("id") != nullptr);
}

TEST_CASE("an added key is then accepted where the form is used", "[schema][extension]") {
    // The check that the extension did something, rather than merely being
    // recorded: a key that would have been unknown a moment ago now is not.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("schema_extension = {\n"
                     "    of_schema = sector\n"
                     "    key = { name = ambience  type = identifier }\n"
                     "}\n"
                     "sector = { id = station_alpha  ambience = engine_hum }\n");

    require_clean(loaded);
    CHECK_FALSE(loaded.reported(diag::Code::UnknownKey));
}

TEST_CASE("a schema_extension may not redeclare a key differently", "[schema][extension]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("schema_extension = {\n"
                     "    of_schema = action\n"
                     "    key = { name = id  type = int }\n"
                     "}\n");

    CHECK(loaded.reported(diag::Code::PropDefTypeMismatch));
    CHECK(mentions(loaded, diag::Code::PropDefTypeMismatch, "identifier"));
    CHECK(mentions(loaded, diag::Code::PropDefTypeMismatch, "extension's clothes"));

    // And `id` is still what core reads.
    CHECK(loaded.set.find("action")->find_key("id")->type.to_string() == "identifier");
}

TEST_CASE("a schema_extension redeclaring a key identically only warns", "[schema][extension]") {
    // Two libraries may both want a key and both declare it. §7.5 makes that
    // redundant rather than a conflict -- it forbids disagreement, not
    // agreement.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("schema_extension = {\n"
                     "    of_schema = sector\n"
                     "    key = { name = always_resident  type = bool }\n"
                     "}\n");

    CHECK(loaded.reported(diag::Code::PropDefRedundant));
    CHECK(loaded.sink.error_count() == 0);
    CHECK(loaded.sink.warning_count() == 1);
}

TEST_CASE("extending a form nothing declares is refused", "[schema][extension]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("schema_extension = {\n"
                     "    of_schema = acton\n"
                     "    key = { name = stamina_cost  type = int }\n"
                     "}\n");

    CHECK(loaded.reported(diag::Code::SchemaInvalid));
    CHECK(mentions(loaded, diag::Code::SchemaInvalid, "acton"));
    // Quietly creating one would produce a form nothing validates against
    // and no diagnostic at all.
    CHECK(loaded.set.find("acton") == nullptr);
}

// --- the no-duplicates rule (spec 7.6) ---------------------------------

TEST_CASE("two declarations of the same id are refused, citing both", "[schema][duplicate]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("sector = { id = station_alpha }\n"
                     "sector = { id = station_alpha  always_resident = yes }\n");

    CHECK(loaded.reported(diag::Code::SchemaDuplicate));
    const diag::Diagnostic* duplicate = loaded.first(diag::Code::SchemaDuplicate);
    REQUIRE(duplicate != nullptr);

    // Both spans, as §14.3's row requires -- an error that cites only the
    // second one leaves the author hunting for the first.
    REQUIRE_FALSE(duplicate->notes().empty());
    CHECK(duplicate->notes().front().span.has_value());
    CHECK(duplicate->notes().front().span->offset < duplicate->primary_span().offset);

    // And it says what to write if the second one was meant to win.
    CHECK(mentions(loaded, diag::Code::SchemaDuplicate, "@replaces"));
}

TEST_CASE("the rule reaches every form with a unique_in key", "[schema][duplicate]") {
    // Not just schemas and classes. Before §7.6 the uniqueness rule was
    // enforced on the two kinds the loader happened to track, which is to
    // say on the two an author is least likely to duplicate by accident.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("enum = { id = mood_enum  values = { calm } }\n"
                     "enum = { id = mood_enum  values = { angry } }\n"
                     "global = { id = turns  type = int }\n"
                     "global = { id = turns  type = int }\n");

    std::size_t duplicates = 0;
    for (const diag::Diagnostic& diagnostic : loaded.sink.diagnostics()) {
        duplicates += diagnostic.code() == diag::Code::SchemaDuplicate ? 1 : 0;
    }
    CHECK(duplicates == 2);
}

TEST_CASE("several rules are normal, because rule has no unique id", "[schema][duplicate]") {
    // §7.6 names `rule` and `loc` as unaffected, and the reason is not a
    // special case: neither declares a `unique_in` key, so the rule that is
    // written in terms of `unique_in` simply does not reach them.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("rule = { of_action = take  priority = 1 }\n"
                     "rule = { of_action = take  priority = 2 }\n");

    CHECK_FALSE(loaded.reported(diag::Code::SchemaDuplicate));
    CHECK(loaded.sink.error_count() == 0);
}

TEST_CASE("a class and a trait may share an id", "[schema][duplicate]") {
    // `unique_in` is a namespace, not a global flat space (§7.2), and the
    // core schemas put classes in `class` and traits in `trait`.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("class = { id = luminous }\n"
                     "trait = { id = luminous }\n");

    CHECK_FALSE(loaded.reported(diag::Code::SchemaDuplicate));
    CHECK(loaded.sink.error_count() == 0);
}

// --- @replaces (spec 7.6) ----------------------------------------------

TEST_CASE("@replaces supersedes a declaration from the named library", "[schema][replaces]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib_core();
    loaded.load_text("action = @replaces(star_core) {\n"
                     "    id    = take\n"
                     "    match = { \"snaffle [something]\" }\n"
                     "}\n",
                     "a_game");

    require_clean(loaded);
    const schema::SchemaSet::Declaration* take = loaded.set.find_declaration("action", "take");
    REQUIRE(take != nullptr);
    // Replacement is total: the declaration now belongs to whoever replaced
    // it, and nothing is merged from the original.
    CHECK(take->owner == "a_game");
}

TEST_CASE("@replaces naming the wrong source is refused, and says the right one",
          "[schema][replaces]") {
    // The whole value of naming a source: a typo, an upstream rename, or a
    // library that stopped shipping the thing being patched all become build
    // failures rather than a declaration that silently never takes effect.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib_core();
    loaded.load_text("action = @replaces(starscape) {\n"
                     "    id    = take\n"
                     "    match = { \"take [something]\" }\n"
                     "}\n");

    CHECK(loaded.reported(diag::Code::SchemaInvalid));
    CHECK(mentions(loaded, diag::Code::SchemaInvalid, "star_core"));

    const diag::Diagnostic* wrong = loaded.first(diag::Code::SchemaInvalid);
    REQUIRE(wrong != nullptr);
    REQUIRE_FALSE(wrong->fix_its().empty());
    CHECK(wrong->fix_its().front().replacement == "@replaces(star_core)");
}

TEST_CASE("@replaces with nothing to replace is refused", "[schema][replaces]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("sector = @replaces(star_core) { id = nowhere }\n");

    CHECK(loaded.reported(diag::Code::SchemaInvalid));
    CHECK(mentions(loaded, diag::Code::SchemaInvalid, "nowhere"));
    // The fix-it is the honest one: if this is a new declaration, drop the
    // annotation rather than inventing a source for it.
    const diag::Diagnostic* missing = loaded.first(diag::Code::SchemaInvalid);
    REQUIRE_FALSE(missing->fix_its().empty());
    CHECK(missing->fix_its().front().replacement.empty());
}

TEST_CASE("@replaces on a sealed declaration is refused", "[schema][replaces]") {
    // "Extend freely, never supersede" -- §7.2.2's rule stated the other way
    // round. This is the case sealing exists for.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("schema = @replaces(starcore) {\n"
                     "    id        = class\n"
                     "    top_level = yes\n"
                     "    key = { name = id  type = identifier  required = yes }\n"
                     "}\n");

    CHECK(loaded.reported(diag::Code::SchemaSealed));
    CHECK(mentions(loaded, diag::Code::SchemaSealed, "extend freely, never supersede"));

    // The core form is intact, with all its keys.
    const schema::Schema* form = loaded.set.find("class");
    REQUIRE(form != nullptr);
    CHECK(form->owner == "starcore");
    CHECK(form->find_key("prop_def") != nullptr);
}

TEST_CASE("a sealed core class cannot be replaced either", "[schema][replaces]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("class = @replaces(starcore) { id = starcore.room }\n");

    CHECK(loaded.reported(diag::Code::SchemaSealed));
    const schema::ClassDecl* room = loaded.set.find_class("starcore.room");
    REQUIRE(room != nullptr);
    CHECK(room->of_class == "starcore.object");
}

// --- provides_schema as a manifest (spec 13.3) -------------------------

TEST_CASE("provides_schema is checked against what the library declares", "[schema][manifest]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("library = { id = ghost_forms  provides_schema = { stat_block } }\n",
                     "ghost_forms");

    diag::DiagnosticSink manifests;
    schema::check_library_manifests(loaded.set, manifests);

    REQUIRE(manifests.diagnostics().size() == 1);
    CHECK(manifests.diagnostics().front().code() == diag::Code::ProvidesMismatch);
    CHECK(manifests.diagnostics().front().severity() == diag::Severity::Warning);
    CHECK(manifests.diagnostics().front().message().find("stat_block") != std::string::npos);
}

TEST_CASE("a form declared and not listed is reported too", "[schema][manifest]") {
    // Both directions, because the manifest is what a reader and the
    // editor's library browser go by: a form missing from it is a form
    // nobody finds.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("library = { id = quiet  provides_schema = { } }\n"
                     "schema = { id = loot_table  top_level = yes\n"
                     "           key = { name = id  type = identifier  required = yes } }\n",
                     "quiet");

    diag::DiagnosticSink manifests;
    schema::check_library_manifests(loaded.set, manifests);

    REQUIRE(manifests.diagnostics().size() == 1);
    CHECK(manifests.diagnostics().front().message().find("loot_table") != std::string::npos);
}

TEST_CASE("a library that says nothing about its forms is not nagged", "[schema][manifest]") {
    // Saying nothing is not a mismatch. `provides_schema` is optional, and a
    // warning for omitting it would be a warning on almost every library.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("library = { id = quiet  version = \"1.0.0\" }\n"
                     "schema = { id = loot_table  top_level = yes\n"
                     "           key = { name = id  type = identifier  required = yes } }\n",
                     "quiet");

    diag::DiagnosticSink manifests;
    schema::check_library_manifests(loaded.set, manifests);
    CHECK(manifests.diagnostics().empty());
}

TEST_CASE("stdlib core's own manifest agrees with what it declares", "[schema][manifest]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib_core();

    diag::DiagnosticSink manifests;
    schema::check_library_manifests(loaded.set, manifests);
    for (const diag::Diagnostic& diagnostic : manifests.diagnostics()) {
        INFO("manifest mismatch: " << diagnostic.message());
        CHECK(false);
    }
}
