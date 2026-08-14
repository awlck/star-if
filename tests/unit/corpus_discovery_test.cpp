// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "support/corpus.hpp"

namespace {
// STARIF_CORPUS_DIR is injected by tests/unit/CMakeLists.txt from
// CMAKE_SOURCE_DIR, so this works regardless of where the build directory
// lives.
constexpr const char* kCorpusDir = STARIF_CORPUS_DIR;
} // namespace

TEST_CASE("corpus_files discovers the reference corpus", "[skeleton][corpus]") {
    const auto files = stardata::test::corpus_files(kCorpusDir);
    REQUIRE_FALSE(files.empty());
}

TEST_CASE("corpus_files discovers the negative fixtures", "[skeleton][corpus]") {
    const auto files = stardata::test::corpus_files(std::filesystem::path(kCorpusDir) / "invalid");
    REQUIRE_FALSE(files.empty());
}
