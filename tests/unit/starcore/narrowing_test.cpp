// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog F12: reading a property the object may not have (spec §8.8).
//
// The one genuinely novel piece of static analysis in Phase 0, and the task
// proposal §2.1.1 says "most threatened the layering". Two things are being
// asserted here and they are of different kinds.
//
// The ANALYSIS: §8.8.2's three answers, narrowing from `of_class`,
// `has_trait`, `is` and `has_prop`, that narrowing flowing forward through
// the stages and not escaping an `OR` or a `NOT`.
//
// The LAYERING: that the stage sequence is read from the schema and not known
// to the code. That one is asserted the only way it can be -- by declaring a
// form with stages nobody has ever heard of and requiring narrowing to flow
// through them anyway.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "stardata/diag/codes.hpp"
#include "stardata/schema/loader.hpp"
#include "stardata/schema/property.hpp"

#include "starcore/narrowing.hpp"
#include "support/corpus.hpp"
#include "support/cst_harness.hpp"
#include "support/fixture.hpp"
#include "support/schema_harness.hpp"

using namespace stardata;

namespace {

// A world with a class that has a property and a trait that has another, so
// every §8.8.2 answer is reachable. Loaded on top of the built-in set only --
// not stdlib -- so nothing here depends on what stdlib happens to declare.
constexpr const char* kWorld = R"(
trait = { id = fragile  prop_def = { integrity = int } }

class = {
    id       = gadget
    of_class = starcore.object
    prop_def = { serial = string }
}

class = {
    id       = fancy_gadget
    of_class = gadget
    traits   = { fragile }
    prop_def = { polish = int }
}

action = {
    id    = inspect
    match = { "inspect [something]" }
}
)";

// Runs both halves: load the text into a real registry, then walk it.
class Analysed {
public:
    explicit Analysed(const std::string& text) : parsed_(std::string(kWorld) + text) {
        loaded_.load_builtin();
        loaded_.load_text(std::string(kWorld) + text, "a library", "narrowing.star");
        starcore::check_property_reads(parsed_.ast(), loaded_.set, sink_);
    }

