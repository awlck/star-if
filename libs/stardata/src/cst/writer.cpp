// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/cst/writer.hpp"

#include <ostream>

namespace stardata::cst {

void write(const GreenNode& node, std::string& out) {
    out.reserve(out.size() + node.text_length());
    node.write_text(out);
}

void write(const GreenNode& node, std::ostream& out) {
    std::string text;
    write(node, text);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string to_text(const GreenNode& node) {
    return node.text();
}

std::string to_text(const SyntaxNode& node) {
    return node.text();
}

} // namespace stardata::cst
