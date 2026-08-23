// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog F11's first `[OPEN]`: the permitted-key rule of spec §7.4. §8.7
// states the point directly -- "`times_reboofed` is an error rather than a
// second property" -- and F4 deliberately left it unchecked, because telling
// a typo apart from one of §7.4's universal keys needs the universal-key
// list, and most of that list is core vocabulary `libs/stardata` may not
// name.
//
// This also closes backlog F6's own `[OPEN]`: nothing suggested a property
// name inside an instantiation, because nothing reported one as unknown in
// the first place. The suggestion machinery (schema/suggest.hpp) was already
// built and unused at this call site.
#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

#include "stardata/cst/parser.hpp"
#include "stardata/diag/codes.hpp"
#include "stardata/schema/loader.hpp"

#include "starcore/instance_properties.hpp"
#include "support/corpus.hpp"
#include "support/cst_harness.hpp"
#include "support/fixture.hpp"
#include "support/schema_harness.hpp"

using namespace stardata;

namespace {

// A world with a class that has a property, a trait that has another, and a
// second class descending from the first -- enough surface for "own",
// "inherited" and "through a trait" to each be a different property.
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
)";

// Loads `kWorld` plus `text` into a real registry and walks the same source
// with the pass under test -- the shape narrowing_test.cpp's `Analysed`
// uses, for the same reason: the registry and the tree have to agree about
// what `gadget` declares, and loading the identical text into both is what
// guarantees it.
class Checked {
public:
    explicit Checked(const std::string& text) : parsed_(std::string(kWorld) + text) {
        loaded_.load_builtin();
        loaded_.load_text(std::string(kWorld) + text, "a library", "instances.star");
        starcore::check_instance_properties(parsed_.ast(), loaded_.set, sink_);
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

private:
    test::LoadedSet loaded_;
    test::Parsed parsed_;
    diag::DiagnosticSink sink_;
};

} // namespace

// --- the ordinary case: real properties are left alone ------------------

TEST_CASE("an object's own, inherited and trait-borne properties are all accepted",
          "[starcore][instance_properties]") {
    const Checked world(
        "fancy_gadget = { id = one  serial = \"A1\"  polish = 3  integrity = 9 }\n");
    CHECK(world.count() == 0);
}

TEST_CASE("an object-local prop_def is accepted, and suggested for a nearby typo",
          "[starcore][instance_properties]") {
    const Checked exact(
        "gadget = { id = odd_one  prop_def = { rune_count = int }  rune_count = 3 }\n");
    CHECK(exact.count() == 0);

    const Checked typo(
        "gadget = { id = odd_one  prop_def = { rune_count = int }  rune_conut = 3 }\n");
    REQUIRE(typo.reported(diag::Code::PropUnknown));
    const diag::Diagnostic* diagnostic = nullptr;
    for (const diag::Diagnostic& candidate : typo.sink().diagnostics()) {
        if (candidate.code() == diag::Code::PropUnknown) {
            diagnostic = &candidate;
        }
    }
    REQUIRE(diagnostic != nullptr);
    REQUIRE(diagnostic->fix_its().size() == 1);
    CHECK(diagnostic->fix_its()[0].replacement == "rune_count");
}

// --- the typo case: F11's own worked example -----------------------------

TEST_CASE("a key naming no property is an error with a suggestion",
          "[starcore][instance_properties]") {
    const Checked world("gadget = { id = one  serail = \"A1\" }\n");
    REQUIRE(world.reported(diag::Code::PropUnknown));
    const diag::Diagnostic* diagnostic = nullptr;
    for (const diag::Diagnostic& candidate : world.sink().diagnostics()) {
        if (candidate.code() == diag::Code::PropUnknown) {
            diagnostic = &candidate;
        }
    }
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->message().find("serail") != std::string::npos);
    REQUIRE(diagnostic->fix_its().size() == 1);
    CHECK(diagnostic->fix_its()[0].replacement == "serial");
}

