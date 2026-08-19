// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog F5: annotations and combination modes (spec §5.4).
//
// Three things are being asserted here, and only the first is §5.4.1's table
// read back.
//
// ONE: the annotation vocabulary is closed and checked. §3.8 requires an
// unknown annotation to be an error "since silently ignoring one changes
// behaviour invisibly", which is the same argument §7.3 makes for an unknown
// key -- and the same failure mode, one level up.
//
// TWO: `combine` finally means something. F2 read the mode into `KeyDecl` and
// F3 left it there; until something computed the effective combination, a
// schema declaring `combine = smart` and one declaring nothing were the same
// file as far as any test could tell.
//
// THREE, and the one worth reading the comments for: `@debug` and `@platform`
// are not the same kind of thing. The specification originally said they were
// -- one sentence covering both, and this file's first version was written
// against it. They resolve at different times, and the arity rule falls out
// of *that* rather than out of the annotations looking alike.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "stardata/ast/ast.hpp"
#include "stardata/diag/codes.hpp"
#include "stardata/schema/annotation.hpp"
#include "stardata/schema/loader.hpp"

#include "support/corpus.hpp"
#include "support/cst_harness.hpp"
#include "support/fixture.hpp"
#include "support/schema_harness.hpp"

using namespace stardata;

namespace {

// The annotation pass on its own, over a string. Registry-free by design, so
// nothing here has to load the core-owned set to ask about `@merge`.
class Checked {
public:
    explicit Checked(std::string text) : parsed_(std::move(text)) {
        schema::check_annotations(parsed_.ast(), sink_);
    }

    [[nodiscard]] bool reported(diag::Code code) const { return first(code) != nullptr; }

