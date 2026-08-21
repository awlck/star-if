// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Globals, constants and flags (spec §6.4 and §6.4.1, backlog F10).
//
// §6.4.1's argument is the one to keep in mind reading these: as undeclared
// magic strings, `set_flag = captain_found` paired with `flag_set =
// captain_finded` is a silent, permanent bug -- the condition never fires and
// nothing reports it. Every test below is a file that would load without
// complaint under a Clausewitz-style reader.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <sstream>
#include <string>

#include "stardata/cst/parser.hpp"
#include "stardata/diag/codes.hpp"
#include "stardata/diag/render.hpp"
#include "stardata/diag/sink.hpp"

#include "starcore/globals.hpp"
#include "support/corpus.hpp"
#include "support/cst_harness.hpp"
#include "support/fixture.hpp"
#include "support/schema_harness.hpp"

using namespace stardata;

namespace {

// A file loaded against the real core-owned set and then walked, which is
// how the pass runs: the registry decides whether the declaration itself is
// well formed, and this decides what the uses mean.
class Checked {
public:
    explicit Checked(const std::string& text, const std::string& name = "game.star") {
        loaded.load_builtin();
        loaded.load_stdlib();
        REQUIRE(loaded.sink.error_count() == 0);
        loaded.load_text(text, "a ruleset", name);

        const diag::SourceId id = sources.add_file(name, text);
        green = cst::parse(sources, id, cache, sink);
        const ast::File file = ast::File::from(cst::SyntaxNode::root(green), id);
        index.add_file(file, loaded.set, sink);
        index.check(sink);
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

    test::LoadedSet loaded;
    diag::SourceManager sources;
    diag::DiagnosticSink sink;
    cst::GreenCache cache;
    cst::GreenNodePtr green;
    starcore::GlobalIndex index;
};

} // namespace

// --- §6.4, declared and typed -------------------------------------------

TEST_CASE("a global is registered with its declared type", "[starcore][globals]") {
    const Checked checked(
        "global = { id = alert_level  type = int  initial = 0 }\n"
        "const  = { id = max_temp     type = int  value   = 1200 }\n"
        "rule = { of_action = look  when = { }\n"
        "         conditions = { global = { alert_level >= 1 } }\n"
        "         effects    = { add_global = { id = max_temp  amount = 0 } } }\n");
    REQUIRE(checked.index.declarations().size() == 2);
    CHECK(checked.index.declarations()[0].id == "alert_level");
    CHECK_FALSE(checked.index.declarations()[0].is_const);
    CHECK(checked.index.declarations()[0].type.to_string() == "int");

    const starcore::GlobalIndex::Declaration* found = checked.index.find("max_temp");
    REQUIRE(found != nullptr);
    CHECK(found->is_const);
}

TEST_CASE("a global's declared type has to name a type", "[starcore][globals]") {
    // The gap this closes. `check_declared_types` walks schemas and classes,
    // and a global is neither -- so until F10 a global could be declared
    // `type = frobnicate` and load without a word.
    const Checked checked("global = { id = alert_level  type = frobnicate  initial = 0 }\n"
                          "rule = { of_action = look  when = { }\n"
                          "         conditions = { global = { alert_level >= 1 } } }\n");
    CHECK(checked.count(diag::Code::SchemaInvalid) == 1);
}

TEST_CASE("an initial value is checked against the type declared beside it",
          "[starcore][globals]") {
    // A dependent type: the `initial` key's type is the value of the `type`
    // key next to it, which no `type =` on a key declaration can express.
    // Both keys are declared `any` and checked here instead.
    const Checked checked("global = { id = alert_level  type = int  initial = \"high\" }\n"
                          "rule = { of_action = look  when = { }\n"
                          "         conditions = { global = { alert_level >= 1 } } }\n");
    CHECK(checked.count(diag::Code::TypeMismatch) == 1);
}

TEST_CASE("a collection-valued initial is legal", "[starcore][globals]") {
    // §6.4's own examples: `initial = { }` for a set, and a record block for
    // a map. `scalar` rejected both, which is what `any` was added for.
    const Checked checked("global = { id = seen  type = set<identifier>  initial = { } }\n"
                          "global = { id = moods type = map<identifier, identifier>\n"
                          "           initial = { vex = wary } }\n"
                          "rule = { of_action = look  when = { }\n"
                          "         conditions = { global = { seen == none  moods == none } } }\n");
    CHECK(checked.count(diag::Code::TypeMismatch) == 0);
    CHECK(checked.count(diag::Code::GlobalUnused) == 0);
}

// --- §6.4.1, flags -------------------------------------------------------

TEST_CASE("a flag names a declared bool global", "[starcore][globals]") {
    const Checked checked("global = { id = captain_found  type = bool  initial = no }\n"
                          "rule = { of_action = look  when = { }\n"
                          "         conditions = { flag_set = captain_found }\n"
                          "         effects    = { set_flag = captain_found } }\n");
    CHECK(checked.sink.diagnostics().empty());
}

TEST_CASE("the running example of A17 is a compile error", "[starcore][globals]") {
    // §6.4.1's own words: `set_flag = captain_found` paired with `flag_set =
    // captain_finded` is "a silent, permanent bug of exactly the kind the
    // schema layer exists to prevent -- the condition simply never fires, and
    // nothing reports it".
    const Checked checked("global = { id = captain_found  type = bool  initial = no }\n"
                          "rule = { of_action = look  when = { }\n"
                          "         conditions = { flag_set = captain_finded }\n"
                          "         effects    = { set_flag = captain_found } }\n");
    REQUIRE(checked.count(diag::Code::FlagUndeclared) == 1);

    const diag::Diagnostic* reported = checked.first(diag::Code::FlagUndeclared);
    REQUIRE(reported != nullptr);
    REQUIRE(reported->fix_its().size() == 1);
    CHECK(reported->fix_its()[0].replacement == "captain_found");

    // And the second symptom of the same bug: the only place that would have
    // read the global reads something else.
    CHECK(checked.count(diag::Code::GlobalUnused) == 1);
}

TEST_CASE("a flag naming a non-bool global is an error", "[starcore][globals]") {
    const Checked checked("global = { id = alert_level  type = int  initial = 0 }\n"
                          "rule = { of_action = look  when = { }\n"
                          "         effects = { set_flag = alert_level } }\n");
    REQUIRE(checked.count(diag::Code::FlagNotBool) == 1);

    // Citing the declaration, since that is the other end of the decision:
    // either the flag is wrong or the type is.
    const diag::Diagnostic* reported = checked.first(diag::Code::FlagNotBool);
    REQUIRE(reported != nullptr);
    REQUIRE(reported->notes().size() == 1);
    CHECK(reported->notes()[0].span.has_value());
}

TEST_CASE("clear_flag and flag_set are held to the same rule", "[starcore][globals]") {
    const Checked checked("global = { id = alert_level  type = int  initial = 0 }\n"
                          "rule = { of_action = look  when = { }\n"
                          "         conditions = { flag_set  = alert_level }\n"
                          "         effects    = { clear_flag = alert_level } }\n");
    CHECK(checked.count(diag::Code::FlagNotBool) == 2);
}

// --- §6.4, the explicit forms -------------------------------------------

TEST_CASE("set_global naming no declared global is an error", "[starcore][globals]") {
    // §6.4: "Both MUST be declared. There is no implicit creation." Not
    // E-FLAG-UNDECLARED -- §14.3 gives the flag sugar and a plain global
    // reference separate rows.
    const Checked checked("global = { id = alert_level  type = int  initial = 0 }\n"
                          "rule = { of_action = look  when = { }\n"
                          "         conditions = { global = { alert_level >= 1 } }\n"
                          "         effects = { set_global = { id = alert_levl  value = 2 } } }\n");
    CHECK(checked.count(diag::Code::GlobalUndeclared) == 1);
    CHECK(checked.count(diag::Code::FlagUndeclared) == 0);
}

TEST_CASE("a condition naming no declared global is an error", "[starcore][globals]") {
    const Checked checked(
        "global = { id = alert_level  type = int  initial = 0 }\n"
        "rule = { of_action = look  when = { }\n"
        "         conditions = { global = { alert_levl >= 1 } }\n"
        "         effects = { add_global = { id = alert_level  amount = 1 } } }\n");
    CHECK(checked.count(diag::Code::GlobalUndeclared) == 1);
}

// --- §6.4, never read ----------------------------------------------------

TEST_CASE("a global that is only written is never read", "[starcore][globals]") {
    // The write sites are the exact half of the analysis: `set_flag` and
    // `set_global` name a global unambiguously, so "set and never tested" is
    // caught rather than assumed away.
    const Checked checked("global = { id = coolant_vented  type = bool  initial = no }\n"
                          "rule = { of_action = look  when = { }\n"
                          "         effects = { set_flag = coolant_vented } }\n");
    REQUIRE(checked.count(diag::Code::GlobalUnused) == 1);

    const diag::Diagnostic* reported = checked.first(diag::Code::GlobalUnused);
    REQUIRE(reported != nullptr);
    CHECK(checked.sources.text(reported->primary_span()) == "coolant_vented");
}

TEST_CASE("testing a flag is a read", "[starcore][globals]") {
    const Checked checked("global = { id = coolant_vented  type = bool  initial = no }\n"
                          "rule = { of_action = look  when = { }\n"
                          "         conditions = { flag_set = coolant_vented } }\n");
    CHECK(checked.count(diag::Code::GlobalUnused) == 0);
}

TEST_CASE("an identifier anywhere else counts as a read", "[starcore][globals]") {
    // Deliberately generous. §6.6.3 makes "a bare identifier in an argument
    // position always a global", so `collection = seen_endings` and
    // `value_of = core_temp` (§10.6.1) are reads -- but telling those from an
    // unrelated identifier of the same name needs §6.6's datum resolution,
    // which is backlog F9's. Erring toward "read" is right for a warning: a
    // missed one costs nothing and a false one is noise on correct data.
    const Checked checked("global = { id = core_temp  type = int  initial = 300 }\n"
                          "rule = { of_action = look  when = { }\n"
                          "         conditions = { compare = { left = { value_of = core_temp }\n"
                          "                                    op = \">=\"\n"
                          "                                    right = { literal = 400 } } } }\n");
    CHECK(checked.count(diag::Code::GlobalUnused) == 0);
}

TEST_CASE("a const nothing names is reported too", "[starcore][globals]") {
    const Checked checked("const = { id = max_temp  type = int  value = 1200 }\n");
    REQUIRE(checked.count(diag::Code::GlobalUnused) == 1);
    const diag::Diagnostic* reported = checked.first(diag::Code::GlobalUnused);
    REQUIRE(reported != nullptr);
    CHECK(reported->message().find("const") != std::string::npos);
}

TEST_CASE("a global declared in one file and read in another is read", "[starcore][globals]") {
    // Which is why the pass is two-phase: §13.2 lets the two sit in either
    // order and in either file.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    REQUIRE(loaded.sink.error_count() == 0);

    diag::SourceManager sources;
    diag::DiagnosticSink sink;
    cst::GreenCache cache;
    starcore::GlobalIndex index;

    // The reader first, so the declaration genuinely arrives afterwards.
    const std::string uses = "rule = { of_action = look  when = { }\n"
                             "         conditions = { flag_set = captain_found } }\n";
    const std::string declares = "global = { id = captain_found  type = bool  initial = no }\n";
    for (const auto& [name, text] :
         {std::pair<std::string, std::string>{"uses.star", uses}, {"declares.star", declares}}) {
        const diag::SourceId id = sources.add_file(name, text);
        const cst::GreenNodePtr green = cst::parse(sources, id, cache, sink);
        index.add_file(ast::File::from(cst::SyntaxNode::root(green), id), loaded.set, sink);
    }
    index.check(sink);

    CHECK(sink.diagnostics().empty());
}

// --- the reference corpus ------------------------------------------------

TEST_CASE("stdlib and the valid corpus keep the global rules", "[starcore][globals][corpus]") {
    // This found five real cases in tour.star on its first run: `times_caught`,
    // `last_accused`, `coolant_vented`, `hydration` and `intoxication` were
    // each written by an effect and tested by nothing. The Python checker
    // missed them because its read detection counted a write as a read; the
    // corpus now reads all five, and that checker counts positions instead.
    test::LoadedSet builtin;
    builtin.load_builtin();
    builtin.load_stdlib();
    REQUIRE(builtin.sink.error_count() == 0);
    REQUIRE_FALSE(builtin.files.empty());

    // The library's own files are checked against the set they are: nothing
    // else declares what they use.
    for (const std::filesystem::path& path : builtin.files) {
        INFO("file: " << path.string());
        test::Parsed parsed(test::read_bytes(path), path.generic_string());
        diag::DiagnosticSink sink;
        starcore::GlobalIndex index;
        index.add_file(parsed.ast(), builtin.set, sink);
        index.check(sink);
        for (const diag::Diagnostic& diagnostic : sink.diagnostics()) {
            std::ostringstream rendered;
            diag::render_human(rendered, diagnostic, parsed.sources(), /*use_color=*/false);
            INFO(rendered.str());
            CHECK(false);
        }
    }

    // A game is checked against a set that has the game in it. tour.star
    // declares the enums its own globals are typed by (`alert_level_enum`,
    // `mood_enum`), so a set holding only the library would report them as
    // types nobody declares -- which is true of that set and false of the
    // project, and the second is the question being asked.
    for (const auto& path : test::corpus_files(test::corpus_dir())) {
        INFO("valid fixture: " << path.string());
        const std::string contents = test::read_bytes(path);

        test::LoadedSet loaded;
        loaded.load_builtin();
        loaded.load_stdlib();
        loaded.load_text(contents, "a game", test::corpus_name(path));

        test::Parsed parsed(contents, test::corpus_name(path));
        diag::DiagnosticSink sink;
        starcore::GlobalIndex index;
        index.add_file(parsed.ast(), loaded.set, sink);
        index.check(sink);

        for (const diag::Diagnostic& diagnostic : sink.diagnostics()) {
            std::ostringstream rendered;
            diag::render_human(rendered, diagnostic, parsed.sources(), /*use_color=*/false);
            INFO(rendered.str());
            CHECK(false);
        }
    }
}
