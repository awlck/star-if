// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <string>
#include <string_view>

#include "stardata/diag/sink.hpp"
#include "stardata/diag/source_manager.hpp"
#include "stardata/lex/token.hpp"

namespace stardata::lex {

// Turns one registered source into tokens and trivia, per spec §3.
//
// Lexing never fails: every byte of the input ends up in some token or some
// piece of trivia, and a byte that begins no token becomes a TokenKind::Error
// token with a diagnostic beside it. One bad token therefore does not abandon
// the file (backlog D3), which is what an editor needs -- it must have a tree
// even for input the author is halfway through typing.
//
// Diagnostics go to `sink`; the returned stream is usable whether or not any
// were reported.
[[nodiscard]] TokenStream lex(const diag::SourceManager& sources, diag::SourceId source,
                              diag::DiagnosticSink& sink);

// Spec §3.9's reserved words. The lexer emits all of them as ordinary
// Identifier tokens, because most are legal values (`yes`, `none`,
// `inherit`) or condition-language keys, and whether a given position expects
// a user-chosen name is a question only the grammar and the schema can
// answer. The two exceptions are `true` and `false`, which are never valid
// anywhere and so are diagnosed at lex time (E-RESERVED-WORD), as §3.9
// requires, with a fix-it pointing at `yes` / `no`.
[[nodiscard]] bool is_reserved_word(std::string_view identifier) noexcept;

// Decodes the escapes of §3.5 in one String token's text, which must include
// its surrounding quotes, and appends the result to `out`. Invalid escapes
// are copied through verbatim rather than diagnosed a second time -- lex()
// has already reported them.
//
// The concatenated decode of a TokenStream::string_run_at range is the value
// of the scalar those adjacent literals form (§3.5.1).
//
// Note for the template layer (backlog F7): `\[`, `\]`, `\$` and `\@` decode
// to their bare characters here, per §3.5. That loses the distinction
// between a bracket the author escaped and one they meant as template
// syntax, so template parsing must read the token text rather than this
// value.
void decode_string_escapes(std::string_view literal, std::string& out);

[[nodiscard]] std::string decode_string_escapes(std::string_view literal);

} // namespace stardata::lex
