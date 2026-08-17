// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "stardata/cst/syntax.hpp"
#include "stardata/diag/source_manager.hpp"

namespace stardata::ast {

// A typed view over the concrete syntax tree (backlog F1).
//
// There is no second tree. Every class here is a cursor -- a `cst::SyntaxNode`
// and the SourceId it came from -- that answers the questions a consumer of
// the grammar actually asks ("what is this statement's key?") instead of the
// questions the CST answers ("what is this node's fourth child?"). The CST
// stays the single representation, so an edit through `cst::edit` is visible
// through these views immediately and nothing has to be kept in sync.
//
// EVERY ACCESSOR TOLERATES A MALFORMED TREE. The parser never fails: it
// produces `Error` nodes and carries on, so a `Statement` may genuinely have
// no key, no operator or no value, and an editor asks about a half-typed
// file constantly. Accessors therefore return `std::optional` or an empty
// vector rather than asserting, and no accessor here can abort the process.
//
// The SourceId travels with the view purely so `span()` can hand a caller
// something a diagnostic accepts; the schema layer above this emits a great
// many diagnostics, and threading the id through every call site by hand is
// the sort of friction that ends in diagnostics pointing at the wrong file.

class Annotation;
class Block;
class Call;
class Key;
class Scalar;
class Statement;
class TypeExpr;
class Value;

// The common part: which node, which source, and where it sits.
class NodeView {
public:
    [[nodiscard]] const cst::SyntaxNode& syntax() const noexcept { return node_; }
    [[nodiscard]] diag::SourceId source() const noexcept { return source_; }
    [[nodiscard]] cst::SyntaxKind kind() const noexcept { return node_.kind(); }
    [[nodiscard]] cst::TextRange text_range() const noexcept { return node_.text_range(); }

    // The node's own range as a diagnostic span. Note that a `Statement`
    // owns its leading and trailing trivia (see the trivia policy in
    // cst/parser.cpp), so its span covers the surrounding whitespace and
    // comments too -- point at `key()` instead when reporting about one.
    [[nodiscard]] diag::Span span() const noexcept;

    // The source text this node covers, reconstructed from its leaves.
    [[nodiscard]] std::string text() const { return node_.text(); }

protected:
    NodeView(cst::SyntaxNode node, diag::SourceId source) noexcept
        : node_(std::move(node)), source_(source) {}

    cst::SyntaxNode node_;
    diag::SourceId source_;
};

// An owned, recursive rendering of a type expression: `map<identifier,
// ref<action>>` becomes name "map" with two arguments, the second of which
// is itself "ref" with one argument.
//
// Owned rather than a view, unlike everything else in this header, for two
// reasons. A bare type argument is an identifier *token* and not a node
// (see `parse_type_arguments`), so half the tree here has nothing to point a
// view at; and F4 will want to compare declared types structurally, which a
// pair of cursors cannot do.
struct TypeRef {
    std::string name;
    std::vector<TypeRef> args;
    cst::TextRange range;

    // Canonical spelling: `map<identifier, ref<action>>`. Rebuilt from the
    // structure rather than copied from the source, so a diagnostic quoting
    // a type prints the same text no matter how the author spaced it.
    [[nodiscard]] std::string to_string() const;

    // Structural equality, ignoring source position -- which is what
    // "the same type" means.
    [[nodiscard]] bool same_as(const TypeRef& other) const;
};

// A key: `id` in `id = cell`. An identifier, or a string where the key is
// not a valid identifier.
class Key : public NodeView {
public:
    [[nodiscard]] static std::optional<Key> cast(const cst::SyntaxNode& node,
                                                 diag::SourceId source);

    // The key's name -- an identifier's text, or a string key's decoded
    // contents. Empty only when the Key node holds no token at all, which
    // happens in a partially typed file.
    [[nodiscard]] std::optional<std::string> name() const;
    [[nodiscard]] bool is_string() const;

private:
    using NodeView::NodeView;
};

// One literal, or a run of adjacent string literals (§3.5.1), which the
// parser keeps as a single Scalar so the author's line breaks survive.
class Scalar : public NodeView {
public:
    [[nodiscard]] static std::optional<Scalar> cast(const cst::SyntaxNode& node,
                                                    diag::SourceId source);

    // The lexical kind of the literal: Identifier, Integer, Decimal, String
    // or LocKey. Empty when the node holds no literal token.
    [[nodiscard]] std::optional<cst::SyntaxKind> literal_kind() const;

    // Every literal token in the run. One element except for adjacent
    // strings, where §3.5.1's split points are each their own token.
    [[nodiscard]] std::vector<cst::SyntaxToken> literals() const;

    // Typed readings. Each is empty when the scalar is of another kind, so a
    // caller can ask without checking `literal_kind()` first. Coercion
    // between kinds -- deciding whether an `integer` may satisfy a declared
    // `float` -- is F4's, not this layer's: these report only what was
    // written.
    [[nodiscard]] std::optional<std::string_view> as_identifier() const;
    [[nodiscard]] std::optional<std::int64_t> as_integer() const;
    [[nodiscard]] std::optional<std::string_view> as_loc_key() const; // without the '$'

    // A decimal as its scaled 64-bit representation: three fractional digits
    // exactly (§3.4), so `1.500` reads as 1500. The lexer has already
    // rejected any other precision.
    [[nodiscard]] std::optional<std::int64_t> as_decimal_scaled() const;

    // A string's contents: escapes decoded, adjacent literals concatenated
    // with no separator. Empty when the scalar is not a string.
    //
    // An escape the lexer rejected is passed through verbatim rather than
    // dropped -- the diagnostic was already reported, and inventing a
    // character here would make the error harder to see, not easier.
    [[nodiscard]] std::optional<std::string> as_string() const;

