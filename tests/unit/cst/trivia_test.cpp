// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog E3: the trivia attachment policy. These tests are the policy
// stated as behaviour -- the prose lives in CONTRIBUTING.md and at the top
// of libs/stardata/src/cst/parser.cpp, and if the two ever disagree, this
// file is what says which one is true.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "stardata/cst/syntax.hpp"

#include "support/cst_harness.hpp"

using namespace stardata::cst;
using stardata::test::Parsed;

namespace {

// The text of every trivia leaf directly inside `node`, in order. Direct
// children only: the question these tests ask is always "which node did this
// comment attach to", and a nested statement's trivia is not this node's.
std::vector<std::string> own_trivia(const SyntaxNode& node) {
    std::vector<std::string> result;
    for (const SyntaxToken& token : node.child_tokens()) {
        if (token.is_trivia()) {
            result.emplace_back(token.text());
        }
    }
    return result;
}

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

TEST_CASE("rule 1: trailing trivia runs to and including the newline", "[cst][trivia]") {
    Parsed parsed{std::string("id = cell   # why this one\nnext = 1\n")};
    const std::vector<SyntaxNode> statements = statements_of(parsed.root());
    REQUIRE(statements.size() == 2);

    // The end-of-line comment stays with the statement it follows, and so
    // does the newline that ends the line.
    const std::vector<std::string> trailing = own_trivia(statements[0]);
    REQUIRE(trailing.size() >= 3);
    CHECK(trailing[trailing.size() - 3] == "   ");
    CHECK(trailing[trailing.size() - 2] == "# why this one");
    CHECK(trailing.back() == "\n");
}

TEST_CASE("rule 2: a comment on its own line attaches to the statement below", "[cst][trivia]") {
    // The requirement in backlog E3's second bullet, and the reason the
    // policy exists: moving the statement must move the comment.
    Parsed parsed{std::string("first = 1\n# describes the second\nsecond = 2\n")};
    const std::vector<SyntaxNode> statements = statements_of(parsed.root());
    REQUIRE(statements.size() == 2);

    const std::vector<std::string> leading = own_trivia(statements[1]);
    REQUIRE_FALSE(leading.empty());
    CHECK(leading[0] == "# describes the second");

    // Stated as the property that matters: the statement's own text carries
    // its comment, so anything that moves the node moves the comment.
    CHECK(statements[1].text() == "# describes the second\nsecond = 2\n");
    CHECK(statements[0].text() == "first = 1\n");
}

TEST_CASE("rule 3: a blank line detaches trivia from the statement below", "[cst][trivia]") {
    // Without this, a file's header banner would attach to whatever
    // statement happened to be first, and moving that statement would take
    // the banner with it.
    Parsed parsed{std::string("# file banner\n# second line\n\nfirst = 1\n")};
    const SyntaxNode root = parsed.root();

    const std::vector<std::string> detached = own_trivia(root);
    REQUIRE(detached.size() >= 3);
    CHECK(detached[0] == "# file banner");
    CHECK(detached[2] == "# second line");

    const std::vector<SyntaxNode> statements = statements_of(root);
    REQUIRE(statements.size() == 1);
    CHECK(statements[0].text() == "first = 1\n");
}

TEST_CASE("a comment above a statement, with a banner above that", "[cst][trivia]") {
    // Both rules at once, which is the arrangement almost every real file
    // has: the banner stays put, the description travels.
    Parsed parsed{std::string("# banner\n\n# describes room\nroom = { id = a }\n")};
    const SyntaxNode root = parsed.root();

    CHECK(own_trivia(root)[0] == "# banner");

    const std::vector<SyntaxNode> statements = statements_of(root);
    REQUIRE(statements.size() == 1);
    CHECK(statements[0].text() == "# describes room\nroom = { id = a }\n");
}

TEST_CASE("the policy applies inside a block too", "[cst][trivia]") {
    Parsed parsed{std::string("room = {\n"
                              "    id = cell\n"
                              "\n"
                              "    # the way out\n"
                              "    exits = { north = hall }\n"
                              "}\n")};

    const auto statement = parsed.root().first_child_of_kind(SyntaxKind::Statement);
    REQUIRE(statement);
    const auto value = statement->first_child_of_kind(SyntaxKind::Value);
    REQUIRE(value);
    const auto block = value->first_child_of_kind(SyntaxKind::Block);
    REQUIRE(block);

    const std::vector<SyntaxNode> inner = statements_of(*block);
    REQUIRE(inner.size() == 2);
    CHECK(inner[0].text() == "    id = cell\n");
    // The blank line detached to the Block; the comment travelled with the
    // statement it describes.
    CHECK(inner[1].text() == "    # the way out\n    exits = { north = hall }\n");
}

TEST_CASE("a byte-order mark stays at the top of the file", "[cst][trivia]") {
    Parsed parsed{std::string("\xEF\xBB\xBF"
                              "a = 1\n")};
    const SyntaxNode root = parsed.root();
    const std::vector<SyntaxToken> leaves = root.tokens();
    REQUIRE_FALSE(leaves.empty());
    CHECK(leaves.front().kind() == SyntaxKind::ByteOrderMark);
    CHECK(parsed.written() == parsed.source());
}

TEST_CASE("trivia is split, never dropped, when a rule cuts through a run", "[cst][trivia]") {
    // The trailing/leading boundary falls inside one whitespace run here
    // ("\n\n" splits after the first newline), so the run becomes two leaves.
    // Losing a byte at the split is the failure mode this guards.
    const std::string source = "a = 1\n\nb = 2\n";
    Parsed parsed{source};
    CHECK(parsed.written() == source);

    std::string rebuilt;
    for (const SyntaxToken& token : parsed.root().tokens()) {
        rebuilt += token.text();
    }
    CHECK(rebuilt == source);
}

TEST_CASE("the worked example in CONTRIBUTING.md behaves as it claims", "[cst][trivia]") {
    // The documented example, verbatim. Prose that describes behaviour drifts
    // from it unless something checks; this is the something. If you change
    // the block in CONTRIBUTING.md, change it here too -- and if the two
    // disagree, this is the one that is true.
    const std::string source =
        "# A file banner.                 <- rule 3: a blank line follows, so this\n"
        "# Two lines of it.                  comment stays with the file\n"
        "\n"
        "# What this room is.             <- rule 2: moves with `room`\n"
        "room = {\n"
        "    id = cell     # the id       <- rule 1: moves with `id`\n"
        "\n"
        "    # The way out.               <- rule 3 detached the blank line above to\n"
        "    #                               the block; rule 2 then moved this\n"
        "    #                               comment along with `exits`\n"
        "    exits = { north = hall }\n"
        "}\n";
    Parsed parsed{source};

    // It is a conforming file, as the surrounding prose claims.
    CHECK(parsed.sink().error_count() == 0);
    CHECK(parsed.written() == source);

    // Rule 3: the banner stayed with the file.
    const std::vector<std::string> detached = own_trivia(parsed.root());
    REQUIRE_FALSE(detached.empty());
    CHECK(detached[0].rfind("# A file banner.", 0) == 0);

    // Rule 2: the description moved inside the statement it describes.
    const std::vector<SyntaxNode> statements = statements_of(parsed.root());
    REQUIRE(statements.size() == 1);
    CHECK(statements[0].text().rfind("# What this room is.", 0) == 0);

    const auto block = statements[0]
                           .first_child_of_kind(SyntaxKind::Value)
                           ->first_child_of_kind(SyntaxKind::Block);
    REQUIRE(block);
    const std::vector<SyntaxNode> inner = statements_of(*block);
    REQUIRE(inner.size() == 2);

    // Rule 1: the end-of-line comment stayed with `id`.
    CHECK(inner[0].text().find("# the id") != std::string::npos);
    // Rule 2 again, inside a block: the three comment lines moved with
    // `exits`, and the blank line before them did not.
    CHECK(inner[1].text().rfind("    # The way out.", 0) == 0);
    CHECK(inner[1].text().find("exits = { north = hall }") != std::string::npos);
}
