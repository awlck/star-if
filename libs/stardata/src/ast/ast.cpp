// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/ast/ast.hpp"

#include <charconv>
#include <system_error>

namespace stardata::ast {

namespace {

using cst::SyntaxKind;
using cst::SyntaxNode;
using cst::SyntaxToken;

// The direct child tokens that carry meaning. Trivia is a child like any
// other in the CST -- that is what makes the round-trip work -- and no view
// in this file ever wants to see it.
[[nodiscard]] std::vector<SyntaxToken> significant_tokens(const SyntaxNode& node) {
    std::vector<SyntaxToken> tokens;
    for (SyntaxToken& token : node.child_tokens()) {
        if (!token.is_trivia()) {
            tokens.push_back(std::move(token));
        }
    }
    return tokens;
}

[[nodiscard]] std::optional<SyntaxToken> first_significant_token(const SyntaxNode& node) {
    for (SyntaxToken& token : node.child_tokens()) {
        if (!token.is_trivia()) {
            return std::move(token);
        }
    }
    return std::nullopt;
}

// Appends `code_point` to `out` as UTF-8. Only reached for a `\uXXXX`
// escape, so the value cannot exceed 0xFFFF.
void append_utf8(std::uint32_t code_point, std::string& out) {
    if (code_point < 0x80) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

[[nodiscard]] std::optional<std::uint32_t> hex_quad(std::string_view text) {
    if (text.size() < 4) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const char c = text[i];
        std::uint32_t digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<std::uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<std::uint32_t>(c - 'a') + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<std::uint32_t>(c - 'A') + 10;
        } else {
            return std::nullopt;
        }
        value = (value << 4) | digit;
    }
    return value;
}

// Decodes one string literal's contents into `out`, quotes stripped.
//
// The lexer has already reported anything malformed here (§3.5), so this
// runs on text that is normally well formed and must not make a second fuss
// when it is not: an escape it does not recognise is copied through as
// written, which keeps the author's bytes visible next to the diagnostic
// that already explained them.
void decode_string_literal(std::string_view raw, std::string& out) {
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
        raw = raw.substr(1, raw.size() - 2);
    } else if (!raw.empty() && raw.front() == '"') {
        raw = raw.substr(1); // unterminated; take what there is
    }

    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\' || i + 1 >= raw.size()) {
            out.push_back(raw[i]);
            continue;
        }
        const char escape = raw[i + 1];
        switch (escape) {
        case 'n':
            out.push_back('\n');
            ++i;
            break;
        case 't':
            out.push_back('\t');
            ++i;
            break;
        case '"':
        case '\\':
        case '[':
        case ']':
        case '$':
        case '@':
            out.push_back(escape);
            ++i;
            break;
        case 'u': {
            const std::optional<std::uint32_t> value = hex_quad(raw.substr(i + 2));
            if (!value) {
                out.push_back(raw[i]); // malformed: verbatim, as documented
                break;
            }
            append_utf8(*value, out);
            i += 5;
            break;
        }
        default:
            out.push_back(raw[i]);
            break;
        }
    }
}

[[nodiscard]] std::optional<std::int64_t> parse_int64(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    std::int64_t value = 0;
    const char* first = text.data();
    const char* last = first + text.size();
    const std::from_chars_result result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
        return std::nullopt;
    }
    return value;
}

} // namespace

// --- TypeRef -----------------------------------------------------------

std::string TypeRef::to_string() const {
    std::string out = name;
    if (args.empty()) {
        return out;
    }
    out.push_back('<');
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += args[i].to_string();
    }
    out.push_back('>');
    return out;
}

bool TypeRef::same_as(const TypeRef& other) const {
    if (name != other.name || args.size() != other.args.size()) {
        return false;
    }
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (!args[i].same_as(other.args[i])) {
            return false;
        }
    }
    return true;
}

// --- NodeView ----------------------------------------------------------

diag::Span NodeView::span() const noexcept {
    const cst::TextRange range = node_.text_range();
    return diag::Span{source_, range.offset, range.length};
}

// --- Key ---------------------------------------------------------------

std::optional<Key> Key::cast(const SyntaxNode& node, diag::SourceId source) {
    if (node.kind() != SyntaxKind::Key) {
        return std::nullopt;
    }
    return Key(node, source);
}

bool Key::is_string() const {
    const std::optional<SyntaxToken> token = first_significant_token(node_);
    return token && token->kind() == SyntaxKind::String;
}

