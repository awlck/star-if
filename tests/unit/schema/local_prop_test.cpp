// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog F11: properties declared on a single object (spec §8.7, §8.4).
//
// §8.7's argument is worth restating, because it is the one place this format
// deliberately departs from how a general-purpose language would do it: "in
// C++ or Java a field must belong to a type, and a type with one instance is
// an accepted cost. Here it is not, because a game is mostly singular things."
// The reactor console has a `diagnostic_code`; nothing else ever will; making
// the author invent a class for it is the wrong tax.
//
// What that buys is only worth having if the declaration is enforced, which
// is what these tests are. A local property is typed like any other, resolves
// before the class's (§8.4), and may not quietly give an inherited name a
// second type.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "stardata/diag/codes.hpp"
#include "stardata/schema/loader.hpp"
#include "stardata/schema/schema.hpp"
#include "stardata/schema/types.hpp"

#include "support/schema_harness.hpp"

using namespace stardata;

namespace {

// A class with one property, and whatever instantiations a test adds. The
// class is a library's, not core's, so nothing here depends on stdlib
// happening to declare a property of a given type.
constexpr const char* kConsole = "class = {\n"
                                 "    id       = console\n"
                                 "    prop_def = { times_rebooted = int }\n"
                                 "}\n";

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

TEST_CASE("an object-local prop_def declares a property on that object only",
          "[schema][localprop]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text(std::string(kConsole) + "console = {\n"
                                             "    id              = reactor_console\n"
                                             "    prop_def        = { diagnostic_code = string }\n"
                                             "    diagnostic_code = \"E-114\"\n"
                                             "}\n");
    // The value type-checks against the local declaration, which is the whole
    // point: before F11 it was an unrecognised key and nothing looked at it.
    CHECK(loaded.sink.error_count() == 0);
}

TEST_CASE("a value that contradicts its object-local declaration is an error",
          "[schema][localprop]") {
    // The other half of the same fact. If a local `prop_def` did not really
    // type anything, this would load silently -- which is exactly the untyped
    // looseness §8.7's last paragraph says the declaration exists to remove.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text(std::string(kConsole) + "console = {\n"
                                             "    id              = reactor_console\n"
                                             "    prop_def        = { diagnostic_code = string }\n"
                                             "    diagnostic_code = 114\n"
                                             "}\n");
    CHECK(loaded.reported(diag::Code::TypeMismatch));
}

TEST_CASE("a local property does not leak to siblings or to the class", "[schema][localprop]") {
    // "The property exists on **this object only**. It is not added to the
    // class and no sibling gains it." A sibling writing the same key is
    // writing a key nothing declares -- which today is silently permitted
    // (see the [OPEN] on §7.4's universal keys), so what is asserted here is
    // the part that is decidable: the class itself did not acquire it.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text(std::string(kConsole) + "console = {\n"
                                             "    id              = reactor_console\n"
                                             "    prop_def        = { diagnostic_code = string }\n"
                                             "    diagnostic_code = \"E-114\"\n"
                                             "}\n");

    const schema::ClassDecl* console = loaded.set.find_class("console");
    REQUIRE(console != nullptr);
    CHECK(console->find_property("times_rebooted") != nullptr);
    CHECK(console->find_property("diagnostic_code") == nullptr);
}

TEST_CASE("resolution puts object-local declarations first", "[schema][localprop]") {
    // §8.4, step 1. Asserted directly rather than through a diagnostic,
    // because the ordering is the mechanism and a test that only watched for
    // an error would pass just as well if the class won and happened to
    // agree.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text(kConsole);

    const schema::ClassDecl* console = loaded.set.find_class("console");
    REQUIRE(console != nullptr);

    std::vector<schema::PropDecl> local;
    schema::PropDecl own;
    own.name = "times_rebooted";
    own.type = ast::TypeRef{"string", {}, {}};
    local.push_back(own);

    const schema::PropDecl* resolved =
        schema::resolve_property("times_rebooted", local, *console, loaded.set);
    REQUIRE(resolved != nullptr);
    CHECK(resolved->type.to_string() == "string"); // the local one, not the class's `int`

    // And with no local declaration, the class's.
    const schema::PropDecl* inherited =
        schema::resolve_property("times_rebooted", {}, *console, loaded.set);
    REQUIRE(inherited != nullptr);
    CHECK(inherited->type.to_string() == "int");
}

