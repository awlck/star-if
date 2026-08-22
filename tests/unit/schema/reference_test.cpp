// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Reference resolution (spec §6.2, §7.4, §13.2; backlog F9).
//
// §6.2 gives `ref<C>` one sentence -- "a reference to an object of class `C`
// or a subclass; validated at compile time" -- and the last three words are
// the whole task. Until F9 any identifier satisfied any `ref`, so a room that
// had been renamed, a typo, and a working link were the same thing to the
// compiler and different things to the player.
//
// TWO NAMESPACES, because `ref<C>` takes two kinds of target and the built-in
// set uses both. `ref<room>` names an OBJECT -- §7.4's instantiations.
// `ref<action>` names an INSTANCE OF A FORM, in whatever namespace that form
// declares itself unique in (§7.2).
//
// THE ORDER MUST NOT MATTER (§13.2). Half these tests write the reference
// before the thing it names, or in another file entirely, because that is the
// case a single-pass loader gets wrong -- and the reason the load runs
// declarations to completion across every file before it validates anything.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "stardata/diag/codes.hpp"
#include "stardata/schema/loader.hpp"

#include "support/schema_harness.hpp"

using namespace stardata;

namespace {

[[nodiscard]] const diag::Diagnostic* first_of(const test::LoadedSet& loaded, diag::Code code) {
    for (const diag::Diagnostic& diagnostic : loaded.sink.diagnostics()) {
        if (diagnostic.code() == code) {
            return &diagnostic;
        }
    }
    return nullptr;
}

[[nodiscard]] bool mentions(const diag::Diagnostic& diagnostic, std::string_view text) {
    if (diagnostic.message().find(text) != std::string::npos) {
        return true;
    }
    for (const diag::Note& note : diagnostic.notes()) {
        if (note.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

// --- the registry (spec §7.4) -------------------------------------------

TEST_CASE("an instantiation registers an object", "[schema][reference]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    loaded.load_text("room  = { id = your_cell }\n"
                     "thing = { id = brass_key  holder = your_cell }\n");

    REQUIRE(loaded.set.find_object("your_cell") != nullptr);
    CHECK(loaded.set.find_object("your_cell")->class_id == "room");
    CHECK(loaded.set.find_object("brass_key")->class_id == "thing");
    CHECK(loaded.sink.error_count() == 0);
}

TEST_CASE("a trait mixed in by name does not create an object", "[schema][reference]") {
    // §7.4: a top-level statement instantiates a *class*. `openable` is a
    // trait, so `openable = { ... }` is not an instantiation and there is no
    // object of that name for a `ref` to find -- which is the same rule
    // `check_top_level` applies when it refuses the statement.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    REQUIRE(loaded.set.find_trait("openable") != nullptr);
    CHECK(loaded.set.find_object("openable") == nullptr);
}

// --- §13.2, in any order and any file -----------------------------------

TEST_CASE("a reference may be written before its target", "[schema][reference]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    loaded.load_text("thing = { id = brass_key  holder = ornate_box }\n"
                     "thing = { id = ornate_box }\n");

    CHECK(loaded.sink.error_count() == 0);
}

TEST_CASE("a reference may name an object in another file", "[schema][reference]") {
    // The case a loader that resolved as it read would get wrong, and the
    // reason a load finishes every declaration across every file before it
    // validates one of them. One load, two files, the reference first.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    loaded.load_texts({{"uses.star", "thing = { id = brass_key  holder = ornate_box }\n"},
                       {"declares.star", "thing = { id = ornate_box }\n"}},
                      "a game");

    CHECK(loaded.sink.error_count() == 0);
}

TEST_CASE("a later load sees an earlier one's objects", "[schema][reference]") {
    // §13.2's other direction, and the one that must NOT be symmetric: a
    // project may name what its libraries declared, and a library may not
    // name what the project will. Two loads, in order.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    loaded.load_text("thing = { id = ornate_box }\n", "a library", "lib.star");
    loaded.load_text("thing = { id = brass_key  holder = ornate_box }\n", "a game", "game.star");

    CHECK(loaded.sink.error_count() == 0);
}

TEST_CASE("a class_extension in a later file applies before instantiations are checked",
          "[schema][reference]") {
    // Not F9's bullet, but F9's pass split is what fixed it: the extension
    // used to be applied while validation was already running, so an object
    // written above it was checked against a class that had not grown the
    // property yet.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    loaded.load_text("thing = { id = brass_key  serial = 4 }\n"
                     "class_extension = { of_class = thing  prop_def = { serial = int } }\n");

    CHECK_FALSE(loaded.reported(diag::Code::UnknownKey));
    CHECK(loaded.sink.error_count() == 0);
}

// --- §6.2, unresolvable ---------------------------------------------------

TEST_CASE("a ref to no object is an error naming the object", "[schema][reference]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    loaded.load_text("thing = { id = ornate_box }\n"
                     "thing = { id = brass_key  holder = ornate_bx }\n");

    const diag::Diagnostic* reported = first_of(loaded, diag::Code::RefUnresolved);
    REQUIRE(reported != nullptr);
    CHECK(mentions(*reported, "'ornate_bx'"));
    CHECK(mentions(*reported, "ref<starcore.object>"));

    // §14.3 asks for a suggestion, and the near miss is the case that earns
    // it: a rename leaves the old spelling one edit away from the new one.
    REQUIRE_FALSE(reported->fix_its().empty());
    CHECK(reported->fix_its().front().replacement == "ornate_box");
}

