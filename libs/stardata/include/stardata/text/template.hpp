// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "stardata/ast/ast.hpp"
#include "stardata/diag/sink.hpp"

namespace stardata::text {

// The template grammar of spec §9.1 and §9.2 (backlog F7).
//
// A value of type `text` is not a string; it is a program. This file parses
// it. Evaluating it is the text VM's, which is Phase 1 -- so everything here
// stops at structure, and the one thing it reports is the one mistake that
// is a fact about structure alone: brackets that do not balance.
//
// WHY THIS IS MECHANISM AND NOT VOCABULARY. §1.2.1 puts §9 in `starcore`'s
// column, and the *contents* of a template certainly are: `actor`, `noun`,
// `the`, `verb`, `plural` are interactive fiction and none of them is named
// here. The grammar around them is not. `[`, `]`, `\`, `@style`, `@endstyle`
// and the reserved `if` / `else` / `end` (§3.9) are all defined by §3 and
// §5.4.1, which are Stardata's, and a parser that knows only those knows
// nothing about rooms. So the split is the one backlog F12 landed on:
// `libs/stardata` gets the grammar, `libs/starcore` gets the pass that knows
// what the names in it mean (starcore/text.hpp).
//
// IT READS THE SOURCE TEXT, NOT THE DECODED STRING, and this is the one
// non-obvious thing about the whole file. §3.5 makes `\[`, `\]`, `\$` and
// `\@` *string* escapes, so the lexer has already turned them into bare
// characters by the time `Scalar::as_string()` hands them over -- at which
// point an escaped bracket and a real one are the same byte and §9.1's
// escape rule is unimplementable. Parsing therefore walks the literal
// tokens, which also means every span here points at the author's own
// bytes rather than at an offset into a reconstructed string.

// One expression inside `[ ]` (§9.2).
//
// `Apply` is kept distinct from `Call` although §9.2.1 defines the two as
// equivalent -- "the two spellings compile to identical code". The
// difference is not semantic and is preserved for the same reason the CST is
// lossless: a formatter or a refactoring that rewrote `[the noun]` as
// `[the(noun)]` would be changing text an author wrote, and §14.2's
// round-trip requirement does not stop at the string quote.
struct Expr {
    enum class Kind : std::uint8_t {
        Name,   // a bare identifier: a slot (§9.2), or a nullary reference
        Path,   // `self.range` -- a head and one or more '.' segments
        Call,   // `verb(actor, take)`
        Apply,  // `the noun` -- §9.2.1's single-argument juxtaposition
        Number, // an integer literal
        String, // a quoted literal
    };

    Kind kind = Kind::Name;

    // The identifier: the name itself, a path's head, or a call's callee.
    std::string name;

    // A path's segments after the head, in order. `self.a.b` has two.
    std::vector<std::string> segments;

    // A call's arguments, or an apply's single one.
    std::vector<Expr> args;

    // A Number's or String's text, as written.
    std::string literal;

    diag::Span span;      // the whole expression
    diag::Span name_span; // just `name`, which is what a suggestion replaces
};

// One piece of a template (§9.1).
//
// FLAT, NOT NESTED, although §9.1's `Conditional` production is nested. The
// reason is §9.4: `[tip(plasma_cutter)]battered yellow tool[end]` closes with
// the same `[end]` that closes an `[if]`, and which of the two a given
// `[end]` belongs to depends on whether `tip` opens a span -- which is a fact
// about the builtin table, which is vocabulary. A parser that nested them
// would have to know that `tip` is special, and would be wrong for any
// author-defined span-opening function. So the sequence is produced as
// written and pairing is the text VM's, which has the table.
struct Fragment {
    enum class Kind : std::uint8_t {
        Literal,       // everything not otherwise matched, escapes decoded
        Interpolation, // `[ Expr ]`
        StyleOpen,     // `@style(id)` -- opens a span (§9.3)
        StyleClose,    // `@endstyle`
        If,            // `[if Expr]`
        Else,          // `[else]`
        End,           // `[end]`
    };

    Kind kind = Kind::Literal;

    // A Literal's decoded contents, or a StyleOpen's style id.
    std::string text;

    // An Interpolation's or an If's expression, absent when it could not be
    // parsed. See `parse_template` on why that is silent in Phase 0.
    std::optional<Expr> expr;

    diag::Span span;
};

struct Template {
    std::vector<Fragment> fragments;
    diag::Span span;

    [[nodiscard]] bool empty() const noexcept { return fragments.empty(); }

    // Every `@style(id)` the template opens, in source order. The style
    // *annotation* of §5.4.1 is not one of these -- it sits on the value
    // rather than inside it, and a caller reads it from `ast::Value`.
    [[nodiscard]] std::vector<const Fragment*> style_directives() const;
};

// Parses the template a `text`-typed scalar holds (§9.1).
//
// Adjacent literals are one template, not several (§3.5.1): tour.star's
// `conditional_demo` opens its `[if]` in one literal and closes it two
// literals later, and a per-literal parse would report three bracket errors
// on a correct file.
//
// Reports E-TEMPLATE-BRACKETS, and nothing else. In particular a malformed
// *expression* inside the brackets is silent, and the fragment simply
// carries no `expr`: §9.2's own MUST is that a function name "MUST resolve
// at compile time to either a template builtin or a Starscript function",
// and Starscript is §12, which Phase 0 does not have. Diagnosing the shape
// of an expression while being unable to say whether it names anything would
// report the smaller half of the problem and imply the larger half was
// checked. Recorded as an [OPEN] on backlog F7.
//
// Returns an empty template for a scalar that is not a string -- a `$loc_key`
// is a reference to a template, not one, and the table it names is §9.6's.
[[nodiscard]] Template parse_template(const ast::Scalar& scalar, diag::DiagnosticSink& sink);

// The same, for one string literal's source text -- quotes included, exactly
// as `cst::SyntaxToken::text()` returns it -- at a known span. `at.offset`
// must be the offset of the opening quote.
[[nodiscard]] Template parse_template(std::string_view literal, diag::Span at,
                                      diag::DiagnosticSink& sink);

// §9.2.2's capitalisation rule, which is stated once and generically:
// "if a name does not resolve and begins with an upper-case letter, the
// lower-case form is looked up and its output capitalised."
//
// So the rule is not a property of any builtin -- `The`, `A`, `Name` and
// `Number` are not entries in a table anywhere, and an author-defined script
// function gets the same treatment for free. Which makes it mechanism, and
// which is why it is a function here rather than a branch inside whatever
// eventually owns the builtin table.
struct NameLookup {
    std::string name;         // the name to look up
    bool capitalises = false; // whether its result is capitalised
};

[[nodiscard]] NameLookup name_lookup(std::string_view written);

} // namespace stardata::text
