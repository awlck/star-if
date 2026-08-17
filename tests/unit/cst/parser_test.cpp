// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog E4: the grammar of spec §4, the `<` disambiguation of §4.2, calls
// of §4.3, block shape from §5.2, and error recovery.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "stardata/cst/syntax.hpp"
#include "stardata/diag/codes.hpp"

#include "support/cst_harness.hpp"

using namespace stardata::cst;
using stardata::diag::Code;
using stardata::test::Parsed;

namespace {

// The kinds of a node's children, trivia excluded -- the shape a grammar
// rule produces, without the whitespace that makes every expectation noisy.
std::vector<SyntaxKind> shape_of(const SyntaxNode& node) {
    std::vector<SyntaxKind> result;
    for (const SyntaxElement& child : node.children()) {
        if (auto token = child.as_token(); token && token->is_trivia()) {
            continue;
        }
        result.push_back(child.kind());
    }
    return result;
}

SyntaxNode only_statement(const Parsed& parsed) {
    const auto statement = parsed.root().first_child_of_kind(SyntaxKind::Statement);
    REQUIRE(statement);
    return *statement;
}

SyntaxNode value_of(const Parsed& parsed) {
    const auto value = only_statement(parsed).first_child_of_kind(SyntaxKind::Value);
    REQUIRE(value);
    return *value;
}

} // namespace

TEST_CASE("a statement is a key, an operator and a value", "[cst][parser]") {
    Parsed parsed{std::string("id = brass_key\n")};
    CHECK(shape_of(only_statement(parsed)) ==
          std::vector{SyntaxKind::Key, SyntaxKind::Operator, SyntaxKind::Value});
    CHECK(parsed.sink().error_count() == 0);
}

TEST_CASE("a key may be a string", "[cst][parser]") {
    // Spec §4: permitted so that keys needing characters outside the
    // identifier set can be expressed.
    Parsed parsed{std::string("\"key.with-unusual characters\" = 1\n")};
    const auto key = only_statement(parsed).first_child_of_kind(SyntaxKind::Key);
    REQUIRE(key);
    CHECK(key->first_token_of_kind(SyntaxKind::String));
    CHECK(parsed.sink().error_count() == 0);
}

TEST_CASE("every operator of spec 3.6 is accepted in operator position", "[cst][parser]") {
    for (std::string_view op : {"=", "==", "!=", "<=", ">=", "+=", "-=", "<", ">"}) {
        INFO("operator: " << op);
        Parsed parsed{"strength " + std::string(op) + " 14\n"};
        CHECK(shape_of(only_statement(parsed)) ==
              std::vector{SyntaxKind::Key, SyntaxKind::Operator, SyntaxKind::Value});
        CHECK(parsed.sink().error_count() == 0);
    }
}

TEST_CASE("spec 4.2: the same '<' is a comparison or a type argument by position",
          "[cst][parser]") {
    SECTION("operator position is a comparison") {
        Parsed parsed{std::string("strength < 14\n")};
        const SyntaxNode statement = only_statement(parsed);
        CHECK(shape_of(statement) ==
              std::vector{SyntaxKind::Key, SyntaxKind::Operator, SyntaxKind::Value});
        // The value is a plain scalar, not a type expression.
        CHECK(shape_of(value_of(parsed)) == std::vector{SyntaxKind::Scalar});
    }

    SECTION("value position opens a type argument list") {
        Parsed parsed{std::string("type = list<int>\n")};
        CHECK(shape_of(value_of(parsed)) == std::vector{SyntaxKind::TypeExpr});
    }

    SECTION("nested type arguments") {
        Parsed parsed{std::string("type = map<direction, ref<room>>\n")};
        const SyntaxNode value = value_of(parsed);
        REQUIRE(shape_of(value) == std::vector{SyntaxKind::TypeExpr});
        const auto outer = value.first_child_of_kind(SyntaxKind::TypeExpr);
        REQUIRE(outer);
        CHECK(outer->first_child_of_kind(SyntaxKind::TypeExpr)); // ref<room>
        CHECK(outer->text() == "map<direction, ref<room>>");
        CHECK(parsed.sink().error_count() == 0);
    }

    SECTION("whitespace decides nothing") {
        // §4.2 forbids depending on it, and §3.1 makes it insignificant, so
        // these three must produce the same shape.
        for (std::string_view source :
             {"type = list<int>\n", "type = list < int >\n", "type=list<int>\n"}) {
            INFO("source: " << source);
            Parsed parsed{std::string(source)};
            CHECK(shape_of(value_of(parsed)) == std::vector{SyntaxKind::TypeExpr});
            CHECK(parsed.sink().error_count() == 0);
        }
    }
}

