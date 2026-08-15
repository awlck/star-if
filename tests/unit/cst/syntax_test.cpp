// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog E2: the red tree. Parent and absolute offset, computed on the way
// down rather than stored, plus the navigation an editor needs.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "stardata/cst/syntax.hpp"

#include "support/cst_harness.hpp"

using namespace stardata::cst;
using stardata::test::Parsed;

TEST_CASE("a cursor computes absolute offsets from the root down", "[cst][syntax]") {
    Parsed parsed{std::string("id = brass_key\n")};
    const SyntaxNode root = parsed.root();

    CHECK(root.kind() == SyntaxKind::File);
    CHECK(root.text_range() == TextRange{0, 15});

    const auto statement = root.first_child_of_kind(SyntaxKind::Statement);
    REQUIRE(statement);
    CHECK(statement->text_range() == TextRange{0, 15});

    const auto key = statement->first_child_of_kind(SyntaxKind::Key);
    REQUIRE(key);
    CHECK(key->text_range() == TextRange{0, 2});
    CHECK(key->text() == "id");

    const auto value = statement->first_child_of_kind(SyntaxKind::Value);
    REQUIRE(value);
    // The Value begins at its own first token: the space after the operator
    // belongs to the Statement, not to the Value (see parser.cpp's note on
    // node ranges).
    CHECK(value->text() == "brass_key");
    CHECK(value->text_range() == TextRange{5, 9});
}

TEST_CASE("parent, siblings and ancestors", "[cst][syntax]") {
    Parsed parsed{std::string("room = { id = cell }\n")};
    const SyntaxNode root = parsed.root();

    const auto statement = root.first_child_of_kind(SyntaxKind::Statement);
    REQUIRE(statement);
    const auto value = statement->first_child_of_kind(SyntaxKind::Value);
    REQUIRE(value);
    const auto block = value->first_child_of_kind(SyntaxKind::Block);
    REQUIRE(block);
    const auto inner = block->first_child_of_kind(SyntaxKind::Statement);
    REQUIRE(inner);

    // Up.
    REQUIRE(inner->parent());
    CHECK(inner->parent()->kind() == SyntaxKind::Block);
    CHECK_FALSE(root.parent());

    const std::vector<SyntaxNode> ancestors = inner->ancestors();
    REQUIRE(ancestors.size() == 4);
    CHECK(ancestors[0].kind() == SyntaxKind::Block);
    CHECK(ancestors[1].kind() == SyntaxKind::Value);
    CHECK(ancestors[2].kind() == SyntaxKind::Statement);
    CHECK(ancestors[3].kind() == SyntaxKind::File);

    // Sideways: the Key of the inner statement is followed by whitespace,
    // then the operator.
    const auto key = inner->first_child_of_kind(SyntaxKind::Key);
    REQUIRE(key);
    const auto after = key->next_sibling();
    REQUIRE(after);
    CHECK(after->kind() == SyntaxKind::Whitespace);
    // The Key is NOT first inside its statement: the statement owns its
    // leading trivia (backlog E3), so the indentation comes before the key.
    const auto before = key->prev_sibling();
    REQUIRE(before);
    CHECK(before->kind() == SyntaxKind::Whitespace);
}

TEST_CASE("descendants come back in document order", "[cst][syntax]") {
    Parsed parsed{std::string("a = { b = { c = 1 } }\n")};
    const std::vector<SyntaxNode> all = parsed.root().descendants();

    REQUIRE_FALSE(all.empty());
    CHECK(all.front().kind() == SyntaxKind::File);
    for (std::size_t index = 1; index < all.size(); ++index) {
        // Document order: each node starts at or after the previous one.
        CHECK(all[index].text_range().offset >= all[index - 1].text_range().offset);
    }
    // Three nested blocks' worth of statements.
    std::size_t statements = 0;
    for (const SyntaxNode& node : all) {
        statements += node.kind() == SyntaxKind::Statement ? 1 : 0;
    }
    CHECK(statements == 3);
}

TEST_CASE("text() reconstructs the source a node covers", "[cst][syntax]") {
    // Backlog E2's third bullet, and the property E5's writer is built on.
    const std::string source = "room = {\n    id = cell   # here\n}\n";
    Parsed parsed{source};

    CHECK(parsed.root().text() == source);
    for (const SyntaxNode& node : parsed.root().descendants()) {
        const TextRange range = node.text_range();
        INFO("node " << to_string(node.kind()) << " @" << range.offset << ".." << range.end());
        CHECK(node.text() == source.substr(range.offset, range.length));
    }
}

TEST_CASE("tokens() walks every leaf in order, trivia included", "[cst][syntax]") {
    const std::string source = "a = 1\n";
    Parsed parsed{source};

    std::string rebuilt;
    std::uint32_t expected_offset = 0;
    for (const SyntaxToken& token : parsed.root().tokens()) {
        CHECK(token.text_range().offset == expected_offset);
        expected_offset = token.text_range().end();
        rebuilt += token.text();
    }
    CHECK(rebuilt == source);
}

TEST_CASE("token_at_offset finds the leaf covering a byte", "[cst][syntax]") {
    // What an editor asks when the caret moves.
    Parsed parsed{std::string("id = brass_key\n")};
    const SyntaxNode root = parsed.root();

    const auto at_key = root.token_at_offset(0);
    REQUIRE(at_key);
    CHECK(at_key->text() == "id");

    const auto at_value = root.token_at_offset(7);
    REQUIRE(at_value);
    CHECK(at_value->text() == "brass_key");

    // Past the end of the file there is nothing.
    CHECK_FALSE(root.token_at_offset(999));
}

TEST_CASE("next_token and prev_token cross node boundaries", "[cst][syntax]") {
    Parsed parsed{std::string("a = { b = 1 }\n")};
    const SyntaxNode root = parsed.root();

    std::vector<std::string> forwards;
    for (auto token = root.token_at_offset(0); token; token = token->next_token()) {
        forwards.emplace_back(token->text());
    }
    CHECK(forwards == std::vector<std::string>{"a", " ", "=", " ", "{", " ", "b", " ", "=", " ",
                                               "1", " ", "}", "\n"});

    // And back again.
    std::vector<std::string> backwards;
    auto last = root.token_at_offset(root.text_length() - 1);
    for (auto token = last; token; token = token->prev_token()) {
        backwards.emplace_back(token->text());
    }
    std::reverse(backwards.begin(), backwards.end());
    CHECK(backwards == forwards);
}

TEST_CASE("two cursors onto the same place are equal", "[cst][syntax]") {
    Parsed parsed{std::string("a = 1\n")};
    const auto first = parsed.root().first_child_of_kind(SyntaxKind::Statement);
    const auto second = parsed.root().first_child_of_kind(SyntaxKind::Statement);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(*first == *second);
}
