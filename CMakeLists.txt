# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Adrian Welcker
cmake_minimum_required(VERSION 3.21)

project(star-if-system
    LANGUAGES CXX
)

# C++20 is a hard requirement, not a suggestion (backlog B1): fail clearly,
# at configure time, rather than silently compiling against an older
# standard or failing deep inside a template instantiation.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12)
    message(FATAL_ERROR
        "star-if-system requires GCC >= 12 for C++20 support; found GCC ${CMAKE_CXX_COMPILER_VERSION}.")
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
    message(FATAL_ERROR
        "star-if-system requires Clang >= 14 for C++20 support; found Clang ${CMAKE_CXX_COMPILER_VERSION}.")
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND MSVC_VERSION LESS 1929)
    message(FATAL_ERROR
        "star-if-system requires MSVC >= 19.29 (Visual Studio 2019 16.10) for C++20 support.")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

option(STARIF_ENABLE_TESTS "Build the test suite" ON)

# libs/, apps/ layout of docs/proposal.md §2.1. Only components with actual
# content are add_subdirectory'd; the rest are placeholders (a short README
# each, no CMakeLists.txt) until their backlog workstream lands -- see
# docs/phase-0-backlog.md and each directory's own README.md for when.
#
#   libs/starcore    Phase 1        libs/stardebug   later (author's kit)
#   libs/stardata    Phase 0 (D-F)  libs/starforge   Phase 4
#   libs/starvfs     Phase 0 (G)    libs/startext    Phase 1
#                                    libs/starlang    Phase 1
#
#   apps/starhelm-cli   Phase 1     apps/starbase      Phase 3 (Qt)
#   apps/starhelm-glk   later       apps/starhelm-qt   Phase 3 (Qt)
#   apps/starhelm-web   later       apps/starforge-cli Phase 4
add_subdirectory(libs/stardata)

if (STARIF_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests/unit)
endif()
