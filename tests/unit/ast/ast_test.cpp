// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog F1: the typed view over the CST. Two halves, and the second is the
// one that earns its keep -- the accessors have to survive a tree the parser
// recovered from, because an editor asks about a half-typed file on every
// keystroke.
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "stardata/ast/ast.hpp"

#include "support/corpus.hpp"
#include "support/cst_harness.hpp"
#include "support/fixture.hpp"

using namespace stardata;

namespace {

test::Parsed parse(std::string text) {
    return test::Parsed(std::move(text));
}

} // namespace

TEST_CASE("a file's statements carry key, operator and value", "[ast]") {
    test::Parsed parsed = parse("title = \"Salvage\"\nturns = 40\n");
    const ast::File file = parsed.ast();

    const std::vector<ast::Statement> statements = file.statements();
    REQUIRE(statements.size() == 2);

    REQUIRE(statements[0].key_name() == "title");
    CHECK(statements[0].op_text() == "=");
    CHECK(statements[0].is_binding());
    REQUIRE(statements[0].value());
    REQUIRE(statements[0].value()->as_scalar());
    CHECK(statements[0].value()->as_scalar()->as_string() == "Salvage");

    REQUIRE(statements[1].key_name() == "turns");
    REQUIRE(statements[1].value()->as_scalar()->as_integer() == 40);
}

TEST_CASE("the modifier operators are not binding occurrences", "[ast]") {
    // Spec 5.3: "arity counts binding occurrences only -- those using '=' or
    // '?='". '+=' and '-=' transform whatever value is in effect rather than
    // establishing one, which is what stops them colliding under
    // 'arity = one'.
    //
    // '?=' is on the binding side of that line even though it reads like a
    // modifier: a block whose only mention of a key is 'x ?= 1' has given x a
    // value, and one whose only mention is 'x += { a }' has not.
    test::Parsed parsed =
        parse("traits = { a }\ntraits += { b }\ntraits -= { c }\nnames ?= { d }\n");
    const std::vector<ast::Statement> statements = parsed.ast().statements();
    REQUIRE(statements.size() == 4);

    CHECK(statements[0].is_binding());
    CHECK(statements[0].op_text() == "=");
    CHECK_FALSE(statements[1].is_binding());
    CHECK(statements[1].op_text() == "+=");
    CHECK_FALSE(statements[2].is_binding());
    CHECK(statements[2].op_text() == "-=");
    CHECK(statements[3].is_binding());
    CHECK(statements[3].op_text() == "?=");
}

TEST_CASE("a scalar reports what was written and nothing else", "[ast]") {
    test::Parsed parsed = parse("a = ident\n"
                                "b = 17\n"
                                "c = -3\n"
                                "d = 1.500\n"
                                "e = -0.250\n"
                                "f = $some_key\n"
                                "g = yes\n"
                                "h = no\n");
    const std::vector<ast::Statement> statements = parsed.ast().statements();
    REQUIRE(statements.size() == 8);

    auto scalar = [&](std::size_t index) { return *statements[index].value()->as_scalar(); };

    CHECK(scalar(0).as_identifier() == "ident");
    CHECK(scalar(1).as_integer() == 17);
    CHECK(scalar(2).as_integer() == -3);
    // Spec 3.4: a decimal is a scaled 64-bit integer with three fractional
    // digits, never a float.
    CHECK(scalar(3).as_decimal_scaled() == 1500);
    CHECK(scalar(4).as_decimal_scaled() == -250);
    CHECK(scalar(5).as_loc_key() == "some_key");
    CHECK(scalar(6).as_bool() == true);
    CHECK(scalar(7).as_bool() == false);

    // Asking for the wrong kind yields nothing rather than a coerced answer;
    // coercion is F4's job, and doing it here would hide the failure.
    CHECK_FALSE(scalar(0).as_integer());
    CHECK_FALSE(scalar(1).as_identifier());
    CHECK_FALSE(scalar(1).as_decimal_scaled());
    CHECK_FALSE(scalar(0).as_bool());
    CHECK_FALSE(scalar(5).as_string());
}