TEST_CASE("spec 4.3: a call in value position", "[cst][parser]") {
    Parsed parsed{std::string("second = HolderOf(noun)\n")};
    CHECK(shape_of(value_of(parsed)) == std::vector{SyntaxKind::Call});

    const auto call = value_of(parsed).first_child_of_kind(SyntaxKind::Call);
    REQUIRE(call);
    CHECK(call->text() == "HolderOf(noun)");
    CHECK(parsed.sink().error_count() == 0);
}

TEST_CASE("a call may take a call, and may take nothing", "[cst][parser]") {
    Parsed nested{std::string("x = A(B(c), 1)\n")};
    CHECK(nested.sink().error_count() == 0);
    const auto outer = value_of(nested).first_child_of_kind(SyntaxKind::Call);
    REQUIRE(outer);
    CHECK(outer->first_child_of_kind(SyntaxKind::Call));

    Parsed empty{std::string("x = Now()\n")};
    CHECK(empty.sink().error_count() == 0);
    CHECK(shape_of(value_of(empty)) == std::vector{SyntaxKind::Call});
}

TEST_CASE("spec 3.8: an annotation, with and without arguments", "[cst][parser]") {
    SECTION("bare") {
        Parsed parsed{std::string("effects = @debug { }\n")};
        CHECK(shape_of(value_of(parsed)) == std::vector{SyntaxKind::Annotation, SyntaxKind::Block});
        CHECK(parsed.sink().error_count() == 0);
    }

    SECTION("with arguments") {
        Parsed parsed{std::string("conditions = @platform(glk, cli) { }\n")};
        const auto annotation = value_of(parsed).first_child_of_kind(SyntaxKind::Annotation);
        REQUIRE(annotation);
        CHECK(annotation->text() == "@platform(glk, cli)");
        CHECK(parsed.sink().error_count() == 0);
    }

    SECTION("several on one value") {
        Parsed parsed{std::string("msg = @before @priority(3) \"text\"\n")};
        CHECK(shape_of(value_of(parsed)) ==
              std::vector{SyntaxKind::Annotation, SyntaxKind::Annotation, SyntaxKind::Scalar});
        CHECK(parsed.sink().error_count() == 0);
    }
}

TEST_CASE("spec 5.2: a record block holds statements", "[cst][parser]") {
    Parsed parsed{std::string("exits = { north = corridor  south = hall }\n")};
    const auto block = value_of(parsed).first_child_of_kind(SyntaxKind::Block);
    REQUIRE(block);
    CHECK(shape_of(*block) == std::vector{SyntaxKind::Punctuation, SyntaxKind::Statement,
                                          SyntaxKind::Statement, SyntaxKind::Punctuation});
    CHECK(parsed.sink().error_count() == 0);
}

TEST_CASE("spec 5.2: a list block holds bare scalars", "[cst][parser]") {
    Parsed parsed{std::string("values = { breathable toxic vacuum }\n")};
    const auto block = value_of(parsed).first_child_of_kind(SyntaxKind::Block);
    REQUIRE(block);
    CHECK(shape_of(*block) == std::vector{SyntaxKind::Punctuation, SyntaxKind::Scalar,
                                          SyntaxKind::Scalar, SyntaxKind::Scalar,
                                          SyntaxKind::Punctuation});
    CHECK(parsed.sink().error_count() == 0);
}

TEST_CASE("spec 5.2: mixing the two shapes is an error naming the minority", "[cst][parser]") {
    Parsed parsed{std::string("traits = { openable  lockable = yes }\n")};

    REQUIRE(parsed.reported(Code::BlockMixed));
    const auto& diagnostics = parsed.sink().diagnostics();
    REQUIRE(diagnostics.size() == 1);
    // The bare value is the minority, so it is what gets pointed at, and the
    // note cites the statement it collided with -- both spans, since the
    // usual cause is one missing '='.
    CHECK(parsed.sources().text(diagnostics[0].primary_span()) == "openable");
    REQUIRE_FALSE(diagnostics[0].notes().empty());
    CHECK(diagnostics[0].notes()[0].span.has_value());
    REQUIRE_FALSE(diagnostics[0].fix_its().empty());
    CHECK(diagnostics[0].fix_its()[0].replacement == " = ");
}

