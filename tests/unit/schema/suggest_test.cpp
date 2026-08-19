// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog F6: "did you mean ...?" (spec §7.3, §14.3).
//
// The spec asks for this in three places and the proposal explains why in
// one. §7.3: "an unknown key MUST be an error, with a 'did you mean ...?'
// suggestion computed by edit distance against the declared keys". §14.3
// repeats it for a map key outside its domain and for `@replaces` naming a
// source that declared no such thing. Proposal §4.9 gives the worked example
// this file ends on -- a `class` declaring `outdoors_room` and an
// instantiation writing `outdoor_room`, which "in an untyped Clausewitz-style
// loader is a silently ignored block and a room that mysteriously doesn't
// exist".
//
// Two properties matter as much as the suggestions themselves, and both are
// easy to lose later: the search is DETERMINISTIC (§14.1 requires it, and an
// author comparing two builds must get the same advice from both), and it
// declines to guess when nothing is close. A confident wrong suggestion
// costs more trust than no suggestion costs convenience.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "stardata/diag/codes.hpp"
#include "stardata/schema/loader.hpp"
#include "stardata/schema/suggest.hpp"
#include "stardata/schema/types.hpp"

#include "support/schema_harness.hpp"

using namespace stardata;

namespace {

[[nodiscard]] std::string suggestion_in(const diag::Diagnostic& diagnostic) {
    for (const diag::FixIt& fix : diagnostic.fix_its()) {
        if (fix.message.rfind("did you mean", 0) == 0) {
            return fix.replacement;
        }
    }
    return {};
}

// The suggestion a load produced for the first diagnostic of a code.
[[nodiscard]] std::string suggestion_for(const test::LoadedSet& loaded, diag::Code code) {
    const diag::Diagnostic* reported = loaded.first(code);
    return reported == nullptr ? std::string("<nothing reported>") : suggestion_in(*reported);
}

} // namespace

// --- the distance function ----------------------------------------------

TEST_CASE("edit distance counts the four Damerau operations", "[schema][suggest]") {
    CHECK(schema::edit_distance("north", "north", 4) == 0);
    CHECK(schema::edit_distance("nrth", "north", 4) == 1);   // insertion
    CHECK(schema::edit_distance("norths", "north", 4) == 1); // deletion
    CHECK(schema::edit_distance("nortk", "north", 4) == 1);  // substitution

    // Transposition is one edit, not two. A plain Levenshtein charges two for
    // a swapped pair, which is enough to push a one-finger slip past the
    // threshold on a short name.
    CHECK(schema::edit_distance("nroth", "north", 4) == 1);
}

TEST_CASE("edit distance stops once it is past the limit", "[schema][suggest]") {
    // The contract is "limit + 1 for anything further apart", so a caller can
    // treat any answer above the limit as "not close" without caring how far.
    CHECK(schema::edit_distance("north", "somewhere_else_entirely", 2) == 3);
    CHECK(schema::edit_distance("", "north", 2) == 3);
    CHECK(schema::edit_distance("north", "", 10) == 5);
}

// --- choosing among candidates ------------------------------------------

TEST_CASE("the nearest candidate wins", "[schema][suggest]") {
    const std::vector<std::string_view> directions = {"north", "south", "east", "west"};
    CHECK(schema::nearest("nrth", directions) == "north");
    CHECK(schema::nearest("wset", directions) == "west");
}

TEST_CASE("nothing near enough gets no suggestion", "[schema][suggest]") {
    // The half that keeps the feature trustworthy. A suggestion offered for
    // a name that resembles nothing is worse than silence: it reads as though
    // the checker knows something, and it does not.
    const std::vector<std::string_view> directions = {"north", "south", "east", "west"};
    CHECK_FALSE(schema::nearest("basement", directions).has_value());
    CHECK_FALSE(schema::nearest("", directions).has_value());

    // An exact match is not a suggestion either -- whoever called has other
    // news for the author.
    CHECK_FALSE(schema::nearest("north", directions).has_value());
}

TEST_CASE("a candidate differing only in case wins outright", "[schema][suggest]") {
    // Nine edits from `successMsg` by distance, and obviously meant to be it.
    // The one case where distance is the wrong measure.
    const std::vector<std::string_view> keys = {"successMsg", "failureMsg"};
    CHECK(schema::nearest("SUCCESSMSG", keys) == "successMsg");
    CHECK(schema::nearest("successmsg", keys) == "successMsg");
}

TEST_CASE("ties are broken by declaration order, not by chance", "[schema][suggest]") {
    // §14.1's determinism requirement, in the place it is easiest to lose.
    // Both candidates are one substitution away; the first declared wins, and
    // reversing the input reverses the answer -- which is the check that the
    // order is really the input's and not something incidental.
    CHECK(schema::nearest("aaa", {"aba", "aca"}) == "aba");
    CHECK(schema::nearest("aaa", {"aca", "aba"}) == "aca");
}

// --- through the passes that report ------------------------------------

