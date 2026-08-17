// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog F3: the schema registry (spec §13.3) and key validation (§5.3,
// §7.2.1, §7.3).
//
// The four rules here are the ones a schema states about a key and nothing
// else was in a position to enforce. `arity` and `exclusive_group` were both
// read into the key declaration by F2 and then acted on by nobody, which is
// the worst of both: an author writing `arity = many` got no error and no
// effect, and had no way to tell which.
//
// The registry half is less visible and worth stating plainly. Every
// declaration is keyed by its id, and a library's form sits in the same table
// as a core one -- the built-in set is first only because it loads first
// (§13.2), not because it lives anywhere privileged. The tests below check
// that from the outside: a library declares a form, and it is then found,
// instantiated and validated by exactly the code that serves `action`.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "stardata/ast/ast.hpp"
#include "stardata/diag/codes.hpp"
#include "stardata/schema/loader.hpp"

#include "support/cst_harness.hpp"
#include "support/schema_harness.hpp"

using namespace stardata;

namespace {

// Every diagnostic of a code, so a test can count them as well as find them.
[[nodiscard]] std::vector<const diag::Diagnostic*> all_of(const test::LoadedSet& loaded,
                                                          diag::Code code) {
    std::vector<const diag::Diagnostic*> found;
    for (const diag::Diagnostic& diagnostic : loaded.sink.diagnostics()) {
        if (diagnostic.code() == code) {
            found.push_back(&diagnostic);
        }
    }
    return found;
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

// A library loaded on top of the real core-owned set, which is the situation
// every one of these rules applies in.
[[nodiscard]] std::string an_action(const std::string& body) {
    return "action = {\n    id = ring_bell\n    match = { \"ring bell\" }\n" + body + "}\n";
}

} // namespace

// --- arity (spec §5.3) -------------------------------------------------

TEST_CASE("a second binding of an arity-one key is refused, citing both", "[schema][arity]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text(an_action("    successMsg = \"One.\"\n    successMsg = \"Two.\"\n"));

    const std::vector<const diag::Diagnostic*> reported = all_of(loaded, diag::Code::DuplicateKey);
    REQUIRE(reported.size() == 1);
    CHECK(mentions(*reported[0], "successMsg"));

    // §5.3 and §14.3 both require both spans. The note is what carries the
    // second one, and pointing at the *first* occurrence is the point: the
    // error is reported where the author is standing, and the note says what
    // they are colliding with.
    REQUIRE_FALSE(reported[0]->notes().empty());
    CHECK(reported[0]->notes()[0].span.has_value());
    CHECK(reported[0]->notes()[0].span->offset < reported[0]->primary_span().offset);
}

TEST_CASE("every binding after the first is reported", "[schema][arity]") {
    // Not just the second. Three bindings are two mistakes, and an author
    // fixing one of them should not have to re-run to discover the other.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text(an_action(
        "    successMsg = \"One.\"\n    successMsg = \"Two.\"\n    successMsg = \"Three.\"\n"));

    CHECK(all_of(loaded, diag::Code::DuplicateKey).size() == 2);
}

TEST_CASE("the modifier operators never collide with a binding", "[schema][arity]") {
    // Spec §5.3's worked example: a binding followed by any number of `+=`
    // and `-=` is legal for an `arity = one` key, because only the first
    // statement binds.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text(an_action("    match += { \"toll bell\" }\n    match -= { \"ring bell\" }\n"));

    CHECK(all_of(loaded, diag::Code::DuplicateKey).empty());
}

TEST_CASE("an arity-many key repeats freely, in source order", "[schema][arity]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text(an_action("    rule = { id = first }\n"
                               "    rule = { id = second }\n"
                               "    rule = { id = third }\n"));

    CHECK(all_of(loaded, diag::Code::DuplicateKey).empty());

    // §5.3: "repeated occurrences are collected, in source order, into a
    // sequence". Multi-arity keys are how the format expresses an ordered
    // sub-collection without a list syntax, so the order is the content --
    // `stage`, `node` and `choice` all depend on it.
    const test::Parsed parsed(an_action("    rule = { id = first }\n"
                                        "    rule = { id = second }\n"
                                        "    rule = { id = third }\n"));
    const ast::Block block = *parsed.ast().statements()[0].value()->as_block();
    const std::vector<ast::Statement> rules = block.find_all("rule");
    REQUIRE(rules.size() == 3);
    CHECK(rules[0].value()->as_block()->value_of("id")->as_scalar()->as_identifier() == "first");
    CHECK(rules[1].value()->as_block()->value_of("id")->as_scalar()->as_identifier() == "second");
    CHECK(rules[2].value()->as_block()->value_of("id")->as_scalar()->as_identifier() == "third");
}

