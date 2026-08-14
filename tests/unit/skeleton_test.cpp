// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// backlog B3: "tests/unit/ builds and runs one trivial passing test." This
// is that test -- it exists to prove the CMake + Catch2 + ctest wiring
// works, not to test anything about the Stardata format yet.

#include <catch2/catch_test_macros.hpp>

#include "stardata/version.hpp"

TEST_CASE("the build skeleton compiles, links and runs", "[skeleton]") {
    REQUIRE(1 + 1 == 2);
}

TEST_CASE("stardata::version returns a non-empty string", "[skeleton]") {
    REQUIRE_FALSE(stardata::version().empty());
}