TEST_CASE("an empty block is legal", "[cst][parser]") {
    // Spec §18.7 of the corpus: "an empty file is legal; so is an empty
    // block".
    Parsed parsed{std::string("effects = { }\n")};
    CHECK(parsed.sink().error_count() == 0);
    const auto block = value_of(parsed).first_child_of_kind(SyntaxKind::Block);
    REQUIRE(block);
    CHECK(block->text() == "{ }");
}

TEST_CASE("an empty file is legal", "[cst][parser]") {
    Parsed parsed{std::string("")};
    CHECK(parsed.sink().error_count() == 0);
    CHECK(parsed.root().kind() == SyntaxKind::File);
    CHECK(parsed.root().text_length() == 0);
}

TEST_CASE("spec 3.5.1: adjacent literals become one Scalar holding both", "[cst][parser]") {
    // The split points survive, which is what lets E6 reproduce the author's
    // line breaks rather than joining the text onto one line.
    const std::string source = "description = \"The reactor is scorched. \"\n"
                               "              \"Something went wrong here.\"\n";
    Parsed parsed{source};

    const auto scalar = value_of(parsed).first_child_of_kind(SyntaxKind::Scalar);
    REQUIRE(scalar);
    std::size_t strings = 0;
    for (const SyntaxToken& token : scalar->child_tokens()) {
        strings += token.kind() == SyntaxKind::String ? 1 : 0;
    }
    CHECK(strings == 2);
    CHECK(parsed.written() == source);
    CHECK(parsed.sink().error_count() == 0);
}

TEST_CASE("recovery: a malformed block still yields a tree, and parsing continues",
          "[cst][parser]") {
    const std::string source = "before = 1\n"
                               "broken = { %% }\n"
                               "after = 2\n";
    Parsed parsed{source};

    CHECK(parsed.sink().has_errors());
    // Both good statements are intact.
    const std::vector<SyntaxNode> statements = parsed.root().child_nodes();
    std::vector<std::string> keys;
    for (const SyntaxNode& statement : statements) {
        if (statement.kind() != SyntaxKind::Statement) {
            continue;
        }
        if (auto key = statement.first_child_of_kind(SyntaxKind::Key)) {
            keys.push_back(key->text());
        }
    }
    CHECK(keys == std::vector<std::string>{"before", "broken", "after"});
    // And the tree still covers every byte.
    CHECK(parsed.written() == source);
}

TEST_CASE("recovery: an unclosed block is reported and closed at the end", "[cst][parser]") {
    const std::string source = "room = {\n    id = cell\n";
    Parsed parsed{source};

    REQUIRE(parsed.reported(Code::BraceUnbalanced));
    const auto& diagnostics = parsed.sink().diagnostics();
    REQUIRE_FALSE(diagnostics.empty());
    REQUIRE_FALSE(diagnostics[0].fix_its().empty());
    CHECK(diagnostics[0].fix_its()[0].replacement == "}");
    CHECK(parsed.written() == source);
}

TEST_CASE("recovery: a stray closing brace at file level", "[cst][parser]") {
    const std::string source = "a = 1\n}\nb = 2\n";
    Parsed parsed{source};

    CHECK(parsed.reported(Code::BraceUnbalanced));
    CHECK(parsed.written() == source);
}

TEST_CASE("recovery: an operator with no value", "[cst][parser]") {
    Parsed parsed{std::string("id =\n")};
    CHECK(parsed.reported(Code::ValueMissing));
    CHECK(parsed.written() == "id =\n");
}

TEST_CASE("recovery: a token where the grammar allows none", "[cst][parser]") {
    const std::string source = "= 1\n";
    Parsed parsed{source};
    CHECK(parsed.reported(Code::StrayToken));
    CHECK(parsed.written() == source);
}

TEST_CASE("the tree covers every byte, however broken the input", "[cst][parser]") {
    // Backlog E4's last bullet, stated directly.
    for (std::string_view source : {"", "}", "= = =", "a = { { {", "@@@", "a = \"unclosed", "[ ]",
                                    "a = ) ( ,", "\xEF\xBB\xBF}", "%%%\n\n%%%"}) {
        INFO("source: " << source);
        Parsed parsed{std::string(source)};
        CHECK(parsed.written() == source);
        CHECK(parsed.root().text_length() == source.size());
    }
}