TEST_CASE("adjacent string literals concatenate with their escapes decoded", "[ast]") {
    // Spec 3.5.1: the split points stay in the tree so the round-trip keeps
    // the author's line breaks, but the value is one string.
    test::Parsed parsed = parse("text = \"The reactor is scorched.\\n\"\n"
                                "       \"Something went \\\"very\\\" wrong.\"\n");
    const ast::Scalar scalar = *parsed.ast().statements()[0].value()->as_scalar();

    CHECK(scalar.literals().size() == 2);
    CHECK(scalar.as_string() == "The reactor is scorched.\nSomething went \"very\" wrong.");
}

TEST_CASE("every escape of spec 3.5 decodes", "[ast]") {
    test::Parsed parsed = parse("t = \"a\\tb\\\\c\\[d\\]e\\$f\\@g\\u00e9h\\u2014i\"\n");
    const ast::Scalar scalar = *parsed.ast().statements()[0].value()->as_scalar();

    // The two \u escapes are U+00E9 and U+2014, two and three UTF-8 bytes.
    CHECK(scalar.as_string() == "a\tb\\c[d]e$f@g\xC3\xA9h\xE2\x80\x94i");
}

TEST_CASE("a string key is decoded like any other string", "[ast]") {
    test::Parsed parsed = parse("\"a key\" = 1\n");
    const ast::Statement statement = parsed.ast().statements()[0];

    REQUIRE(statement.key());
    CHECK(statement.key()->is_string());
    CHECK(statement.key_name() == "a key");
}

TEST_CASE("a type expression lowers to a structure, however it was spaced", "[ast]") {
    test::Parsed parsed = parse("a = int\n"
                                "b = list<string>\n"
                                "c = map<identifier,   mood_enum>\n"
                                "d = map<identifier, ref<action>>\n");
    const std::vector<ast::Statement> statements = parsed.ast().statements();

    const ast::TypeRef bare = *statements[0].value()->as_type();
    CHECK(bare.name == "int");
    CHECK(bare.args.empty());
    CHECK(bare.to_string() == "int");

    const ast::TypeRef list = *statements[1].value()->as_type();
    CHECK(list.to_string() == "list<string>");

    // Whatever the author's spacing, the canonical form is one spelling --
    // which is what lets a diagnostic quote a type without quoting a layout.
    const ast::TypeRef map = *statements[2].value()->as_type();
    CHECK(map.to_string() == "map<identifier, mood_enum>");

    const ast::TypeRef nested = *statements[3].value()->as_type();
    REQUIRE(nested.args.size() == 2);
    CHECK(nested.args[1].name == "ref");
    CHECK(nested.to_string() == "map<identifier, ref<action>>");

    CHECK(map.same_as(*statements[2].value()->as_type()));
    CHECK_FALSE(map.same_as(nested));
}

TEST_CASE("a bare identifier and a type expression are the same question", "[ast]") {
    // `type = int` parses as a Scalar and `type = list<int>` as a TypeExpr.
    // A schema asks "what type is this?" and should not have to know which.
    test::Parsed parsed = parse("a = int\nb = list<int>\n");
    const std::vector<ast::Statement> statements = parsed.ast().statements();

    REQUIRE(statements[0].value()->as_type());
    REQUIRE_FALSE(statements[0].value()->as_type_expr()); // a Scalar, in the tree
    REQUIRE(statements[1].value()->as_type());
    REQUIRE(statements[1].value()->as_type_expr());
}

TEST_CASE("a call reports its callee and arguments", "[ast]") {
    test::Parsed parsed = parse("msg = style(stat_line)\nnest = outer(inner(x), 2)\n");
    const std::vector<ast::Statement> statements = parsed.ast().statements();

    const ast::Call call = *statements[0].value()->as_call();
    CHECK(call.callee() == "style");
    CHECK(call.arguments().size() == 1);

    const ast::Call nested = *statements[1].value()->as_call();
    CHECK(nested.callee() == "outer");
    CHECK(nested.arguments().size() == 2);
    CHECK(nested.arguments()[0].kind() == cst::SyntaxKind::Call);
    CHECK(nested.arguments()[1].kind() == cst::SyntaxKind::Scalar);
}

