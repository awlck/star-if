// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Backlog D5: "Fuzz the lexer on random bytes: no crash, no hang, no
// unbounded memory."
//
// This is a deterministic in-process fuzzer rather than a libFuzzer target,
// because it has to run in the same `ctest` invocation on all three
// platforms and inside the same ASan/UBSan Debug build that already guards
// the rest of the suite (cmake/Sanitizers.cmake). A crash surfaces as a
// sanitiser report; a hang surfaces as a CTest timeout; unbounded memory is
// bounded here by an explicit assertion on the size of the output, since a
// lexer that allocates per byte is the realistic failure and it is cheap to
// rule out.
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "stardata/diag/sink.hpp"
#include "stardata/diag/source_manager.hpp"
#include "stardata/lex/lexer.hpp"

using namespace stardata;

namespace {

// Lexes `source` and asserts the invariants that must hold for any input at
// all, well-formed or not.
void check_invariants(const std::string& source) {
    diag::SourceManager sources;
    const auto id = sources.add_file("fuzz.star", source);
    // A limit keeps a pathological input from making the sink, rather than
    // the lexer, the thing that allocates without bound.
    diag::DiagnosticSink sink(64);
    const lex::TokenStream stream = lex::lex(sources, id, sink);

    // 1. Termination and totality: the stream ends in EndOfFile and its
    //    spans tile the input exactly.
    REQUIRE_FALSE(stream.tokens().empty());
    REQUIRE(stream.tokens().back().kind == lex::TokenKind::EndOfFile);
    REQUIRE(stream.covers_source(sources));

    // 2. Bounded output: every token and every piece of trivia consumes at
    //    least one byte, except the zero-length EndOfFile, so the two
    //    vectors together cannot exceed one entry per byte plus that one.
    REQUIRE(stream.tokens().size() + stream.trivia().size() <= source.size() + 1);

    // 3. Bounded diagnostics: the sink's limit is respected however many
    //    conditions the input trips.
    REQUIRE(sink.diagnostics().size() <= 64);
}

// A fixed seed, so a failure is reproducible from the test name alone. The
// point is coverage of the byte-level state machine, not a search that
// differs from run to run -- backlog E8 is where a persistent, corpus-driven
// fuzzer belongs.
constexpr std::uint32_t kSeed = 0x5AFEC0DE;

} // namespace

TEST_CASE("the lexer survives uniformly random bytes", "[lex][fuzz]") {
    std::mt19937 engine(kSeed);
    std::uniform_int_distribution<int> byte(0, 255);
    std::uniform_int_distribution<std::size_t> length(0, 512);

    for (int iteration = 0; iteration < 400; ++iteration) {
        std::string source(length(engine), '\0');
        for (char& c : source) {
            c = static_cast<char>(byte(engine));
        }
        INFO("iteration " << iteration << ", " << source.size() << " bytes");
        check_invariants(source);
    }
}

TEST_CASE("the lexer survives random sequences of Stardata fragments", "[lex][fuzz]") {
    // Uniform random bytes rarely produce a valid token, so they exercise
    // the error paths and little else. Splicing real fragments together
    // reaches the states that matter: a string opened and never closed, an
    // escape at the end of input, an annotation with no name.
    // clang-format off
    static constexpr std::string_view kFragments[] = {
        "room", "=",    "{",  "}",  "\"text", "\"text\"", "\\",   "\\u",  "\\u00", "\\ud800",
        "$key", "@ann", "(",  ")",  ",",      "<",        ">",    "<=",   "1.5",   ".5",
        "5.",   "-",    "#c", "\n", "\r\n",   " ",        "\t",   "[",    "]",     "%",
        "true", "yes",  "\xC2\xA0", "\xFF", "\xEF\xBB\xBF", "99999999999999999999", "*=", "::"};
    // clang-format on

    std::mt19937 engine(kSeed ^ 0xFFFFFFFFU);
    std::uniform_int_distribution<std::size_t> pick(0, std::size(kFragments) - 1);
    std::uniform_int_distribution<std::size_t> count(0, 64);

    for (int iteration = 0; iteration < 400; ++iteration) {
        std::string source;
        const std::size_t pieces = count(engine);
        for (std::size_t n = 0; n < pieces; ++n) {
            source += kFragments[pick(engine)];
        }
        INFO("iteration " << iteration << ", source: " << source);
        check_invariants(source);
    }
}

TEST_CASE("the lexer survives every single byte on its own", "[lex][fuzz]") {
    // Exhaustive at length one, which is where the lookahead in each branch
    // is most likely to read past the end.
    for (int value = 0; value <= 255; ++value) {
        const std::string source(1, static_cast<char>(value));
        INFO("byte 0x" << std::hex << value);
        check_invariants(source);
    }
}

TEST_CASE("the lexer survives every byte pair", "[lex][fuzz]") {
    // 65,536 inputs, each two bytes: cheap, and it covers every two-byte
    // lookahead the lexer performs -- `\` then anything, `-` then anything,
    // a lead byte then anything.
    for (int first = 0; first <= 255; ++first) {
        for (int second = 0; second <= 255; ++second) {
            std::string source;
            source.push_back(static_cast<char>(first));
            source.push_back(static_cast<char>(second));
            INFO("bytes 0x" << std::hex << first << " 0x" << second);
            check_invariants(source);
        }
    }
}
