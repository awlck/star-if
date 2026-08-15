// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog E7: editing. Replace, insert and delete, each returning a new tree
// that shares every untouched subtree with the old one -- which is what
// §14.2's "an edit to one node MUST re-print only the affected span" means
// in practice.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "stardata/cst/edit.hpp"
#include "stardata/cst/writer.hpp"

#include "support/cst_harness.hpp"

using namespace stardata::cst;
using stardata::test::Parsed;

namespace {

std::vector<SyntaxNode> statements_of(const SyntaxNode& node) {
    std::vector<SyntaxNode> result;
    for (const SyntaxNode& child : node.child_nodes()) {
        if (child.kind() == SyntaxKind::Statement) {
            result.push_back(child);
        }
    }
    return result;
}

} // namespace

TEST_CASE("replacing a token changes that token and nothing else", "[cst][edit]") {
    const std::string source = "room = {\n    id = cell   # unchanged\n    name = \"Cell\"\n}\n";
    Parsed parsed{source};

    // Find the `cell` identifier.
    const auto target =
        parsed.root().token_at_offset(static_cast<std::uint32_t>(source.find("cell")));
    REQUIRE(target);
    REQUIRE(target->text() == "cell");

    const GreenNodePtr edited = replace_token(
        *target, parsed.cache().token(SyntaxKind::Identifier, "holding_cell"), parsed.cache());
    REQUIRE(edited);

    const std::string after = to_text(*edited);
    CHECK(after == "room = {\n    id = holding_cell   # unchanged\n    name = \"Cell\"\n}\n");

    // Everything before and after the edit is byte-identical.
    const std::size_t at = source.find("cell");
    CHECK(after.substr(0, at) == source.substr(0, at));
    CHECK(after.substr(after.size() - 30) == source.substr(source.size() - 30));

    // The original tree is untouched -- green nodes are immutable, so the
    // caller who still holds it sees nothing.
    CHECK(parsed.written() == source);
}

TEST_CASE("an edit shares every subtree it did not touch", "[cst][edit]") {
    // The property that makes editing a large file cheap, and the reason the
    // green tree has no parent pointers.
    Parsed parsed{std::string("a = { x = 1 }\nb = { y = 2 }\nc = { z = 3 }\n")};
    const std::vector<SyntaxNode> before = statements_of(parsed.root());
    REQUIRE(before.size() == 3);

    const auto target = before[0].first_child_of_kind(SyntaxKind::Key);
    REQUIRE(target);
    const auto token = target->first_token_of_kind(SyntaxKind::Identifier);
    REQUIRE(token);

    const GreenNodePtr edited =
        replace_token(*token, parsed.cache().token(SyntaxKind::Identifier, "aa"), parsed.cache());
    REQUIRE(edited);

    const std::vector<SyntaxNode> after = statements_of(SyntaxNode::root(edited));
    REQUIRE(after.size() == 3);

    // The two statements away from the edit are the very same objects.
    CHECK(after[1].green().get() == before[1].green().get());
    CHECK(after[2].green().get() == before[2].green().get());
    // The edited one is not.
    CHECK(after[0].green().get() != before[0].green().get());
}

TEST_CASE("deleting a statement takes its comment and its line with it", "[cst][edit]") {
    // The payoff of E3's attachment policy: because a statement owns its
    // leading comment and its trailing newline, deleting it leaves no
    // orphaned comment and no blank line, and every other byte is untouched.
    const std::string source = "room = {\n"
                               "    id = cell\n"
                               "    # the way out\n"
                               "    exits = { north = hall }\n"
                               "    name = \"Cell\"\n"
                               "}\n";
    Parsed parsed{source};

    const auto block = parsed.root()
                           .first_child_of_kind(SyntaxKind::Statement)
                           ->first_child_of_kind(SyntaxKind::Value)
                           ->first_child_of_kind(SyntaxKind::Block);
    REQUIRE(block);
    const std::vector<SyntaxNode> inner = statements_of(*block);
    REQUIRE(inner.size() == 3);
    REQUIRE(inner[1].text() == "    # the way out\n    exits = { north = hall }\n");

    const GreenNodePtr edited = remove_statement(inner[1], parsed.cache());
    CHECK(to_text(*edited) == "room = {\n"
                              "    id = cell\n"
                              "    name = \"Cell\"\n"
                              "}\n");
}

TEST_CASE("inserting a statement preserves the trivia around it", "[cst][edit]") {
    const std::string source = "room = {\n    id = cell\n    name = \"Cell\"\n}\n";
    Parsed parsed{source};

    const auto block = parsed.root()
                           .first_child_of_kind(SyntaxKind::Statement)
                           ->first_child_of_kind(SyntaxKind::Value)
                           ->first_child_of_kind(SyntaxKind::Block);
    REQUIRE(block);
    const std::vector<SyntaxNode> inner = statements_of(*block);
    REQUIRE(inner.size() == 2);

    // The new statement carries its own indentation and line ending, exactly
    // as it will appear.
    const auto fragment = statement_from_text("    dark = yes\n", parsed.cache());
    REQUIRE(fragment);

    const GreenNodePtr edited = insert_statement_after(inner[0], *fragment, parsed.cache());
    CHECK(to_text(*edited) == "room = {\n"
                              "    id = cell\n"
                              "    dark = yes\n"
                              "    name = \"Cell\"\n"
                              "}\n");
}

TEST_CASE("statement_from_text refuses anything that is not one statement", "[cst][edit]") {
    GreenCache cache;
    CHECK(statement_from_text("id = cell\n", cache));
    CHECK(statement_from_text("  id = cell  # why\n", cache));

    CHECK_FALSE(statement_from_text("a = 1\nb = 2\n", cache));     // two
    CHECK_FALSE(statement_from_text("# just a comment\n", cache)); // none
    CHECK_FALSE(statement_from_text("= 1\n", cache));              // does not parse
    CHECK_FALSE(statement_from_text("", cache));
}

TEST_CASE("replacing a whole node", "[cst][edit]") {
    Parsed parsed{std::string("exits = { north = hall }\n")};

    const auto value = parsed.root()
                           .first_child_of_kind(SyntaxKind::Statement)
                           ->first_child_of_kind(SyntaxKind::Value);
    REQUIRE(value);
    const auto block = value->first_child_of_kind(SyntaxKind::Block);
    REQUIRE(block);

    // Swap the block for a scalar: `exits = none`.
    std::vector<GreenElement> scalar_children;
    scalar_children.emplace_back(parsed.cache().token(SyntaxKind::Identifier, "none"));
    const GreenNodePtr scalar = parsed.cache().node(SyntaxKind::Scalar, std::move(scalar_children));

    const GreenNodePtr edited = replace_node(*block, scalar, parsed.cache());
    CHECK(to_text(*edited) == "exits = none\n");
}

TEST_CASE("an edited tree re-parses to the same text", "[cst][edit]") {
    // An edit must produce a tree that is still a legal file, or the next
    // save writes something the parser cannot read back.
    Parsed parsed{std::string("a = 1\nb = 2\n")};
    const std::vector<SyntaxNode> statements = statements_of(parsed.root());
    REQUIRE(statements.size() == 2);

    const auto fragment = statement_from_text("c = 3\n", parsed.cache());
    REQUIRE(fragment);
    const GreenNodePtr edited = insert_statement_after(statements[1], *fragment, parsed.cache());
    const std::string text = to_text(*edited);
    CHECK(text == "a = 1\nb = 2\nc = 3\n");

    Parsed reparsed{text};
    CHECK(reparsed.sink().error_count() == 0);
    CHECK(reparsed.written() == text);
}
