// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Where a `failureMsg` may go, and where one is owed (spec §10.5, backlog F8).
//
// Every case below is a well-formed file. That is the point of the rule: an
// author gets this wrong by writing a perfectly good message in a position
// the engine will never read, and nothing about the result looks broken
// afterwards -- the game simply says "You can't do that right now" where a
// sentence was written.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <set>
#include <sstream>
#include <string>

#include "stardata/cst/parser.hpp"
#include "stardata/diag/codes.hpp"
#include "stardata/diag/render.hpp"
#include "stardata/diag/sink.hpp"

#include "starcore/conditions.hpp"
#include "starcore/messages.hpp"
#include "support/corpus.hpp"
#include "support/cst_harness.hpp"
#include "support/fixture.hpp"
#include "support/schema_harness.hpp"

using namespace stardata;

namespace {

// A file checked against the real core-owned set, because the pass finds its
// condition blocks by asking the schema what a key's declared type is.
class Checked {
public:
    explicit Checked(const std::string& text, const std::string& name = "game.star") {
        loaded.load_builtin();
        loaded.load_stdlib();
        REQUIRE(loaded.sink.error_count() == 0);

        // Loaded into the registry as well as parsed, so that a file
        // declaring its own `schema` is checked against it. Without this the
        // ruleset case below would report nothing and pass for the wrong
        // reason -- the pass finds condition blocks by asking the registry.
        loaded.load_text(text, "a ruleset", name);

        const diag::SourceId id = sources.add_file(name, text);
        green = cst::parse(sources, id, cache, sink);
        const ast::File file = ast::File::from(cst::SyntaxNode::root(green), id);
        starcore::check_failure_messages(file, loaded.set, sink);
    }

    [[nodiscard]] std::size_t count(diag::Code code) const {
        std::size_t found = 0;
        for (const diag::Diagnostic& diagnostic : sink.diagnostics()) {
            found += diagnostic.code() == code ? 1 : 0;
        }
        return found;
    }

