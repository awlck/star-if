// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/schema/types.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "stardata/diag/diagnostic.hpp"
#include "stardata/schema/loader.hpp"
#include "stardata/schema/suggest.hpp"
#include "stardata/text/template.hpp"

namespace stardata::schema {

namespace {

using diag::Code;
using diag::Diagnostic;

// How many type arguments each of §6.2's names takes. A name absent from
// this table is not a builtin type, which is the only question §4.2's
// shorthand has to answer before it goes looking for an enum.
[[nodiscard]] std::optional<std::size_t> builtin_arity(std::string_view name) noexcept {
    if (name == "bool" || name == "int" || name == "decimal" || name == "float" || name == "text" ||
        name == "string" || name == "identifier" || name == "script" || name == "resource" ||
        name == "clock_time" || name == "duration" || name == "dice" || name == "condition_block" ||
        name == "effect_block" || name == "text_or_script") {
        return 0;
    }
    // Two the schema layer needs that §6.2's table does not list, because
    // they describe the schema language rather than the data it describes:
    // §7.2's key table types `type` as "type expression (§6.2)" and `default`
    // as "scalar", and those are the two names that says.
    //
    // `any` is the third, and it is the one §6.2 now carries a row for. It
    // means "this value's type is decided by other data, and is checked
    // where that data is known" -- a `global`'s `initial` against the
    // global's own `type` (§6.4), which is a dependent type no `type =` on a
    // key declaration can express. It is not a hole in the checking: it moves
    // the check to the pass that can perform it, and every use of it is one
    // this document names.
    if (name == "type_expr" || name == "scalar" || name == "any") {
        return 0;
    }
    if (name == "ref" || name == "enum" || name == "flags" || name == "list" || name == "set" ||
        name == "block") {
        return 1;
    }
    if (name == "map") {
        return 2;
    }
    return std::nullopt;
}

// The types whose value is written in braces.
[[nodiscard]] bool is_block_type(std::string_view name) noexcept {
    return name == "flags" || name == "list" || name == "set" || name == "map" || name == "block" ||
           name == "condition_block" || name == "effect_block";
}

// §6.2's "Accepts" column, as a phrase. The declared type on its own is not
// much help to somebody who does not have the table open.
[[nodiscard]] std::string accepts(std::string_view name) {
    if (name == "bool") {
        return "yes or no";
    }
    if (name == "int" || name == "duration") {
        return "a whole number";
    }
    if (name == "decimal" || name == "float") {
        return "a number";
    }
    if (name == "text") {
        return "a string or a localisation key";
    }
    if (name == "string" || name == "resource" || name == "clock_time" || name == "dice") {
        return "a string";
    }
    if (name == "identifier" || name == "script" || name == "enum" || name == "ref") {
        return "an identifier";
    }
    if (name == "text_or_script") {
        return "a string, a localisation key or the name of a function";
    }
    if (name == "type_expr") {
        return "a type expression";
    }
    if (name == "scalar") {
        return "a single value";
    }
    if (name == "any") {
        return "a value of whatever type the declaration says";
    }
    if (name == "flags" || name == "set") {
        return "a list of identifiers in braces";
    }
    if (name == "list") {
        return "a list in braces";
    }
    if (name == "map" || name == "block" || name == "condition_block" || name == "effect_block") {
        return "a block in braces";
    }
    return "something else";
}

// What a scalar turns out to be, in the words a diagnostic wants. Reports
// what the author wrote, never what it might have been coerced to.
[[nodiscard]] std::string written(const ast::Scalar& scalar) {
    const std::optional<cst::SyntaxKind> kind = scalar.literal_kind();
    if (!kind) {
        return "nothing I can read";
    }
    switch (*kind) {
    case cst::SyntaxKind::Identifier:
        return "the identifier '" + std::string(scalar.as_identifier().value_or("")) + "'";
    case cst::SyntaxKind::Integer:
        return "a whole number";
    case cst::SyntaxKind::Decimal:
        return "a decimal";
    case cst::SyntaxKind::String:
        return "a string";
    case cst::SyntaxKind::LocKey:
        return "a localisation key";
    default:
        break;
    }
    return "something I can't read";
}

[[nodiscard]] std::string written(const ast::Value& value) {
    if (const std::optional<ast::Block> block = value.as_block()) {
        if (block->is_list()) {
            return "a list";
        }
        return block->is_record() ? "a block" : "an empty block";
    }
    if (value.as_call()) {
        return "a call";
    }
    if (value.as_type_expr()) {
        return "a type expression";
    }
    if (const std::optional<ast::Scalar> scalar = value.as_scalar()) {
        return written(*scalar);
    }
    return "nothing I can read";
}

void report_mismatch(std::string_view what, diag::Span at, const std::string& found,
                     const ast::TypeRef& type, diag::DiagnosticSink& sink) {
    Diagnostic diagnostic(Code::TypeMismatch, at,
                          std::string(what) + " is declared " + type.to_string() +
                              ", and this is " + found);
    diagnostic.with_note(type.to_string() + " takes " + accepts(type.name) + " (spec §6.2)");
    sink.report(std::move(diagnostic));
}

// An enum's values, as suggestion candidates.
[[nodiscard]] std::vector<std::string_view> values_of(const EnumDecl& decl) {
    std::vector<std::string_view> values;
    values.reserve(decl.values.size());
    for (const std::string& value : decl.values) {
        values.emplace_back(value);
    }
    return values;
}

// An enum's values, listed for a diagnostic. Kept short: a long enum makes
// the message unreadable, and the edit-distance suggestion beside it is the
// right answer for one of those rather than a wall of names.
[[nodiscard]] std::string list_values(const EnumDecl& decl) {
    std::string text;
    const std::size_t shown = std::min<std::size_t>(decl.values.size(), 8);
    for (std::size_t i = 0; i < shown; ++i) {
        text += i > 0 ? ", " : "";
        text += decl.values[i];
    }
    if (shown < decl.values.size()) {
        text += ", ...";
    }
    return text;
}

// Whether a scalar's lexical kind satisfies a scalar type -- §6.2's table,
// and nothing else. Reporting is the caller's, so one message shape serves
// every type.
[[nodiscard]] bool scalar_fits(const ast::Scalar& scalar, std::string_view name) {
    const std::optional<cst::SyntaxKind> kind = scalar.literal_kind();
    if (!kind) {
        return false;
    }
    const bool identifier = *kind == cst::SyntaxKind::Identifier;
    const bool integer = *kind == cst::SyntaxKind::Integer;
    const bool decimal = *kind == cst::SyntaxKind::Decimal;
    const bool string = *kind == cst::SyntaxKind::String;
    const bool loc_key = *kind == cst::SyntaxKind::LocKey;

    if (name == "bool") {
        return scalar.as_bool().has_value();
    }
    if (name == "int") {
        return integer;
    }
    if (name == "decimal" || name == "float") {
        return decimal || integer; // §6.2: both accept an Integer
    }
    if (name == "text") {
        return string || loc_key;
    }
    if (name == "string" || name == "resource" || name == "clock_time" || name == "dice") {
        return string;
    }
    if (name == "identifier" || name == "script" || name == "enum" || name == "ref") {
        return identifier; // `none` lexes as an identifier, which §6.2 accepts for a ref
    }
    if (name == "text_or_script") {
        return string || loc_key || identifier;
    }
    if (name == "duration") {
        // §6.2: "Integer, or `default`". The word is spelled as an
        // identifier, which is why this is not simply `integer`.
        return integer || (identifier && scalar.as_identifier() == "default");
    }
    if (name == "type_expr") {
        return identifier; // a bare type name; `list<int>` is a TypeExpr node
    }
    if (name == "scalar" || name == "any") {
        return true;
    }
    return false;
}

// The element type of a collection: what each entry between the braces is
// checked against. `flags<E>` is the one that is not simply its argument --
// its entries are values of E, so the element type is `enum<E>`.
[[nodiscard]] std::optional<ast::TypeRef> element_type(const ast::TypeRef& type) {
    if (type.args.empty()) {
        return std::nullopt;
    }
    if (type.name == "list" || type.name == "set") {
        return type.args[0];
    }
    if (type.name == "flags") {
        ast::TypeRef as_enum;
        as_enum.name = "enum";
        as_enum.range = type.range;
        as_enum.args.push_back(type.args[0]);
        return as_enum;
    }
    return std::nullopt;
}

// One scalar against a scalar type. Shared by the top-level path and by
// every entry of a collection, so a bad element reads the same as a bad
// value.
void check_scalar(std::string_view what, const ast::Scalar& scalar, const ast::TypeRef& type,
                  const SchemaSet& set, diag::DiagnosticSink& sink) {
    if (!scalar_fits(scalar, type.name)) {
        report_mismatch(what, scalar.span(), written(scalar), type, sink);
        return;
    }

    if (type.name == "enum" && !type.args.empty()) {
        const EnumDecl* declared = set.find_enum(type.args[0].name);
        const std::string text(scalar.as_identifier().value_or(""));
        if (declared != nullptr && !declared->has_value(text)) {
            Diagnostic diagnostic(Code::TypeMismatch, scalar.span(),
                                  "'" + text + "' is not one of " + type.args[0].name +
                                      "'s values");
            diagnostic.with_note(type.args[0].name + " is " + list_values(*declared) +
                                     " (spec §6.2)",
                                 declared->span);
            suggest(diagnostic, scalar.span(), text, values_of(*declared));
            sink.report(std::move(diagnostic));
        }
        return;
    }

    // §9.1: a `text` value is not a string but a template, and one thing
    // about it is decidable without knowing a single builtin -- whether its
    // brackets balance. Everything else §9 asks of a template needs the
    // vocabulary, and is starcore/text.hpp's.
    //
    // TYPE-DIRECTED ON PURPOSE. `[` and `]` are reserved for "the template
    // language and parser grammar tokens" (§15), and the second of those is
    // a string too: stdlib's `match = { "take [something]" }` is a grammar
    // line, not a template. Parsing every string here would read those as
    // interpolations. The declared type is what tells the two apart, which
    // is why this lives in the type checker rather than in a token scan.
    if (type.name == "text" || type.name == "text_or_script") {
        const text::Template parsed = text::parse_template(scalar, sink);
        (void)parsed; // the structure is Phase 1's; the diagnostic is this pass's
    }

    // The sub-grammars: structure carried inside a string, which §6.2 says
    // is "parsed at compile time". The alternative is finding out that "3d"
    // is not a dice expression when somebody rolls it.
    const std::optional<std::string> contents = scalar.as_string();
    if (!contents) {
        return;
    }
    if (type.name == "dice" && !is_dice(*contents)) {
        Diagnostic diagnostic(Code::TypeMismatch, scalar.span(),
                              "\"" + *contents + "\" is not a dice expression");
        diagnostic.with_note("a dice expression is a count, a 'd', a number of faces and an "
                             "optional modifier: \"3d6\", \"d20\", \"3d6+2\" (spec §6.2)");
        sink.report(std::move(diagnostic));
        return;
    }
    if (type.name == "clock_time" && !is_clock_time(*contents)) {
        Diagnostic diagnostic(Code::TypeMismatch, scalar.span(),
                              "\"" + *contents + "\" is not a time of day");
        diagnostic.with_note("a clock_time is written \"HH:MM\" or \"HH:MM:SS\", two digits per "
                             "field (spec §6.2)");
        diagnostic.with_note("what those numbers count is the sector's calendar's to say, so the "
                             "hour is not range-checked here (spec §11.6)");
        sink.report(std::move(diagnostic));
    }
}

} // namespace

// --- resolution --------------------------------------------------------

ast::TypeRef resolve_type(const ast::TypeRef& type, const SchemaSet& set) {
    // §4.2: "in a type position, a bare identifier naming a declared `enum`
    // is shorthand for `enum<that>`". A builtin name always wins, so an enum
    // called `text` could not quietly change what `text` means.
    if (!type.args.empty() || builtin_arity(type.name).has_value()) {
        return type;
    }
    if (set.find_enum(type.name) == nullptr) {
        return type;
    }
    ast::TypeRef expanded;
    expanded.name = "enum";
    expanded.range = type.range;
    expanded.args.push_back(type);
    return expanded;
}

// --- value checking ----------------------------------------------------

void check_value(std::string_view what, const ast::Value& value, const ast::TypeRef& type,
                 const SchemaSet& set, diag::DiagnosticSink& sink) {
    const ast::TypeRef resolved = resolve_type(type, set);
    const std::string& name = resolved.name;

    // A type nothing declares cannot say anything about a value.
    // `check_declared_types` has already reported it, at the schema, which is
    // where the mistake was made -- repeating it at every value would bury
    // that one report under a hundred.
    if (!builtin_arity(name).has_value()) {
        return;
    }

    // A call is a template-language expression (§4.3) evaluated in the
    // enclosing context. Whether this slot accepts one is §11.5's question
    // and backlog F7's; what is certain is that its result is not something
    // this pass can type.
    if (value.as_call()) {
        return;
    }

    // `any` accepts any shape, scalar or block, because the type that
    // decides is somewhere else -- a `global`'s declared `type` for its
    // `initial` (§6.4). The pass that knows where calls `check_value` again
    // with the real type, so this is a deferral rather than an exemption.
    if (name == "any") {
        return;
    }

    if (!is_block_type(name)) {
        const std::optional<ast::Scalar> scalar = value.as_scalar();
        if (!scalar) {
            // `type = list<int>` parses as a TypeExpr node rather than a
            // Scalar, and is exactly what a `type_expr` slot wants.
            if (name == "type_expr" && value.as_type_expr()) {
                return;
            }
            report_mismatch(what, value.span(), written(value), resolved, sink);
            return;
        }
        check_scalar(what, *scalar, resolved, set, sink);
        return;
    }

    const std::optional<ast::Block> block = value.as_block();
    if (!block) {
        report_mismatch(what, value.span(), written(value), resolved, sink);
        return;
    }
    if (block->is_empty()) {
        return; // `{ }` is the empty collection, and satisfies any of them
    }

    if (name == "flags" || name == "set" || name == "list") {
        // §6.2 lets `list<T>` hold a record block when T is a block type,
        // which is how a repeated `rule = { ... }` makes a sequence without a
        // second syntax. Nothing else may.
        const std::optional<ast::TypeRef> element = element_type(resolved);
        const bool record_allowed =
            name == "list" && element && is_block_type(resolve_type(*element, set).name);
        if (block->is_record()) {
            if (!record_allowed) {
                report_mismatch(what, value.span(), written(value), resolved, sink);
            }
            return;
        }

        const std::string entry_of = "an entry of " + std::string(what);
        std::vector<std::string> seen;
        for (const ast::Scalar& entry : block->values()) {
            if (element) {
                check_scalar(entry_of, entry, resolve_type(*element, set), set, sink);
            }
            if (name != "set") {
                continue;
            }
            // §6.2: a set rejects duplicates. Collapsing them silently would
            // make `{ a b a }` mean something the author cannot see in what
            // they wrote.
            const std::string text(entry.as_identifier().value_or(entry.text()));
            if (std::find(seen.begin(), seen.end(), text) != seen.end()) {
                Diagnostic diagnostic(Code::TypeMismatch, entry.span(),
                                      "'" + text + "' is already in this set");
                diagnostic.with_note("a set rejects duplicates, where a list permits them "
                                     "(spec §6.2)");
                sink.report(std::move(diagnostic));
                continue;
            }
            seen.push_back(text);
        }
        return;
    }

    // The record-block types: `map<K,V>`, `block<S>`, and the two context
    // blocks. Each wants statements rather than bare values.
    if (!block->is_record()) {
        report_mismatch(what, value.span(), written(value), resolved, sink);
        return;
    }

    if (name != "map" || resolved.args.size() != 2) {
        return;
    }

    // A map's keys are checked when K is an enum, which is the case that
    // matters: `exits = { nrth = corridor }` on a `map<direction, ...>` is a
    // typo the author will otherwise meet as a door that does not exist.
    const ast::TypeRef key_type = resolve_type(resolved.args[0], set);
    const EnumDecl* keys = key_type.name == "enum" && !key_type.args.empty()
                               ? set.find_enum(key_type.args[0].name)
                               : nullptr;
    const std::string entry_of = "an entry of " + std::string(what);

    for (const ast::Statement& statement : block->statements()) {
        const std::optional<std::string> key = statement.key_name();
        if (keys != nullptr && key && !key->empty() && !keys->has_value(*key)) {
            Diagnostic diagnostic(Code::TypeMismatch, statement.report_span(),
                                  "'" + *key + "' is not one of " + key_type.args[0].name +
                                      "'s values");
            diagnostic.with_note("this map is keyed by " + key_type.args[0].name + ", which is " +
                                     list_values(*keys) + " (spec §6.2, §6.6.1)",
                                 keys->span);
            // §14.3 names this row and this example: `exits.nrth` on a
            // `map<direction, ...>`, "error, with a suggestion".
            suggest(diagnostic, statement.key() ? statement.key()->span() : statement.report_span(),
                    *key, values_of(*keys));
            sink.report(std::move(diagnostic));
        }
        if (const std::optional<ast::Value> entry = statement.value()) {
            check_value(entry_of, *entry, resolved.args[1], set, sink);
        }
    }
}

// --- instantiations (spec §7.4) ----------------------------------------

namespace {

// The property a class declares under `name`, following §8.4's resolution
// order as far as it exists today: the class itself, then its parent.
//
// TRAITS ARE NOT IN THIS WALK, because they are not yet in the model --
// `class` declares a `traits` key and `read_class` does not read it. That is
// a gap rather than a decision, and it means a property that arrives only
// through a trait is not type-checked on an instantiation yet.
[[nodiscard]] const PropDecl* find_declared_property(const ClassDecl& decl, std::string_view name,
                                                     const SchemaSet& set) {
    const ClassDecl* at = &decl;
    // A depth cap rather than a visited set: a cycle in `of_class` is a
    // malformed hierarchy, which is the class graph's to report and not
    // this function's to diagnose on the way past.
    for (int depth = 0; at != nullptr && depth < 64; ++depth) {
        if (const PropDecl* property = at->find_property(name)) {
            return property;
        }
        if (at->of_class.empty()) {
            return nullptr;
        }
        at = set.find_class(at->of_class);
    }
    return nullptr;
}

} // namespace

const PropDecl* resolve_property(std::string_view name, const std::vector<PropDecl>& local,
                                 const ClassDecl& decl, const SchemaSet& set) {
    // §8.4, step 1: the object's own declarations come first. That ordering
    // is the whole of what makes §8.7 a *declaration* rather than an override
    // -- a local `prop_def` shadows nothing, because §8.7 forbids it from
    // colliding with an inherited name at a different type in the first
    // place, and the redundant case is reported where it is written.
    for (const PropDecl& property : local) {
        if (property.name == name) {
            return &property;
        }
    }
    return find_declared_property(decl, name, set);
}

// §8.7's last bullet, which is the rule that keeps a local declaration from
// quietly becoming a second, differently-typed property of the same name:
//
//   "A local `prop_def` MUST NOT redeclare a name the object already
//    inherits, with a different type. Redeclaring with the *same* type is
//    redundant and SHOULD be reported as such."
//
// Both halves matter for different reasons. The type mismatch is a
// correctness failure -- the save format and every reader of that class
// disagree about the slot's width. The redundancy is only noise, but it is
// the kind of noise that accumulates: a property promoted to the class
// leaves its local declarations behind, and nothing would otherwise say so.
void check_local_prop_defs(const std::vector<PropDecl>& local, const ClassDecl& decl,
                           const SchemaSet& set, diag::DiagnosticSink& sink) {
    for (const PropDecl& property : local) {
        const PropDecl* inherited = find_declared_property(decl, property.name, set);
        if (inherited == nullptr) {
            continue; // the ordinary case: a one-off, belonging to this object
        }
        if (inherited->type.same_as(property.type)) {
            Diagnostic diagnostic(Code::PropDefRedundant, property.span,
                                  "'" + property.name + "' is already a " +
                                      inherited->type.to_string() + " on every '" + decl.id + "'");
            diagnostic.with_note("declared here", inherited->span);
            diagnostic.with_note("harmless, but the line does nothing -- this object would have "
                                 "the property either way (spec §8.7)");
            diagnostic.with_fix_it(property.span, "", "remove the redundant declaration");
            sink.report(std::move(diagnostic));
            continue;
        }
        Diagnostic diagnostic(
            Code::PropDefTypeMismatch, property.span,
            "'" + property.name + "' is already a " + inherited->type.to_string() + " on every '" +
                decl.id + "', and this would make it a " + property.type.to_string() + " here");
        diagnostic.with_note("declared here", inherited->span);
        diagnostic.with_note("an object may declare properties of its own, and may not give an "
                             "inherited one a second type -- everything that reads this class "
                             "reads that slot at the type the class declared (spec §8.7, §8.4)");
        sink.report(std::move(diagnostic));
    }
}

void check_instantiation(const ast::Block& block, const ClassDecl& decl, const SchemaSet& set,
                         diag::DiagnosticSink& sink) {
    // §8.7: the object's own `prop_def`. Read before anything else in the
    // block is looked at, because §8.4 puts these first and a value written
    // above its own declaration is still that property's value -- block
    // contents are ordered (§5.1), but resolution is not positional.
    const std::vector<PropDecl> local = read_local_prop_defs(block, set.find("prop_marker"), sink);
    check_local_prop_defs(local, decl, set, sink);

    for (const ast::Statement& statement : block.statements()) {
        const std::optional<std::string> name = statement.key_name();
        if (!name || name->empty()) {
            continue;
        }
        const PropDecl* property = resolve_property(*name, local, decl, set);
        const std::optional<ast::Value> value = statement.value();
        if (property == nullptr || !value) {
            // A universal key of §7.4 -- `id`, `traits`, `sector`, or one of
            // §8.5's placement keywords -- or a key naming nothing at all.
            // Telling those two apart is what §7.4's permitted-key rule
            // needs, and its list of universals is core vocabulary, so the
            // check cannot live in this library. See the note on
            // `check_instantiation` in the header.
            continue;
        }
        check_value("'" + *name + "'", *value, property->type, set, sink);
    }
}

// --- the sub-grammars --------------------------------------------------

bool is_dice(std::string_view text) noexcept {
    // [count] 'd' faces [ ('+'|'-') modifier ]
    std::size_t at = 0;
    const auto digits = [&text, &at]() {
        const std::size_t start = at;
        while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])) != 0) {
            ++at;
        }
        return at - start;
    };

    digits(); // the count is optional: "d20" rolls one die
    if (at >= text.size() || (text[at] != 'd' && text[at] != 'D')) {
        return false;
    }
    ++at;
    if (digits() == 0) {
        return false; // a die with no faces is not a die
    }
    if (at == text.size()) {
        return true;
    }
    if (text[at] != '+' && text[at] != '-') {
        return false;
    }
    ++at;
    return digits() > 0 && at == text.size();
}

