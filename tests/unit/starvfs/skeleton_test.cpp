// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// Mirrors tests/unit/skeleton_test.cpp: proves the build + Catch2 + ctest
// wiring for this third binary works, independent of the other two
// (see tests/unit/CMakeLists.txt for why starvfs_unit_tests exists
// separately and does not link starif_test_support).
#include <catch2/catch_test_macros.hpp>

#include "starvfs/version.hpp"

TEST_CASE("the starvfs build skeleton compiles, links and runs", "[skeleton]") {
    REQUIRE(1 + 1 == 2);
}

TEST_CASE("starvfs::version returns a non-empty string", "[skeleton]") {
    REQUIRE_FALSE(starvfs::version().empty());
}