TEST_CASE("an undeclared key in an open form has no arity to violate", "[schema][arity]") {
    // §5.3 states arity as something "declared by the schema", and `class` is
    // open precisely because its other keys are property defaults -- whose
    // shape is the object model's question (backlog F11) and not this pass's.
    // Reporting a duplicate here would be this pass guessing.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("class = {\n"
                     "    id       = bell\n"
                     "    of_class = starcore.object\n"
                     "    prop_def = { pitch = int }\n"
                     "    pitch    = 3\n"
                     "    pitch    = 4\n"
                     "}\n");

    CHECK(all_of(loaded, diag::Code::DuplicateKey).empty());
}

// --- exclusive groups (spec §7.2.1) ------------------------------------

TEST_CASE("two keys of one exclusive group are refused, naming the members",
          "[schema][exclusive]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("rule = {\n"
                     "    id        = when_rung\n"
                     "    of_action = ring_bell\n"
                     "    of_event  = bell_rang\n"
                     "}\n");

    const std::vector<const diag::Diagnostic*> reported =
        all_of(loaded, diag::Code::ExclusiveGroup);
    REQUIRE(reported.size() == 1);

    // §14.3 requires the diagnostic to name the group's members, which is
    // what makes it actionable: the author knows one of the two lines has to
    // go, and which two lines are in question.
    CHECK(mentions(*reported[0], "'of_action'"));
    CHECK(mentions(*reported[0], "'of_event'"));

    REQUIRE_FALSE(reported[0]->notes().empty());
    CHECK(reported[0]->notes()[0].span.has_value());
}

TEST_CASE("exactly one key of an exclusive group is what the group is for", "[schema][exclusive]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("rule = {\n    id = when_rung\n    of_action = ring_bell\n}\n");

    CHECK(all_of(loaded, diag::Code::ExclusiveGroup).empty());
    CHECK(all_of(loaded, diag::Code::ExclusiveMissing).empty());
}

TEST_CASE("a required group refuses a block that sets none of it", "[schema][exclusive]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("schema = {\n"
                     "    id        = salvage_table\n"
                     "    top_level = yes\n"
                     "    key = { name = id      type = identifier  required = yes }\n"
                     "    key = { name = drops   type = list<identifier>  required = yes\n"
                     "            exclusive_group = source }\n"
                     "    key = { name = copies  type = identifier        required = yes\n"
                     "            exclusive_group = source }\n"
                     "}\n"
                     "salvage_table = { id = hold }\n");

    const std::vector<const diag::Diagnostic*> reported =
        all_of(loaded, diag::Code::ExclusiveMissing);
    REQUIRE(reported.size() == 1);
    CHECK(mentions(*reported[0], "'drops'"));
    CHECK(mentions(*reported[0], "'copies'"));
}

TEST_CASE("a required group member is not separately reported as missing", "[schema][exclusive]") {
    // The regression this guards against is the obvious one: `required` on a
    // group member means "the group needs an answer", so a block that
    // answered with `drops` must not also be told it has no `copies`.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("schema = {\n"
                     "    id        = salvage_table\n"
                     "    top_level = yes\n"
                     "    key = { name = id      type = identifier  required = yes }\n"
                     "    key = { name = drops   type = list<identifier>  required = yes\n"
                     "            exclusive_group = source }\n"
                     "    key = { name = copies  type = identifier        required = yes\n"
                     "            exclusive_group = source }\n"
                     "}\n"
                     "salvage_table = { id = hold  drops = { coin rope } }\n");

    CHECK(all_of(loaded, diag::Code::KeyMissing).empty());
    CHECK(all_of(loaded, diag::Code::ExclusiveMissing).empty());
    CHECK(all_of(loaded, diag::Code::ExclusiveGroup).empty());
}

TEST_CASE("an optional group is silent when nothing answers it", "[schema][exclusive]") {
    // `rule`'s own group is optional, because a rule nested inside an action
    // responds to that action and needs neither key. Zero is only an error
    // when a member is required (§7.2.1).
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("rule = {\n    id = always\n    effects = { }\n}\n");

    CHECK(all_of(loaded, diag::Code::ExclusiveMissing).empty());
}