TEST_CASE("a ref to a form instance resolves in that form's namespace", "[schema][reference]") {
    // `of_action` is `ref<action>`, and `action` declares `unique_in =
    // action`. The namespace is read out of the schema rather than assumed
    // from the form's id, because §7.2 lets them differ -- `const` and
    // `global` share one namespace and neither is called by its own name.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    loaded.load_text("action = { id = polish  match = { \"polish [something]\" } }\n"
                     "rule = { of_action = polish   effects = { } }\n"
                     "rule = { of_action = polsih   effects = { } }\n");

    const diag::Diagnostic* reported = first_of(loaded, diag::Code::RefUnresolved);
    REQUIRE(reported != nullptr);
    CHECK(mentions(*reported, "the action 'polsih'"));
    REQUIRE_FALSE(reported->fix_its().empty());
    CHECK(reported->fix_its().front().replacement == "polish");
}

TEST_CASE("two objects may not share an id", "[schema][reference]") {
    // ONE namespace across every class, not one per class. §6.6's paths and
    // §11.1's effects name an object by id alone and never say what class
    // they expect, so a `room` and a `thing` both called `airlock` would be
    // two things one word resolves to.
    //
    // The namespace is implied: no schema describes an instantiation, so
    // there is no `unique_in` to read it out of. It exists anyway, because
    // `ref<C>` resolving to two objects is not a resolution.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    loaded.load_text("room  = { id = airlock }\n"
                     "thing = { id = airlock }\n");

    const diag::Diagnostic* reported = first_of(loaded, diag::Code::SchemaDuplicate);
    REQUIRE(reported != nullptr);
    CHECK(mentions(*reported, "'airlock'"));
    // Both spans, so the author sees the pair rather than hunting for it.
    REQUIRE_FALSE(reported->notes().empty());
    CHECK(reported->notes().front().span.has_value());

    // The loser is not stored, so a reference resolves to exactly one thing.
    REQUIRE(loaded.set.find_object("airlock") != nullptr);
    CHECK(loaded.set.find_object("airlock")->class_id == "room");
}

TEST_CASE("a mod may supersede an object with @replaces", "[schema][reference]") {
    // §7.6's escape, which objects get for free by going through the same
    // gate as everything else: a mod that means to replace the game's lantern
    // says whose it was, and is believed.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    loaded.load_text("thing = { id = lantern }\n", "mygame", "game.star");
    loaded.load_text("room = @replaces(mygame) { id = lantern }\n", "amod", "mod.star");

    CHECK(loaded.sink.error_count() == 0);
    REQUIRE(loaded.set.find_object("lantern") != nullptr);
    CHECK(loaded.set.find_object("lantern")->class_id == "room");
    CHECK(loaded.set.find_object("lantern")->owner == "amod");
}

TEST_CASE("an object and a form instance may share an id", "[schema][reference]") {
    // The other half of the namespace rule. `object` is its own space, so an
    // object called `lever` and a sector called `lever` do not collide -- and
    // §7.6 says as much about every other pair of spaces.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    loaded.load_text("thing  = { id = lever }\n"
                     "sector = { id = lever }\n");

    CHECK_FALSE(loaded.reported(diag::Code::SchemaDuplicate));
    CHECK(loaded.sink.error_count() == 0);
}

TEST_CASE("an object and a form instance are separate namespaces", "[schema][reference]") {
    // An action called `lever` and an object called `lever` are two different
    // things, and a `ref` to one must not be satisfied by the other. Nothing
    // in §7.6 makes them collide, so nothing here may make them resolve.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    loaded.load_text("thing  = { id = lever }\n"
                     "rule   = { of_action = lever  effects = { } }\n");

    const diag::Diagnostic* reported = first_of(loaded, diag::Code::RefUnresolved);
    REQUIRE(reported != nullptr);
    CHECK(mentions(*reported, "the action 'lever'"));
}

// --- §5.5, the two words that are not names -------------------------------

TEST_CASE("none clears a reference and inherit declines to set one", "[schema][reference]") {
    // Resolving these would demand an object called `none` in every program
    // that ever emptied a slot.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    loaded.load_text("thing = { id = brass_key  holder = none }\n"
                     "thing = { id = ornate_box  holder = inherit }\n");

    CHECK_FALSE(loaded.reported(diag::Code::RefUnresolved));
    CHECK(loaded.sink.error_count() == 0);
}

// --- inside collections ---------------------------------------------------

TEST_CASE("a ref inside a collection is resolved too", "[schema][reference]") {
    // `present_in` is `set<ref<starcore.room>>` and `exits` is
    // `map<direction, ref<room>>`. A reference is no less a reference for
    // being an element, and the entry is named in the message so that one bad
    // id among six is findable.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    loaded.load_text("room  = { id = your_cell  exits = { north = corridr } }\n"
                     "room  = { id = corridor }\n"
                     "thing = { id = cell_door  present_in = { your_cell corridr } }\n");

    std::size_t reported = 0;
    for (const diag::Diagnostic& diagnostic : loaded.sink.diagnostics()) {
        reported += diagnostic.code() == diag::Code::RefUnresolved ? 1 : 0;
    }
    CHECK(reported == 2);

    const diag::Diagnostic* first = first_of(loaded, diag::Code::RefUnresolved);
    REQUIRE(first != nullptr);
    CHECK(mentions(*first, "an entry of 'exits'"));
}