std::optional<std::string> Key::name() const {
    const std::optional<SyntaxToken> token = first_significant_token(node_);
    if (!token) {
        return std::nullopt;
    }
    if (token->kind() == SyntaxKind::String) {
        std::string out;
        decode_string_literal(token->text(), out);
        return out;
    }
    return std::string(token->text());
}

// --- Scalar ------------------------------------------------------------

std::optional<Scalar> Scalar::cast(const SyntaxNode& node, diag::SourceId source) {
    if (node.kind() != SyntaxKind::Scalar) {
        return std::nullopt;
    }
    return Scalar(node, source);
}

std::vector<SyntaxToken> Scalar::literals() const {
    return significant_tokens(node_);
}

std::optional<SyntaxKind> Scalar::literal_kind() const {
    const std::optional<SyntaxToken> token = first_significant_token(node_);
    if (!token) {
        return std::nullopt;
    }
    return token->kind();
}

std::optional<std::string_view> Scalar::as_identifier() const {
    const std::optional<SyntaxToken> token = first_significant_token(node_);
    if (!token || token->kind() != SyntaxKind::Identifier) {
        return std::nullopt;
    }
    return token->text();
}

std::optional<std::int64_t> Scalar::as_integer() const {
    const std::optional<SyntaxToken> token = first_significant_token(node_);
    if (!token || token->kind() != SyntaxKind::Integer) {
        return std::nullopt;
    }
    return parse_int64(token->text());
}

std::optional<std::string_view> Scalar::as_loc_key() const {
    const std::optional<SyntaxToken> token = first_significant_token(node_);
    if (!token || token->kind() != SyntaxKind::LocKey) {
        return std::nullopt;
    }
    const std::string_view text = token->text();
    return text.empty() ? text : text.substr(1); // drop the '$'
}

std::optional<std::int64_t> Scalar::as_decimal_scaled() const {
    const std::optional<SyntaxToken> token = first_significant_token(node_);
    if (!token || token->kind() != SyntaxKind::Decimal) {
        return std::nullopt;
    }
    const std::string_view text = token->text();
    const std::size_t dot = text.find('.');
    if (dot == std::string_view::npos) {
        return std::nullopt;
    }

    // §3.4 fixes the scale at three fractional digits, and the lexer has
    // rejected any other precision. Pad and truncate anyway: this runs on
    // trees the lexer already complained about, and returning nothing would
    // make a downstream pass report a second, less useful error about the
    // same literal.
    std::string digits(text.substr(0, dot));
    std::string fraction(text.substr(dot + 1));
    fraction.resize(3, '0');
    const bool negative = !digits.empty() && digits.front() == '-';
    if (negative) {
        digits.erase(0, 1);
    }
    digits += fraction;

    const std::optional<std::int64_t> value = parse_int64(digits);
    if (!value) {
        return std::nullopt;
    }
    return negative ? -*value : *value;
}

std::optional<std::string> Scalar::as_string() const {
    const std::vector<SyntaxToken> tokens = literals();
    if (tokens.empty() || tokens.front().kind() != SyntaxKind::String) {
        return std::nullopt;
    }
    std::string out;
    for (const SyntaxToken& token : tokens) {
        if (token.kind() != SyntaxKind::String) {
            continue;
        }
        decode_string_literal(token.text(), out);
    }
    return out;
}

std::optional<bool> Scalar::as_bool() const {
    const std::optional<std::string_view> word = as_identifier();
    if (!word) {
        return std::nullopt;
    }
    if (*word == "yes") {
        return true;
    }
    if (*word == "no") {
        return false;
    }
    return std::nullopt;
}

// --- Annotation --------------------------------------------------------

std::optional<Annotation> Annotation::cast(const SyntaxNode& node, diag::SourceId source) {
    if (node.kind() != SyntaxKind::Annotation) {
        return std::nullopt;
    }
    return Annotation(node, source);
}

std::optional<std::string_view> Annotation::name() const {
    const std::optional<SyntaxToken> token = first_significant_token(node_);
    if (!token || token->kind() != SyntaxKind::AnnotationName) {
        return std::nullopt;
    }
    const std::string_view text = token->text();
    return text.empty() ? text : text.substr(1); // drop the '@'
}

std::vector<SyntaxToken> Annotation::arguments() const {
    std::vector<SyntaxToken> args;
    for (const SyntaxToken& token : significant_tokens(node_)) {
        if (token.kind() == SyntaxKind::Identifier || token.kind() == SyntaxKind::Integer) {
            args.push_back(token);
        }
    }
    return args;
}