TEST_CASE("redeclaring an inherited name at a different type is an error", "[schema][localprop]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text(std::string(kConsole) + "console = {\n"
                                             "    id       = odd_console\n"
                                             "    prop_def = { times_rebooted = string }\n"
                                             "}\n");
    REQUIRE(loaded.reported(diag::Code::PropDefTypeMismatch));
    // Both types named, and both spans cited -- an author looking at one line
    // needs to be sent to the other.
    const diag::Diagnostic* reported = loaded.first(diag::Code::PropDefTypeMismatch);
    REQUIRE(reported != nullptr);
    CHECK(mentions(*reported, "int"));
    CHECK(mentions(*reported, "string"));
    REQUIRE_FALSE(reported->notes().empty());
    CHECK(reported->notes().front().span.has_value());
}

TEST_CASE("redeclaring an inherited name at the same type is a warning", "[schema][localprop]") {
    // A warning and not an error, because nothing is wrong with the data --
    // §8.7 says "redundant", and the value of saying so is that a property
    // promoted to its class leaves these behind.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text(std::string(kConsole) + "console = {\n"
                                             "    id       = spare_console\n"
                                             "    prop_def = { times_rebooted = int }\n"
                                             "}\n");
    REQUIRE(loaded.reported(diag::Code::PropDefRedundant));
    CHECK(loaded.sink.error_count() == 0);
    // With a fix-it, since the correction is exactly "delete this line".
    CHECK_FALSE(loaded.first(diag::Code::PropDefRedundant)->fix_its().empty());
}

TEST_CASE("an inherited name is found through the class chain", "[schema][localprop]") {
    // §8.4 step 3 walks ancestors, so the rule above has to fire on a
    // property inherited from a grandparent as much as from the class itself.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text(std::string(kConsole) + "class = { id = bridge_console  of_class = console }\n"
                                             "bridge_console = {\n"
                                             "    id       = helm\n"
                                             "    prop_def = { times_rebooted = string }\n"
                                             "}\n");
    CHECK(loaded.reported(diag::Code::PropDefTypeMismatch));
}

TEST_CASE("the reference corpus declares object-local properties", "[schema][localprop]") {
    // tour.star carries §8.7's own example -- `reactor_console` with its
    // `times_rebooted` and `diagnostic_code` -- and the two values written
    // against them. Until F11 those keys resolved to nothing and their values
    // were never checked; this is the assertion that the corpus exercises the
    // feature rather than merely containing the syntax.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    const std::size_t before = loaded.sink.diagnostics().size();

    loaded.load_text("thing = {\n"
                     "    id       = reactor_console\n"
                     "    prop_def = {\n"
                     "        times_rebooted  = int\n"
                     "        diagnostic_code = string\n"
                     "    }\n"
                     "    times_rebooted  = 0\n"
                     "    diagnostic_code = \"E-114\"\n"
                     "}\n");
    CHECK(loaded.sink.diagnostics().size() == before);

    // The same object with the two values swapped is now caught.
    test::LoadedSet wrong;
    wrong.load_builtin();
    wrong.load_stdlib();
    wrong.load_text("thing = {\n"
                    "    id       = reactor_console\n"
                    "    prop_def = {\n"
                    "        times_rebooted  = int\n"
                    "        diagnostic_code = string\n"
                    "    }\n"
                    "    times_rebooted  = \"E-114\"\n"
                    "    diagnostic_code = 0\n"
                    "}\n");
    CHECK(wrong.reported(diag::Code::TypeMismatch));
}