    [[nodiscard]] const diag::Diagnostic* first(diag::Code code) const {
        for (const diag::Diagnostic& diagnostic : sink.diagnostics()) {
            if (diagnostic.code() == code) {
                return &diagnostic;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::string rendered() const {
        std::ostringstream out;
        for (const diag::Diagnostic& diagnostic : sink.diagnostics()) {
            diag::render_human(out, diagnostic, sources, /*use_color=*/false);
        }
        return out.str();
    }

    test::LoadedSet loaded;
    diag::SourceManager sources;
    diag::DiagnosticSink sink;
    cst::GreenCache cache;
    cst::GreenNodePtr green;
};

} // namespace

// --- §10.5.1, where the message goes ------------------------------------

TEST_CASE("a message on the failing child of a conjunction is reachable", "[starcore][failmsg]") {
    const Checked checked("action = {\n"
                          "    id    = pry\n"
                          "    match = { \"pry [something]\" }\n"
                          "    restrictions = {\n"
                          "        noun = { has_trait = portable\n"
                          "                 failureMsg = \"It is fixed in place.\" }\n"
                          "    }\n"
                          "}\n");
    CHECK(checked.sink.diagnostics().empty());
}

TEST_CASE("a message on the NOT itself is reachable", "[starcore][failmsg]") {
    // §10.5's own example, and the case the first draft of this pass got
    // backwards: the barrier's OWN message is the reachable one. Every
    // correct action in the reference corpus has this shape, which is how
    // the mistake was caught.
    const Checked checked("action = {\n"
                          "    id    = take_it\n"
                          "    match = { \"take [something]\" }\n"
                          "    restrictions = {\n"
                          "        NOT = { carrying = { holder = actor  obj = noun }\n"
                          "                failureMsg = \"You are already holding it.\" }\n"
                          "    }\n"
                          "}\n");
    CHECK(checked.sink.diagnostics().empty());
}

TEST_CASE("a message below a NOT is unreachable", "[starcore][failmsg]") {
    const Checked checked("action = {\n"
                          "    id    = take_it\n"
                          "    match = { \"take [something]\" }\n"
                          "    restrictions = {\n"
                          "        NOT = { carrying = { holder = actor  obj = noun\n"
                          "                             failureMsg = \"Never printed.\" } }\n"
                          "    }\n"
                          "}\n");
    REQUIRE(checked.count(diag::Code::FailmsgUnreachable) == 1);

    // The diagnostic cites the barrier as well as the message: the fix is to
    // move one to the other, and an author needs to see both ends of it.
    const diag::Diagnostic* reported = checked.first(diag::Code::FailmsgUnreachable);
    REQUIRE(reported != nullptr);
    REQUIRE_FALSE(reported->notes().empty());
    REQUIRE(reported->notes()[0].span.has_value());
    CHECK(checked.sources.text(*reported->notes()[0].span) == "NOT");
}

TEST_CASE("a message on an OR branch is unreachable", "[starcore][failmsg]") {
    // An OR fails only when every branch fails, so neither branch is the
    // reason. §10.5.1's table, third and fourth rows.
    const Checked checked("action = {\n"
                          "    id    = shift\n"
                          "    match = { \"shift [something]\" }\n"
                          "    restrictions = {\n"
                          "        OR = {\n"
                          "            actor    = { strength >= 14\n"
                          "                         failureMsg = \"Not strong enough.\" }\n"
                          "            carrying = { holder = actor  obj = crowbar }\n"
                          "        }\n"
                          "    }\n"
                          "}\n");
    CHECK(checked.count(diag::Code::FailmsgUnreachable) == 1);
    CHECK(checked.rendered().find("below an OR") != std::string::npos);
}

TEST_CASE("a message below a COUNT_AT_LEAST is unreachable", "[starcore][failmsg]") {
    const Checked checked("action = {\n"
                          "    id    = ritual\n"
                          "    match = { \"perform the ritual\" }\n"
                          "    restrictions = {\n"
                          "        COUNT_AT_LEAST = {\n"
                          "            n = 2\n"
                          "            actor = { focus >= 3  failureMsg = \"Never printed.\" }\n"
                          "        }\n"
                          "    }\n"
                          "}\n");
    CHECK(checked.count(diag::Code::FailmsgUnreachable) == 1);
}

TEST_CASE("an AND is transparent, so a message under one is reachable", "[starcore][failmsg]") {
    // §10.5.1's second row. `AND` is the explicit spelling of the implicit
    // conjunction and fails because of a specific child, so the child can
    // explain it.
    const Checked checked("action = {\n"
                          "    id    = pry\n"
                          "    match = { \"pry [something]\" }\n"
                          "    restrictions = {\n"
                          "        AND = {\n"
                          "            noun = { has_trait = portable\n"
                          "                     failureMsg = \"It is fixed in place.\" }\n"
                          "        }\n"
                          "    }\n"
                          "}\n");
    CHECK(checked.sink.diagnostics().empty());
}

TEST_CASE("the outermost barrier is the one reported", "[starcore][failmsg]") {
    // Once unreachable, always unreachable -- and moving the message to the
    // inner barrier would not help, so the outer one is what an author is
    // pointed at.
    const Checked checked("action = {\n"
                          "    id    = tangle\n"
                          "    match = { \"tangle [something]\" }\n"
                          "    restrictions = {\n"
                          "        OR = {\n"
                          "            NOT = { carrying = { holder = actor  obj = noun\n"
                          "                                 failureMsg = \"Never.\" } }\n"
                          "        }\n"
                          "    }\n"
                          "}\n");
    REQUIRE(checked.count(diag::Code::FailmsgUnreachable) == 1);
    CHECK(checked.rendered().find("below an OR") != std::string::npos);
}

// --- §10.5, the silent stages -------------------------------------------

TEST_CASE("a message in conditions or when is an error", "[starcore][failmsg]") {
    const Checked checked("rule = {\n"
                          "    of_action  = take\n"
                          "    when       = { noun = { failureMsg = \"Never.\" } }\n"
                          "    conditions = { actor = { failureMsg = \"Also never.\" } }\n"
                          "}\n");
    CHECK(checked.count(diag::Code::FailmsgSilent) == 2);
}

TEST_CASE("a silent stage is silent regardless of where the message sits", "[starcore][failmsg]") {
    // Both faults at once: silent AND below a barrier. One diagnostic, and
    // it is the one that matters -- moving it within the `when` block would
    // fix nothing.
    const Checked checked("rule = {\n"
                          "    of_action = take\n"
                          "    when      = { NOT = { carrying = { holder = actor\n"
                          "                                       failureMsg = \"Never.\" } } }\n"
                          "}\n");
    CHECK(checked.count(diag::Code::FailmsgSilent) == 1);
    CHECK(checked.count(diag::Code::FailmsgUnreachable) == 0);
}

// --- §10.5.3, the fallback chain ----------------------------------------

TEST_CASE("a restriction with no message anywhere is a warning", "[starcore][failmsg]") {
    const Checked checked("action = {\n"
                          "    id    = refuse\n"
                          "    match = { \"refuse [something]\" }\n"
                          "    restrictions = { noun = { has_trait = portable } }\n"
                          "}\n");
    CHECK(checked.count(diag::Code::FailmsgMissing) == 1);
}

TEST_CASE("the action's own failureMsg covers every restriction in it", "[starcore][failmsg]") {
    // §10.5.3 step 4, and the reason the spec's word is SHOULD rather than
    // MUST: one message for the whole action is a legitimate way to write it.
    const Checked checked("action = {\n"
                          "    id         = refuse\n"
                          "    match      = { \"refuse [something]\" }\n"
                          "    restrictions = { noun = { has_trait = portable } }\n"
                          "    failureMsg = \"You can't manage that.\"\n"
                          "}\n");
    CHECK(checked.count(diag::Code::FailmsgMissing) == 0);
}

TEST_CASE("an enclosing action covers a rule nested inside it", "[starcore][failmsg]") {
    // §10.5.3 walks steps 3 then 4, so a `rule` written inside an `action`
    // (`key = { name = rule  type = block<rule> }`) inherits the action's
    // message. The pass follows `block<S>` to get there rather than knowing
    // that a thing called `rule` nests in a thing called `action`.
    const Checked checked("action = {\n"
                          "    id         = refuse\n"
                          "    match      = { \"refuse [something]\" }\n"
                          "    failureMsg = \"You can't manage that.\"\n"
                          "    rule = {\n"
                          "        of_action    = refuse\n"
                          "        restrictions = { noun = { has_trait = portable } }\n"
                          "    }\n"
                          "}\n");
    CHECK(checked.count(diag::Code::FailmsgMissing) == 0);
}

TEST_CASE("a nested rule with no cover anywhere still warns", "[starcore][failmsg]") {
    // The control for the case above: if inheritance were not being computed
    // at all, that test would pass by reporting nothing for the wrong reason.
    const Checked checked("action = {\n"
                          "    id    = refuse\n"
                          "    match = { \"refuse [something]\" }\n"
                          "    rule = {\n"
                          "        of_action    = refuse\n"
                          "        restrictions = { noun = { has_trait = portable } }\n"
                          "    }\n"
                          "}\n");
    CHECK(checked.count(diag::Code::FailmsgMissing) == 1);
}

TEST_CASE("a misplaced message is not also a missing one", "[starcore][failmsg]") {
    // The author wrote the explanation; the complaint is where it sits.
    // Moving it answers both, so saying both is one diagnostic too many.
    const Checked checked("action = {\n"
                          "    id    = take_it\n"
                          "    match = { \"take [something]\" }\n"
                          "    restrictions = {\n"
                          "        NOT = { carrying = { holder = actor  obj = noun\n"
                          "                             failureMsg = \"Never printed.\" } }\n"
                          "    }\n"
                          "}\n");
    CHECK(checked.count(diag::Code::FailmsgUnreachable) == 1);
    CHECK(checked.count(diag::Code::FailmsgMissing) == 0);
}

TEST_CASE("an empty restrictions block owes nothing", "[starcore][failmsg]") {
    // §5.4.2: `restrictions` is declared `combine = smart`, under which an
    // empty block is an explicit override -- "this action has no
    // restrictions". There is nothing there to fail, so nothing to explain.
    const Checked checked("action = {\n"
                          "    id           = unblocked\n"
                          "    match        = { \"jump\" }\n"
                          "    restrictions = { }\n"
                          "}\n");
    CHECK(checked.count(diag::Code::FailmsgMissing) == 0);
}

// --- the vocabulary is named once ---------------------------------------

TEST_CASE("the condition stages come from the schema, not from a list", "[starcore][failmsg]") {
    // A ruleset declares a form with a condition stage of its own. The pass
    // has never heard of `pounce_when`, and finds it because the schema says
    // its declared type is `condition_block` (§6.2).
    //
    // It is left UNCLASSIFIED -- neither restriction nor silent -- so it gets
    // the reachability check and nothing else. Guessing "silent" would turn
    // a correct message into an error, which is the more expensive way to be
    // wrong.
    const Checked checked("schema = {\n"
                          "    id        = pounce\n"
                          "    top_level = yes\n"
                          "    key = { name = id           type = identifier }\n"
                          "    key = { name = pounce_when  type = condition_block }\n"
                          "}\n"
                          "pounce = {\n"
                          "    id = ambush\n"
                          "    pounce_when = {\n"
                          "        NOT = { actor = { asleep == yes\n"
                          "                          failureMsg = \"Never printed.\" } }\n"
                          "    }\n"
                          "}\n");
    CHECK(checked.count(diag::Code::FailmsgUnreachable) == 1);
    CHECK(checked.count(diag::Code::FailmsgSilent) == 0);
    CHECK(checked.count(diag::Code::FailmsgMissing) == 0);
}

TEST_CASE("a key of some other type is not walked as a condition block", "[starcore][failmsg]") {
    // The control for the case above, and it is the one that makes it mean
    // something. The same file with the same `failureMsg` in the same place,
    // and only the declared type changed: nothing is reported, because
    // nothing here is a condition block. A pass that simply walked every
    // nested block would report both files identically.
    const Checked checked("schema = {\n"
                          "    id        = pounce_notes\n"
                          "    top_level = yes\n"
                          "    open      = yes\n"
                          "    key = { name = id           type = identifier }\n"
                          "    key = { name = pounce_when  type = block<pounce_notes> }\n"
                          "}\n"
                          "pounce_notes = {\n"
                          "    id = ambush\n"
                          "    pounce_when = {\n"
                          "        NOT = { actor = { asleep == yes\n"
                          "                          failureMsg = \"Never printed.\" } }\n"
                          "    }\n"
                          "}\n");
    CHECK(checked.count(diag::Code::FailmsgUnreachable) == 0);
    CHECK(checked.count(diag::Code::FailmsgSilent) == 0);
}

TEST_CASE("the barrier set is the one narrowing uses", "[starcore][failmsg]") {
    // §10.5.1's "fails as a whole" set and §8.8.3's "narrowing does not
    // survive" set are the same three combinators, and are now named once.
    CHECK(starcore::is_barrier(starcore::combinator::kNot));
    CHECK(starcore::is_barrier(starcore::combinator::kOr));
    CHECK(starcore::is_barrier(starcore::combinator::kCountAtLeast));
    CHECK_FALSE(starcore::is_barrier(starcore::combinator::kAnd));
    CHECK(starcore::barrier_names().size() == 3);
}

TEST_CASE("the silent stages are the two the spec names", "[starcore][failmsg]") {
    CHECK(starcore::restriction_stage() == "restrictions");
    CHECK(starcore::failure_message_key() == "failureMsg");
    REQUIRE(starcore::silent_stages().size() == 2);
    CHECK(starcore::silent_stages()[0] == "conditions");
    CHECK(starcore::silent_stages()[1] == "when");
}

// --- the reference corpus ------------------------------------------------

TEST_CASE("stdlib and the valid corpus keep the failureMsg rule", "[starcore][failmsg][corpus]") {
    // The check that the rule does not fire on correct files, and the one
    // that earned its keep: the first draft of this pass reported three
    // faults in tour.star, all of them false -- it applied the barrier on the
    // way INTO a `NOT` rather than on the way out, so every message written
    // in exactly the place §10.5's example puts it was an error.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    REQUIRE(loaded.sink.error_count() == 0);
    REQUIRE_FALSE(loaded.files.empty());

    std::vector<std::filesystem::path> files = loaded.files;
    for (const auto& path : test::corpus_files(test::corpus_dir())) {
        files.push_back(path);
    }

    for (const std::filesystem::path& path : files) {
        INFO("file: " << path.string());
        const std::string contents = test::read_bytes(path);
        test::Parsed parsed(contents, path.generic_string());
        diag::DiagnosticSink sink;
        starcore::check_failure_messages(parsed.ast(), loaded.set, sink);

        for (const diag::Diagnostic& diagnostic : sink.diagnostics()) {
            std::ostringstream rendered;
            diag::render_human(rendered, diagnostic, parsed.sources(), /*use_color=*/false);
            INFO(rendered.str());
            CHECK(false);
        }
    }
}
