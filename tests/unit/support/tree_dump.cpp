// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "support/tree_dump.hpp"

#include <cstdio>
#include <sstream>
#include <string_view>

namespace stardata::test {

namespace {

constexpr std::size_t kMaxTextBytes = 40;

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
        case '"':
            out += "\\\"";
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
    // Never cut a UTF-8 sequence in half; the golden must stay valid UTF-8.
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

void dump(const cst::SyntaxNode& node, int depth, std::ostringstream& out) {
    const cst::TextRange range = node.text_range();
    out << std::string(static_cast<std::size_t>(depth) * 2, ' ') << cst::to_string(node.kind())
        << " @" << range.offset << ".." << range.end() << '\n';

    for (const cst::SyntaxElement& child : node.children()) {
        if (auto child_node = child.as_node()) {
            dump(*child_node, depth + 1, out);
            continue;
        }
        const cst::SyntaxToken token = *child.as_token();
        const cst::TextRange token_range = token.text_range();
        out << std::string(static_cast<std::size_t>(depth + 1) * 2, ' ')
            << cst::to_string(token.kind()) << " @" << token_range.offset << ".."
            << token_range.end() << " \"" << render_text(token.text()) << "\"\n";
    }
}

} // namespace

std::string dump_tree(const cst::SyntaxNode& root) {
    std::ostringstream out;
    dump(root, 0, out);
    return out.str();
}

} // namespace stardata::test
