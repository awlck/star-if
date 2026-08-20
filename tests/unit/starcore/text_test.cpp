// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Styles and localisation (spec §9.3 and §9.6, backlog F7).
//
// The vocabulary half of the text layer. `libs/stardata` parses the grammar
// and knows no name inside a template; this is the pass that knows what
// `style` and `loc` are and reports the four diagnostics that follow from
// them.
#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

#include "stardata/cst/parser.hpp"
#include "stardata/diag/codes.hpp"
#include "stardata/diag/render.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/diag/source_manager.hpp"

#include "starcore/text.hpp"
#include "support/corpus.hpp"
#include "support/fixture.hpp"
#include "support/schema_harness.hpp"

using namespace stardata;

namespace {

// One or more files indexed and then checked, which is the two-phase shape
// the pass has to have: §13.2 lets a `$key` and the `loc` that defines it
// sit in either order and in either file.
class Indexed {
public:
    Indexed& add(const std::string& text, const std::string& name = "game.star") {
        const diag::SourceId id = sources.add_file(name, text);
        const cst::GreenNodePtr green = cst::parse(sources, id, cache, sink);
        files.push_back(ast::File::from(cst::SyntaxNode::root(green), id));
        index.add_file(files.back(), sink);
        return *this;
    }

    Indexed& done() {
        index.check(sink);
        return *this;
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

    diag::SourceManager sources;
    diag::DiagnosticSink sink;
    cst::GreenCache cache;
    std::vector<ast::File> files;
    starcore::TextIndex index;
};

} // namespace

TEST_CASE("a style annotation names a declared style", "[starcore][text]") {
    Indexed indexed;
    indexed
        .add("style = { id = danger }\n"
             "thing = { id = knife  description = @style(danger) \"It gleams.\" }\n")
        .done();
    CHECK(indexed.count(diag::Code::StyleUndeclared) == 0);
}

TEST_CASE("an undeclared style is an error with a suggestion", "[starcore][text]") {
    Indexed indexed;
    indexed
        .add("style = { id = danger }\n"
             "thing = { id = knife  description = @style(dangor) \"It gleams.\" }\n")
        .done();
    REQUIRE(indexed.count(diag::Code::StyleUndeclared) == 1);

    const diag::Diagnostic* reported = indexed.first(diag::Code::StyleUndeclared);
    REQUIRE(reported != nullptr);
    CHECK(indexed.sources.text(reported->primary_span()) == "dangor");
    REQUIRE(reported->fix_its().size() == 1);
    CHECK(reported->fix_its()[0].replacement == "danger");
}

TEST_CASE("a style opened inside a template is checked too", "[starcore][text]") {
    // §9.3 gives `@style(name)` two spellings -- as a §5.4 value annotation,
    // and as a span opened within the template -- and both name the same
    // thing. tour.star's `tooltip` uses both in one value.
    Indexed indexed;
    indexed
        .add("style = { id = stat_line }\n"
             "thing = { id = cutter  description = \"@style(stat_line)Damage\"\n"
             "                       tooltip     = \"@style(stat_lien)Range\" }\n")
        .done();
    REQUIRE(indexed.count(diag::Code::StyleUndeclared) == 1);
    const diag::Diagnostic* reported = indexed.first(diag::Code::StyleUndeclared);
    REQUIRE(reported != nullptr);
    CHECK(indexed.sources.text(reported->primary_span()) == "@style(stat_lien)");
}

TEST_CASE("a style declared after its use still resolves", "[starcore][text]") {
    // Which is the whole reason the pass is two-phase. Order within a file
    // and order between files are the same question (§13.2).
    Indexed indexed;
    indexed.add("thing = { id = knife  description = @style(danger) \"It gleams.\" }\n", "a.star")
        .add("style = { id = danger }\n", "b.star")
        .done();
    CHECK(indexed.count(diag::Code::StyleUndeclared) == 0);
}

TEST_CASE("a $key resolves against any loc table", "[starcore][text]") {
    Indexed indexed;
    indexed
        .add("loc = { lang = en  greeting = \"Hello.\" }\n"
             "action = { id = wave  successMsg = $greeting }\n")
        .done();
    CHECK(indexed.count(diag::Code::LocUndefined) == 0);
    CHECK(indexed.count(diag::Code::LocUnused) == 0);
}

TEST_CASE("a $key nothing defines is an error with a suggestion", "[starcore][text]") {
    Indexed indexed;
    indexed
        .add("loc = { lang = en  greeting = \"Hello.\" }\n"
             "action = { id = wave  successMsg = $greetimg }\n")
        .done();
    REQUIRE(indexed.count(diag::Code::LocUndefined) == 1);

    const diag::Diagnostic* reported = indexed.first(diag::Code::LocUndefined);
    REQUIRE(reported != nullptr);
    REQUIRE(reported->fix_its().size() == 1);
    CHECK(reported->fix_its()[0].replacement == "greeting");

    // And the key it should have been is not also reported as unused: the
    // author has one mistake, not two.
    CHECK(indexed.count(diag::Code::LocUnused) == 1);
}

TEST_CASE("a key defined in one language and referenced is not undefined", "[starcore][text]") {
    // §9.6's fallback chain read forwards: a key present in the source
    // language and missing from a translation is what the chain exists to
    // survive. Reporting it would make adding a language an error rather
    // than a partial translation.
    Indexed indexed;
    indexed
        .add("loc = { lang = en  greeting = \"Hello.\"  farewell = \"Goodbye.\" }\n"
             "loc = { lang = fr  greeting = \"Bonjour.\" }\n"
             "action = { id = wave  successMsg = $greeting  failureMsg = $farewell }\n")
        .done();
    CHECK(indexed.count(diag::Code::LocUndefined) == 0);
    CHECK(indexed.count(diag::Code::LocUnused) == 0);
}

TEST_CASE("two entries for one key in one language are a duplicate", "[starcore][text]") {
    Indexed indexed;
    indexed
        .add("loc = { lang = en  greeting = \"Hello.\" }\n"
             "loc = { lang = en  greeting = \"Hello again.\" }\n"
             "action = { id = wave  successMsg = $greeting }\n")
        .done();
    REQUIRE(indexed.count(diag::Code::LocDuplicate) == 1);

    // Citing both spans, which is what §5.3 asks of every duplicate and what
    // makes the diagnostic actionable rather than merely correct.
    const diag::Diagnostic* reported = indexed.first(diag::Code::LocDuplicate);
    REQUIRE(reported != nullptr);
    REQUIRE(reported->notes().size() == 1);
    CHECK(reported->notes()[0].span.has_value());
}

TEST_CASE("one language's key does not collide with another's", "[starcore][text]") {
    Indexed indexed;
    indexed
        .add("loc = { lang = en  greeting = \"Hello.\" }\n"
             "loc = { lang = fr  greeting = \"Bonjour.\" }\n"
             "action = { id = wave  successMsg = $greeting }\n")
        .done();
    CHECK(indexed.count(diag::Code::LocDuplicate) == 0);
}

TEST_CASE("a later file supersedes an earlier one's default rather than colliding",
          "[starcore][text]") {
    // The scope decision of §9.6, and the one that makes stdlib's
    // `opened_default` a default rather than a wall. Two entries in ONE table
    // are an ambiguity nothing can resolve; a later file superseding an
    // earlier one is what load order means everywhere else in the format
    // (§13.2), and a library naming its messages `_default` is presuming
    // exactly that.
    Indexed indexed;
    indexed.add("loc = { lang = en  opened_default = \"You open it.\" }\n", "stdlib/messages.star")
        .add("loc = { lang = en  opened_default = \"The hatch hisses open.\" }\n"
             "action = { id = open  successMsg = $opened_default }\n",
             "game/messages.star")
        .done();
    CHECK(indexed.count(diag::Code::LocDuplicate) == 0);
    CHECK(indexed.count(diag::Code::LocUndefined) == 0);
    CHECK(indexed.count(diag::Code::LocUnused) == 0);
}

TEST_CASE("a key nothing references is a warning", "[starcore][text]") {
    Indexed indexed;
    indexed
        .add("loc = { lang = en  greeting = \"Hello.\"  unused_line = \"Nobody says this.\" }\n"
             "action = { id = wave  successMsg = $greeting }\n")
        .done();
    REQUIRE(indexed.count(diag::Code::LocUnused) == 1);
    const diag::Diagnostic* reported = indexed.first(diag::Code::LocUnused);
    REQUIRE(reported != nullptr);
    CHECK(indexed.sources.text(reported->primary_span()) == "unused_line");
}

TEST_CASE("lang is not a localisation key", "[starcore][text]") {
    Indexed indexed;
    indexed
        .add("loc = { lang = en  greeting = \"Hello.\" }\n"
             "action = { id = wave  successMsg = $greeting }\n")
        .done();
    CHECK(indexed.count(diag::Code::LocUnused) == 0);
}

TEST_CASE("a loc entry is a template, and its brackets are checked", "[starcore][text]") {
    // The one place outside the type checker where a value is known to be a
    // template without asking a schema. `loc` is an open schema -- its keys
    // are whatever the game says -- so its entries have no declared type,
    // and §9.6 is what says they are templates.
    Indexed indexed;
    indexed
        .add("loc = { lang = en  greeting = \"Hello, [the noun.\" }\n"
             "action = { id = wave  successMsg = $greeting }\n")
        .done();
    CHECK(indexed.count(diag::Code::TemplateBrackets) == 1);
}

TEST_CASE("a parser grammar line is not read as a template", "[starcore][text]") {
    // §15 reserves `[` and `]` for "the template language and parser grammar
    // tokens", and the second of those is a string too. stdlib's every action
    // has one. If this pass read every string as a template, `match = {
    // "take [something]" }` would be an unbalanced-bracket error on the
    // reference library.
    Indexed indexed;
    indexed.add("action = { id = take  match = { \"take [something]\" } }\n").done();
    CHECK(indexed.sink.diagnostics().empty());
}

// --- the reference corpus ----------------------------------------------

TEST_CASE("the built-in set and stdlib define every key they name", "[starcore][text][corpus]") {
    // This found nine real errors when it was first run. stdlib's actions
    // named `$taken_default`, `$already_open` and seven more, and nothing
    // anywhere declared them -- so a game that did not happen to define them
    // itself would have rendered «taken_default» in play, §9.6's visible
    // fallback doing exactly the job it exists to do, on the reference
    // library. stdlib/stdlib/messages.star is the fix.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    REQUIRE(loaded.sink.error_count() == 0);
    REQUIRE_FALSE(loaded.files.empty());

    Indexed indexed;
    for (const std::filesystem::path& path : loaded.files) {
        indexed.add(test::read_bytes(path), path.generic_string());
    }
    indexed.done();

    for (const diag::Diagnostic& diagnostic : indexed.sink.diagnostics()) {
        std::ostringstream rendered;
        diag::render_human(rendered, diagnostic, indexed.sources, /*use_color=*/false);
        INFO(rendered.str());
        CHECK(false);
    }
}

TEST_CASE("the valid corpus keeps the text layer's rules", "[starcore][text][corpus]") {
    // Each valid fixture on its own, which is how a game is loaded: tour.star
    // is a project, not a library added to one.
    //
    // Warnings a file suppresses with `# check: allow` are honoured, using
    // the same pragma tests/check_stardata.py reads. tour.star carries one
    // for W-LOC-UNUSED and says why: its §18 declares loc entries purely to
    // demonstrate string syntax, and its §17 declares the engine's own
    // fallback message, which the runtime references rather than the data.
    const auto files = test::corpus_files(test::corpus_dir());
    REQUIRE_FALSE(files.empty());

    for (const auto& path : files) {
        INFO("valid fixture: " << path.string());
        const std::string contents = test::read_bytes(path);
        const std::set<std::string> allowed = test::allowed_codes(contents);

        Indexed indexed;
        indexed.add(contents, test::corpus_name(path)).done();

        for (const diag::Diagnostic& diagnostic : indexed.sink.diagnostics()) {
            if (allowed.contains(std::string(diag::code_string(diagnostic.code())))) {
                continue;
            }
            std::ostringstream rendered;
            diag::render_human(rendered, diagnostic, indexed.sources, /*use_color=*/false);
            INFO(rendered.str());
            CHECK(false);
        }
    }
}