    // `yes` / `no` (§3.9). Written out because it is the single most common
    // question the schema layer asks of a scalar.
    [[nodiscard]] std::optional<bool> as_bool() const;

private:
    using NodeView::NodeView;
};

// `@name` or `@name(args)` attached to a value (§3.8, §5.4).
class Annotation : public NodeView {
public:
    [[nodiscard]] static std::optional<Annotation> cast(const cst::SyntaxNode& node,
                                                        diag::SourceId source);

    // The name without its '@'.
    [[nodiscard]] std::optional<std::string_view> name() const;

    // The identifier and integer tokens between the parentheses, in order.
    [[nodiscard]] std::vector<cst::SyntaxToken> arguments() const;

private:
    using NodeView::NodeView;
};

// `name(arg, ...)` in a value position (§4.3). Phase 0 does not evaluate
// calls; F7 gives them meaning inside templates.
class Call : public NodeView {
public:
    [[nodiscard]] static std::optional<Call> cast(const cst::SyntaxNode& node,
                                                  diag::SourceId source);

    [[nodiscard]] std::optional<std::string_view> callee() const;

    // Each argument is a Scalar or a nested Call, in source order.
    [[nodiscard]] std::vector<cst::SyntaxNode> arguments() const;

private:
    using NodeView::NodeView;
};

// `name<arg, ...>` (§4.2). `lower()` is usually what a caller wants; the
// view exists so a diagnostic can point at the expression itself.
class TypeExpr : public NodeView {
public:
    [[nodiscard]] static std::optional<TypeExpr> cast(const cst::SyntaxNode& node,
                                                      diag::SourceId source);

    [[nodiscard]] std::optional<std::string_view> name() const;
    [[nodiscard]] TypeRef lower() const;

private:
    using NodeView::NodeView;
};

// `{ ... }`. Either a list of bare values or a set of statements, never both
// (§5.2) -- though a malformed file can of course hold both, which is what
// the parser's E-BLOCK-MIXED reports, so both accessors below are honest
// about what is actually there.
class Block : public NodeView {
public:
    [[nodiscard]] static std::optional<Block> cast(const cst::SyntaxNode& node,
                                                   diag::SourceId source);

    [[nodiscard]] std::vector<Statement> statements() const;
    [[nodiscard]] std::vector<Scalar> values() const;

    [[nodiscard]] bool is_record() const; // holds statements
    [[nodiscard]] bool is_list() const;   // holds bare values
    [[nodiscard]] bool is_empty() const;

    // The first statement binding `key`, and every one of them. `find_all`
    // is what an `arity = many` key needs, and what a duplicate-key
    // diagnostic needs in order to cite both spans (§5.3).
    [[nodiscard]] std::optional<Statement> find(std::string_view key) const;
    [[nodiscard]] std::vector<Statement> find_all(std::string_view key) const;

    // The value bound to `key` by the first statement that binds it.
    [[nodiscard]] std::optional<Value> value_of(std::string_view key) const;

private:
    using NodeView::NodeView;
};

// The right-hand side of a statement: annotations, then one of a block, a
// type expression, a call or a scalar.
class Value : public NodeView {
public:
    [[nodiscard]] static std::optional<Value> cast(const cst::SyntaxNode& node,
                                                   diag::SourceId source);

    [[nodiscard]] std::vector<Annotation> annotations() const;

    [[nodiscard]] std::optional<Block> as_block() const;
    [[nodiscard]] std::optional<Scalar> as_scalar() const;
    [[nodiscard]] std::optional<Call> as_call() const;
    [[nodiscard]] std::optional<TypeExpr> as_type_expr() const;

    // A type written either way: `type = int` parses as a Scalar and
    // `type = list<string>` as a TypeExpr, and a schema does not care which.
    [[nodiscard]] std::optional<TypeRef> as_type() const;

private:
    using NodeView::NodeView;
};

// `key op value`, plus the trivia the trivia policy attached to it.
class Statement : public NodeView {
public:
    [[nodiscard]] static std::optional<Statement> cast(const cst::SyntaxNode& node,
                                                       diag::SourceId source);

    [[nodiscard]] std::optional<Key> key() const;
    [[nodiscard]] std::optional<std::string> key_name() const;
    [[nodiscard]] std::optional<cst::SyntaxToken> op() const;
    [[nodiscard]] std::string_view op_text() const;
    [[nodiscard]] std::optional<Value> value() const;

    // True for `=` alone. §5.3 turns on this: "arity counts binding
    // occurrences only -- those using `=`", while `+=` and `-=` transform
    // whatever value is in effect and so never collide under `arity = one`.
    //
    // `?=` was once the hard case here, and is now not a case at all: §6.3.1
    // removed the operator. That it produced a disagreement between the spec
    // and this function on first contact is part of why -- an operator whose
    // binding-ness could be read two ways was one the format was better off
    // without. The lexer still recognises it, to say so (§15).
    [[nodiscard]] bool is_binding() const;

    // The span to point a diagnostic at: the key if there is one, otherwise
    // the statement's own text less the trivia it owns. Never the raw
    // `span()`, which would underline the blank line above the statement.
    [[nodiscard]] diag::Span report_span() const;

private:
    using NodeView::NodeView;
};

// The whole file: a sequence of statements.
class File : public NodeView {
public:
    // From a parsed root. The node is expected to be of kind File; anything
    // else yields a view whose `statements()` is empty rather than an error,
    // because this is called on whatever the parser produced.
    [[nodiscard]] static File from(cst::SyntaxNode root, diag::SourceId source);

    [[nodiscard]] std::vector<Statement> statements() const;
    [[nodiscard]] std::vector<Statement> find_all(std::string_view key) const;

private:
    using NodeView::NodeView;
};

} // namespace stardata::ast
