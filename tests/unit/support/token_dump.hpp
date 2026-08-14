// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <string>

#include "stardata/diag/source_manager.hpp"
#include "stardata/lex/token.hpp"

namespace stardata::test {

// Renders a token stream as text, for the golden tests of backlog D5. One
// line per token: byte offset, byte length, kind, and the token's text with
// control characters escaped.
//
// Comment and BOM trivia are listed alongside the tokens; whitespace trivia
// is not. Whitespace is over half the stream by count on a real file and
// says nothing a reader can act on, and TokenStream::covers_source -- which
// the same tests assert -- already proves that every byte between two listed
// pieces is whitespace the lexer classified as such. Eliding it roughly
// halves the checked-in golden and makes what remains readable.
[[nodiscard]] std::string dump_tokens(const lex::TokenStream& stream,
                                      const diag::SourceManager& sources);

} // namespace stardata::test
