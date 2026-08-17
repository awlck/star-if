// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog F4: type checking, spec §6.2.
//
// §6.2 is a table of type names against what each accepts, and most of what
// follows is that table read back. The parts worth reading are the three
// that are not:
//
// The BARE-ENUM SHORTHAND of §4.2, because it means a type name resolves
// against the registry rather than against a fixed list, and a builtin has to
// win so that declaring an enum called `text` cannot change what `text`
// means anywhere else.
//
// The DECLARED-TYPE check, which asks whether a type expression means
// anything at all rather than whether a value fits it. Adding it found five
// keys in the built-in set and stdlib whose types nothing declared -- keys
// that had looked checked and were not.
//
// And the SUB-GRAMMARS, where `clock_time` deliberately stops at the shape.
// §11.6 resolves a time against the sector's calendar, and a sector may
// declare a `local_clock`, so whether hour 30 exists is not this pass's
// question to answer.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "stardata/diag/codes.hpp"
#include "stardata/schema/loader.hpp"
#include "stardata/schema/types.hpp"

#include "support/schema_harness.hpp"

using namespace stardata;

namespace {

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

// A form with one key of a given type, and one value written against it.
// Loaded on top of the real core-owned set, so `enum<E>` and `ref<C>` resolve
// against the same registry an author's would.
class Typed {
public:
    Typed(const std::string& type, const std::string& value) {
        loaded.load_builtin();
        loaded.load_stdlib();
        before_ = loaded.sink.diagnostics().size();
        loaded.load_text("schema = {\n"
                         "    id        = probe\n"
                         "    top_level = yes\n"
                         "    key = { name = id     type = identifier  required = yes }\n"
                         "    key = { name = field  type = " +
                         type +
                         " }\n"
                         "}\n"
                         "probe = { id = one  field = " +
                         value + " }\n");
        schema::check_declared_types(loaded.set, loaded.sink);
    }

    // Whether the value was accepted: no diagnostic at all after the stack
    // itself finished loading.
    [[nodiscard]] bool accepted() const { return loaded.sink.diagnostics().size() == before_; }

    [[nodiscard]] bool rejected_as(diag::Code code) const {
        for (std::size_t i = before_; i < loaded.sink.diagnostics().size(); ++i) {
            if (loaded.sink.diagnostics()[i].code() == code) {
                return true;
            }
        }
        return false;
    }

    test::LoadedSet loaded;

private:
    std::size_t before_ = 0;
};

[[nodiscard]] bool accepts(const std::string& type, const std::string& value) {
    return Typed(type, value).accepted();
}

[[nodiscard]] bool rejects(const std::string& type, const std::string& value) {
    return Typed(type, value).rejected_as(diag::Code::TypeMismatch);
}

} // namespace

// --- §6.2's table ------------------------------------------------------

TEST_CASE("the scalar types accept what spec 6.2 says they accept", "[schema][types]") {
    CHECK(accepts("bool", "yes"));
    CHECK(accepts("bool", "no"));
    CHECK(accepts("int", "42"));
    CHECK(accepts("int", "-1"));
    CHECK(accepts("text", "\"a string\""));
    CHECK(accepts("text", "$a_loc_key"));
    CHECK(accepts("string", "\"raw\""));
    CHECK(accepts("identifier", "some_name"));
    CHECK(accepts("script", "on_examine"));
    CHECK(accepts("resource", "\"art/lantern.png\""));
    CHECK(accepts("text_or_script", "\"a message\""));
    CHECK(accepts("text_or_script", "$a_loc_key"));
    CHECK(accepts("text_or_script", "describe_lantern"));

    // §6.2: `decimal` and `float` both accept an Integer, which is the one
    // widening in the table -- writing `1` where a decimal is wanted is not
    // an author being loose, it is the ordinary way to write a round number.
    CHECK(accepts("decimal", "1.500"));
    CHECK(accepts("decimal", "2"));
    CHECK(accepts("float", "0.250"));
    CHECK(accepts("float", "3"));
}