// --- Call --------------------------------------------------------------

std::optional<Call> Call::cast(const SyntaxNode& node, diag::SourceId source) {
    if (node.kind() != SyntaxKind::Call) {
        return std::nullopt;
    }
    return Call(node, source);
}

std::optional<std::string_view> Call::callee() const {
    const std::optional<SyntaxToken> token = first_significant_token(node_);
    if (!token || token->kind() != SyntaxKind::Identifier) {
        return std::nullopt;
    }
    return token->text();
}

std::vector<SyntaxNode> Call::arguments() const {
    std::vector<SyntaxNode> args;
    for (SyntaxNode& child : node_.child_nodes()) {
        if (child.kind() == SyntaxKind::Scalar || child.kind() == SyntaxKind::Call) {
            args.push_back(std::move(child));
        }
    }
    return args;
}

// --- TypeExpr ----------------------------------------------------------

std::optional<TypeExpr> TypeExpr::cast(const SyntaxNode& node, diag::SourceId source) {
    if (node.kind() != SyntaxKind::TypeExpr) {
        return std::nullopt;
    }
    return TypeExpr(node, source);
}

std::optional<std::string_view> TypeExpr::name() const {
    const std::optional<SyntaxToken> token = first_significant_token(node_);
    if (!token || token->kind() != SyntaxKind::Identifier) {
        return std::nullopt;
    }
    return token->text();
}

TypeRef TypeExpr::lower() const {
    TypeRef result;
    result.range = node_.text_range();

    // Children in source order: the name, '<', then arguments -- each either
    // an identifier token (a bare type) or a nested TypeExpr node -- and '>'.
    // `seen_open` is what separates the name from an argument, since both are
    // plain identifier tokens.
    bool seen_open = false;
    for (const cst::SyntaxElement& child : node_.children()) {
        if (const std::optional<SyntaxNode> nested = child.as_node()) {
            if (nested->kind() == SyntaxKind::TypeExpr) {
                result.args.push_back(TypeExpr(*nested, source_).lower());
            }
            continue;
        }
        const SyntaxToken token = *child.as_token();
        if (token.is_trivia()) {
            continue;
        }
        if (token.kind() == SyntaxKind::Angle && token.text() == "<") {
            seen_open = true;
            continue;
        }
        if (token.kind() != SyntaxKind::Identifier) {
            continue;
        }
        if (!seen_open) {
            result.name = std::string(token.text());
        } else {
            result.args.push_back(TypeRef{std::string(token.text()), {}, token.text_range()});
        }
    }
    return result;
}

// --- Block -------------------------------------------------------------

std::optional<Block> Block::cast(const SyntaxNode& node, diag::SourceId source) {
    if (node.kind() != SyntaxKind::Block) {
        return std::nullopt;
    }
    return Block(node, source);
}

std::vector<Statement> Block::statements() const {
    std::vector<Statement> result;
    for (const SyntaxNode& child : node_.child_nodes()) {
        if (std::optional<Statement> statement = Statement::cast(child, source_)) {
            result.push_back(*std::move(statement));
        }
    }
    return result;
}

std::vector<Scalar> Block::values() const {
    std::vector<Scalar> result;
    for (const SyntaxNode& child : node_.child_nodes()) {
        if (std::optional<Scalar> scalar = Scalar::cast(child, source_)) {
            result.push_back(*std::move(scalar));
        }
    }
    return result;
}

bool Block::is_record() const {
    return !statements().empty();
}

bool Block::is_list() const {
    return !values().empty();
}

bool Block::is_empty() const {
    return statements().empty() && values().empty();
}

std::optional<Statement> Block::find(std::string_view key) const {
    for (const Statement& statement : statements()) {
        const std::optional<std::string> name = statement.key_name();
        if (name && *name == key) {
            return statement;
        }
    }
    return std::nullopt;
}

std::vector<Statement> Block::find_all(std::string_view key) const {
    std::vector<Statement> result;
    for (const Statement& statement : statements()) {
        const std::optional<std::string> name = statement.key_name();
        if (name && *name == key) {
            result.push_back(statement);
        }
    }
    return result;
}

std::optional<Value> Block::value_of(std::string_view key) const {
    const std::optional<Statement> statement = find(key);
    return statement ? statement->value() : std::nullopt;
}

// --- Value -------------------------------------------------------------