TEST_CASE("an unknown key is offered the schema's keys", "[schema][suggest]") {
    // §7.3's own sentence, and the case it describes.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("sector = { id = bridge  always_residnt = yes }\n");
    REQUIRE(loaded.reported(diag::Code::UnknownKey));
    CHECK(suggestion_for(loaded, diag::Code::UnknownKey) == "always_resident");
}

TEST_CASE("an unknown enum value is offered the enum's values", "[schema][suggest]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("action = { id = a  match = { \"a\" }  advances_turn = on_sucess }\n");
    REQUIRE(loaded.reported(diag::Code::TypeMismatch));
    CHECK(suggestion_for(loaded, diag::Code::TypeMismatch) == "on_success");
}

TEST_CASE("a map key outside its domain is offered the domain", "[schema][suggest]") {
    // §14.3 names this row and this example: `exits.nrth` for
    // `map<direction, ...>`, "error, with a suggestion".
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    loaded.load_text("room = { id = antecourt  exits = { nrth = corridor } }\n");
    REQUIRE(loaded.reported(diag::Code::TypeMismatch));
    CHECK(suggestion_for(loaded, diag::Code::TypeMismatch) == "north");
}

TEST_CASE("a type naming no declared enum is offered the enums", "[schema][suggest]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("schema = {\n"
                     "    id  = stat_block\n"
                     "    key = { name = mood  type = enum<advances_turn_enm> }\n"
                     "}\n");
    schema::check_declared_types(loaded.set, loaded.sink);
    REQUIRE(loaded.reported(diag::Code::SchemaInvalid));
    CHECK(suggestion_for(loaded, diag::Code::SchemaInvalid) == "advances_turn_enum");
}

TEST_CASE("the fix-it for a mistyped type covers the type, not the key", "[schema][suggest]") {
    // The diagnostic points at the KEY, because that is where the mistake is
    // reported; the fix-it has to point at the type name, or applying it
    // would rename the key. Two different spans in one diagnostic, and
    // getting them the same way round is not automatic.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("schema = {\n"
                     "    id  = stat_block\n"
                     "    key = { name = mood  type = enum<advances_turn_enm> }\n"
                     "}\n");
    schema::check_declared_types(loaded.set, loaded.sink);

    const diag::Diagnostic* reported = loaded.first(diag::Code::SchemaInvalid);
    REQUIRE(reported != nullptr);
    REQUIRE_FALSE(reported->fix_its().empty());

    const std::string_view text = loaded.sources.contents(reported->fix_its().front().span.source);
    const diag::Span at = reported->fix_its().front().span;
    CHECK(text.substr(at.offset, at.length) == "advances_turn_enm");
}

TEST_CASE("an unknown annotation is offered the ten that exist", "[schema][suggest]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("rule = { effects = @ovveride { } }\n");
    REQUIRE(loaded.reported(diag::Code::UnknownAnnotation));
    CHECK(suggestion_for(loaded, diag::Code::UnknownAnnotation) == "@override");
}

TEST_CASE("@replaces naming nothing is offered the ids that exist", "[schema][suggest]") {
    // §14.3: "`@replaces` naming a source that declared no such thing --
    // error, with a suggestion". The useful candidates are the ids in the
    // same `unique_in` namespace, since misspelling the thing being replaced
    // is likelier than inventing one.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    loaded.load_text("action = @replaces(stdlib) {\n"
                     "    id    = taek\n"
                     "    match = { \"take [something]\" }\n"
                     "}\n");
    REQUIRE(loaded.reported(diag::Code::SchemaInvalid));
    CHECK(suggestion_for(loaded, diag::Code::SchemaInvalid) == "take");
}

// --- proposal §4.9's worked example -------------------------------------

TEST_CASE("the outdoors_room case from the proposal", "[schema][suggest]") {
    // Proposal §4.9, quoted in full because it is the argument for the whole
    // schema layer: "the current star-if-example.txt declares
    // `class = { id = outdoors_room }` and then instantiates
    // `outdoor_room = { id = antecourt }`. Under §4.4 that is a compile error
    // pointing at the exact span with a 'did you mean outdoors_room?'
    // suggestion. In an untyped Clausewitz-style loader it is a silently
    // ignored block and a room that mysteriously doesn't exist."
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("class = { id = outdoors_room }\n"
                     "outdoor_room = { id = antecourt }\n");

    REQUIRE(loaded.reported(diag::Code::UnknownKey));
    CHECK(suggestion_for(loaded, diag::Code::UnknownKey) == "outdoors_room");

    // "pointing at the exact span" -- at the mistyped name, not at the block
    // it opens, and not at the whole statement with its leading trivia.
    const diag::Diagnostic* reported = loaded.first(diag::Code::UnknownKey);
    REQUIRE(reported != nullptr);
    const std::string_view text = loaded.sources.contents(reported->primary_span().source);
    CHECK(text.substr(reported->primary_span().offset, reported->primary_span().length) ==
          "outdoor_room");
}
