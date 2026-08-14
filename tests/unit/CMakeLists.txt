# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Adrian Welcker

find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(stardata_unit_tests
    skeleton_test.cpp
    corpus_discovery_test.cpp
)

target_link_libraries(stardata_unit_tests
    PRIVATE
        stardata::stardata
        Catch2::Catch2WithMain
)

target_include_directories(stardata_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Used by corpus_discovery_test.cpp so fixture discovery works regardless of
# where the build directory lives.
target_compile_definitions(stardata_unit_tests PRIVATE
    STARIF_CORPUS_DIR="${CMAKE_SOURCE_DIR}/tests/corpus"
)

starif_set_warnings(stardata_unit_tests)
starif_enable_sanitizers(stardata_unit_tests)

# catch_discover_tests runs the binary at build time to enumerate individual
# TEST_CASEs into ctest, so `ctest` reports each one rather than one lump.
catch_discover_tests(stardata_unit_tests)