TEST_CASE("a name resembling nothing gets no suggestion", "[starcore][instance_properties]") {
    // §7.3's other rule, carried over unchanged: refusing to guess is what
    // keeps a real suggestion worth reading.
    const Checked world("gadget = { id = one  zzzzzzzzzzz = 1 }\n");
    REQUIRE(world.reported(diag::Code::PropUnknown));
    for (const diag::Diagnostic& diagnostic : world.sink().diagnostics()) {
        if (diagnostic.code() == diag::Code::PropUnknown) {
            CHECK(diagnostic.fix_its().empty());
        }
    }
}

// --- the universal keys: none of them is a property, all are accepted ----

TEST_CASE("id, traits and prop_def are accepted with no property of that name",
          "[starcore][instance_properties]") {
    const Checked world("fancy_gadget = {\n"
                        "    id       = one\n"
                        "    traits   = { fragile }\n"
                        "    prop_def = { one_off = int }\n"
                        "    serial   = \"A1\"\n"
                        "    polish   = 3\n"
                        "    integrity = 9\n"
                        "    one_off  = 4\n"
                        "}\n");
    CHECK(world.count() == 0);
}

TEST_CASE("every placement keyword is accepted, sugar and long form alike",
          "[starcore][instance_properties]") {
    for (const char* keyword : {"in", "on", "under", "behind", "carried", "worn", "part_of"}) {
        INFO("keyword: " << keyword);
        const Checked sugar(std::string("gadget = { id = one  ") + keyword + " = somewhere }\n");
        CHECK(sugar.count() == 0);
    }

    // The long spelling: `holder` and `relation` are ordinary properties of
    // `starcore.object` since F13, reachable through the same class walk as
    // `serial` -- no special case needed, and this is the test that it holds.
    const Checked longform(
        "gadget = { id = one  holder = somewhere  relation = in  sector = a_sector }\n");
    CHECK(longform.count() == 0);
}

TEST_CASE("the long `object` spelling is checked the same way, and of_class is not a property",
          "[starcore][instance_properties]") {
    const Checked clean("object = { id = one  of_class = gadget  serial = \"A1\" }\n");
    CHECK(clean.count() == 0);

    const Checked typo("object = { id = one  of_class = gadget  serail = \"A1\" }\n");
    REQUIRE(typo.reported(diag::Code::PropUnknown));
}

// --- the corpus: the rule fires on nothing correct ------------------------

TEST_CASE("stdlib and the corpus both keep the permitted-key rule",
          "[starcore][instance_properties][corpus]") {
    // Loaded, not merely parsed. Unlike placement, this pass asks the
    // registry what a class declares -- and tour.star's own §6.1 adds
    // `strength`, `presence` and friends to `person` with a `class_extension`
    // of its own, so those have to be in the registry the pass consults, not
    // only in the tree it walks. narrowing_test.cpp's `Analysed` takes the
    // same care for the same reason.
    //
    // A `# check: allow E-PROP-UNKNOWN` file honours the same pragma
    // text_test.cpp does for W-LOC-UNUSED: tour.star carries one, and says
    // why in its header -- two properties reached only through a trait mixed
    // in at the object rather than the class (§8.4 step 2, not yet resolved
    // by this pass or by F4's or F12's), and one, `side`, that is proposal
    // §5.7's Phase 2 two-sided-door facet with no schema yet.
    for (const auto& path : test::corpus_files(test::corpus_dir())) {
        INFO("valid fixture: " << path.string());
        test::LoadedSet loaded;
        loaded.load_builtin();
        loaded.load_stdlib();
        REQUIRE(loaded.sink.error_count() == 0);

        const std::string contents = test::read_bytes(path);
        const std::set<std::string> allowed = test::allowed_codes(contents);
        const diag::SourceId id = loaded.load_text(contents, "a library", test::corpus_name(path));

        diag::DiagnosticSink parse_sink;
        const cst::GreenNodePtr green = cst::parse(loaded.sources, id, loaded.cache, parse_sink);
        const ast::File ast = ast::File::from(cst::SyntaxNode::root(green), id);

        diag::DiagnosticSink sink;
        starcore::check_instance_properties(ast, loaded.set, sink);
        for (const diag::Diagnostic& diagnostic : sink.diagnostics()) {
            INFO("diagnostic: " << diagnostic.message());
            CHECK(allowed.contains(std::string(diag::code_string(diagnostic.code()))));
        }
    }
}