// --- the registry (spec §13.3, §7.4) -----------------------------------

TEST_CASE("a library's form joins the registry and is validated like any other",
          "[schema][registry]") {
    // §13.3's claim, checked from the outside. The library's form is found by
    // id, is instantiable at the top level, and its required keys are
    // enforced -- by the same code that enforces `action`'s, because there is
    // one table and one validator.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("schema = {\n"
                     "    id        = stat_block\n"
                     "    top_level = yes\n"
                     "    key = { name = id     type = identifier  required = yes\n"
                     "            unique_in = stat_block }\n"
                     "    key = { name = might  type = int         required = yes }\n"
                     "}\n",
                     "starscape", "starscape/stats.star");

    const schema::Schema* declared = loaded.set.find("stat_block");
    REQUIRE(declared != nullptr);
    CHECK(declared->owner == "starscape");
    CHECK(declared->top_level);
    CHECK_FALSE(declared->sealed);

    loaded.load_text("stat_block = { id = brawler }\n", "a game", "game/heroes.star");
    const std::vector<const diag::Diagnostic*> missing = all_of(loaded, diag::Code::KeyMissing);
    REQUIRE(missing.size() == 1);
    CHECK(mentions(*missing[0], "'might'"));
}

TEST_CASE("a class and a trait of one id are two declarations", "[schema][registry]") {
    // §7.2.4 gives `class` and `trait` separate `unique_in` namespaces, so
    // this is legal -- and a lookup has to say which it means. The registry
    // keeps one index per namespace for exactly that reason.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("class = { id = beacon  of_class = starcore.object }\n"
                     "trait = { id = beacon  prop_def = { lit = bool } }\n");

    CHECK(loaded.sink.error_count() == 0);

    const schema::ClassDecl* as_class = loaded.set.find_class("beacon");
    const schema::ClassDecl* as_trait = loaded.set.find_trait("beacon");
    REQUIRE(as_class != nullptr);
    REQUIRE(as_trait != nullptr);
    CHECK_FALSE(as_class->is_trait);
    CHECK(as_trait->is_trait);
    CHECK(as_class->of_class == "starcore.object");
}

TEST_CASE("only a class is instantiable at the top level", "[schema][registry]") {
    // §7.4: "a statement whose key is the id of a declared class instantiates
    // an object of that class". A trait is mixed in through `traits`, never
    // created -- so a top-level statement naming one is not an instantiation
    // and has to be reported rather than quietly accepted as an object of a
    // kind that cannot exist.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("trait = { id = luminous  prop_def = { lit = bool } }\n");
    const std::size_t before = loaded.sink.diagnostics().size();

    loaded.load_text("luminous = { id = lantern }\n", "a game", "game/things.star");

    bool reported = false;
    for (std::size_t i = before; i < loaded.sink.diagnostics().size(); ++i) {
        const diag::Diagnostic& diagnostic = loaded.sink.diagnostics()[i];
        reported = reported || (diagnostic.code() == diag::Code::UnknownKey &&
                                mentions(diagnostic, "luminous"));
    }
    CHECK(reported);
}

TEST_CASE("a replaced form is replaced in the registry, not shadowed", "[schema][registry]") {
    // §7.6: replacement is total, no merge. The index has to follow the
    // replacement or a later lookup finds the superseded declaration -- which
    // is the kind of bug a linear scan hides, since it happens to return the
    // first match either way.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("schema = {\n"
                     "    id        = loot_table\n"
                     "    top_level = yes\n"
                     "    key = { name = id     type = identifier  required = yes }\n"
                     "    key = { name = drops  type = list<identifier> }\n"
                     "}\n",
                     "starscape", "starscape/loot.star");
    loaded.load_text("schema = @replaces(starscape) {\n"
                     "    id        = loot_table\n"
                     "    top_level = yes\n"
                     "    key = { name = id      type = identifier  required = yes }\n"
                     "    key = { name = weight  type = int }\n"
                     "}\n",
                     "a game", "game/loot.star");

    CHECK(loaded.sink.error_count() == 0);

    const schema::Schema* declared = loaded.set.find("loot_table");
    REQUIRE(declared != nullptr);
    CHECK(declared->owner == "a game");
    CHECK(declared->find_key("weight") != nullptr);
    CHECK(declared->find_key("drops") == nullptr);
}