TEST_CASE("the scalar types reject what they do not accept", "[schema][types]") {
    CHECK(rejects("bool", "3"));
    CHECK(rejects("bool", "\"yes\""));
    CHECK(rejects("int", "1.500"));
    CHECK(rejects("int", "\"42\""));
    CHECK(rejects("text", "bare_identifier"));
    CHECK(rejects("string", "$a_loc_key"));
    CHECK(rejects("identifier", "\"quoted\""));
    CHECK(rejects("int", "{ 1 2 3 }"));
}

TEST_CASE("a duration is a number of ticks, or the word 'default'", "[schema][types]") {
    // §6.2: "Integer, or `default`". The word is an identifier, which is why
    // the check is not simply "an integer" -- and why `default` written as a
    // string is not it.
    CHECK(accepts("duration", "60"));
    CHECK(accepts("duration", "default"));
    CHECK(rejects("duration", "\"default\""));
    CHECK(rejects("duration", "quickly"));
}

TEST_CASE("a ref accepts an identifier and 'none'", "[schema][types]") {
    // Resolving the identifier to an actual object is backlog F9. What is
    // checkable here is the spelling, and that the class named by the type
    // exists -- which the declared-type check covers.
    CHECK(accepts("ref<starcore.object>", "the_lantern"));
    CHECK(accepts("ref<starcore.object>", "none"));
    CHECK(rejects("ref<starcore.object>", "\"the_lantern\""));
}

// --- collections -------------------------------------------------------

TEST_CASE("a list takes a list block, and checks each entry", "[schema][types]") {
    CHECK(accepts("list<identifier>", "{ lantern lamp }"));
    CHECK(accepts("list<identifier>", "{ }"));
    CHECK(rejects("list<identifier>", "lantern"));
    CHECK(rejects("list<int>", "{ 1 two 3 }"));

    // The entry is named in the message, because "this is not an int" is not
    // much use when three of them are on one line.
    const Typed probe("list<int>", "{ 1 two 3 }");
    const std::vector<const diag::Diagnostic*> reported =
        all_of(probe.loaded, diag::Code::TypeMismatch);
    REQUIRE(reported.size() == 1);
    CHECK(mentions(*reported[0], "an entry of 'field'"));
}

TEST_CASE("a set rejects duplicates where a list permits them", "[schema][types]") {
    // §6.2's one behavioural difference between the two. Collapsing the
    // duplicate silently would make `{ a b a }` mean something the author
    // cannot see in what they wrote.
    CHECK(accepts("list<identifier>", "{ a b a }"));
    CHECK(rejects("set<identifier>", "{ a b a }"));
    CHECK(accepts("set<identifier>", "{ a b c }"));
}

TEST_CASE("a map takes a record block and checks its values", "[schema][types]") {
    CHECK(accepts("map<identifier, int>", "{ north = 1  south = 2 }"));
    CHECK(rejects("map<identifier, int>", "{ north south }"));
    CHECK(rejects("map<identifier, int>", "{ north = one }"));
}

TEST_CASE("a map keyed by an enum checks its keys", "[schema][types]") {
    // §14.3 names this case directly: "a map key in a path that is not a
    // valid member of the key's type -- `exits.nrth` for `map<direction, ...>`".
    // Unchecked, it is a door that silently is not there.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    const std::size_t before = loaded.sink.diagnostics().size();
    loaded.load_text("room = { id = your_cell  exits = { north = corridor  nrth = closet } }\n");

    const std::vector<const diag::Diagnostic*> reported = all_of(loaded, diag::Code::TypeMismatch);
    REQUIRE(reported.size() == 1);
    CHECK(mentions(*reported[0], "'nrth'"));
    CHECK(mentions(*reported[0], "direction"));
    CHECK(loaded.sink.diagnostics().size() == before + 1);
}

TEST_CASE("flags are a list over an enum's values", "[schema][types]") {
    CHECK(accepts("flags<relation_enum>", "{ in on }"));
    CHECK(rejects("flags<relation_enum>", "{ in sideways }"));
    CHECK(rejects("flags<relation_enum>", "in"));
}

