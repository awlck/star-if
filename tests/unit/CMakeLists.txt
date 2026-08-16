# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Adrian Welcker

find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(stardata_unit_tests
    main.cpp
    skeleton_test.cpp
    corpus_discovery_test.cpp
    support/snapshot.cpp
    support/token_dump.cpp
    support/tree_dump.cpp
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

target_link_libraries(stardata_unit_tests
    PRIVATE
        stardata::stardata
        Catch2::Catch2
)

target_include_directories(stardata_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Used by corpus_discovery_test.cpp so fixture discovery works regardless of
# where the build directory lives, and by support/fixture.hpp to name corpus
# files repo-relatively in diagnostics; STARIF_UNIT_TEST_DIR does the same
# for the checked-in snapshot files under */snapshots/.
target_compile_definitions(stardata_unit_tests PRIVATE
    STARIF_CORPUS_DIR="${CMAKE_SOURCE_DIR}/tests/corpus"
    STARIF_UNIT_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    STARIF_BUILTIN_DIR="${CMAKE_SOURCE_DIR}/libs/starcore/builtin"
    STARIF_STDLIB_DIR="${CMAKE_SOURCE_DIR}/stdlib"
    STARIF_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

starif_set_warnings(stardata_unit_tests)
starif_enable_sanitizers(stardata_unit_tests)

# catch_discover_tests runs the binary at build time to enumerate individual
# TEST_CASEs into ctest, so `ctest` reports each one rather than one lump.
catch_discover_tests(stardata_unit_tests)
