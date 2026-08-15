// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include "stardata/cst/green.hpp"
#include "stardata/cst/syntax.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/diag/source_manager.hpp"
#include "stardata/lex/token.hpp"

namespace stardata::cst {

// Parses one source into a lossless concrete syntax tree, per the grammar of
// spec §4 (backlog E4).
//
// Parsing never fails. Input the grammar does not accept produces an `Error`
// node holding the bytes verbatim and parsing continues, because an editor
// must have a tree for a file the author is halfway through typing. The
// tree covers every byte of the input -- trivia, error text and all -- which
// is what E5's writer relies on to reproduce the source exactly.
//
// Diagnostics go to `sink`. The returned tree is usable whether or not any
// were reported.
[[nodiscard]] GreenNodePtr parse(const diag::SourceManager& sources, diag::SourceId source,
                                 GreenCache& cache, diag::DiagnosticSink& sink);

// The same, from an already-lexed token stream, for a caller that wants the
// tokens too.
[[nodiscard]] GreenNodePtr parse(const diag::SourceManager& sources, const lex::TokenStream& tokens,
                                 GreenCache& cache, diag::DiagnosticSink& sink);

// Convenience: parse and wrap in a cursor at the root.
[[nodiscard]] SyntaxNode parse_to_syntax(const diag::SourceManager& sources, diag::SourceId source,
                                         GreenCache& cache, diag::DiagnosticSink& sink);

} // namespace stardata::cst