// --- the shorthand (spec §4.2) -----------------------------------------

TEST_CASE("a bare enum name is shorthand for enum<that>", "[schema][types]") {
    // §4.2: "in a type position, a bare identifier naming a declared `enum`
    // is shorthand for `enum<that>`". Both spellings have to behave
    // identically or the shorthand is a trap rather than a convenience.
    CHECK(accepts("relation_enum", "in"));
    CHECK(accepts("enum<relation_enum>", "in"));
    CHECK(rejects("relation_enum", "sideways"));
    CHECK(rejects("enum<relation_enum>", "sideways"));
}

TEST_CASE("a builtin type name wins over an enum of the same name", "[schema][types]") {
    // The shorthand resolves against the registry, so this is the question it
    // raises: could a library declaring `enum = { id = text ... }` change
    // what `text` means for everybody? It must not, and the resolution order
    // is what stops it.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("enum = { id = text  values = { a b } }\n");

    ast::TypeRef bare;
    bare.name = "text";
    const ast::TypeRef resolved = schema::resolve_type(bare, loaded.set);
    CHECK(resolved.name == "text");
    CHECK(resolved.args.empty());
}

// --- declared types ----------------------------------------------------

TEST_CASE("the built-in set and stdlib declare only types that exist", "[schema][types][builtin]") {
    // The check that pays for itself. When it was written it found five keys
    // whose declared type nothing declared -- `enum<advances_turn_enum>`,
    // three `block<...>` shapes named by the manifests, and stdlib's `exits`
    // -- every one of them a key that looked checked and was not.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    const std::size_t before = loaded.sink.diagnostics().size();

    schema::check_declared_types(loaded.set, loaded.sink);

    for (std::size_t i = before; i < loaded.sink.diagnostics().size(); ++i) {
        INFO("unresolved type: " << loaded.sink.diagnostics()[i].message());
        CHECK(false);
    }
    CHECK(loaded.sink.diagnostics().size() == before);
}

TEST_CASE("a type nobody declares is reported at the schema, once", "[schema][types]") {
    // Once, at the key that declared it -- not at every value written against
    // it. An author who mistyped one type name should not have to read a
    // hundred reports of the consequence to find the cause.
    test::LoadedSet loaded;
    loaded.load_builtin();
    const std::size_t before = loaded.sink.diagnostics().size();
    loaded.load_text("schema = {\n"
                     "    id        = journal_entry\n"
                     "    top_level = yes\n"
                     "    key = { name = id    type = identifier  required = yes }\n"
                     "    key = { name = mood  type = enum<mood_enum> }\n"
                     "}\n"
                     "journal_entry = { id = day_one   mood = bleak }\n"
                     "journal_entry = { id = day_two   mood = worse }\n");
    schema::check_declared_types(loaded.set, loaded.sink);

    const std::vector<const diag::Diagnostic*> reported = all_of(loaded, diag::Code::SchemaInvalid);
    REQUIRE(reported.size() == 1);
    CHECK(mentions(*reported[0], "mood_enum"));
    CHECK(all_of(loaded, diag::Code::TypeMismatch).empty());
    CHECK(loaded.sink.diagnostics().size() == before + 1);
}

TEST_CASE("a type expression with the wrong number of arguments is reported", "[schema][types]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("schema = {\n"
                     "    id        = probe\n"
                     "    top_level = yes\n"
                     "    key = { name = pairs  type = map<identifier> }\n"
                     "}\n");
    schema::check_declared_types(loaded.set, loaded.sink);

    const std::vector<const diag::Diagnostic*> reported = all_of(loaded, diag::Code::SchemaInvalid);
    REQUIRE_FALSE(reported.empty());
    CHECK(mentions(*reported[0], "2 type arguments"));
}

