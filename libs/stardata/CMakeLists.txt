# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Adrian Welcker

add_library(stardata STATIC
    src/version.cpp
    src/diag/source_manager.cpp
    src/diag/codes.cpp
    src/diag/diagnostic.cpp
    src/diag/sink.cpp
    src/diag/render.cpp
    src/lex/token.cpp
    src/lex/lexer.cpp
    src/cst/kind.cpp
    src/cst/green.cpp
    src/cst/syntax.cpp
    src/cst/builder.cpp
    src/cst/parser.cpp
    src/cst/writer.cpp
    src/cst/edit.cpp
    src/ast/ast.cpp
    src/schema/schema.cpp
    src/schema/loader.cpp
)
add_library(stardata::stardata ALIAS stardata)

target_include_directories(stardata
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(stardata PUBLIC cxx_std_20)

starif_set_warnings(stardata)
starif_enable_sanitizers(stardata)