    [[nodiscard]] const diag::Diagnostic* first(diag::Code code) const {
        for (const diag::Diagnostic& diagnostic : sink_.diagnostics()) {
            if (diagnostic.code() == code) {
                return &diagnostic;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::size_t count() const { return sink_.diagnostics().size(); }
    [[nodiscard]] const ast::File ast() const { return parsed_.ast(); }

private:
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

// The first statement's value, for the presence and combination tests.
[[nodiscard]] ast::Statement only_statement(const ast::File& file) {
    const std::vector<ast::Statement> statements = file.statements();
    REQUIRE(statements.size() == 1);
    return statements.front();
}

} // namespace

// --- §3.8: the vocabulary is closed ------------------------------------

TEST_CASE("an unknown annotation is an error", "[schema][annotation]") {
    const Checked checked("rule = {\n    successMsg = @prepend \"...\"\n}\n");
    REQUIRE(checked.reported(diag::Code::UnknownAnnotation));
    // The list is in the diagnostic, because an author who guessed wrong has
    // no way to guess right without it.
    CHECK(mentions(*checked.first(diag::Code::UnknownAnnotation), "@before"));
}

TEST_CASE("a reserved annotation says it is reserved, not unknown", "[schema][annotation]") {
    // §15 holds `@deprecated`, `@since` and `@experimental` back so that
    // defining one later is not a breaking change -- which only works if
    // writing one today is refused. Same code, different message: the
    // `?=` precedent of §6.3.1, where being told what happened beats being
    // told the spelling is unrecognised.
    const Checked checked("rule = {\n    successMsg = @since \"...\"\n}\n");
    const diag::Diagnostic* reported = checked.first(diag::Code::UnknownAnnotation);
    REQUIRE(reported != nullptr);
    CHECK(mentions(*reported, "reserved"));
    CHECK(mentions(*reported, "§15"));
}

TEST_CASE("every annotation the spec defines is accepted", "[schema][annotation]") {
    // The other direction, and the one that catches a typo in the table
    // above: each row of §5.4.1, on a value it applies to.
    const Checked checked("action = @replaces(stdlib) {\n"
                          "    restrictions = @before { a = { } }\n"
                          "    conditions   = @after { b = { } }\n"
                          "    effects      = @override { }\n"
                          "    prop_def     = @merge { c = int }\n"
                          "    synonyms     = @remove { d }\n"
                          "    rule         = @priority(50) { }\n"
                          "    successMsg   = @style(flavour) \"...\"\n"
                          "    failureMsg   = @debug \"...\"\n"
                          "    doc          = @platform(glk, cli) \"...\"\n"
                          "}\n");
    CHECK(checked.count() == 0);
}

// --- §5.4.1: the "Applies to" column ------------------------------------

TEST_CASE("an annotation on a value it cannot act on is an error", "[schema][annotation]") {
    // `@merge` merges an inherited block key by key, and a string has no
    // keys. Left unreported this is the worst kind of mistake: the author
    // sees an annotation, believes it is doing something, and it is not.
    const Checked checked("rule = {\n    successMsg = @merge \"You take it.\"\n}\n");
    REQUIRE(checked.reported(diag::Code::AnnotationMisapplied));
    CHECK(mentions(*checked.first(diag::Code::AnnotationMisapplied), "a block"));
}

TEST_CASE("@replaces on a key inside a declaration is an error", "[schema][annotation]") {
    // §7.6 gives `@replaces` "a whole top-level declaration". On a key it
    // names a library that has no declaration of that key to supersede --
    // combining a key's value with the inherited one is what the other five
    // annotations are for.
    const Checked nested("action = {\n    match = @replaces(stdlib) { \"polish bell\" }\n}\n");
    CHECK(nested.reported(diag::Code::AnnotationMisapplied));

    const Checked top_level("action = @replaces(stdlib) {\n    id = wait\n}\n");
    CHECK_FALSE(top_level.reported(diag::Code::AnnotationMisapplied));
}

// --- §5.4: at most one combining annotation -----------------------------

TEST_CASE("two combining annotations on one value are rejected", "[schema][annotation]") {
    // §5.4: "Contradictory combinations (`@before` `@after`) MUST be
    // rejected". There is no order of application under which a block runs
    // both before and after the one it inherited.
    const Checked checked("rule = {\n    restrictions = @before @after { a = { } }\n}\n");
    REQUIRE(checked.reported(diag::Code::AnnotationConflict));
    // Both spans, so the author can see which two.
    CHECK_FALSE(checked.first(diag::Code::AnnotationConflict)->notes().empty());
    CHECK(checked.first(diag::Code::AnnotationConflict)->notes().front().span.has_value());
}

TEST_CASE("the same combining annotation written twice is rejected", "[schema][annotation]") {
    // Likelier than the contradiction: an annotation moved to another line
    // with the original left behind.
    const Checked checked("rule = {\n    effects = @override @override { }\n}\n");
    REQUIRE(checked.reported(diag::Code::AnnotationConflict));
    CHECK(mentions(*checked.first(diag::Code::AnnotationConflict), "twice"));
}

TEST_CASE("a combining annotation alongside a non-combining one is fine", "[schema][annotation]") {
    // `@priority` orders within a phase rather than choosing one, and
    // `@debug` and `@platform` decide presence. None of the three answers
    // the question the five combining annotations answer.
    const Checked checked("rule = @debug {\n"
                          "    conditions = @after @priority(10) { a = { } }\n"
                          "}\n");
    CHECK(checked.count() == 0);
}

// --- §3.8: arguments ----------------------------------------------------

TEST_CASE("an annotation's arguments are checked", "[schema][annotation]") {
    CHECK(Checked("rule = { conditions = @priority { } }\n")
              .reported(diag::Code::AnnotationArgument));
    CHECK(Checked("rule = { conditions = @priority(fast) { } }\n")
              .reported(diag::Code::AnnotationArgument));
    CHECK(Checked("rule = @platform() { id = a }\n").reported(diag::Code::AnnotationArgument));
    CHECK(Checked("rule = { effects = @override(now) { } }\n")
              .reported(diag::Code::AnnotationArgument));
    CHECK(Checked("action = @replaces(3) { id = a }\n").reported(diag::Code::AnnotationArgument));
}

TEST_CASE("a frontend nobody reports can never be selected", "[schema][annotation]") {
    // Not pedantry about a name: the engine selects an alternative by
    // matching the running frontend against these, so `@platform(gtk)` is a
    // statement that ships and never runs.
    const Checked checked("rule = @platform(gtk) {\n    of_action = examine\n}\n");
    REQUIRE(checked.reported(diag::Code::AnnotationArgument));
    CHECK(mentions(*checked.first(diag::Code::AnnotationArgument), "glk"));
}

// --- §5.4.2: the defaults -----------------------------------------------

TEST_CASE("the combination of a value with no annotation comes from the schema",
          "[schema][annotation]") {
    // §5.4.2's table. Without this, `combine` was a word the loader stored
    // and nothing consulted -- a schema declaring `combine = smart` and one
    // declaring nothing produced identical behaviour.
    schema::KeyDecl key;
    key.name = "k";

    const auto mode_for = [&key](schema::Combine combine, const std::string& value) {
        key.combine = combine;
        const test::Parsed parsed("k = " + value + "\n");
        return schema::combination_of(only_statement(parsed.ast()), &key);
    };

    CHECK(mode_for(schema::Combine::Override, "{ a = 1 }").mode ==
          schema::Combination::Mode::Override);
    CHECK(mode_for(schema::Combine::Merge, "{ a = 1 }").mode == schema::Combination::Mode::Merge);
    CHECK(mode_for(schema::Combine::Append, "{ a = 1 }").mode == schema::Combination::Mode::After);

    // `smart` is the row that has to look at the value: an empty block is an
    // explicit override with no content (§5.2), a non-empty one appends.
    CHECK(mode_for(schema::Combine::Smart, "{ }").mode == schema::Combination::Mode::Override);
    CHECK(mode_for(schema::Combine::Smart, "{ a = 1 }").mode == schema::Combination::Mode::After);

    // Nothing declared: §5.4.2's first row, since `combine` defaults to
    // `override`. This is the case for an open schema's extra key and for a
    // property inside an instantiation.
    const test::Parsed parsed("k = { a = 1 }\n");
    CHECK(schema::combination_of(only_statement(parsed.ast()), nullptr).mode ==
          schema::Combination::Mode::Override);
}

TEST_CASE("an annotation overrides the schema's default, and says it did", "[schema][annotation]") {
    schema::KeyDecl key;
    key.name = "k";
    key.combine = schema::Combine::Override;

    const test::Parsed parsed("k = @before @priority(7) { a = 1 }\n");
    const schema::Combination combination =
        schema::combination_of(only_statement(parsed.ast()), &key);
    CHECK(combination.mode == schema::Combination::Mode::Before);
    CHECK(combination.priority == 7);
    // `annotated` is not decoration: an editor rewriting the statement must
    // not invent an annotation the author did not put there.
    CHECK(combination.annotated);

    const test::Parsed plain("k = { a = 1 }\n");
    CHECK_FALSE(schema::combination_of(only_statement(plain.ast()), &key).annotated);
}

// --- §5.4.1: the two conditional-presence annotations are not alike -----

TEST_CASE("@debug is stripped by the compiler and @platform is not",
          "[schema][annotation][presence]") {
    // The distinction the specification originally elided, and the reason
    // this test exists rather than a simpler one. `@debug` produces a release
    // build without the statement; `@platform` cannot, because one `.spak` is
    // signed and shipped for every frontend (proposal §14.2 and proposal
    // §14.4) and the running frontend declares itself at session start
    // (proposal §12.2).
    const test::Parsed debug_only("k = @debug \"...\"\n");
    const schema::Presence debug = schema::presence_of(only_statement(debug_only.ast()));
    CHECK(debug.stripped_from_release());
    CHECK_FALSE(debug.selected_at_runtime());

    const test::Parsed gated("k = @platform(glk, cli) \"...\"\n");
    const schema::Presence platform = schema::presence_of(only_statement(gated.ast()));
    CHECK_FALSE(platform.stripped_from_release());
    CHECK(platform.selected_at_runtime());
    CHECK(platform.frontends == std::vector<std::string>{"glk", "cli"});

    const test::Parsed plain("k = \"...\"\n");
    CHECK(schema::presence_of(only_statement(plain.ast())).unconditional());
}

TEST_CASE("disjoint @platform alternatives are one binding, overlapping ones are two",
          "[schema][annotation][presence]") {
    const auto presence = [](const std::string& annotation) {
        const test::Parsed parsed("k = " + annotation + " \"...\"\n");
        return schema::presence_of(only_statement(parsed.ast()));
    };

    // Disjoint: both ship, and exactly one is selected per session.
    CHECK_FALSE(
        presence("@platform(qt, web, mobile)").can_coexist_with(presence("@platform(glk, cli)")));

    // Overlapping: on `cli` the engine would hold two candidates and have no
    // rule to choose between them.
    CHECK(presence("@platform(glk, cli)").can_coexist_with(presence("@platform(cli)")));

    // No `@platform` at all runs on every frontend, so it overlaps every
    // gated one. §5.4.1 declines to make this pair a default and an
    // exception -- there is no fallback precedence.
    CHECK(presence("@platform(glk)").can_coexist_with(presence("")));

    // `@debug` separates nothing: a development build has both.
    CHECK(presence("@debug").can_coexist_with(presence("")));
    CHECK(presence("@debug").can_coexist_with(presence("@debug")));
}

TEST_CASE("arity counts a disjoint pair of frontends once", "[schema][annotation][presence]") {
    // The rule above, through the check that actually enforces it. Both are
    // bindings of `successMsg`, which `action` declares `arity = one`.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    const std::size_t before = loaded.sink.diagnostics().size();

    loaded.load_text("action = {\n"
                     "    id    = unlock_the_hatch\n"
                     "    match = { \"unlock hatch\" }\n"
                     "    successMsg = @platform(qt, web, mobile) \"It yields with a chime.\"\n"
                     "    successMsg = @platform(glk, cli)        \"It yields.\"\n"
                     "}\n");
    CHECK(loaded.sink.diagnostics().size() == before);
}

TEST_CASE("arity counts an overlapping pair of frontends twice", "[schema][annotation][presence]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();

    loaded.load_text("action = {\n"
                     "    id    = unlock_the_hatch\n"
                     "    match = { \"unlock hatch\" }\n"
                     "    successMsg = @platform(qt, cli) \"It yields with a chime.\"\n"
                     "    successMsg = @platform(cli)     \"It yields.\"\n"
                     "}\n");
    REQUIRE(loaded.reported(diag::Code::DuplicateKey));
    // The note that explains why these two collided and a disjoint pair
    // would not, since the rule is not one an author will guess.
    CHECK(mentions(*loaded.first(diag::Code::DuplicateKey), "frontends"));
}

TEST_CASE("a @debug binding still collides with a plain one", "[schema][annotation][presence]") {
    // §5.4.1: `@debug` narrows which builds a statement reaches and never
    // excludes another statement from those, so a development build has both
    // and there is nothing to choose between them.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();

    loaded.load_text("action = {\n"
                     "    id    = unlock_the_hatch\n"
                     "    match = { \"unlock hatch\" }\n"
                     "    successMsg = @debug \"It yields. [DebugId(noun)]\"\n"
                     "    successMsg = \"It yields.\"\n"
                     "}\n");
    CHECK(loaded.reported(diag::Code::DuplicateKey));
}

// --- the reference corpus ----------------------------------------------

TEST_CASE("the valid corpus carries no annotation diagnostics", "[schema][annotation][corpus]") {
    // tour.star exercises nine of the ten annotations, which makes it the
    // check that this pass does not fire on correct files -- the half of a
    // new diagnostic that is easy to leave untested and expensive to get
    // wrong, since a false positive lands on every author at once.
    const auto files = test::corpus_files(test::corpus_dir());
    REQUIRE_FALSE(files.empty());
    for (const auto& path : files) {
        INFO("valid fixture: " << path.string());
        test::Parsed parsed(test::read_bytes(path), test::corpus_name(path));
        diag::DiagnosticSink sink;
        schema::check_annotations(parsed.ast(), sink);
        for (const diag::Diagnostic& diagnostic : sink.diagnostics()) {
            INFO("unexpected " << diag::code_string(diagnostic.code()) << ": "
                               << diagnostic.message());
            CHECK(false);
        }
    }
}
