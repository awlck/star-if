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
#include "stardata/schema/types.hpp"

#include "starcore/globals.hpp"
#include "support/corpus.hpp"
#include "support/cst_harness.hpp"
#include "support/fixture.hpp"
#include "support/schema_harness.hpp"

using namespace stardata;

namespace {

// A file loaded against the real set and then walked, which is how the pass
// runs. TWO SINKS, because the work is split across two libraries: the format
// layer reads the `global` and `const` declarations and reports anything
// wrong with one (§7.2.4 makes them format forms), and this pass reports what
// the *uses* mean. A test should not have to know which half spoke, so
// `count` and `first` look in both, load order first.
class Checked {
public:
    explicit Checked(const std::string& text, const std::string& name = "game.star") {
        loaded.load_builtin();
        loaded.load_stdlib();
        REQUIRE(loaded.sink.error_count() == 0);
        const diag::SourceId id = loaded.load_text(text, "a ruleset", name);
        // The whole-program type pass, which is where a global's declared
        // type is checked for naming a type at all. It runs once, after
        // loading, because a type may be declared in a later file (§13.2).
        schema::check_declared_types(loaded.set, loaded.sink);

        green = cst::parse(loaded.sources, id, loaded.cache, sink);
        const ast::File file = ast::File::from(cst::SyntaxNode::root(green), id);
        index.add_file(file);
        index.check(loaded.set, sink);
    }

    [[nodiscard]] std::size_t count(diag::Code code) const {
        std::size_t found = 0;
        for (const diag::DiagnosticSink* which : {&loaded.sink, &sink}) {
            for (const diag::Diagnostic& diagnostic : which->diagnostics()) {
                found += diagnostic.code() == code ? 1 : 0;
            }
        }
        return found;
    }

    [[nodiscard]] const diag::Diagnostic* first(diag::Code code) const {
        for (const diag::DiagnosticSink* which : {&loaded.sink, &sink}) {
            for (const diag::Diagnostic& diagnostic : which->diagnostics()) {
                if (diagnostic.code() == code) {
                    return &diagnostic;
                }
            }
        }
        return nullptr;
    }

    test::LoadedSet loaded;
    // One manager for both halves, so a span from either renders. Declared
    // after `loaded` because it binds to a member of it.
    diag::SourceManager& sources = loaded.sources;
    diag::DiagnosticSink sink;
    cst::GreenNodePtr green;
    starcore::GlobalIndex index;
};

} // namespace

// --- §6.4, declared and typed -------------------------------------------

TEST_CASE("a global is registered with its declared type", "[starcore][globals]") {
    // The registry is the format layer's, not this pass's: `global` and
    // `const` are format forms (§7.2.4), so what a program declares is
    // answered by `SchemaSet`. The test stays here because what it is really
    // asserting is that the two halves see the same globals -- this pass
    // resolves every name below against exactly these entries.
    const Checked checked(
        "global = { id = alert_level  type = int  initial = 0 }\n"
        "const  = { id = max_temp     type = int  value   = 1200 }\n"
        "rule = { of_action = look  when = { }\n"
        "         conditions = { global = { alert_level >= 1 } }\n"
        "         effects    = { add_global = { id = max_temp  amount = 0 } } }\n");
    REQUIRE(checked.loaded.set.globals().size() == 2);
    CHECK(checked.loaded.set.globals()[0].id == "alert_level");
    CHECK_FALSE(checked.loaded.set.globals()[0].is_const);
    CHECK(checked.loaded.set.globals()[0].type.to_string() == "int");

    const schema::GlobalDecl* found = checked.loaded.set.find_global("max_temp");
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
    // key next to it, which no fixed `type =` on a key declaration can
    // express. §7.2's `type_of = type` says exactly that, and the format
    // layer's generic value check does the rest.
    const Checked checked("global = { id = alert_level  type = int  initial = \"high\" }\n"
                          "rule = { of_action = look  when = { }\n"
                          "         conditions = { global = { alert_level >= 1 } } }\n");
    CHECK(checked.count(diag::Code::TypeMismatch) == 1);
}

TEST_CASE("a collection-valued initial is legal", "[starcore][globals]") {
    // §6.4's own examples: `initial = { }` for a set, and a record block for
    // a map. A `type = scalar` on the key would reject both, which is the
    // whole reason the type has to come from the sibling.
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

    diag::DiagnosticSink sink;
    starcore::GlobalIndex index;

    // The reader first, so the declaration genuinely arrives afterwards.
    const std::string uses = "rule = { of_action = look  when = { }\n"
                             "         conditions = { flag_set = captain_found } }\n";
    const std::string declares = "global = { id = captain_found  type = bool  initial = no }\n";
    for (const auto& [name, text] :
         {std::pair<std::string, std::string>{"uses.star", uses}, {"declares.star", declares}}) {
        const diag::SourceId id = loaded.load_text(text, "a ruleset", name);
        const cst::GreenNodePtr green = cst::parse(loaded.sources, id, loaded.cache, sink);
        index.add_file(ast::File::from(cst::SyntaxNode::root(green), id));
    }
    index.check(loaded.set, sink);

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
    //
    // One file at a time, which is the right shape for the error questions --
    // "does this file name a global nobody declares" is answerable about one
    // file -- and the wrong shape for W-GLOBAL-UNUSED, which is a question
    // about a whole program. The declarations come from the registry now, so
    // a per-file index would call every global in the set unread except the
    // ones this file happens to mention. Skipped rather than accumulated,
    // because rendering a diagnostic needs the manager its span came from.
    for (const std::filesystem::path& path : builtin.files) {
        INFO("file: " << path.string());
        test::Parsed parsed(test::read_bytes(path), path.generic_string());
        diag::DiagnosticSink sink;
        starcore::GlobalIndex index;
        index.add_file(parsed.ast());
        index.check(builtin.set, sink);
        for (const diag::Diagnostic& diagnostic : sink.diagnostics()) {
            if (diagnostic.code() == diag::Code::GlobalUnused) {
                continue;
            }
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
        index.add_file(parsed.ast());
        index.check(loaded.set, sink);

        for (const diag::Diagnostic& diagnostic : sink.diagnostics()) {
            std::ostringstream rendered;
            diag::render_human(rendered, diagnostic, parsed.sources(), /*use_color=*/false);
            INFO(rendered.str());
            CHECK(false);
        }
    }
}
