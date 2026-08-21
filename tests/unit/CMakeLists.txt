# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Adrian Welcker

find_package(Catch2 3 REQUIRED)
include(Catch)

# One test binary per library, mirroring the mechanism/vocabulary split of
# proposal §2.1.1. The split is not cosmetic: `starcore_unit_tests` links
# `starcore` and `stardata_unit_tests` does not, so a semantic pass that
# drifted back into the format library would have nowhere to be tested from
# and the move would be visible in the build file rather than only in a
# comment.
#
# Everything both need lives in starif_test_support: the snapshot machinery,
# the dumpers, and the compile definitions that point tests at the source
# tree. PUBLIC throughout, so a test target gets them by linking one thing.
add_library(starif_test_support STATIC
    support/snapshot.cpp
    support/token_dump.cpp
    support/tree_dump.cpp
)

target_link_libraries(starif_test_support PUBLIC stardata::stardata Catch2::Catch2)

target_include_directories(starif_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Used by corpus_discovery_test.cpp so fixture discovery works regardless of
# where the build directory lives, and by support/fixture.hpp to name corpus
# files repo-relatively in diagnostics; STARIF_UNIT_TEST_DIR does the same
# for the checked-in snapshot files under */snapshots/.
target_compile_definitions(starif_test_support PUBLIC
    STARIF_CORPUS_DIR="${CMAKE_SOURCE_DIR}/tests/corpus"
    STARIF_UNIT_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    STARIF_FORMAT_DIR="${CMAKE_SOURCE_DIR}/libs/stardata/builtin"
    STARIF_BUILTIN_DIR="${CMAKE_SOURCE_DIR}/libs/starcore/builtin"
    STARIF_STDLIB_DIR="${CMAKE_SOURCE_DIR}/stdlib"
    STARIF_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

starif_set_warnings(starif_test_support)
starif_enable_sanitizers(starif_test_support)

add_executable(stardata_unit_tests
    main.cpp
    skeleton_test.cpp
    corpus_discovery_test.cpp
    diagnostics/source_manager_test.cpp
    diagnostics/diagnostic_test.cpp
    diagnostics/render_test.cpp
    diagnostics/snapshot_test.cpp
    diagnostics/corpus_snapshot_test.cpp
    ast/ast_test.cpp
    schema/builtin_test.cpp
    schema/sealing_test.cpp
    schema/corpus_test.cpp
    schema/extension_test.cpp
    schema/markers_test.cpp
    schema/keys_test.cpp
    schema/annotation_test.cpp
    schema/suggest_test.cpp
    schema/local_prop_test.cpp
    schema/types_test.cpp
    text/template_test.cpp
    lex/token_test.cpp
    lex/lexer_test.cpp
    lex/lexer_diagnostics_test.cpp
    lex/lexer_corpus_test.cpp
    lex/lexer_fuzz_test.cpp
    cst/green_test.cpp
    cst/syntax_test.cpp
    cst/trivia_test.cpp
    cst/parser_test.cpp
    cst/roundtrip_test.cpp
    cst/edit_test.cpp
    cst/edit_fuzz_test.cpp
)

target_link_libraries(stardata_unit_tests PRIVATE starif_test_support)

starif_set_warnings(stardata_unit_tests)
starif_enable_sanitizers(stardata_unit_tests)

# The vocabulary half (spec §8-§12): containment (F2c), §8.8's narrowing
# (F12), the text layer's styles and localisation (F7), §10.5's `failureMsg`
# placement (F8) and §6.4's globals, constants and flags (F10).
add_executable(starcore_unit_tests
    main.cpp
    starcore/placement_test.cpp
    starcore/narrowing_test.cpp
    starcore/text_test.cpp
    starcore/messages_test.cpp
    starcore/globals_test.cpp
)

target_link_libraries(starcore_unit_tests PRIVATE starif_test_support starcore::starcore)

starif_set_warnings(starcore_unit_tests)
starif_enable_sanitizers(starcore_unit_tests)

# catch_discover_tests runs each binary at build time to enumerate individual
# TEST_CASEs into ctest, so `ctest` reports each one rather than one lump.
catch_discover_tests(stardata_unit_tests)
catch_discover_tests(starcore_unit_tests)
