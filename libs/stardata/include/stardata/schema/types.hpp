// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <string>
#include <string_view>

#include "stardata/ast/ast.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/schema/schema.hpp"

namespace stardata::schema {

class SchemaSet;

// Type checking (backlog F4, spec §6.2).
//
// §6.2's table is the whole specification of this file: a column of type
// names and a column of what each accepts. The work is in three parts, and
// only the first is the table itself.
//
// ONE: does the value's lexical kind satisfy the declared type? `int` takes
// an Integer and `text` takes a String or a `$loc_key`, and the lexer has
// already told us which of those was written. This is where a typo in a
// number or a forgotten pair of quotes stops being invisible.
//
// TWO: does the *type expression* mean anything? `enum<mood_enum>` is only a
// type if something declares `mood_enum`, and a key declared with a type
// nobody declares is a key nothing can ever check -- which is a hole in the
// schema layer wearing the appearance of a schema. `check_declared_types`
// runs after everything has loaded, for the same reason the core
// requirements do: a form may be declared in any file (§13.2).
//
// THREE: the sub-grammars. `dice`, `clock_time` and `duration` carry
// structure inside a string, and §6.2 says each is "parsed at compile time".
// The alternative is discovering that `"3d"` is not a dice expression when
// somebody rolls it.
//
// WHAT IS DELIBERATELY NOT HERE. Resolving a `ref<C>` to an actual object is
// backlog F9 -- this checks that the reference is spelled as an identifier
// and that `C` names something, and stops. `resource` existence is the VFS's
// (workstream G). The contents of a `condition_block` or an `effect_block`
// are §10 and §11, which is F8 and Phase 1. Each of those is a checkable
// fact this pass does not have the information to check, rather than one it
// has chosen to skip.

// A `type` position's name, resolved through §4.2's shorthand: a bare
// identifier naming a declared enum means `enum<that>`. Returns the type
// unchanged when no shorthand applies, which is the common case.
[[nodiscard]] ast::TypeRef resolve_type(const ast::TypeRef& type, const SchemaSet& set);

// Checks one value against a declared type.
//
// `what` names the thing being checked, for the diagnostic -- a key name at
// the top, and something like "an entry of 'synonyms'" inside a collection.
// It is passed rather than derived because by the time this recurses into a
// map's values there is no statement left to ask.
void check_value(std::string_view what, const ast::Value& value, const ast::TypeRef& type,
                 const SchemaSet& set, diag::DiagnosticSink& sink);

// Checks an object instantiation (§7.4) against the property types its class
// declares, inherited ones included.
//
// The keys inside an instantiation are properties rather than schema keys, so
// they are looked up through the class chain instead of in a `Schema`. A key
// naming no property is left alone: §7.4 says which keys are permitted inside
// an instantiation, and enforcing that needs the object-local `prop_def` of
// backlog F11. Being wrong about a property's *type* is checkable now, and is
// the case §14.3 names -- `exits.nrth` on a `map<direction, ...>`.
void check_instantiation(const ast::Block& block, const ClassDecl& decl, const SchemaSet& set,
                         diag::DiagnosticSink& sink);

// Checks that every type expression in the set resolves: each name is one of
// §6.2's, or a declared enum; each takes the right number of arguments; and
// `enum<E>`, `flags<E>`, `ref<C>` and `block<S>` name something that exists.
//
// Runs after the load, like `check_requirements`.
void check_declared_types(const SchemaSet& set, diag::DiagnosticSink& sink);

// --- the sub-grammars (§6.2) -------------------------------------------
//
// Each takes the *decoded* string contents, not the source text, and each is
// exposed so it can be tested as the small grammar it is.

// `"3d6+2"`: an optional count, `d`, a number of faces, and an optional
// signed modifier. `d6`, `3d6`, `3d6+2` and `3d6-1` are all dice.
[[nodiscard]] bool is_dice(std::string_view text) noexcept;

// `"HH:MM"` or `"HH:MM:SS"`, two digits per field.
//
// SHAPE ONLY, DELIBERATELY. §6.2 resolves a clock_time "against the sector's
// calendar" (§11.6), and a sector may declare a `local_clock` -- so whether
// hour 30 exists is the calendar's question and not this pass's. Checking
// `HH < 24` here would reject a legitimate thirty-hour day, which is a worse
// failure than not checking it: the author would have no way to say what they
// meant.
[[nodiscard]] bool is_clock_time(std::string_view text) noexcept;

} // namespace stardata::schema