bool is_clock_time(std::string_view text) noexcept {
    const auto two_digits = [](std::string_view field) {
        return field.size() == 2 && std::isdigit(static_cast<unsigned char>(field[0])) != 0 &&
               std::isdigit(static_cast<unsigned char>(field[1])) != 0;
    };
    if (text.size() != 5 && text.size() != 8) {
        return false;
    }
    if (text[2] != ':' || !two_digits(text.substr(0, 2)) || !two_digits(text.substr(3, 2))) {
        return false;
    }
    if (text.size() == 5) {
        return true;
    }
    return text[5] == ':' && two_digits(text.substr(6, 2));
}

// --- declared types ----------------------------------------------------

namespace {

// The span of one name inside a type expression, for a fix-it to rewrite.
//
// Necessary because `at` below is the span of the KEY, which is where the
// diagnostic points -- a fix-it over that span would replace the key's name
// with a type. `TypeRef` carries the range of the name it was read from, and
// the type expression is in the same file as the key that declares it.
[[nodiscard]] std::optional<diag::Span> span_of(const ast::TypeRef& type,
                                                diag::Span in_file) noexcept {
    if (type.range.length == 0) {
        return std::nullopt;
    }
    return diag::Span{in_file.source, type.range.offset, type.range.length};
}

// The names §6.2 gives a type, plus the enums §4.2's shorthand admits.
[[nodiscard]] std::vector<std::string_view> type_names(const SchemaSet& set) {
    std::vector<std::string_view> names = {"bool",
                                           "int",
                                           "decimal",
                                           "float",
                                           "text",
                                           "string",
                                           "identifier",
                                           "script",
                                           "resource",
                                           "clock_time",
                                           "duration",
                                           "dice",
                                           "condition_block",
                                           "effect_block",
                                           "text_or_script",
                                           "type_expr",
                                           "scalar",
                                           "any",
                                           "ref",
                                           "enum",
                                           "flags",
                                           "list",
                                           "set",
                                           "block",
                                           "map"};
    for (const EnumDecl& declared : set.enums()) {
        names.emplace_back(declared.id);
    }
    return names;
}

} // namespace