std::optional<Value> Value::cast(const SyntaxNode& node, diag::SourceId source) {
    if (node.kind() != SyntaxKind::Value) {
        return std::nullopt;
    }
    return Value(node, source);
}

std::vector<Annotation> Value::annotations() const {
    std::vector<Annotation> result;
    for (const SyntaxNode& child : node_.child_nodes()) {
        if (std::optional<Annotation> annotation = Annotation::cast(child, source_)) {
            result.push_back(*std::move(annotation));
        }
    }
    return result;
}

std::optional<Block> Value::as_block() const {
    const std::optional<SyntaxNode> child = node_.first_child_of_kind(SyntaxKind::Block);
    return child ? Block::cast(*child, source_) : std::nullopt;
}

std::optional<Scalar> Value::as_scalar() const {
    const std::optional<SyntaxNode> child = node_.first_child_of_kind(SyntaxKind::Scalar);
    return child ? Scalar::cast(*child, source_) : std::nullopt;
}

std::optional<Call> Value::as_call() const {
    const std::optional<SyntaxNode> child = node_.first_child_of_kind(SyntaxKind::Call);
    return child ? Call::cast(*child, source_) : std::nullopt;
}

std::optional<TypeExpr> Value::as_type_expr() const {
    const std::optional<SyntaxNode> child = node_.first_child_of_kind(SyntaxKind::TypeExpr);
    return child ? TypeExpr::cast(*child, source_) : std::nullopt;
}

std::optional<TypeRef> Value::as_type() const {
    if (const std::optional<TypeExpr> expr = as_type_expr()) {
        return expr->lower();
    }
    // A type with no arguments -- `type = int` -- is an ordinary identifier
    // as far as the grammar is concerned, and a schema does not care which
    // of the two spellings it was handed.
    const std::optional<Scalar> scalar = as_scalar();
    if (!scalar) {
        return std::nullopt;
    }
    const std::optional<std::string_view> name = scalar->as_identifier();
    if (!name) {
        return std::nullopt;
    }
    return TypeRef{std::string(*name), {}, scalar->text_range()};
}

// --- Statement ---------------------------------------------------------

std::optional<Statement> Statement::cast(const SyntaxNode& node, diag::SourceId source) {
    if (node.kind() != SyntaxKind::Statement) {
        return std::nullopt;
    }
    return Statement(node, source);
}

std::optional<Key> Statement::key() const {
    const std::optional<SyntaxNode> child = node_.first_child_of_kind(SyntaxKind::Key);
    return child ? Key::cast(*child, source_) : std::nullopt;
}

std::optional<std::string> Statement::key_name() const {
    const std::optional<Key> k = key();
    return k ? k->name() : std::nullopt;
}

std::optional<SyntaxToken> Statement::op() const {
    for (SyntaxToken& token : node_.child_tokens()) {
        if (!token.is_trivia() && token.kind() == SyntaxKind::Operator) {
            return std::move(token);
        }
    }
    return std::nullopt;
}

std::string_view Statement::op_text() const {
    const std::optional<SyntaxToken> token = op();
    return token ? token->text() : std::string_view{};
}

std::optional<Value> Statement::value() const {
    const std::optional<SyntaxNode> child = node_.first_child_of_kind(SyntaxKind::Value);
    return child ? Value::cast(*child, source_) : std::nullopt;
}

bool Statement::is_binding() const {
    return op_text() == "=";
}

diag::Span Statement::report_span() const {
    if (const std::optional<Key> k = key()) {
        return k->span();
    }
    if (const std::optional<SyntaxToken> token = op()) {
        const cst::TextRange range = token->text_range();
        return diag::Span{source_, range.offset, range.length};
    }
    return span();
}

// --- File --------------------------------------------------------------

File File::from(SyntaxNode root, diag::SourceId source) {
    return File(std::move(root), source);
}

std::vector<Statement> File::statements() const {
    std::vector<Statement> result;
    for (const SyntaxNode& child : node_.child_nodes()) {
        if (std::optional<Statement> statement = Statement::cast(child, source_)) {
            result.push_back(*std::move(statement));
        }
    }
    return result;
}

std::vector<Statement> File::find_all(std::string_view key) const {
    std::vector<Statement> result;
    for (const Statement& statement : statements()) {
        const std::optional<std::string> name = statement.key_name();
        if (name && *name == key) {
            result.push_back(statement);
        }
    }
    return result;
}

} // namespace stardata::ast
