// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "support/token_dump.hpp"

#include <cstddef>
#include <cstdio>
#include <sstream>
#include <string_view>

namespace stardata::test {

namespace {

constexpr std::size_t kMaxTextBytes = 48;

// Escapes the control characters that would otherwise break the one-line-per
// -token layout. Bytes >= 0x80 pass through: the corpus is UTF-8, and a
// golden that spells `é` as four hex escapes is a golden nobody reads.
void append_escaped(std::string_view text, std::string& out) {
    for (char c : text) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
                char buffer[5];
                std::snprintf(buffer, sizeof(buffer), "\\x%02X",
                              static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buffer;
            } else {
                out.push_back(c);
            }
            break;
        }
    }
}

std::string render_text(std::string_view text) {
    std::string_view head = text.substr(0, std::min(text.size(), kMaxTextBytes));
    // Never cut a UTF-8 sequence in half: back up off continuation bytes so
    // the golden file stays valid UTF-8 and diffs render.
    while (!head.empty() && (static_cast<unsigned char>(head.back()) & 0xC0) == 0x80) {
        head.remove_suffix(1);
    }
    std::string out;
    append_escaped(head, out);
    if (head.size() < text.size()) {
        out += "...";
    }
    return out;
}

void append_row(std::ostringstream& out, const diag::Span& span, std::string_view kind,
                std::string_view text) {
    out << span.offset << '+' << span.length << ' ' << kind << ' ' << render_text(text) << '\n';
}

} // namespace

std::string dump_tokens(const lex::TokenStream& stream, const diag::SourceManager& sources) {
    std::ostringstream out;
    out << "# tokens: " << stream.tokens().size() << ", trivia: " << stream.trivia().size()
        << " (whitespace elided)\n";

    for (std::size_t index = 0; index < stream.size(); ++index) {
        for (const lex::Trivia& piece : stream.preceding_trivia(index)) {
            if (piece.kind == lex::TriviaKind::Whitespace) {
                continue;
            }
            append_row(out, piece.span, lex::to_string(piece.kind), sources.text(piece.span));
        }
        const lex::Token& token = stream[index];
        append_row(out, token.span, lex::to_string(token.kind), sources.text(token.span));
    }
    return out.str();
}

} // namespace stardata::test