TEST_CASE("a class property's type is checked too", "[schema][types]") {
    // Properties are declared with the same type expressions as keys, and
    // §8.8 will read them at those types. A property typed by a name nobody
    // declares is the same hole one level down.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_text("class = { id = beacon  of_class = starcore.object\n"
                     "          prop_def = { hue = enum<no_such_enum> } }\n");
    schema::check_declared_types(loaded.set, loaded.sink);

    const std::vector<const diag::Diagnostic*> reported = all_of(loaded, diag::Code::SchemaInvalid);
    REQUIRE_FALSE(reported.empty());
    CHECK(mentions(*reported[0], "no_such_enum"));
    CHECK(mentions(*reported[0], "'hue' on 'beacon'"));
}

// --- instantiations (spec §7.4) ----------------------------------------

TEST_CASE("an instantiation is checked against its class's property types", "[schema][types]") {
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    const std::size_t before = loaded.sink.diagnostics().size();
    loaded.load_text("room = { id = your_cell  dark = maybe }\n");

    const std::vector<const diag::Diagnostic*> reported = all_of(loaded, diag::Code::TypeMismatch);
    REQUIRE(reported.size() == 1);
    CHECK(mentions(*reported[0], "'dark'"));
    CHECK(loaded.sink.diagnostics().size() == before + 1);
}

TEST_CASE("an inherited property is found through the class chain", "[schema][types]") {
    // `name` is declared on `starcore.object`, two links above stdlib's
    // `room`. A check that only looked at the class's own properties would
    // miss every property an author is most likely to write.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    loaded.load_text("room = { id = your_cell  name = your_cell }\n");

    const std::vector<const diag::Diagnostic*> reported = all_of(loaded, diag::Code::TypeMismatch);
    REQUIRE(reported.size() == 1);
    CHECK(mentions(*reported[0], "'name'"));
    CHECK(mentions(*reported[0], "text"));
}

TEST_CASE("a key naming no property is left for F11", "[schema][types]") {
    // §7.4 lists the keys permitted inside an instantiation, and enforcing
    // that needs the object-local `prop_def` of backlog F11. Guessing here --
    // reporting every unrecognised key as unknown -- would be wrong for every
    // object that declares its own property.
    test::LoadedSet loaded;
    loaded.load_builtin();
    loaded.load_stdlib();
    const std::size_t before = loaded.sink.diagnostics().size();
    loaded.load_text("room = { id = your_cell  peculiar_local_thing = 3 }\n");

    CHECK(loaded.sink.diagnostics().size() == before);
}

// --- the sub-grammars --------------------------------------------------

TEST_CASE("dice notation is parsed at compile time", "[schema][types]") {
    CHECK(schema::is_dice("3d6"));
    CHECK(schema::is_dice("d20"));
    CHECK(schema::is_dice("3d6+2"));
    CHECK(schema::is_dice("1d100-10"));

    CHECK_FALSE(schema::is_dice(""));
    CHECK_FALSE(schema::is_dice("3d")); // a die with no faces
    CHECK_FALSE(schema::is_dice("d"));
    CHECK_FALSE(schema::is_dice("3"));    // a number is not a roll
    CHECK_FALSE(schema::is_dice("3d6+")); // a modifier with no amount
    CHECK_FALSE(schema::is_dice("3d6+2x"));
    CHECK_FALSE(schema::is_dice("3 d 6")); // whitespace is significant inside a string
}

TEST_CASE("a clock_time is checked for shape and not for range", "[schema][types]") {
    CHECK(schema::is_clock_time("09:30"));
    CHECK(schema::is_clock_time("09:30:15"));

    CHECK_FALSE(schema::is_clock_time("9:30"));   // two digits per field
    CHECK_FALSE(schema::is_clock_time("09:30:")); // ...including the last
    CHECK_FALSE(schema::is_clock_time("0930"));
    CHECK_FALSE(schema::is_clock_time(""));

    // Deliberately accepted. §6.2 resolves a clock_time "against the sector's
    // calendar" and §11.6 lets a sector declare a `local_clock`, so a
    // thirty-hour day is a thing an author may legitimately have. Rejecting
    // hour 30 here would leave them no way to say what they meant, which is a
    // worse failure than not checking it.
    CHECK(schema::is_clock_time("30:00"));
    CHECK(schema::is_clock_time("99:99:99"));
}