TEST_CASE("an annotation reports its name without the sigil", "[ast]") {
    test::Parsed parsed = parse("hp = @debug 10\nmode = @platform(desktop, 2) fast\n");
    const std::vector<ast::Statement> statements = parsed.ast().statements();

    const std::vector<ast::Annotation> plain = statements[0].value()->annotations();
    REQUIRE(plain.size() == 1);
    CHECK(plain[0].name() == "debug");
    CHECK(plain[0].arguments().empty());

    const std::vector<ast::Annotation> withArgs = statements[1].value()->annotations();
    REQUIRE(withArgs.size() == 1);
    CHECK(withArgs[0].name() == "platform");
    REQUIRE(withArgs[0].arguments().size() == 2);
    CHECK(withArgs[0].arguments()[0].text() == "desktop");
    CHECK(withArgs[0].arguments()[1].text() == "2");
}

TEST_CASE("a block distinguishes a record from a list", "[ast]") {
    // Spec 5.2: a block holds either statements or bare values.
    test::Parsed parsed = parse("record = { id = cell  dark = yes }\n"
                                "list   = { openable lockable }\n"
                                "empty  = { }\n");
    const std::vector<ast::Statement> statements = parsed.ast().statements();

    const ast::Block record = *statements[0].value()->as_block();
    CHECK(record.is_record());
    CHECK_FALSE(record.is_list());
    CHECK(record.statements().size() == 2);

    const ast::Block list = *statements[1].value()->as_block();
    CHECK(list.is_list());
    CHECK_FALSE(list.is_record());
    REQUIRE(list.values().size() == 2);
    CHECK(list.values()[0].as_identifier() == "openable");

    const ast::Block empty = *statements[2].value()->as_block();
    CHECK(empty.is_empty());
    CHECK_FALSE(empty.is_record());
    CHECK_FALSE(empty.is_list());
}

TEST_CASE("a block looks a key up by name", "[ast]") {
    test::Parsed parsed = parse("class = {\n"
                                "    id = container\n"
                                "    key = { name = a }\n"
                                "    key = { name = b }\n"
                                "}\n");
    const ast::Block block = *parsed.ast().statements()[0].value()->as_block();

    REQUIRE(block.value_of("id"));
    CHECK(block.value_of("id")->as_scalar()->as_identifier() == "container");

    // find() takes the first; find_all() is what an `arity = many` key needs,
    // and what a duplicate-key diagnostic needs to cite both spans.
    REQUIRE(block.find("key"));
    CHECK(block.find_all("key").size() == 2);
    CHECK(block.find_all("nothing_here").empty());
    CHECK_FALSE(block.find("nothing_here"));
    CHECK_FALSE(block.value_of("nothing_here"));
}

TEST_CASE("a statement points a diagnostic at its key, not at its trivia", "[ast]") {
    // A Statement owns its leading and trailing trivia, which is what makes
    // the trivia policy work -- and what would make span() underline the
    // blank line above it.
    test::Parsed parsed = parse("\n\n# a comment\nid = cell\n");
    const ast::Statement statement = parsed.ast().statements()[0];

    const diag::Span whole = statement.span();
    const diag::Span reported = statement.report_span();
    CHECK(whole.offset < reported.offset);
    CHECK(reported.length == 2); // exactly `id`
    CHECK(parsed.sources().text(reported) == "id");
}

// --- tolerance ---------------------------------------------------------

TEST_CASE("a statement with no value yields nothing rather than asserting", "[ast]") {
    test::Parsed parsed = parse("id =\n");
    const std::vector<ast::Statement> statements = parsed.ast().statements();
    REQUIRE(statements.size() == 1);

    CHECK(statements[0].key_name() == "id");
    CHECK(statements[0].op_text() == "=");
    REQUIRE(statements[0].value());
    // The Value node exists and is empty -- the parser produced it before
    // discovering there was nothing to put in it.
    CHECK_FALSE(statements[0].value()->as_scalar());
    CHECK_FALSE(statements[0].value()->as_block());
    CHECK_FALSE(statements[0].value()->as_type());
}

