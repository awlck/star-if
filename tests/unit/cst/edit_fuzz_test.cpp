// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog E8: round-trip fuzzing. Proposal §16.1 names round-trip
// degradation as a high risk -- the failure where a tree survives one edit
// intact and loses a comment on the fifth -- and this is the mitigation.
//
// Deterministic and in-process, for the same reasons as the lexer's fuzzer:
// it has to run inside the ordinary `ctest` invocation on all three
// platforms and under the Debug sanitisers. A fixed seed makes any failure
// reproducible from the test name alone.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "stardata/cst/edit.hpp"
#include "stardata/cst/parser.hpp"
#include "stardata/cst/writer.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/diag/source_manager.hpp"

#include "support/corpus.hpp"
#include "support/fixture.hpp"

using namespace stardata;

namespace {

constexpr std::uint32_t kSeed = 0xC57EDDU;

using test::read_bytes;

cst::GreenNodePtr parse_text(const std::string& text, cst::GreenCache& cache,
                             diag::DiagnosticSink& sink) {
    diag::SourceManager sources;
    const diag::SourceId id = sources.add_file("fuzz.star", text);
    return cst::parse(sources, id, cache, sink);
}

// Every node in the tree, so an edit can pick one at random.
std::vector<cst::SyntaxNode> all_nodes(const cst::SyntaxNode& root) {
    return root.descendants();
}

} // namespace

TEST_CASE("a tree survives a run of random edits", "[cst][fuzz]") {
    // Apply N edits in sequence. After each one the tree must still write
    // out, re-parse, and write out to the same bytes -- the property that
    // degrades silently if sharing or trivia attachment is subtly wrong.
    cst::GreenCache cache;
    std::mt19937 engine(kSeed);

    const std::string start = "room = {\n"
                              "    id = cell        # the id\n"
                              "\n"
                              "    # the way out\n"
                              "    exits = { north = hall  south = yard }\n"
                              "    name = \"Holding Cell\"\n"
                              "    dark = yes\n"
                              "}\n"
                              "thing = { id = key  weight = 1.500 }\n";

    diag::DiagnosticSink sink;
    cst::GreenNodePtr tree = parse_text(start, cache, sink);
    REQUIRE_FALSE(sink.has_errors());

    for (int iteration = 0; iteration < 200; ++iteration) {
        INFO("iteration " << iteration);
        const cst::SyntaxNode root = cst::SyntaxNode::root(tree);
        const std::vector<cst::SyntaxNode> nodes = all_nodes(root);
        REQUIRE_FALSE(nodes.empty());

        const cst::SyntaxNode target =
            nodes[std::uniform_int_distribution<std::size_t>(0, nodes.size() - 1)(engine)];

        cst::GreenNodePtr edited = tree;
        switch (std::uniform_int_distribution<int>(0, 2)(engine)) {
        case 0: {
            // Rename an identifier somewhere under the chosen node.
            std::vector<cst::SyntaxToken> names;
            for (const cst::SyntaxToken& token : target.tokens()) {
                if (token.kind() == cst::SyntaxKind::Identifier) {
                    names.push_back(token);
                }
            }
            if (names.empty()) {
                continue;
            }
            const cst::SyntaxToken& pick =
                names[std::uniform_int_distribution<std::size_t>(0, names.size() - 1)(engine)];
            edited = cst::replace_token(
                pick,
                cache.token(cst::SyntaxKind::Identifier, "renamed_" + std::to_string(iteration)),
                cache);
            break;
        }
        case 1: {
            // Delete a statement.
            if (target.kind() != cst::SyntaxKind::Statement || !target.parent()) {
                continue;
            }
            edited = cst::remove_statement(target, cache);
            break;
        }
        default: {
            // Insert a statement after an existing one.
            if (target.kind() != cst::SyntaxKind::Statement || !target.parent()) {
                continue;
            }
            const auto fragment = cst::statement_from_text(
                "    added_" + std::to_string(iteration) + " = 1\n", cache);
            if (!fragment) {
                continue;
            }
            edited = cst::insert_statement_after(target, *fragment, cache);
            break;
        }
        }

        REQUIRE(edited);
        const std::string text = cst::to_text(*edited);

        // The result must re-parse, and re-parsing must reproduce it exactly.
        diag::DiagnosticSink reparse_sink;
        cst::GreenCache reparse_cache;
        const cst::GreenNodePtr reparsed = parse_text(text, reparse_cache, reparse_sink);
        INFO("text after edit:\n" << text);
        REQUIRE(cst::to_text(*reparsed) == text);

        tree = edited;
    }
}