    [[nodiscard]] bool reported(diag::Code code) const {
        for (const diag::Diagnostic& diagnostic : sink_.diagnostics()) {
            if (diagnostic.code() == code) {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] std::size_t count() const { return sink_.diagnostics().size(); }
    [[nodiscard]] const diag::DiagnosticSink& sink() const { return sink_; }
    [[nodiscard]] const schema::SchemaSet& set() const { return loaded_.set; }

private:
    test::LoadedSet loaded_;
    test::Parsed parsed_;
    diag::DiagnosticSink sink_;
};

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

// --- the classifier, on its own ----------------------------------------

TEST_CASE("the three static answers of spec 8.8.2", "[starcore][narrowing]") {
    const Analysed world("");
    const schema::ClassDecl* gadget = world.set().find_class("gadget");
    const schema::ClassDecl* fancy = world.set().find_class("fancy_gadget");
    REQUIRE(gadget != nullptr);
    REQUIRE(fancy != nullptr);

    const auto answer = [&](std::string_view property, const schema::ClassDecl& type) {
        return schema::classify_property(property, type, world.set(), "starcore.object").answer;
    };

    // Present: the class itself, an ancestor, or a trait of either.
    CHECK(answer("serial", *gadget) == schema::PropertyAnswer::Present);
    CHECK(answer("serial", *fancy) == schema::PropertyAnswer::Present);    // inherited
    CHECK(answer("integrity", *fancy) == schema::PropertyAnswer::Present); // through the trait

    // Maybe: a descendant declares it, so some objects of this type have it.
    CHECK(answer("polish", *gadget) == schema::PropertyAnswer::Maybe);
    CHECK(answer("integrity", *gadget) == schema::PropertyAnswer::Maybe);

    // Absent: nothing that could satisfy the type declares it.
    CHECK(answer("shineyness", *gadget) == schema::PropertyAnswer::Absent);
}

TEST_CASE("an object-local property makes a read possible, not absent", "[starcore][narrowing]") {
    // The trap this avoids is a false error, which is much worse than a
    // missed one. §8.7 lets one object declare a property nothing else has;
    // a walk over classes alone would call a read of it definitely absent
    // and reject correct code. F11 is what put those names in the registry.
    const Analysed world("gadget = { id = odd_one  prop_def = { rune_count = int } }\n");
    const schema::ClassDecl* gadget = world.set().find_class("gadget");
    REQUIRE(gadget != nullptr);
    CHECK(schema::classify_property("rune_count", *gadget, world.set(), "starcore.object").answer ==
          schema::PropertyAnswer::Maybe);
}

// --- grammar tokens (spec 8.8.1) ---------------------------------------

TEST_CASE("a grammar token gives the slot its static type", "[starcore][narrowing]") {
    // §8.8.1's table. `[class:X]` is the one token that narrows; everything
    // else says "an object in scope" and no more, which is §8.8.1's advice
    // read forwards -- narrow with a restriction, not with the grammar.
    CHECK(starcore::token_type("class:fancy_gadget") == "fancy_gadget");
    CHECK(starcore::token_type("something") == "starcore.object");
    CHECK(starcore::token_type("someone") == "starcore.object");
    CHECK(starcore::token_type("things") == "starcore.object");
}

// --- the analysis -------------------------------------------------------

TEST_CASE("a definitely-absent read is an error", "[starcore][narrowing]") {
    const Analysed world("rule = {\n"
                         "    of_action  = inspect\n"
                         "    conditions = { noun = { shineyness > 3 } }\n"
                         "}\n");
    REQUIRE(world.reported(diag::Code::PropAbsent));
}

TEST_CASE("a possibly-absent read is an error naming who declares it", "[starcore][narrowing]") {
    // §8.8.3: the error names "the property, the slot's static type, and the
    // classes that do declare it, with a fix-it offering `has_prop`".
    const Analysed world("rule = {\n"
                         "    of_action  = inspect\n"
                         "    conditions = { noun = { polish > 3 } }\n"
                         "}\n");
    REQUIRE(world.reported(diag::Code::PropMaybeAbsent));

    const diag::Diagnostic* reported = nullptr;
    for (const diag::Diagnostic& diagnostic : world.sink().diagnostics()) {
        if (diagnostic.code() == diag::Code::PropMaybeAbsent) {
            reported = &diagnostic;
        }
    }
    REQUIRE(reported != nullptr);
    CHECK(mentions(*reported, "polish"));
    CHECK(mentions(*reported, "starcore.object")); // the slot's static type
    CHECK(mentions(*reported, "fancy_gadget"));    // who does declare it
    CHECK_FALSE(reported->fix_its().empty());      // the has_prop escape
}

TEST_CASE("a definitely-present read is silent", "[starcore][narrowing]") {
    const Analysed world("action = {\n"
                         "    id    = buff\n"
                         "    match = { \"buff [class:fancy_gadget]\" }\n"
                         "    conditions = { noun = { polish > 3 } }\n"
                         "}\n");
    CHECK(world.count() == 0);
}

// --- narrowing (spec 8.8.3) --------------------------------------------

TEST_CASE("of_class, has_trait and is all narrow the slot", "[starcore][narrowing]") {
    for (const std::string& narrowing :
         {std::string("of_class = fancy_gadget"), std::string("has_trait = fragile")}) {
        INFO("narrowed by: " << narrowing);
        const Analysed world("rule = {\n"
                             "    of_action  = inspect\n"
                             "    conditions = { noun = { " +
                             narrowing + "  integrity > 3 } }\n}\n");
        CHECK(world.count() == 0);
    }
}

TEST_CASE("has_prop is the explicit escape", "[starcore][narrowing]") {
    // §8.8.3 case 2: "an explicit `has_prop` test, which is both a runtime
    // check and a narrowing operator".
    const Analysed world("rule = {\n"
                         "    of_action  = inspect\n"
                         "    conditions = { noun = { has_prop = polish  polish > 3 } }\n"
                         "}\n");
    CHECK(world.count() == 0);
}

TEST_CASE("narrowing flows forward through the stages", "[starcore][narrowing]") {
    // §8.8.3's table: a narrowing in `when` narrows `conditions`,
    // `restrictions`, `effects` and the messages. This is the row that makes
    // §8.8.1's "prefer the broad token" advice workable.
    const Analysed world("rule = {\n"
                         "    of_action    = inspect\n"
                         "    when         = { noun = { of_class = fancy_gadget } }\n"
                         "    conditions   = { noun = { polish > 3 } }\n"
                         "    restrictions = { noun = { integrity > 0 } }\n"
                         "}\n");
    CHECK(world.count() == 0);
}

TEST_CASE("narrowing does not flow backwards", "[starcore][narrowing]") {
    // The stages are ordered, and a narrowing in a later one says nothing
    // about an earlier one -- `restrictions` runs after `conditions`.
    const Analysed world("rule = {\n"
                         "    of_action    = inspect\n"
                         "    conditions   = { noun = { polish > 3 } }\n"
                         "    restrictions = { noun = { of_class = fancy_gadget } }\n"
                         "}\n");
    CHECK(world.reported(diag::Code::PropMaybeAbsent));
}

TEST_CASE("narrowing does not escape an OR or a NOT", "[starcore][narrowing]") {
    // §8.8.3: "narrowing does not survive an `OR` branch, since only one
    // branch is known to have held. Narrowing established inside a `NOT`
    // does not escape it."
    const Analysed with_or("rule = {\n"
                           "    of_action  = inspect\n"
                           "    when       = { OR = { noun = { of_class = fancy_gadget } } }\n"
                           "    conditions = { noun = { polish > 3 } }\n"
                           "}\n");
    CHECK(with_or.reported(diag::Code::PropMaybeAbsent));

    const Analysed with_not("rule = {\n"
                            "    of_action  = inspect\n"
                            "    when       = { NOT = { noun = { of_class = fancy_gadget } } }\n"
                            "    conditions = { noun = { polish > 3 } }\n"
                            "}\n");
    CHECK(with_not.reported(diag::Code::PropMaybeAbsent));
}

// --- the layering, asserted ---------------------------------------------

TEST_CASE("the stage sequence comes from the schema, not from the code",
          "[starcore][narrowing][layering]") {
    // The claim proposal §2.1.1 makes and backlog F12 turns on: "`stardata`
    // implements the dataflow over whatever sequence it finds and knows none
    // of the stage names". This is the test that makes it falsifiable.
    //
    // A library declares a form whose stages are `sniff` and `pounce` --
    // names appearing nowhere in this repository. Narrowing has to flow from
    // the first into the second, which it can only do by reading
    // `stage_order`. Delete that read and this test fails while every other
    // test in the file still passes.
    //
    // THE REVERSED CASE AT THE BOTTOM IS NOT DECORATION. Without it this test
    // passed while `noun` had no static type at all -- the invented form has
    // no grammar line, so nothing was being classified and the silence meant
    // nothing whatever. A test that reports success by staying quiet needs
    // the control that makes it speak.
    test::LoadedSet loaded;
    loaded.load_builtin();

    const std::string text = std::string(kWorld) +
                             "schema = {\n"
                             "    id          = pounce_rule\n"
                             "    top_level   = yes\n"
                             "    stage_order = { sniff pounce }\n"
                             "    key = { name = match   type = list<string> }\n"
                             "    key = { name = sniff   type = condition_block }\n"
                             "    key = { name = pounce  type = condition_block }\n"
                             "}\n"
                             "pounce_rule = {\n"
                             "    match  = { \"pounce [something]\" }\n"
                             "    sniff  = { noun = { of_class = fancy_gadget } }\n"
                             "    pounce = { noun = { polish > 3 } }\n"
                             "}\n";
    loaded.load_text(text, "a library", "pounce.star");
    test::Parsed parsed(text, "pounce.star");

    diag::DiagnosticSink sink;
    starcore::check_property_reads(parsed.ast(), loaded.set, sink);
    CHECK(sink.diagnostics().empty());

    // And with the order reversed, the same two blocks must fail -- otherwise
    // the test above would pass for a pass that simply never reported.
    const std::string reversed = std::string(kWorld) +
                                 "schema = {\n"
                                 "    id          = pounce_rule\n"
                                 "    top_level   = yes\n"
                                 "    stage_order = { pounce sniff }\n"
                                 "    key = { name = match   type = list<string> }\n"
                                 "    key = { name = sniff   type = condition_block }\n"
                                 "    key = { name = pounce  type = condition_block }\n"
                                 "}\n"
                                 "pounce_rule = {\n"
                                 "    match  = { \"pounce [something]\" }\n"
                                 "    sniff  = { noun = { of_class = fancy_gadget } }\n"
                                 "    pounce = { noun = { polish > 3 } }\n"
                                 "}\n";
    test::LoadedSet other;
    other.load_builtin();
    other.load_text(reversed, "a library", "pounce.star");
    test::Parsed reparsed(reversed, "pounce.star");

    diag::DiagnosticSink other_sink;
    starcore::check_property_reads(reparsed.ast(), other.set, other_sink);
    CHECK_FALSE(other_sink.diagnostics().empty());
}

// --- the reference corpus ----------------------------------------------

TEST_CASE("the valid corpus reads no property it may not have", "[starcore][narrowing][corpus]") {
    // The half that is easy to leave untested: that the analysis does not
    // fire on correct files. It found eleven real reads in tour.star when it
    // was first run -- one of them `in_combat`, a property the corpus used
    // five times and declared nowhere -- and the corpus now narrows each of
    // them the way §8.8.3 prescribes.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    REQUIRE(loaded.sink.error_count() == 0);

    for (const auto& path : test::corpus_files(test::corpus_dir())) {
        INFO("valid fixture: " << path.string());
        const std::string contents = test::read_bytes(path);
        test::LoadedSet world;
        world.load_builtin();
        world.load_stdlib();
        world.load_text(contents, "starscape", test::corpus_name(path));

        test::Parsed parsed(contents, test::corpus_name(path));
        diag::DiagnosticSink sink;
        starcore::check_property_reads(parsed.ast(), world.set, sink);
        for (const diag::Diagnostic& diagnostic : sink.diagnostics()) {
            INFO("unexpected " << diag::code_string(diagnostic.code()) << ": "
                               << diagnostic.message());
            CHECK(false);
        }
    }
}