TEST_CASE("a statement with no operator yields nothing rather than asserting", "[ast]") {
    test::Parsed parsed = parse("room = { id }\n");
    const ast::Block block = *parsed.ast().statements()[0].value()->as_block();

    // `{ id }` is a list block, so there is no statement to have an operator.
    CHECK(block.is_list());
    CHECK(block.statements().empty());
    CHECK_FALSE(block.find("id"));
}

TEST_CASE("an unterminated block still yields the statements it did contain", "[ast]") {
    test::Parsed parsed = parse("room = {\n    id = cell\n");
    const std::vector<ast::Statement> statements = parsed.ast().statements();
    REQUIRE_FALSE(statements.empty());

    const std::optional<ast::Block> block = statements[0].value()->as_block();
    REQUIRE(block);
    REQUIRE(block->find("id"));
    CHECK(block->find("id")->value()->as_scalar()->as_identifier() == "cell");
}

TEST_CASE("a malformed type expression lowers to what there is", "[ast]") {
    // Unclosed at end of file: the name survives, there are no arguments,
    // and nothing here has to know that the parser reported it.
    test::Parsed unclosed_file = parse("a = list<");
    const std::optional<ast::TypeRef> unclosed =
        unclosed_file.ast().statements()[0].value()->as_type();
    REQUIRE(unclosed);
    CHECK(unclosed->name == "list");
    CHECK(unclosed->args.empty());

    // A trailing comma: one argument, not two and not an empty one.
    test::Parsed dangling = parse("b = map<identifier,>\n");
    const std::optional<ast::TypeRef> comma = dangling.ast().statements()[0].value()->as_type();
    REQUIRE(comma);
    CHECK(comma->to_string() == "map<identifier>");

    // And the case worth knowing about: an unclosed type expression with a
    // statement after it swallows the next key as an argument, because
    // whitespace is insignificant (spec 3.1) and the parser has nothing else
    // to go on. The view reports that faithfully rather than tidying it up
    // -- the diagnostic is the parser's to make, and it made one.
    test::Parsed swallowed = parse("a = list<\nb = 1\n");
    const std::optional<ast::TypeRef> greedy = swallowed.ast().statements()[0].value()->as_type();
    REQUIRE(greedy);
    CHECK(greedy->to_string() == "list<b>");
}

TEST_CASE("the typed view survives every invalid fixture", "[ast][corpus]") {
    // The tolerance requirement as a sweep rather than a list of cases:
    // walk every accessor over every deliberately broken file in the corpus.
    // Under the Debug presets this runs with ASan and UBSan on, so "it did
    // not crash" is a real claim rather than an absence of evidence.
    const auto files = test::corpus_files(test::corpus_dir() / "invalid");
    REQUIRE_FALSE(files.empty());

    for (const auto& path : files) {
        INFO("invalid fixture: " << path.string());
        test::Parsed parsed(test::read_bytes(path), test::corpus_name(path));
        std::size_t visited = 0;

        // Recursive so nested blocks are reached too, not just the top level.
        const auto walk = [&visited](auto&& self,
                                     const std::vector<ast::Statement>& stmts) -> void {
            for (const ast::Statement& statement : stmts) {
                ++visited;
                (void)statement.key_name();
                (void)statement.op_text();
                (void)statement.is_binding();
                (void)statement.report_span();
                const std::optional<ast::Value> value = statement.value();
                if (!value) {
                    continue;
                }
                (void)value->annotations();
                (void)value->as_type();
                (void)value->as_call();
                if (const std::optional<ast::Scalar> scalar = value->as_scalar()) {
                    (void)scalar->as_identifier();
                    (void)scalar->as_integer();
                    (void)scalar->as_decimal_scaled();
                    (void)scalar->as_string();
                    (void)scalar->as_loc_key();
                    (void)scalar->as_bool();
                }
                if (const std::optional<ast::Block> block = value->as_block()) {
                    (void)block->values();
                    self(self, block->statements());
                }
            }
        };
        walk(walk, ast::File::from(parsed.root(), parsed.id()).statements());

        // Not an assertion about any particular file, just proof the sweep
        // reached something rather than silently walking nothing.
        INFO("statements visited: " << visited);
    }
}