// One type expression, checked for meaning rather than for use. Recurses
// through the arguments, so `map<direction, ref<no_such_class>>` is reported
// at the inner name that is wrong.
void check_type(const ast::TypeRef& type, diag::Span at, std::string_view context,
                const SchemaSet& set, diag::DiagnosticSink& sink) {
    const ast::TypeRef resolved = resolve_type(type, set);
    const std::optional<std::size_t> arity = builtin_arity(resolved.name);

    if (!arity) {
        Diagnostic diagnostic(Code::SchemaInvalid, at,
                              std::string(context) + " is declared " + type.to_string() +
                                  ", and nothing declares a type called '" + resolved.name + "'");
        diagnostic.with_note("a type is one of spec §6.2's, or the id of a declared `enum` -- a "
                             "key typed by a name nobody declares is a key nothing can check");
        if (const std::optional<diag::Span> name_at = span_of(type, at)) {
            suggest(diagnostic, *name_at, resolved.name, type_names(set));
        }
        sink.report(std::move(diagnostic));
        return;
    }

    if (resolved.args.size() != *arity) {
        Diagnostic diagnostic(Code::SchemaInvalid, at,
                              "'" + resolved.name + "' takes " + std::to_string(*arity) +
                                  " type argument" + (*arity == 1 ? "" : "s") + ", and this has " +
                                  std::to_string(resolved.args.size()));
        diagnostic.with_note("spec §6.2 gives the shape of every type; §4.2 gives how one is "
                             "written");
        sink.report(std::move(diagnostic));
        return;
    }

    const auto missing = [&](std::string_view kind, const std::string& id) {
        Diagnostic diagnostic(Code::SchemaInvalid, at,
                              std::string(context) + " is declared " + type.to_string() +
                                  ", and nothing declares the " + std::string(kind) + " '" + id +
                                  "'");
        diagnostic.with_note("the whole point of naming it is that this is checked: a rename "
                             "upstream becomes a build failure rather than a type that silently "
                             "stops meaning anything (spec §6.2, §13.2)");
        // Candidates from whichever namespace the type expression was
        // reaching into, so `enum<mood_eunm>` is offered enums and not the
        // hundred class names it was never going to mean.
        std::vector<std::string_view> candidates;
        if (kind == std::string_view("enum")) {
            for (const EnumDecl& declared : set.enums()) {
                candidates.emplace_back(declared.id);
            }
        } else if (kind == std::string_view("form")) {
            for (const Schema& declared : set.schemas()) {
                candidates.emplace_back(declared.id);
            }
        } else {
            for (const ClassDecl& declared : set.classes()) {
                candidates.emplace_back(declared.id);
            }
            for (const Schema& declared : set.schemas()) {
                candidates.emplace_back(declared.id);
            }
        }
        // Over the argument's own span, not `at`: `at` is the key, and a
        // fix-it there would replace the key's name with a type name.
        const ast::TypeRef* argument = resolved.args.empty() ? nullptr : &resolved.args[0];
        if (argument != nullptr) {
            if (const std::optional<diag::Span> name_at = span_of(*argument, at)) {
                suggest(diagnostic, *name_at, id, candidates);
            }
        }
        sink.report(std::move(diagnostic));
    };

    if (resolved.name == "enum" || resolved.name == "flags") {
        if (set.find_enum(resolved.args[0].name) == nullptr) {
            missing("enum", resolved.args[0].name);
        }
        return;
    }
    if (resolved.name == "block") {
        // The two bootstrap forms exist in C++ rather than in a file, for
        // the reason schema.star states: they are what reads the file every
        // other schema is written in. `block<key>` names one of them.
        const std::string& id = resolved.args[0].name;
        if (set.find(id) == nullptr && id != schema_of_schemas().id && id != key_schema().id) {
            missing("form", id);
        }
        return;
    }
    if (resolved.name == "ref") {
        // §6.2 describes `ref<C>` in terms of a class, and the built-in set
        // also writes `ref<action>` and `ref<sector>` -- a reference to a
        // declared *form*, which is the same idea one level up. Both resolve.
        const std::string& id = resolved.args[0].name;
        if (set.find_class_or_trait(id) == nullptr && set.find(id) == nullptr) {
            missing("class or form", id);
        }
        return;
    }
    for (const ast::TypeRef& argument : resolved.args) {
        check_type(argument, at, context, set, sink);
    }
}

void check_declared_types(const SchemaSet& set, diag::DiagnosticSink& sink) {
    for (const Schema& schema : set.schemas()) {
        for (const KeyDecl& key : schema.keys) {
            check_type(key.type, key.span, "'" + key.name + "' on '" + schema.id + "'", set, sink);
        }
    }
    for (const ClassDecl& decl : set.classes()) {
        for (const PropDecl& property : decl.properties) {
            check_type(property.type, property.span, "'" + property.name + "' on '" + decl.id + "'",
                       set, sink);
        }
    }
}

} // namespace stardata::schema