TEST_CASE("an edit leaves the bytes outside it untouched", "[cst][fuzz]") {
    // §14.2's second paragraph as a property rather than an example: for
    // every identifier in the file, renaming it must change exactly its own
    // span and nothing else.
    const std::string source = "a = { x = 1  # one\n"
                               "      y = 2 }\n"
                               "# between\n"
                               "b = { z = 3 }\n";

    cst::GreenCache cache;
    diag::DiagnosticSink sink;
    const cst::GreenNodePtr tree = parse_text(source, cache, sink);
    REQUIRE_FALSE(sink.has_errors());

    const cst::SyntaxNode root = cst::SyntaxNode::root(tree);
    std::size_t checked = 0;
    for (const cst::SyntaxToken& token : root.tokens()) {
        if (token.kind() != cst::SyntaxKind::Identifier) {
            continue;
        }
        const cst::TextRange range = token.text_range();
        const std::string replacement = "Z";
        const cst::GreenNodePtr edited =
            cst::replace_token(token, cache.token(cst::SyntaxKind::Identifier, replacement), cache);
        REQUIRE(edited);
        const std::string after = cst::to_text(*edited);

        INFO("renamed " << token.text() << " at " << range.offset);
        // Everything before the token is identical...
        CHECK(after.substr(0, range.offset) == source.substr(0, range.offset));
        // ...the token itself is the replacement...
        CHECK(after.substr(range.offset, replacement.size()) == replacement);
        // ...and everything after it is identical.
        CHECK(after.substr(range.offset + replacement.size()) == source.substr(range.end()));
        ++checked;
    }
    // a, x, y, b and z -- every identifier in the fixture.
    CHECK(checked == 5);
}

TEST_CASE("random edits over the corpus keep re-parsing", "[cst][fuzz]") {
    // The structure-aware pass of backlog E8: real files rather than a
    // handmade fixture, on a small fixed budget so it stays inside CI's ten
    // minutes.
    std::mt19937 engine(kSeed ^ 0x5A5A5A5AU);

    for (const auto& path : test::corpus_files(std::filesystem::path(STARIF_CORPUS_DIR))) {
        INFO("corpus file: " << path.string());
        const std::string source = read_bytes(path);

        cst::GreenCache cache;
        diag::DiagnosticSink sink;
        cst::GreenNodePtr tree = parse_text(source, cache, sink);
        REQUIRE_FALSE(sink.has_errors());

        for (int iteration = 0; iteration < 25; ++iteration) {
            const cst::SyntaxNode root = cst::SyntaxNode::root(tree);
            std::vector<cst::SyntaxToken> names;
            for (const cst::SyntaxToken& token : root.tokens()) {
                if (token.kind() == cst::SyntaxKind::Identifier) {
                    names.push_back(token);
                }
            }
            if (names.empty()) {
                break;
            }
            const cst::SyntaxToken& pick =
                names[std::uniform_int_distribution<std::size_t>(0, names.size() - 1)(engine)];

            const cst::GreenNodePtr edited = cst::replace_token(
                pick, cache.token(cst::SyntaxKind::Identifier, "edited_name"), cache);
            REQUIRE(edited);

            const std::string text = cst::to_text(*edited);
            diag::DiagnosticSink reparse_sink;
            cst::GreenCache reparse_cache;
            const cst::GreenNodePtr reparsed = parse_text(text, reparse_cache, reparse_sink);
            REQUIRE(cst::to_text(*reparsed) == text);
            tree = edited;
        }
    }
}
