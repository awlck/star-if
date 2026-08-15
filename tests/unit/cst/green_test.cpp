// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog E1: the green tree. Immutable, parent-free, position-free,
// interned, and safe to share between threads.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "stardata/cst/builder.hpp"
#include "stardata/cst/green.hpp"

#include "support/cst_harness.hpp"

using namespace stardata::cst;

TEST_CASE("a green token carries its kind and its exact text", "[cst][green]") {
    GreenCache cache;
    const GreenTokenPtr token = cache.token(SyntaxKind::Identifier, "brass_key");

    CHECK(token->kind() == SyntaxKind::Identifier);
    CHECK(token->text() == "brass_key");
    CHECK(token->text_length() == 9);
    CHECK_FALSE(token->is_trivia());
    CHECK(cache.token(SyntaxKind::Whitespace, "  ")->is_trivia());
}

TEST_CASE("a green node's length is the sum of its children", "[cst][green]") {
    GreenCache cache;
    std::vector<GreenElement> children;
    children.emplace_back(cache.token(SyntaxKind::Identifier, "id"));
    children.emplace_back(cache.token(SyntaxKind::Whitespace, " "));
    children.emplace_back(cache.token(SyntaxKind::Operator, "="));

    const GreenNodePtr node = cache.node(SyntaxKind::Statement, std::move(children));
    CHECK(node->text_length() == 4);
    CHECK(node->text() == "id =");
    CHECK(node->children().size() == 3);
}

TEST_CASE("identical tokens are one object", "[cst][green]") {
    // Interning, backlog E1. A real file is mostly a small vocabulary
    // repeated thousands of times, so this is where the storage goes.
    GreenCache cache;
    const GreenTokenPtr first = cache.token(SyntaxKind::Operator, "=");
    const GreenTokenPtr second = cache.token(SyntaxKind::Operator, "=");

    CHECK(first.get() == second.get());
    CHECK(cache.token_count() == 1);

    // Kind is part of the identity: the same text under a different kind is
    // a different token.
    const GreenTokenPtr other = cache.token(SyntaxKind::Punctuation, "=");
    CHECK(other.get() != first.get());
    CHECK(cache.token_count() == 2);
}

TEST_CASE("identical small subtrees are one object", "[cst][green]") {
    GreenCache cache;
    const auto build = [&cache] {
        std::vector<GreenElement> children;
        children.emplace_back(cache.token(SyntaxKind::Identifier, "yes"));
        return cache.node(SyntaxKind::Scalar, std::move(children));
    };

    CHECK(build().get() == build().get());
    CHECK(cache.node_count() == 1);
}

TEST_CASE("a wide node is not interned, on purpose", "[cst][green]") {
    // rowan's heuristic, and the reason for it: a node with many children is
    // unique by construction, so hashing it on every lookup would cost more
    // than the sharing could ever save.
    GreenCache cache;
    const auto build = [&cache] {
        std::vector<GreenElement> children;
        for (int n = 0; n <= static_cast<int>(GreenCache::kMaxInternedChildren); ++n) {
            children.emplace_back(cache.token(SyntaxKind::Identifier, "a"));
        }
        return cache.node(SyntaxKind::Block, std::move(children));
    };

    CHECK(build().get() != build().get());
    CHECK(cache.node_count() == 0);
}

TEST_CASE("interning does real work on a real file", "[cst][green]") {
    // Asserted rather than assumed: if the cache stopped sharing, this is
    // what would notice.
    stardata::test::Parsed parsed{std::string(
        "room = { id = a }\nroom = { id = b }\nroom = { id = c }\nroom = { id = d }\n")};

    std::size_t leaves = 0;
    for (const auto& node : parsed.root().descendants()) {
        leaves += node.child_tokens().size();
    }
    // Four near-identical statements share almost every leaf between them.
    CHECK(parsed.cache().token_count() < leaves);
}

TEST_CASE("a green subtree has no parent and no position", "[cst][green]") {
    // The two omissions that make sharing possible. There is nothing to
    // assert on a missing member, so this asserts the consequence: one
    // subtree object sits at two different offsets in the same tree.
    stardata::test::Parsed parsed{std::string("a = yes\nb = yes\n")};

    const std::vector<SyntaxNode> statements = parsed.root().child_nodes();
    REQUIRE(statements.size() == 2);

    const auto value_of = [](const SyntaxNode& statement) {
        return statement.first_child_of_kind(SyntaxKind::Value)
            ->first_child_of_kind(SyntaxKind::Scalar);
    };
    const auto first = value_of(statements[0]);
    const auto second = value_of(statements[1]);
    REQUIRE(first);
    REQUIRE(second);

    // The same green object...
    CHECK(first->green().get() == second->green().get());
    // ...seen at two different absolute offsets.
    CHECK(first->text_range().offset != second->text_range().offset);
}

TEST_CASE("a tree is safe to read from several threads at once", "[cst][green]") {
    // Backlog E1's last bullet. shared_ptr's refcount is atomic and the tree
    // is immutable, so this needs no locking; the test exists so that a
    // future change introducing mutable state fails under the sanitisers
    // rather than in production.
    stardata::test::Parsed parsed{std::string("room = { id = cell  exits = { north = hall } }\n")};
    const GreenNodePtr green = parsed.green();

    std::atomic<std::size_t> total{0};
    std::vector<std::thread> threads;
    threads.reserve(4);
    for (int n = 0; n < 4; ++n) {
        threads.emplace_back([&green, &total] {
            for (int pass = 0; pass < 50; ++pass) {
                const SyntaxNode root = SyntaxNode::root(green);
                total += root.descendants().size() + root.text().size();
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    CHECK(total > 0);
}

TEST_CASE("the builder wraps retroactively at a checkpoint", "[cst][green]") {
    // What lets the parser classify a Value only after reading its first
    // token, which the grammar of §4.2 and §4.3 requires.
    GreenCache cache;
    GreenBuilder builder(cache);

    builder.start_node(SyntaxKind::Value);
    const std::size_t checkpoint = builder.checkpoint();
    builder.token(SyntaxKind::Identifier, "list");
    builder.start_node_at(checkpoint, SyntaxKind::TypeExpr);
    builder.token(SyntaxKind::Angle, "<");
    builder.token(SyntaxKind::Identifier, "int");
    builder.token(SyntaxKind::Angle, ">");
    builder.finish_node(); // TypeExpr
    builder.finish_node(); // Value

    const GreenNodePtr root = builder.finish();
    REQUIRE(root->kind() == SyntaxKind::Value);
    REQUIRE(root->children().size() == 1);
    CHECK(root->children()[0].kind() == SyntaxKind::TypeExpr);
    CHECK(root->text() == "list<int>");
}
