# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Adrian Welcker

# The STAR core vocabulary (spec §8-§12, proposal §2.1.1).
#
# Nearly empty, and deliberately created anyway. Proposal §2.1.1's closing
# note is the reason: "a semantic pass that lives in `stardata` 'for now' is
# how the line stops being real." A target and a directory cost nothing.
#
# The dependency runs one way only. `starcore` links `stardata`, reads its
# trees and queries its registry; nothing in `stardata` names anything here.
# That is the deletion test of §2.1.1 -- remove this target and every `.star`
# file, and `stardata` still parses, round-trips, edits and validates against
# whatever schemas somebody else supplies.
#
# builtin/ is the other half of this library and needs no target: it is
# Stardata, read from disk in Phase 0 through STARIF_BUILTIN_DIR and embedded
# as a generated string literal in Phase 1.

add_library(starcore STATIC
    src/placement.cpp
    src/narrowing.cpp
    src/text.cpp
    src/conditions.cpp
    src/messages.cpp
)
add_library(starcore::starcore ALIAS starcore)

target_include_directories(starcore
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(starcore PUBLIC stardata::stardata)

target_compile_features(starcore PUBLIC cxx_std_20)

starif_set_warnings(starcore)
starif_enable_sanitizers(starcore)
