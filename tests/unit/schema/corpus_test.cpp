// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog F2a's last bullet: a fixture per assertion in
// tests/corpus/invalid/, asserted from the fixtures' own `# EXPECT` lines so
// the two cannot drift apart.
//
// The schema layer's fixtures differ from the lexer's in one way worth
// stating: a fixture that redefines a sealed core form only means anything
// once the core form exists, so each is loaded ON TOP OF the built-in set,
// as a library would be. That is also the situation being tested -- these
// are all cases of a library doing something only core may do.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <set>
#include <string>

#include "stardata/diag/codes.hpp"
#include "stardata/schema/loader.hpp"

#include "support/corpus.hpp"
#include "support/fixture.hpp"
#include "support/schema_harness.hpp"

using namespace stardata;

namespace {

// The codes this workstream is responsible for. A fixture may declare codes
// belonging to a workstream that does not exist yet -- W-FAILMSG-MISSING is
// F8's, E-FLAG-NOT-BOOL is F10's -- and those are not the schema layer's to
// report today.
const std::set<std::string>& schema_codes() {
    static const std::set<std::string> codes = {
        "E-SCHEMA-INVALID", "E-SCHEMA-DUPLICATE", "E-SCHEMA-SEALED",         "E-KEY-MISSING",
        "E-CORE-REPARENT",  "E-CORE-REQUIREMENT", "E-PROPDEF-TYPE-MISMATCH", "E-UNKNOWN-KEY"};
    return codes;
}

// Loads the built-in set, then one fixture as a library, then checks the
// requirements -- the full load sequence, which is what a fixture declaring
// E-CORE-REQUIREMENT needs in order to fail.
std::set<std::string> codes_for(const std::filesystem::path& path) {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text(test::read_bytes(path), "a library", test::corpus_name(path));
    schema::check_requirements(loaded.set, loaded.sink);

    std::set<std::string> reported;
    for (const diag::Diagnostic& diagnostic : loaded.sink.diagnostics()) {
        reported.emplace(diag::code_string(diagnostic.code()));
    }
    return reported;
}

} // namespace

TEST_CASE("each invalid fixture reports the schema-layer codes it declares", "[schema][corpus]") {
    const auto files = test::corpus_files(test::corpus_dir() / "invalid");
    REQUIRE_FALSE(files.empty());

    for (const auto& path : files) {
        const std::set<std::string> expected = test::expected_codes(test::read_bytes(path));
        bool relevant = false;
        for (const std::string& code : expected) {
            relevant = relevant || schema_codes().contains(code);
        }
        if (!relevant) {
            continue; // another workstream's fixture
        }

        INFO("invalid fixture: " << path.string());
        const std::set<std::string> reported = codes_for(path);
        for (const std::string& code : expected) {
            if (!schema_codes().contains(code)) {
                continue;
            }
            INFO("expected code: " << code);
            CHECK(reported.contains(code));
        }
    }
}

TEST_CASE("every schema-layer code has a fixture that provokes it", "[schema][corpus]") {
    // The other direction, and the one that actually stops the gap
    // reopening: adding a code without a fixture fails here.
    std::set<std::string> covered;
    for (const auto& path : test::corpus_files(test::corpus_dir() / "invalid")) {
        for (const std::string& code : test::expected_codes(test::read_bytes(path))) {
            covered.insert(code);
        }
    }

    for (const std::string& code : schema_codes()) {
        INFO("schema-layer code without a fixture: " << code);
        CHECK(covered.contains(code));
    }
}

TEST_CASE("stdlib core uses only mechanisms available to any library", "[schema][stdlib]") {
    // Spec §7.2.4's last paragraph: "Everything else in stdlib/core --
    // thing, person, container, supporter, door, backdrop, every action,
    // every message -- is ordinary Stardata with no privileged status, and
    // could be replaced wholesale by a different library."
    //
    // That claim is only worth making if it is checked, and it is exactly
    // the claim ADRIFT 5 and Inform 7 make and do not keep. So: load the
    // built-in set, load stdlib/core as an ordinary library on top of it,
    // and require that nothing in it needed a privilege to load.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib_core();

    for (const diag::Diagnostic& diagnostic : loaded.sink.diagnostics()) {
        INFO("unexpected " << diag::code_string(diagnostic.code()) << ": " << diagnostic.message());
        CHECK(false);
    }
    REQUIRE(loaded.sink.error_count() == 0);

    // Nothing it declared claims to be core. The owner is assigned by the
    // loader and never read from the file, so a library cannot write its way
    // into this -- but a declaration moved from one directory to the other
    // by mistake would show up here.
    std::size_t from_library = 0;
    for (const schema::Schema& declared : loaded.set.schemas()) {
        if (declared.owner != "stdlib/core") {
            continue;
        }
        ++from_library;
        INFO("form declared by stdlib/core: " << declared.id);
        CHECK_FALSE(declared.sealed);
    }

    for (const schema::ClassDecl& declared : loaded.set.classes()) {
        if (declared.owner != "stdlib/core") {
            continue;
        }
        ++from_library;
        INFO("class or trait declared by stdlib/core: " << declared.id);
        // Sealing is core's mechanism for data core reads and writes itself.
        // A library sealing its own declarations is not forbidden by §7.2.2,
        // but stdlib/core doing it would blur exactly the boundary this test
        // exists to keep sharp.
        CHECK_FALSE(declared.sealed);
        CHECK(declared.id.rfind("starcore.", 0) != 0);
    }

    // And it did declare something, so the checks above are not vacuous.
    CHECK(from_library >= 10);

    // The classes an author actually writes are the library's, and they do
    // reach the core root -- `room` derives from `starcore.room`, which is
    // the §7.2.2 boundary in one declaration.
    const schema::ClassDecl* room = loaded.set.find_class("room");
    REQUIRE(room != nullptr);
    CHECK(room->owner == "stdlib/core");
    CHECK(room->of_class == "starcore.room");
}
